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
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
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
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_tnnjeerolos0a f0; gm_tnnjeerolos0a f1; } gm_tcf2f71ra2nps;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
typedef struct { uint64_t f0; gm_tep2drsl9408b f1; } gm_tjvm1fbs3qbum;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
typedef struct { gm_th2lqbo77pvsc f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tm7a6rksvs6n5;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_th2lqbo77pvsc f0; void * f1; } gm_tpouv9r7lrri2;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcf2f71ra2nps) == 16 && _Alignof(gm_tcf2f71ra2nps) == 4 && offsetof(gm_tcf2f71ra2nps, f0) == 0 && offsetof(gm_tcf2f71ra2nps, f1) == 8, "layout of `(ArrayKind, ArrayKind)`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?(variants: []VariantDef, tag: Ty)`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tjvm1fbs3qbum) == 32 && _Alignof(gm_tjvm1fbs3qbum) == 8 && offsetof(gm_tjvm1fbs3qbum, f0) == 0 && offsetof(gm_tjvm1fbs3qbum, f1) == 8, "layout of `(i: usize, x: SourceField)`");
_Static_assert(sizeof(gm_tm7a6rksvs6n5) == 48 && _Alignof(gm_tm7a6rksvs6n5) == 8 && offsetof(gm_tm7a6rksvs6n5, f0) == 0 && offsetof(gm_tm7a6rksvs6n5, f1) == 24 && offsetof(gm_tm7a6rksvs6n5, f2) == 32, "layout of `TuplePart`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpouv9r7lrri2) == 32 && _Alignof(gm_tpouv9r7lrri2) == 8 && offsetof(gm_tpouv9r7lrri2, f0) == 0 && offsetof(gm_tpouv9r7lrri2, f1) == 24, "layout of `SpreadPart`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_bind_temp_sa5rcbr {
    gm_frame h;
    void * ret;
};
struct gm_fr_clear_4qp22g2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_construct_struct_ve6tb88 {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* stmts */
    gm_trqfvmjtt4sij l10; /* slots */
    gm_trqfvmjtt4sij l11; /* explicit */
    gm_trqfvmjtt4sij l14; /* written */
    gm_t7te2tdpebbt4 l18; /* arg */
    gm_tc3oht4ksth9d l19; /* e */
    gm_tc3oht4ksth9d l21; /* e */
    gm_t0mglksrkfa5e l26; /* f */
    gm_tc3oht4ksth9d l29; /* e */
    gm_t0mglksrkfa5e l46; /* f */
    gm_trqfvmjtt4sij l57; /* values */
    void * ret;
};
struct gm_fr_construct_tuple_6ee5ejf {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* slots */
    gm_t7te2tdpebbt4 l13; /* a */
    gm_trqfvmjtt4sij l14; /* stmts */
    gm_tc3oht4ksth9d l16; /* e */
    gm_t7te2tdpebbt4 l24; /* arg */
    gm_trqfvmjtt4sij l25; /* parts */
    gm_tc3oht4ksth9d l26; /* e */
    gm_tc3oht4ksth9d l28; /* e */
    gm_tc3oht4ksth9d l30; /* e */
    gm_tm7a6rksvs6n5 l37; /* part */
    gm_trqfvmjtt4sij l41; /* values */
    void * ret;
};
struct gm_fr_construct_variant_o6is81c {
    gm_frame h;
    gm_tckdobunaulee l10; /* v */
    gm_tc3oht4ksth9d l18; /* x */
    void * ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_default_value_lo723gi {
    gm_frame h;
    gm_tbci5ospk5nnn l11; /* v */
    gm_tngtuiuskn938 l13; /* r */
    void * ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_b8btuen {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dtfif5p {
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
struct gm_fr_empty_slice_ose1ba9 {
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
struct gm_fr_extend_7hs67uj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_field_default_4ssmakm {
    gm_frame h;
    gm_tpautr4br7h3l l7; /* def */
    void * ret;
};
struct gm_fr_field_default_expr_tqfuiff {
    gm_frame h;
    void * ret;
};
struct gm_fr_field_value_19vleud {
    gm_frame h;
    void * ret;
};
struct gm_fr_float_value_cr4h9gi {
    gm_frame h;
    gm_tngtuiuskn938 l1; /* r */
    void * ret;
};
struct gm_fr_format_0o67o8r {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_12rbmhd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1e409ou {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3tnlp8q {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6fdilsr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6nmb3uf {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_843cvhd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9i81nmj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9tg469g {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a07tfib {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_adtncrl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bbsjt91 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bm4rhr0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e1r9br2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j4le8o0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ktbu9b9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l002mid {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lc07prd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s1oodgm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t8pf89n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_plain_data_ej4mh2m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_zero_5aept6u {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_zero_8tcct9o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
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
struct gm_fr_list_5kthfl8 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7gsrufb {
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
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_t2b6tm8c0vgo1 l1; /* kind */
    void * ret;
};
struct gm_fr_neg_0u9uqg6 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_9a0n4ld {
    gm_frame h;
    gm_tm7a6rksvs6n5 l1; /* x */
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
struct gm_fr_push_lnebar3 {
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
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_52486fh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_gp456bk {
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
struct gm_fr_spread_source_fields_5csral7 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_t0mglksrkfa5e l13; /* f */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_strip_wrappers_e5thpib {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_to_float_o1iop8t {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    double ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb2ul45tidi(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_l1kjtap(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_check_const_expr_2ot4q9v(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) void gm_f_clear_4qp22g2(void * a0);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_construct_struct_ve6tb88(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_construct_tuple_6ee5ejf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_variant_o6is81c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, const void * a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_default_const_k60bpu4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void * gm_f_default_value_lo723gi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_b8btuen(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dtfif5p(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_field_default_4ssmakm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void * gm_f_field_default_expr_tqfuiff(void * a0, uint32_t a1, uint64_t a2);
__attribute__((aligned(4))) void * gm_f_field_value_19vleud(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_float_value_cr4h9gi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_format_0o67o8r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_12rbmhd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_1e409ou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_3tnlp8q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6fdilsr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6nmb3uf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_843cvhd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_9i81nmj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_9tg469g(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_a07tfib(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_adtncrl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_bbsjt91(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_bm4rhr0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_e1r9br2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_j4le8o0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ktbu9b9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l002mid(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lc07prd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rkil4k4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_s1oodgm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_t8pf89n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_hidden_field_u7uao2s(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_o9r57ha(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lift_temps_7c6gk6l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5kthfl8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7gsrufb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void gm_f_neg_0u9uqg6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9a0n4ld(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_lnebar3(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_52486fh(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gp456bk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_soa_construct_8nun9tl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_spread_parts_e62q2l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_spread_source_fields_5csral7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_strip_wrappers_e5thpib(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
__attribute__((aligned(4))) double gm_f_to_f16_0pcu0pq(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) float gm_f_to_f32_8bnhsji(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) double gm_f_to_float_o1iop8t(void *env, gm_node *cx, gm_node *inst, const void * a0, uint16_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_uninit_message_u2c3peu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0uum3co6vapt[23];
extern const uint8_t gm_s1lj31vo6lvqe[26];
extern const uint8_t gm_s2haaj13v41fd[25];
extern const uint8_t gm_s323uftq8kr36[49];
extern const uint8_t gm_s454sq1vshj3e[77];
extern const uint8_t gm_s802sbfa6mtsg[27];
extern const uint8_t gm_s9pf9com5692e[39];
extern const uint8_t gm_sbapuchngtvak[28];
extern const uint8_t gm_sbdqlrmtee4eo[66];
extern const uint8_t gm_sc6uqn3rvm1p8[56];
extern const uint8_t gm_ser747td9jt8d[41];
extern const uint8_t gm_setofg57no9qt[34];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfdj0c5vltira[31];
extern const uint8_t gm_sglgjrjci6t1d[25];
extern const uint8_t gm_shi1s7rq8m35t[40];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sknpneumdgqfs[38];
extern const uint8_t gm_slpi6dabivg2e[29];
extern const uint8_t gm_sml38fgda46cm[56];
extern const uint8_t gm_sna4vk4o059d0[10];
extern const uint8_t gm_so1m4qmhi2fvm[93];
extern const uint8_t gm_sr2ptnspelf8s[98];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src7nt72u848oha[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srrqh3pev0igg[9];
extern const uint8_t gm_sto8li44rv2ou[8];
extern const gm_tmpl gm_tpl1g1lh0kp24oa;
extern const gm_tmpl gm_tpl56vg2jjplgvg;
extern const gm_tmpl gm_tpl5hraomijgptv;
extern const gm_tmpl gm_tpl9vu8tqcv1d9s;
extern const gm_tmpl gm_tplacjeuo4at0jj;
extern const gm_tmpl gm_tpldp3bk13rlc44;
extern const gm_tmpl gm_tplful0p0r5pifs;
extern const gm_tmpl gm_tplg575kk57c4i8;
extern const gm_tmpl gm_tplh6c513o0038j;
extern const gm_tmpl gm_tplk7hjamhd5jcl;
extern const gm_tmpl gm_tplpf7ckn15kjth;
extern const gm_tmpl gm_tplq5eond7ctc71;
extern const gm_tmpl gm_tplqbtvqd3a4g25;
extern const gm_tmpl gm_tplr869c1s382eh;
extern const gm_tmpl gm_tplre5cfbnr7ltv;
extern const gm_tmpl gm_tplrivm1rlf1ps3;
extern const gm_tmpl gm_tpls3402qmr7otb;
extern const gm_tmpl gm_tplsnlr78cvfrao;
extern const gm_tmpl gm_tplsqibbtt8789o;
extern const gm_tmpl gm_tpltvrl7dp7h9m1;
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
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqb2ul45tidi(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)1) return gm_eq9m6mvtmgde(a + 4, b + 4);
    return true;
}
inline __attribute__((aligned(4))) void gm_f_clear_4qp22g2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_4qp22g2 F;
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
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_copy_pmkg67r F;
    GM_STACK_CHECK(&F, 174u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tftb1odc739t5 gm_l2;
    gm_trqfvmjtt4sij t2;
    gm_f_list_go2qg0b((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    gm_tftb1odc739t5 t6 = gm_f_extend_7hs67uj(t3, t5);
    gm_l2 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srclv3pj5es0f8o, 330, 5);
    uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)a0));
    gm_trqfvmjtt4sij t9 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_tbci5ospk5nnn t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), t8);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t10 + 8), t9);
    memmove(gm_out, ((uint8_t *)&t10), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_b8btuen(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_b8btuen F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dtfif5p(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dtfif5p F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ose1ba9 F;
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
inline __attribute__((aligned(4))) void * gm_f_field_default_expr_tqfuiff(void * a0, uint32_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_field_default_expr_tqfuiff F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* exprs */
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 1088));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1088) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srclv0dcbbfhves, 716, 14);
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + (uintptr_t)t6 * 184) + 96));
    gm_l3 = t7;
    uint64_t t8 = gm_l2;
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint8_t t10 = (t8 >= t9);
    if (t10) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t11 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t13 = gm_l2;
    void * t14 = GM_LD(void *, (((uint8_t *)t11) + (uintptr_t)t13 * 8));
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_0o67o8r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0o67o8r F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s454sq1vshj3e));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(76));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpltvrl7dp7h9m1), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_12rbmhd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_12rbmhd F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sml38fgda46cm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(55));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplacjeuo4at0jj), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_1e409ou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1e409ou F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sbdqlrmtee4eo));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(65));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplr869c1s382eh), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_3tnlp8q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3tnlp8q F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sfdj0c5vltira));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(30));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpls3402qmr7otb), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6fdilsr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_6fdilsr F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s323uftq8kr36));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(48));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplre5cfbnr7ltv), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6nmb3uf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_6nmb3uf F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_so1m4qmhi2fvm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(92));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplk7hjamhd5jcl), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_843cvhd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_843cvhd F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9pf9com5692e));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsnlr78cvfrao), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9i81nmj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_9i81nmj F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ser747td9jt8d));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(40));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1g1lh0kp24oa), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_9tg469g(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_9tg469g F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sknpneumdgqfs));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldp3bk13rlc44), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a07tfib(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a07tfib F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc6uqn3rvm1p8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplh6c513o0038j), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_adtncrl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_adtncrl F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slpi6dabivg2e));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplpf7ckn15kjth), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_bbsjt91(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bbsjt91 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1lj31vo6lvqe));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl56vg2jjplgvg), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_bm4rhr0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bm4rhr0 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2haaj13v41fd));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqbtvqd3a4g25), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_e1r9br2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_e1r9br2 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shi1s7rq8m35t));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5hraomijgptv), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_j4le8o0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_j4le8o0 F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tf1bjg81u0glv gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s802sbfa6mtsg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_tf1bjg81u0glv t3 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
    gm_f_format_rkil4k4((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplful0p0r5pifs), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ktbu9b9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ktbu9b9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbapuchngtvak));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(27));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplq5eond7ctc71), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l002mid(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l002mid F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sglgjrjci6t1d));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(24));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplg575kk57c4i8), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_s1oodgm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s1oodgm F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr2ptnspelf8s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(97));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsqibbtt8789o), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_t8pf89n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t8pf89n F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tf1bjg81u0glv gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_setofg57no9qt));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(33));
    gm_tf1bjg81u0glv t3 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
    gm_f_format_rkil4k4((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplrivm1rlf1ps3), t2);
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
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_from_int_ofjuoqh F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l0));
    gm_tbci5ospk5nnn t3;
    gm_f_one_08qg1op((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tngtuiuskn938 t4;
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t4 + 48), t3);
    memmove(gm_out, ((uint8_t *)&t4), 96);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_5aept6u F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
    F.ret = t2;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_8tcct9o F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    F.ret = t3;
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
inline __attribute__((aligned(4))) void gm_f_list_5kthfl8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5kthfl8 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_b8btuen();
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
inline __attribute__((aligned(4))) void gm_f_list_7gsrufb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7gsrufb F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_dtfif5p();
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
inline __attribute__((aligned(4))) void gm_f_neg_0u9uqg6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_0u9uqg6 F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t2;
    gm_f_neg_agd5c5k((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t3 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l0) + 48));
    gm_tngtuiuskn938 t4;
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t4 + 48), t3);
    memmove(gm_out, ((uint8_t *)&t4), 96);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_agd5c5k F;
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t gm_l2;
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l0));
    gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    uint8_t t4 = (uint8_t)!t3;
    if (t4) {
        uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)&F.l0) + 8) + 8));
        uint8_t t6 = (t5 > ((uint64_t)0ULL));
        gm_l2 = t6;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t7 = gm_l2;
    GM_ST(uint8_t, ((uint8_t *)&F.l1), t7);
    memmove(gm_out, ((uint8_t *)&F.l1), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_one_08qg1op F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, ((uint64_t)1ULL));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9a0n4ld(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_9a0n4ld F;
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
        gm_b16_8 t9 = gm_f_empty_slice_b8btuen();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_52486fh(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_lnebar3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_lnebar3 F;
    void * gm_l0; /* l */
    gm_tep2drsl9408b gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_dtfif5p();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_gp456bk(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_52486fh(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_52486fh F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gp456bk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_gp456bk F;
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
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_template_9o92cvv F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 176));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 176) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 276, 56);
    uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t3) + (uintptr_t)t6 * 4));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_to_float_o1iop8t(void *env, gm_node *cx, gm_node *inst, const void * a0, uint16_t a1) {
    struct gm_fr_to_float_o1iop8t F;
    uint16_t gm_l1; /* bits */
    GM_STACK_CHECK(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint16_t t2 = gm_l1;
    uint8_t t3 = ((t2 == ((uint16_t)16ULL)));
    if (t3) {
        double t4 = gm_f_to_f16_0pcu0pq((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t4;
        goto gm_fret;
    }
    uint16_t t5 = gm_l1;
    uint8_t t6 = ((t5 == ((uint16_t)32ULL)));
    if (t6) {
        float t7 = gm_f_to_f32_8bnhsji((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        double t8 = (double)t7;
        F.ret = t8;
        goto gm_fret;
    }
    double t9 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
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
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_s25l990 F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_go2qg0b((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t3;
    GM_ST(uint8_t, (uint8_t *)&t3 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t3 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t3 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t3 + 8), t2);
    memmove(gm_out, ((uint8_t *)&t3), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_bind_temp_sa5rcbr F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    void * gm_l2; /* stmts */
    uint32_t gm_l3; /* t */
    GM_STACK_CHECK(&F, 327u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l4;
    gm_tftb1odc739t5 gm_l5;
    void * t12 = gm_l1;
    {
        {
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)0ULL));
            if (!t14) {
                goto gm_lb2;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)1ULL));
            if (!t16) {
                goto gm_lb3;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)2ULL));
            if (!t18) {
                goto gm_lb4;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)4ULL));
            if (!t20) {
                goto gm_lb5;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)5ULL));
            if (!t22) {
                goto gm_lb6;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)7ULL));
            if (!t24) {
                goto gm_lb7;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)8ULL));
            if (!t26) {
                goto gm_lb8;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)14ULL));
            if (!t28) {
                goto gm_lb9;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb9:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 379, 8);
    }
gm_lb1:;
    uint8_t t29 = gm_l4;
    if (t29) {
        void * t30 = gm_l1;
        F.ret = t30;
        goto gm_fret;
    }
    void * t31 = gm_l0;
    void * t32 = gm_l1;
    void * t33 = gm_l2;
    gm_b0_1 t34 = gm_f_lift_temps_7c6gk6l((void *)NULL, cx, NULL, t31, t32, t33);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t35 = gm_l0;
    void * t36 = gm_l1;
    gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t36));
    void * t38 = gm_l1;
    gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 72));
    uint32_t t40 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t35, t37, t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t40;
    void * t41 = gm_l2;
    uint32_t t42 = gm_l3;
    void * t43 = gm_l1;
    gm_ttggq7ng4kp3p t44;
    GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t44 + 0), t42);
    GM_ST(void *, ((uint8_t *)&t44 + 8), t43);
    gm_trcmmd12cefb1 t45;
    GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t45 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)0ULL));
    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t45 + 8), t44);
    gm_tftb1odc739t5 t46 = gm_f_push_d6c76ud_2(t41, &t45);
    gm_l5 = t46;
    uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_src7nt72u848oha, 384, 5);
    void * t48 = gm_l1;
    gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t48));
    uint32_t t50 = gm_l3;
    void * t51 = gm_l1;
    gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t51) + 72));
    gm_t2b6tm8c0vgo1 t53;
    memset(&t53, 0, sizeof t53);
    GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t53 + 8), t50);
    void * t54 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t49, &t53, t52);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t54;
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
__attribute__((aligned(4))) void * gm_f_construct_struct_ve6tb88(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_construct_struct_ve6tb88 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* id */
    gm_tjpqvfd72o62u gm_l5; /* info */
    gm_t6j6smj7qoctu gm_l6; /* h */
    gm_b16_8 gm_l7; /* fields */
    uint64_t gm_l8; /* n */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint8_t gm_l15; /* bad_args */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_t08olq97rscn3 gm_l20; /* fname */
    gm_tf5srh4mp4b3i gm_l22; /* found */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* i */
    uint64_t gm_l27; /* i */
    void * gm_l28; /* v */
    void * gm_l30; /* v */
    gm_t88rcucg1b7uk gm_l31; /* vt */
    gm_b16_8 gm_l32; /* src */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* w */
    void * gm_l36; /* x */
    void * gm_l37; /* base */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* k */
    gm_tep2drsl9408b gm_l41; /* sf */
    gm_tf5srh4mp4b3i gm_l42; /* found */
    gm_b16_8 gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    uint64_t gm_l45; /* i */
    uint64_t gm_l47; /* i */
    void * gm_l48; /* proj */
    uint8_t gm_l49; /* out_of_order */
    uint64_t gm_l50; /* <tmp> */
    uint64_t gm_l51; /* <tmp> */
    uint64_t gm_l52; /* k */
    gm_b16_8 gm_l53; /* <tmp> */
    uint64_t gm_l54; /* <tmp> */
    uint64_t gm_l55; /* w */
    void * gm_l56; /* v */
    uint64_t gm_l58; /* <tmp> */
    uint64_t gm_l59; /* <tmp> */
    uint64_t gm_l60; /* i */
    void * gm_l61; /* v */
    gm_t88rcucg1b7uk gm_l62; /* ft */
    void * gm_l63; /* d */
    void * gm_l64; /* agg */
    GM_STACK_CHECK(&F, 5699u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l65;
    gm_t8cc1mtbrhlpt gm_l66;
    gm_ttu09lij36f8c gm_l67;
    gm_tabb710d9mq2e gm_l68;
    gm_tabb710d9mq2e gm_l69;
    gm_th2lqbo77pvsc gm_l70;
    gm_b16_8 gm_l71;
    gm_tftb1odc739t5 gm_l72;
    gm_tftb1odc739t5 gm_l73;
    gm_tabb710d9mq2e gm_l74;
    gm_t8ta482bl6be0 gm_l75;
    uint64_t gm_l76;
    gm_tabb710d9mq2e gm_l77;
    gm_tabb710d9mq2e gm_l78;
    gm_tftb1odc739t5 gm_l79;
    gm_th2lqbo77pvsc gm_l80;
    gm_b16_8 gm_l81;
    gm_tjvm1fbs3qbum gm_l82;
    gm_t8ta482bl6be0 gm_l83;
    uint64_t gm_l84;
    gm_tabb710d9mq2e gm_l85;
    gm_tabb710d9mq2e gm_l86;
    uint8_t gm_l87;
    gm_tabb710d9mq2e gm_l88;
    gm_tftb1odc739t5 gm_l89;
    uint8_t gm_l90;
    gm_tkn42u7na3t81 gm_l91;
    gm_tcgrjq9toscke gm_l92;
    gm_tftb1odc739t5 gm_l93;
    void * gm_l94;
    gm_tftb1odc739t5 gm_l95;
    gm_tabb710d9mq2e gm_l96;
    gm_tftb1odc739t5 gm_l97;
    {
        {
            {
                {
                    void * t73 = gm_l0;
                    void * t74 = GM_LD(void *, ((uint8_t *)t73));
                    void * t75 = (void *)(((uint8_t *)t74) + 8);
                    gm_t88rcucg1b7uk t76 = gm_l1;
                    gm_f_kind_jcnt4hm((void *)&gm_l65, t75, GM_CONV(gm_csguankko4lmhd, t76));
                    uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l65) + 0));
                    uint8_t t78 = (uint8_t)(t77 == ((uint8_t)17ULL));
                    if (!t78) {
                        goto gm_lb2;
                    }
                    uint32_t t79 = GM_LD(uint32_t, (((uint8_t *)&gm_l65) + 8));
                    gm_l4 = t79;
                    {
                        {
                            void * t80 = gm_l0;
                            void * t81 = GM_LD(void *, ((uint8_t *)t80));
                            gm_t88rcucg1b7uk t82 = gm_l1;
                            gm_f_soa_info_pq4q43l((void *)&gm_l66, t81, GM_CONV(gm_csguankko4lmhd, t82));
                            uint8_t t83 = (uint8_t)((((uint8_t *)&gm_l66))[32] != 0);
                            if (!t83) {
                                goto gm_lb4;
                            }
                            memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l66), 32);
                            void * t84 = gm_l0;
                            gm_t88rcucg1b7uk t85 = gm_l1;
                            gm_b16_8 t86 = gm_l2;
                            gm_tl874t2atnlh3 t87 = gm_l3;
                            void * t88 = gm_f_soa_construct_8nun9tl((void *)NULL, cx, NULL, t84, t85, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l5)), t86, t87);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t88;
                            goto gm_fret;
                        }
gm_lb4:;
                    }
gm_lb3:;
                    {
                        {
                            void * t89 = gm_l0;
                            gm_t88rcucg1b7uk t90 = gm_l1;
                            gm_f_hidden_field_u7uao2s((void *)&gm_l67, t89, GM_CONV(gm_csguankko4lmhd, t90));
                            uint8_t t91 = (uint8_t)((((uint8_t *)&gm_l67))[32] != 0);
                            if (!t91) {
                                goto gm_lb6;
                            }
                            memmove(((uint8_t *)&gm_l6), ((uint8_t *)&gm_l67), 32);
                            void * t92 = gm_l0;
                            gm_tl874t2atnlh3 t93 = gm_l3;
                            void * t94 = gm_l0;
                            gm_t88rcucg1b7uk t95 = gm_l1;
                            gm_b16_8 t96 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t94, t95);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 16));
                            gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
                            gm_tfeab279c16rr t99;
                            GM_ST(gm_b16_8, ((uint8_t *)&t99 + 0), t96);
                            GM_ST(gm_b16_8, ((uint8_t *)&t99 + 16), t97);
                            GM_ST(gm_b16_8, ((uint8_t *)&t99 + 32), t98);
                            gm_f_format_0o67o8r((void *)&gm_l68, (void *)NULL, cx, NULL, &t99);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t100 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                            if (!t100) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 8, gm_src7nt72u848oha, 194, 24);
                            gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l68) + 8));
                            gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
                            gm_b0_1 t103 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t92, t93, t102);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t104 = gm_l0;
                            void * t105 = GM_LD(void *, ((uint8_t *)t104));
                            gm_tl874t2atnlh3 t106 = gm_l3;
                            void * t107 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t105, t106);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t107;
                            goto gm_fret;
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint32_t t108 = gm_l4;
                    void * t109 = gm_l0;
                    void * t110 = GM_LD(void *, ((uint8_t *)t109));
                    gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)t110) + 1848));
                    uint8_t t112 = 0;
                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t111) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t111))) + i * 4, ((uint8_t *)&t108))) { t112 = 1; break; }
                    uint8_t t113 = (uint8_t)(t112);
                    if (t113) {
                        void * t114 = gm_l0;
                        gm_tl874t2atnlh3 t115 = gm_l3;
                        void * t116 = gm_l0;
                        gm_t88rcucg1b7uk t117 = gm_l1;
                        gm_b16_8 t118 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t116, t117);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t119;
                        GM_ST(gm_b16_8, ((uint8_t *)&t119 + 0), t118);
                        gm_f_format_s1oodgm((void *)&gm_l69, (void *)NULL, cx, NULL, t119);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                        if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 8, gm_src7nt72u848oha, 200, 24);
                        gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l69) + 8));
                        gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
                        gm_b0_1 t123 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t114, t115, t122);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t124 = gm_l0;
                        void * t125 = GM_LD(void *, ((uint8_t *)t124));
                        gm_tl874t2atnlh3 t126 = gm_l3;
                        void * t127 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t125, t126);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t127;
                        goto gm_fret;
                    }
                }
                {
                    void * t128 = gm_l0;
                    void * t129 = GM_LD(void *, ((uint8_t *)t128));
                    gm_t88rcucg1b7uk t130 = gm_l1;
                    gm_f_struct_fields_chqu3it((void *)&gm_l70, t129, GM_CONV(gm_csguankko4lmhd, t130));
                    uint8_t t131 = (uint8_t)((((uint8_t *)&gm_l70))[16] != 0);
                    if (t131) {
                        gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l70));
                        gm_l71 = t132;
                    } else {
                        gm_b16_8 t133 = gm_f_empty_slice_ose1ba9();
                        gm_l71 = t133;
                    }
                    gm_b16_8 t134 = gm_l71;
                    gm_l7 = t134;
                    uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    gm_l8 = t135;
                    gm_trqfvmjtt4sij t136;
                    gm_f_list_ir0h3tc_2((void *)&t136, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l9 = t136;
                    gm_trqfvmjtt4sij t137;
                    gm_f_list_pmp06kq((void *)&t137, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l10 = t137;
                    gm_trqfvmjtt4sij t138;
                    gm_f_list_s83jn2q((void *)&t138, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l11 = t138;
                    gm_l13 = ((uint64_t)0ULL);
                    uint64_t t139 = gm_l8;
                    gm_l12 = t139;
                    {
                        for (;;) {
                            uint64_t t140 = gm_l13;
                            uint64_t t141 = gm_l12;
                            uint8_t t142 = (t140 < t141);
                            if (!t142) {
                                goto gm_lb7;
                            }
                            {
                                {
                                    {
                                        void * t143 = (void *)((uint8_t *)&F.l10);
                                        gm_tftb1odc739t5 t144 = gm_f_push_r2k6rjf(t143, ((void *)0));
                                        gm_l72 = t144;
                                        uint8_t t145 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                                        if (!t145) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 1, gm_src7nt72u848oha, 209, 9);
                                        void * t146 = (void *)((uint8_t *)&F.l11);
                                        gm_tftb1odc739t5 t147 = gm_f_push_vd1290p(t146, ((uint8_t)0));
                                        gm_l73 = t147;
                                        uint8_t t148 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
                                        if (!t148) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l73) + 1, gm_src7nt72u848oha, 210, 9);
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 208, 5);
                                }
gm_lb10:;
                            }
gm_lb9:;
                            uint64_t t149 = gm_l13;
                            uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
                            uint64_t t152 = r150;
                            gm_l13 = t152;
                        }
                    }
gm_lb7:;
                    gm_trqfvmjtt4sij t153;
                    gm_f_list_5dhgg4i((void *)&t153, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l14 = t153;
                    gm_l15 = ((uint8_t)0);
                    gm_b16_8 t154 = gm_l2;
                    gm_l16 = t154;
                    gm_l17 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            {
                                uint64_t t155 = gm_l17;
                                uint64_t t156 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                uint8_t t157 = (t155 < t156);
                                if (!t157) {
                                    goto gm_lb12;
                                }
                                {
                                    {
                                        void * t158 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                        uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                        uint64_t t160 = gm_l17;
                                        {
                                            {
                                                {
                                                    {
                                                        memmove(((uint8_t *)&F.l18), (((uint8_t *)t158) + (uintptr_t)t160 * 144), 144);
                                                        {
                                                            {
                                                                {
                                                                    uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
                                                                    uint8_t t162 = (uint8_t)(t161 == ((uint8_t)0ULL));
                                                                    if (!t162) {
                                                                        goto gm_lb18;
                                                                    }
                                                                    memmove(((uint8_t *)&F.l19), (((uint8_t *)&F.l18) + 8), 88);
                                                                    uint8_t t163 = gm_l15;
                                                                    uint8_t t164 = (uint8_t)!t163;
                                                                    if (t164) {
                                                                        void * t165 = gm_l0;
                                                                        gm_tl874t2atnlh3 t166 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 72));
                                                                        void * t167 = gm_l0;
                                                                        gm_t88rcucg1b7uk t168 = gm_l1;
                                                                        gm_b16_8 t169 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t167, t168);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        void * t170 = gm_l0;
                                                                        gm_t88rcucg1b7uk t171 = gm_l1;
                                                                        gm_b16_8 t172 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t170, t171);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        gm_t6j6smj7qoctu t173;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t173 + 0), t169);
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t173 + 16), t172);
                                                                        gm_f_format_1e409ou((void *)&gm_l74, (void *)NULL, cx, NULL, &t173);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        uint8_t t174 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                                                                        if (!t174) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 8, gm_src7nt72u848oha, 218, 38);
                                                                        gm_b16_8 t175 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l74) + 8));
                                                                        gm_b16_8 t176 = GM_LD(gm_b16_8, ((uint8_t *)&t175));
                                                                        gm_b0_1 t177 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t165, t166, t176);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                    }
                                                                    gm_l15 = ((uint8_t)1);
                                                                    goto gm_lb17;
                                                                }
gm_lb18:;
                                                                {
                                                                    uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
                                                                    uint8_t t179 = (uint8_t)(t178 == ((uint8_t)1ULL));
                                                                    if (!t179) {
                                                                        goto gm_lb19;
                                                                    }
                                                                    memmove(((uint8_t *)&gm_l20), (((uint8_t *)&F.l18) + 8), 32);
                                                                    memmove(((uint8_t *)&F.l21), ((((uint8_t *)&F.l18) + 8) + 32), 88);
                                                                    gm_tf5srh4mp4b3i t180;
                                                                    memset(&t180, 0, sizeof t180);
                                                                    gm_l22 = t180;
                                                                    gm_b16_8 t181 = gm_l7;
                                                                    gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
                                                                    gm_l23 = t182;
                                                                    gm_l24 = ((uint64_t)0ULL);
                                                                    {
                                                                        for (;;) {
                                                                            uint64_t t183 = gm_l24;
                                                                            uint64_t t184 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                                            uint8_t t185 = (t183 < t184);
                                                                            if (!t185) {
                                                                                goto gm_lb20;
                                                                            }
                                                                            {
                                                                                uint64_t t186 = gm_l24;
                                                                                void * t187 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                                                                uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                                                uint64_t t189 = gm_l24;
                                                                                gm_t0mglksrkfa5e t190 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t187) + (uintptr_t)t189 * 40));
                                                                                gm_t8ta482bl6be0 t191;
                                                                                GM_ST(uint64_t, ((uint8_t *)&t191 + 0), t186);
                                                                                GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&t191 + 8), t190);
                                                                                gm_l75 = t191;
                                                                                {
                                                                                    {
                                                                                        uint64_t t192 = GM_LD(uint64_t, ((uint8_t *)&gm_l75));
                                                                                        gm_l25 = t192;
                                                                                        memmove(((uint8_t *)&F.l26), (((uint8_t *)&gm_l75) + 8), 40);
                                                                                        gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
                                                                                        gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                                                                                        uint8_t t195 = (gm_eq0iqiubfm7e(((uint8_t *)&t193), ((uint8_t *)&t194)));
                                                                                        if (t195) {
                                                                                            uint64_t t196 = gm_l25;
                                                                                            gm_tf5srh4mp4b3i t197;
                                                                                            GM_ST(uint8_t, (uint8_t *)&t197 + 9, 0);
                                                                                            GM_ST(uint16_t, (uint8_t *)&t197 + 10, 0);
                                                                                            GM_ST(uint32_t, (uint8_t *)&t197 + 12, 0);
                                                                                            GM_ST(uint64_t, ((uint8_t *)&t197), t196);
                                                                                            ((uint8_t *)&t197)[8] = 1;
                                                                                            gm_l22 = t197;
                                                                                            goto gm_lb20;
                                                                                        }
                                                                                        goto gm_lb23;
                                                                                    }
gm_lb24:;
                                                                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 224, 17);
                                                                                }
gm_lb23:;
                                                                            }
gm_lb22:;
                                                                            uint64_t t198 = gm_l24;
                                                                            uint64_t r199; bool o200 = __builtin_add_overflow(t198, ((uint64_t)1ULL), &r199);
                                                                            uint64_t t201 = r199;
                                                                            gm_l24 = t201;
                                                                        }
                                                                    }
gm_lb20:;
                                                                    uint8_t t202 = (uint8_t)((((uint8_t *)&gm_l22))[8] != 0);
                                                                    if (t202) {
                                                                        uint64_t t203 = GM_LD(uint64_t, ((uint8_t *)&gm_l22));
                                                                        gm_l76 = t203;
                                                                    } else {
                                                                        void * t204 = gm_l0;
                                                                        gm_tl874t2atnlh3 t205 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l20) + 16));
                                                                        void * t206 = gm_l0;
                                                                        gm_t88rcucg1b7uk t207 = gm_l1;
                                                                        gm_b16_8 t208 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t206, t207);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                                                                        gm_t6j6smj7qoctu t210;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t210 + 0), t208);
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t210 + 16), t209);
                                                                        gm_f_format_lc07prd((void *)&gm_l77, (void *)NULL, cx, NULL, &t210);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        uint8_t t211 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                                                                        if (!t211) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 8, gm_src7nt72u848oha, 231, 42);
                                                                        gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l77) + 8));
                                                                        gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&t212));
                                                                        gm_b0_1 t214 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t204, t205, t213);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        void * t215 = gm_l0;
                                                                        void * t216 = (void *)((uint8_t *)&F.l21);
                                                                        gm_tkn42u7na3t81 t217;
                                                                        memset(&t217, 0, sizeof t217);
                                                                        void * t218 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t215, t216, t217);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        goto gm_lb14;
                                                                    }
                                                                    uint64_t t219 = gm_l76;
                                                                    gm_l27 = t219;
                                                                    void * t220 = GM_LD(void *, ((uint8_t *)&F.l11));
                                                                    uint64_t t221 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
                                                                    uint64_t t222 = gm_l27;
                                                                    if ((uintptr_t)t222 >= (uintptr_t)(t221)) gm_trap_index((uintptr_t)t222, t221, gm_src7nt72u848oha, 235, 20);
                                                                    uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)t220) + (uintptr_t)t222 * 1));
                                                                    if (t223) {
                                                                        void * t224 = gm_l0;
                                                                        gm_tl874t2atnlh3 t225 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l20) + 16));
                                                                        gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                                                                        gm_tnpifv20irnph t227;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t227 + 0), t226);
                                                                        gm_f_format_bbsjt91((void *)&gm_l78, (void *)NULL, cx, NULL, t227);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        uint8_t t228 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                                                                        if (!t228) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 8, gm_src7nt72u848oha, 235, 61);
                                                                        gm_b16_8 t229 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l78) + 8));
                                                                        gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&t229));
                                                                        gm_b0_1 t231 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t224, t225, t230);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                    }
                                                                    void * t232 = GM_LD(void *, ((uint8_t *)&F.l11));
                                                                    uint64_t t233 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
                                                                    uint64_t t234 = gm_l27;
                                                                    if ((uintptr_t)t234 >= (uintptr_t)(t233)) gm_trap_index((uintptr_t)t234, t233, gm_src7nt72u848oha, 236, 17);
                                                                    GM_ST(uint8_t, (((uint8_t *)t232) + (uintptr_t)t234 * 1), ((uint8_t)1));
                                                                    void * t235 = gm_l0;
                                                                    void * t236 = (void *)((uint8_t *)&F.l21);
                                                                    void * t237 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                                    uint64_t t238 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                                    uint64_t t239 = gm_l27;
                                                                    if ((uintptr_t)t239 >= (uintptr_t)(t238)) gm_trap_index((uintptr_t)t239, t238, gm_src7nt72u848oha, 237, 41);
                                                                    gm_t88rcucg1b7uk t240 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t237) + (uintptr_t)t239 * 40) + 16));
                                                                    void * t241 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t235, t236, t240);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l28 = t241;
                                                                    void * t242 = (void *)((uint8_t *)&F.l14);
                                                                    uint64_t t243 = gm_l27;
                                                                    gm_tftb1odc739t5 t244 = gm_f_push_ucgmlo2(t242, t243);
                                                                    gm_l79 = t244;
                                                                    uint8_t t245 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                                                                    if (!t245) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l79) + 1, gm_src7nt72u848oha, 238, 17);
                                                                    void * t246 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                                    uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                                    uint64_t t248 = gm_l27;
                                                                    if ((uintptr_t)t248 >= (uintptr_t)(t247)) gm_trap_index((uintptr_t)t248, t247, gm_src7nt72u848oha, 239, 17);
                                                                    void * t249 = gm_l28;
                                                                    GM_ST(void *, (((uint8_t *)t246) + (uintptr_t)t248 * 8), t249);
                                                                    goto gm_lb17;
                                                                }
gm_lb19:;
                                                            }
                                                            {
                                                                {
                                                                    uint8_t t250 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
                                                                    uint8_t t251 = (uint8_t)(t250 == ((uint8_t)2ULL));
                                                                    if (!t251) {
                                                                        goto gm_lb25;
                                                                    }
                                                                    memmove(((uint8_t *)&F.l29), (((uint8_t *)&F.l18) + 8), 88);
                                                                    void * t252 = gm_l0;
                                                                    void * t253 = (void *)((uint8_t *)&F.l29);
                                                                    gm_tkn42u7na3t81 t254;
                                                                    memset(&t254, 0, sizeof t254);
                                                                    void * t255 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t252, t253, t254);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l30 = t255;
                                                                    void * t256 = gm_l0;
                                                                    void * t257 = (void *)(((uint8_t *)t256) + 96);
                                                                    void * t258 = gm_l0;
                                                                    void * t259 = GM_LD(void *, ((uint8_t *)t258));
                                                                    void * t260 = gm_l30;
                                                                    gm_t88rcucg1b7uk t261 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t260));
                                                                    gm_t88rcucg1b7uk t262 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t257, t259, t261);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l31 = t262;
                                                                    void * t263 = gm_l0;
                                                                    gm_t88rcucg1b7uk t264 = gm_l31;
                                                                    void * t265 = (void *)((uint8_t *)&F.l29);
                                                                    gm_f_spread_source_fields_5csral7((void *)&gm_l80, (void *)NULL, cx, NULL, t263, t264, t265);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    uint8_t t266 = (uint8_t)((((uint8_t *)&gm_l80))[16] != 0);
                                                                    if (t266) {
                                                                        gm_b16_8 t267 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l80));
                                                                        gm_l81 = t267;
                                                                    } else {
                                                                        goto gm_lb14;
                                                                    }
                                                                    gm_b16_8 t268 = gm_l81;
                                                                    gm_l32 = t268;
                                                                    gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                                                    gm_l33 = t269;
                                                                    gm_l34 = ((uint64_t)0ULL);
                                                                    {
                                                                        for (;;) {
                                                                            uint64_t t270 = gm_l34;
                                                                            uint64_t t271 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                                                            uint8_t t272 = (t270 < t271);
                                                                            if (!t272) {
                                                                                goto gm_lb26;
                                                                            }
                                                                            {
                                                                                void * t273 = GM_LD(void *, ((uint8_t *)&gm_l33));
                                                                                uint64_t t274 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                                                                uint64_t t275 = gm_l34;
                                                                                {
                                                                                    {
                                                                                        uint64_t t276 = GM_LD(uint64_t, (((uint8_t *)t273) + (uintptr_t)t275 * 8));
                                                                                        gm_l35 = t276;
                                                                                        {
                                                                                            {
                                                                                                void * t277 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                                                                uint64_t t278 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                                                                uint64_t t279 = gm_l35;
                                                                                                if ((uintptr_t)t279 >= (uintptr_t)(t278)) gm_trap_index((uintptr_t)t279, t278, gm_src7nt72u848oha, 247, 29);
                                                                                                uint8_t t280 = (uint8_t)(GM_LD(void *, (((uint8_t *)t277) + (uintptr_t)t279 * 8)) != 0);
                                                                                                if (!t280) {
                                                                                                    goto gm_lb32;
                                                                                                }
                                                                                                void * t281 = GM_LD(void *, (((uint8_t *)t277) + (uintptr_t)t279 * 8));
                                                                                                gm_l36 = t281;
                                                                                                void * t282 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                                                                uint64_t t283 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                                                                uint64_t t284 = gm_l35;
                                                                                                if ((uintptr_t)t284 >= (uintptr_t)(t283)) gm_trap_index((uintptr_t)t284, t283, gm_src7nt72u848oha, 247, 46);
                                                                                                void * t285 = gm_l0;
                                                                                                void * t286 = gm_l36;
                                                                                                void * t287 = (void *)((uint8_t *)&F.l9);
                                                                                                void * t288 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t285, t286, t287);
                                                                                                if (gm_unw.kind) {
                                                                                                    goto gm_lad;
                                                                                                }
                                                                                                GM_ST(void *, (((uint8_t *)t282) + (uintptr_t)t284 * 8), t288);
                                                                                                goto gm_lb31;
                                                                                            }
gm_lb32:;
                                                                                        }
gm_lb31:;
                                                                                        goto gm_lb29;
                                                                                    }
gm_lb30:;
                                                                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 246, 17);
                                                                                }
gm_lb29:;
                                                                            }
gm_lb28:;
                                                                            uint64_t t289 = gm_l34;
                                                                            uint64_t r290; bool o291 = __builtin_add_overflow(t289, ((uint64_t)1ULL), &r290);
                                                                            uint64_t t292 = r290;
                                                                            gm_l34 = t292;
                                                                        }
                                                                    }
gm_lb26:;
                                                                    void * t293 = (void *)((uint8_t *)&F.l14);
                                                                    gm_f_clear_4qp22g2(t293);
                                                                    void * t294 = gm_l0;
                                                                    void * t295 = gm_l30;
                                                                    void * t296 = (void *)((uint8_t *)&F.l9);
                                                                    void * t297 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t294, t295, t296);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l37 = t297;
                                                                    gm_b16_8 t298 = gm_l32;
                                                                    gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)&t298));
                                                                    gm_l38 = t299;
                                                                    gm_l39 = ((uint64_t)0ULL);
                                                                    {
                                                                        for (;;) {
                                                                            uint64_t t300 = gm_l39;
                                                                            uint64_t t301 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                                                            uint8_t t302 = (t300 < t301);
                                                                            if (!t302) {
                                                                                goto gm_lb33;
                                                                            }
                                                                            {
                                                                                uint64_t t303 = gm_l39;
                                                                                void * t304 = GM_LD(void *, ((uint8_t *)&gm_l38));
                                                                                uint64_t t305 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                                                                uint64_t t306 = gm_l39;
                                                                                gm_tep2drsl9408b t307 = GM_LD(gm_tep2drsl9408b, (((uint8_t *)t304) + (uintptr_t)t306 * 24));
                                                                                gm_tjvm1fbs3qbum t308;
                                                                                GM_ST(uint64_t, ((uint8_t *)&t308 + 0), t303);
                                                                                GM_ST(gm_tep2drsl9408b, ((uint8_t *)&t308 + 8), t307);
                                                                                gm_l82 = t308;
                                                                                {
                                                                                    {
                                                                                        uint64_t t309 = GM_LD(uint64_t, ((uint8_t *)&gm_l82));
                                                                                        gm_l40 = t309;
                                                                                        memmove(((uint8_t *)&gm_l41), (((uint8_t *)&gm_l82) + 8), 24);
                                                                                        gm_tf5srh4mp4b3i t310;
                                                                                        memset(&t310, 0, sizeof t310);
                                                                                        gm_l42 = t310;
                                                                                        gm_b16_8 t311 = gm_l7;
                                                                                        gm_b16_8 t312 = GM_LD(gm_b16_8, ((uint8_t *)&t311));
                                                                                        gm_l43 = t312;
                                                                                        gm_l44 = ((uint64_t)0ULL);
                                                                                        {
                                                                                            for (;;) {
                                                                                                uint64_t t313 = gm_l44;
                                                                                                uint64_t t314 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                                                                                                uint8_t t315 = (t313 < t314);
                                                                                                if (!t315) {
                                                                                                    goto gm_lb38;
                                                                                                }
                                                                                                {
                                                                                                    uint64_t t316 = gm_l44;
                                                                                                    void * t317 = GM_LD(void *, ((uint8_t *)&gm_l43));
                                                                                                    uint64_t t318 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                                                                                                    uint64_t t319 = gm_l44;
                                                                                                    gm_t0mglksrkfa5e t320 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t317) + (uintptr_t)t319 * 40));
                                                                                                    gm_t8ta482bl6be0 t321;
                                                                                                    GM_ST(uint64_t, ((uint8_t *)&t321 + 0), t316);
                                                                                                    GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&t321 + 8), t320);
                                                                                                    gm_l83 = t321;
                                                                                                    {
                                                                                                        {
                                                                                                            uint64_t t322 = GM_LD(uint64_t, ((uint8_t *)&gm_l83));
                                                                                                            gm_l45 = t322;
                                                                                                            memmove(((uint8_t *)&F.l46), (((uint8_t *)&gm_l83) + 8), 40);
                                                                                                            gm_b16_8 t323 = GM_LD(gm_b16_8, ((uint8_t *)&F.l46));
                                                                                                            gm_b16_8 t324 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l41));
                                                                                                            uint8_t t325 = (gm_eq0iqiubfm7e(((uint8_t *)&t323), ((uint8_t *)&t324)));
                                                                                                            if (t325) {
                                                                                                                uint64_t t326 = gm_l45;
                                                                                                                gm_tf5srh4mp4b3i t327;
                                                                                                                GM_ST(uint8_t, (uint8_t *)&t327 + 9, 0);
                                                                                                                GM_ST(uint16_t, (uint8_t *)&t327 + 10, 0);
                                                                                                                GM_ST(uint32_t, (uint8_t *)&t327 + 12, 0);
                                                                                                                GM_ST(uint64_t, ((uint8_t *)&t327), t326);
                                                                                                                ((uint8_t *)&t327)[8] = 1;
                                                                                                                gm_l42 = t327;
                                                                                                                goto gm_lb38;
                                                                                                            }
                                                                                                            goto gm_lb41;
                                                                                                        }
gm_lb42:;
                                                                                                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 253, 21);
                                                                                                    }
gm_lb41:;
                                                                                                }
gm_lb40:;
                                                                                                uint64_t t328 = gm_l44;
                                                                                                uint64_t r329; bool o330 = __builtin_add_overflow(t328, ((uint64_t)1ULL), &r329);
                                                                                                uint64_t t331 = r329;
                                                                                                gm_l44 = t331;
                                                                                            }
                                                                                        }
gm_lb38:;
                                                                                        uint8_t t332 = (uint8_t)((((uint8_t *)&gm_l42))[8] != 0);
                                                                                        if (t332) {
                                                                                            uint64_t t333 = GM_LD(uint64_t, ((uint8_t *)&gm_l42));
                                                                                            gm_l84 = t333;
                                                                                        } else {
                                                                                            void * t334 = gm_l0;
                                                                                            gm_tl874t2atnlh3 t335 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l29) + 72));
                                                                                            void * t336 = gm_l0;
                                                                                            gm_t88rcucg1b7uk t337 = gm_l1;
                                                                                            gm_b16_8 t338 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t336, t337);
                                                                                            if (gm_unw.kind) {
                                                                                                goto gm_lad;
                                                                                            }
                                                                                            gm_b16_8 t339 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l41));
                                                                                            gm_t6j6smj7qoctu t340;
                                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t340 + 0), t338);
                                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t340 + 16), t339);
                                                                                            gm_f_format_6fdilsr((void *)&gm_l85, (void *)NULL, cx, NULL, &t340);
                                                                                            if (gm_unw.kind) {
                                                                                                goto gm_lad;
                                                                                            }
                                                                                            uint8_t t341 = (uint8_t)!((((uint8_t *)&gm_l85))[0] != 0);
                                                                                            if (!t341) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l85) + 8, gm_src7nt72u848oha, 260, 42);
                                                                                            gm_b16_8 t342 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l85) + 8));
                                                                                            gm_b16_8 t343 = GM_LD(gm_b16_8, ((uint8_t *)&t342));
                                                                                            gm_b0_1 t344 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t334, t335, t343);
                                                                                            if (gm_unw.kind) {
                                                                                                goto gm_lad;
                                                                                            }
                                                                                            goto gm_lb35;
                                                                                        }
                                                                                        uint64_t t345 = gm_l84;
                                                                                        gm_l47 = t345;
                                                                                        void * t346 = GM_LD(void *, ((uint8_t *)&F.l11));
                                                                                        uint64_t t347 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
                                                                                        uint64_t t348 = gm_l47;
                                                                                        if ((uintptr_t)t348 >= (uintptr_t)(t347)) gm_trap_index((uintptr_t)t348, t347, gm_src7nt72u848oha, 263, 24);
                                                                                        uint8_t t349 = GM_LD(uint8_t, (((uint8_t *)t346) + (uintptr_t)t348 * 1));
                                                                                        if (t349) {
                                                                                            void * t350 = gm_l0;
                                                                                            gm_tl874t2atnlh3 t351 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l29) + 72));
                                                                                            gm_b16_8 t352 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l41));
                                                                                            gm_tnpifv20irnph t353;
                                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t353 + 0), t352);
                                                                                            gm_f_format_adtncrl((void *)&gm_l86, (void *)NULL, cx, NULL, t353);
                                                                                            if (gm_unw.kind) {
                                                                                                goto gm_lad;
                                                                                            }
                                                                                            uint8_t t354 = (uint8_t)!((((uint8_t *)&gm_l86))[0] != 0);
                                                                                            if (!t354) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l86) + 8, gm_src7nt72u848oha, 266, 42);
                                                                                            gm_b16_8 t355 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l86) + 8));
                                                                                            gm_b16_8 t356 = GM_LD(gm_b16_8, ((uint8_t *)&t355));
                                                                                            gm_b0_1 t357 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t350, t351, t356);
                                                                                            if (gm_unw.kind) {
                                                                                                goto gm_lad;
                                                                                            }
                                                                                            goto gm_lb35;
                                                                                        }
                                                                                        void * t358 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                                                        uint64_t t359 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                                                        uint64_t t360 = gm_l47;
                                                                                        if ((uintptr_t)t360 >= (uintptr_t)(t359)) gm_trap_index((uintptr_t)t360, t359, gm_src7nt72u848oha, 269, 24);
                                                                                        {
                                                                                            {
                                                                                                uint8_t t361 = (uint8_t)(GM_LD(void *, (((uint8_t *)t358) + (uintptr_t)t360 * 8)) != 0);
                                                                                                if (t361) {
                                                                                                    goto gm_lb44;
                                                                                                }
                                                                                                gm_l87 = ((uint8_t)0);
                                                                                                goto gm_lb43;
                                                                                            }
gm_lb44:;
                                                                                            {
                                                                                                gm_l87 = ((uint8_t)1);
                                                                                                goto gm_lb43;
                                                                                            }
gm_lb45:;
                                                                                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 269, 24);
                                                                                        }
gm_lb43:;
                                                                                        uint8_t t362 = gm_l87;
                                                                                        if (t362) {
                                                                                            void * t363 = gm_l0;
                                                                                            gm_tl874t2atnlh3 t364 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l29) + 72));
                                                                                            gm_b16_8 t365 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l41));
                                                                                            gm_tnpifv20irnph t366;
                                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t366 + 0), t365);
                                                                                            gm_f_format_9tg469g((void *)&gm_l88, (void *)NULL, cx, NULL, t366);
                                                                                            if (gm_unw.kind) {
                                                                                                goto gm_lad;
                                                                                            }
                                                                                            uint8_t t367 = (uint8_t)!((((uint8_t *)&gm_l88))[0] != 0);
                                                                                            if (!t367) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l88) + 8, gm_src7nt72u848oha, 269, 70);
                                                                                            gm_b16_8 t368 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l88) + 8));
                                                                                            gm_b16_8 t369 = GM_LD(gm_b16_8, ((uint8_t *)&t368));
                                                                                            gm_b0_1 t370 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t363, t364, t369);
                                                                                            if (gm_unw.kind) {
                                                                                                goto gm_lad;
                                                                                            }
                                                                                        }
                                                                                        gm_t88rcucg1b7uk t371 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l41) + 16));
                                                                                        void * t372 = gm_l37;
                                                                                        uint64_t t373 = gm_l40;
                                                                                        uint32_t t374 = (uint32_t)t373;
                                                                                        gm_tbl8il92l97ut t375;
                                                                                        GM_ST(uint32_t, (uint8_t *)&t375 + 12, 0);
                                                                                        GM_ST(void *, ((uint8_t *)&t375 + 0), t372);
                                                                                        GM_ST(uint32_t, ((uint8_t *)&t375 + 8), t374);
                                                                                        gm_tl874t2atnlh3 t376 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l29) + 72));
                                                                                        gm_t2b6tm8c0vgo1 t377;
                                                                                        memset(&t377, 0, sizeof t377);
                                                                                        GM_ST(uint8_t, ((uint8_t *)&t377 + 0), ((uint8_t)19ULL));
                                                                                        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t377 + 8), t375);
                                                                                        void * t378 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t371, &t377, t376);
                                                                                        if (gm_unw.kind) {
                                                                                            goto gm_lad;
                                                                                        }
                                                                                        gm_l48 = t378;
                                                                                        void * t379 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                                                        uint64_t t380 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                                                        uint64_t t381 = gm_l47;
                                                                                        if ((uintptr_t)t381 >= (uintptr_t)(t380)) gm_trap_index((uintptr_t)t381, t380, gm_src7nt72u848oha, 271, 21);
                                                                                        void * t382 = gm_l0;
                                                                                        void * t383 = gm_l48;
                                                                                        void * t384 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                                                        uint64_t t385 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                                                        uint64_t t386 = gm_l47;
                                                                                        if ((uintptr_t)t386 >= (uintptr_t)(t385)) gm_trap_index((uintptr_t)t386, t385, gm_src7nt72u848oha, 271, 54);
                                                                                        gm_t88rcucg1b7uk t387 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t384) + (uintptr_t)t386 * 40) + 16));
                                                                                        void * t388 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t382, t383, t387);
                                                                                        if (gm_unw.kind) {
                                                                                            goto gm_lad;
                                                                                        }
                                                                                        GM_ST(void *, (((uint8_t *)t379) + (uintptr_t)t381 * 8), t388);
                                                                                        goto gm_lb36;
                                                                                    }
gm_lb37:;
                                                                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 251, 17);
                                                                                }
gm_lb36:;
                                                                            }
gm_lb35:;
                                                                            uint64_t t389 = gm_l39;
                                                                            uint64_t r390; bool o391 = __builtin_add_overflow(t389, ((uint64_t)1ULL), &r390);
                                                                            uint64_t t392 = r390;
                                                                            gm_l39 = t392;
                                                                        }
                                                                    }
gm_lb33:;
                                                                    goto gm_lb17;
                                                                }
gm_lb25:;
                                                            }
                                                            {
                                                                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 215, 9);
                                                            }
                                                        }
gm_lb17:;
                                                    }
                                                    {
                                                        goto gm_lb15;
                                                    }
                                                }
gm_lb16:;
                                            }
                                            {
                                                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 214, 5);
                                            }
                                        }
gm_lb15:;
                                    }
                                }
gm_lb14:;
                            }
                            {
                                uint64_t t393 = gm_l17;
                                uint64_t r394; bool o395 = __builtin_add_overflow(t393, ((uint64_t)1ULL), &r394);
                                uint64_t t396 = r394;
                                gm_l17 = t396;
                            }
                        }
                    }
gm_lb12:;
                }
                {
                    gm_l49 = ((uint8_t)0);
                    gm_l51 = ((uint64_t)1ULL);
                    uint64_t t397 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                    uint64_t t398 = (t397 > ((uint64_t)1ULL) ? t397 : ((uint64_t)1ULL));
                    gm_l50 = t398;
                    {
                        for (;;) {
                            uint64_t t399 = gm_l51;
                            uint64_t t400 = gm_l50;
                            uint8_t t401 = (t399 < t400);
                            if (!t401) {
                                goto gm_lb46;
                            }
                            {
                                {
                                    {
                                        uint64_t t402 = gm_l51;
                                        gm_l52 = t402;
                                        void * t403 = GM_LD(void *, ((uint8_t *)&F.l14));
                                        uint64_t t404 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                        uint64_t t405 = gm_l52;
                                        uint64_t r406; bool o407 = __builtin_sub_overflow(t405, ((uint64_t)1ULL), &r406);
                                        if (o407) gm_trap("integer overflow in `-`", gm_src7nt72u848oha, 280, 26);
                                        uint64_t t408 = r406;
                                        if ((uintptr_t)t408 >= (uintptr_t)(t404)) gm_trap_index((uintptr_t)t408, t404, gm_src7nt72u848oha, 280, 12);
                                        uint64_t t409 = GM_LD(uint64_t, (((uint8_t *)t403) + (uintptr_t)t408 * 8));
                                        void * t410 = GM_LD(void *, ((uint8_t *)&F.l14));
                                        uint64_t t411 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                                        uint64_t t412 = gm_l52;
                                        if ((uintptr_t)t412 >= (uintptr_t)(t411)) gm_trap_index((uintptr_t)t412, t411, gm_src7nt72u848oha, 280, 35);
                                        uint64_t t413 = GM_LD(uint64_t, (((uint8_t *)t410) + (uintptr_t)t412 * 8));
                                        uint8_t t414 = (t409 > t413);
                                        if (t414) {
                                            gm_l49 = ((uint8_t)1);
                                        }
                                        goto gm_lb49;
                                    }
gm_lb50:;
                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 279, 5);
                                }
gm_lb49:;
                            }
gm_lb48:;
                            uint64_t t415 = gm_l51;
                            uint64_t r416; bool o417 = __builtin_add_overflow(t415, ((uint64_t)1ULL), &r416);
                            uint64_t t418 = r416;
                            gm_l51 = t418;
                        }
                    }
gm_lb46:;
                    uint8_t t419 = gm_l49;
                    if (t419) {
                        gm_b16_8 t420 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                        gm_l53 = t420;
                        gm_l54 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t421 = gm_l54;
                                uint64_t t422 = GM_LD(uint64_t, (((uint8_t *)&gm_l53) + 8));
                                uint8_t t423 = (t421 < t422);
                                if (!t423) {
                                    goto gm_lb51;
                                }
                                {
                                    void * t424 = GM_LD(void *, ((uint8_t *)&gm_l53));
                                    uint64_t t425 = GM_LD(uint64_t, (((uint8_t *)&gm_l53) + 8));
                                    uint64_t t426 = gm_l54;
                                    {
                                        {
                                            uint64_t t427 = GM_LD(uint64_t, (((uint8_t *)t424) + (uintptr_t)t426 * 8));
                                            gm_l55 = t427;
                                            {
                                                {
                                                    void * t428 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                    uint64_t t429 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                    uint64_t t430 = gm_l55;
                                                    if ((uintptr_t)t430 >= (uintptr_t)(t429)) gm_trap_index((uintptr_t)t430, t429, gm_src7nt72u848oha, 284, 21);
                                                    uint8_t t431 = (uint8_t)(GM_LD(void *, (((uint8_t *)t428) + (uintptr_t)t430 * 8)) != 0);
                                                    if (!t431) {
                                                        goto gm_lb57;
                                                    }
                                                    void * t432 = GM_LD(void *, (((uint8_t *)t428) + (uintptr_t)t430 * 8));
                                                    gm_l56 = t432;
                                                    void * t433 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                    uint64_t t434 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                    uint64_t t435 = gm_l55;
                                                    if ((uintptr_t)t435 >= (uintptr_t)(t434)) gm_trap_index((uintptr_t)t435, t434, gm_src7nt72u848oha, 284, 38);
                                                    void * t436 = gm_l0;
                                                    void * t437 = gm_l56;
                                                    void * t438 = (void *)((uint8_t *)&F.l9);
                                                    void * t439 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t436, t437, t438);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    GM_ST(void *, (((uint8_t *)t433) + (uintptr_t)t435 * 8), t439);
                                                    goto gm_lb56;
                                                }
gm_lb57:;
                                            }
gm_lb56:;
                                            goto gm_lb54;
                                        }
gm_lb55:;
                                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 283, 9);
                                    }
gm_lb54:;
                                }
gm_lb53:;
                                uint64_t t440 = gm_l54;
                                uint64_t r441; bool o442 = __builtin_add_overflow(t440, ((uint64_t)1ULL), &r441);
                                uint64_t t443 = r441;
                                gm_l54 = t443;
                            }
                        }
gm_lb51:;
                    }
                }
                {
                    gm_trqfvmjtt4sij t444;
                    gm_f_list_vp1djv1_2((void *)&t444, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l57 = t444;
                    gm_l59 = ((uint64_t)0ULL);
                    uint64_t t445 = gm_l8;
                    gm_l58 = t445;
                    {
                        for (;;) {
                            uint64_t t446 = gm_l59;
                            uint64_t t447 = gm_l58;
                            uint8_t t448 = (t446 < t447);
                            if (!t448) {
                                goto gm_lb58;
                            }
                            {
                                {
                                    {
                                        uint64_t t449 = gm_l59;
                                        gm_l60 = t449;
                                        {
                                            {
                                                void * t450 = GM_LD(void *, ((uint8_t *)&F.l10));
                                                uint64_t t451 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                                uint64_t t452 = gm_l60;
                                                if ((uintptr_t)t452 >= (uintptr_t)(t451)) gm_trap_index((uintptr_t)t452, t451, gm_src7nt72u848oha, 289, 17);
                                                uint8_t t453 = (uint8_t)(GM_LD(void *, (((uint8_t *)t450) + (uintptr_t)t452 * 8)) != 0);
                                                if (!t453) {
                                                    goto gm_lb64;
                                                }
                                                void * t454 = GM_LD(void *, (((uint8_t *)t450) + (uintptr_t)t452 * 8));
                                                gm_l61 = t454;
                                                void * t455 = (void *)((uint8_t *)&F.l57);
                                                void * t456 = gm_l61;
                                                gm_tftb1odc739t5 t457 = gm_f_push_d943vgp_2(t455, &(*(gm_tcgrjq9toscke *)((uint8_t *)t456)));
                                                gm_l89 = t457;
                                                uint8_t t458 = (uint8_t)!((((uint8_t *)&gm_l89))[0] != 0);
                                                if (!t458) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l89) + 1, gm_src7nt72u848oha, 290, 13);
                                                goto gm_lb60;
                                            }
gm_lb64:;
                                        }
gm_lb63:;
                                        {
                                            {
                                                void * t459 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                uint64_t t460 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                uint64_t t461 = gm_l60;
                                                gm_b16_8 t462 = GM_LD(gm_b16_8, (((uint8_t *)t459) + (uintptr_t)t461 * 40));
                                                gm_b16_8 t463;
                                                GM_ST(void *, (uint8_t *)&t463, (void *)(gm_sfdbfrv46lihd));
                                                GM_ST(uintptr_t, (uint8_t *)&t463 + 8, (uintptr_t)(1));
                                                uint8_t t464 = (gm_eq0iqiubfm7e(((uint8_t *)&t462), ((uint8_t *)&t463)));
                                                if (!t464) {
                                                    goto gm_lb66;
                                                }
                                                void * t465 = gm_l0;
                                                void * t466 = GM_LD(void *, ((uint8_t *)t465));
                                                gm_t88rcucg1b7uk t467 = gm_l1;
                                                gm_tkn42u7na3t81 t468 = gm_f_bits_struct_ty_l1kjtap(t466, GM_CONV(gm_csguankko4lmhd, t467));
                                                gm_l91 = t468;
                                                {
                                                    {
                                                        uint8_t t469 = (uint8_t)((((uint8_t *)&gm_l91))[4] != 0);
                                                        if (t469) {
                                                            goto gm_lb68;
                                                        }
                                                        gm_l90 = ((uint8_t)0);
                                                        goto gm_lb67;
                                                    }
gm_lb68:;
                                                    {
                                                        gm_l90 = ((uint8_t)1);
                                                        goto gm_lb67;
                                                    }
gm_lb69:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 294, 38);
                                                }
gm_lb67:;
                                                uint8_t t470 = gm_l90;
                                                if (!t470) {
                                                    goto gm_lb66;
                                                }
                                                void * t471 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                uint64_t t472 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                uint64_t t473 = gm_l60;
                                                gm_t88rcucg1b7uk t474 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t471) + (uintptr_t)t473 * 40) + 16));
                                                gm_l62 = t474;
                                                void * t475 = (void *)((uint8_t *)&F.l57);
                                                void * t476 = gm_l0;
                                                void * t477 = GM_LD(void *, ((uint8_t *)t476));
                                                void * t478 = (void *)(((uint8_t *)t477) + 8);
                                                gm_t88rcucg1b7uk t479 = gm_l62;
                                                gm_t53at80pfdus3 t480;
                                                gm_f_kind_jcnt4hm((void *)&t480, t478, GM_CONV(gm_csguankko4lmhd, t479));
                                                uint8_t t481 = GM_LD(uint8_t, (((uint8_t *)&t480) + 0));
                                                uint8_t t482 = (uint8_t)((t481 == ((uint8_t)4ULL)));
                                                gm_t53at80pfdus3 t483;
                                                memset(&t483, 0, sizeof t483);
                                                GM_ST(uint8_t, ((uint8_t *)&t483 + 0), ((uint8_t)4ULL));
                                                if (t482) {
                                                    gm_t88rcucg1b7uk t484 = gm_l62;
                                                    gm_tl874t2atnlh3 t485 = gm_l3;
                                                    gm_t2b6tm8c0vgo1 t486;
                                                    memset(&t486, 0, sizeof t486);
                                                    GM_ST(uint8_t, ((uint8_t *)&t486 + 0), ((uint8_t)4ULL));
                                                    GM_ST(uint8_t, ((uint8_t *)&t486 + 8), ((uint8_t)0));
                                                    void * t487 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t484, &t486, t485);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    memmove(((uint8_t *)&gm_l92), ((uint8_t *)t487), 88);
                                                } else {
                                                    gm_t88rcucg1b7uk t488 = gm_l62;
                                                    gm_tbci5ospk5nnn t489;
                                                    gm_f_zero_s25l990((void *)&t489, (void *)NULL, cx, NULL);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tl874t2atnlh3 t490 = gm_l3;
                                                    gm_t2b6tm8c0vgo1 t491;
                                                    GM_ST(uint8_t, (uint8_t *)&t491 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t491 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t491 + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t491 + 56, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t491 + 0), ((uint8_t)0ULL));
                                                    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t491 + 8), t489);
                                                    void * t492 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t488, &t491, t490);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    memmove(((uint8_t *)&gm_l92), ((uint8_t *)t492), 88);
                                                }
                                                gm_tftb1odc739t5 t493 = gm_f_push_d943vgp_2(t475, &(*(gm_tcgrjq9toscke *)((uint8_t *)&gm_l92)));
                                                gm_l93 = t493;
                                                uint8_t t494 = (uint8_t)!((((uint8_t *)&gm_l93))[0] != 0);
                                                if (!t494) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l93) + 1, gm_src7nt72u848oha, 296, 13);
                                                goto gm_lb60;
                                            }
gm_lb66:;
                                        }
gm_lb65:;
                                        {
                                            {
                                                void * t495 = gm_l0;
                                                uint32_t t496 = gm_l4;
                                                uint64_t t497 = gm_l60;
                                                void * t498 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                uint64_t t499 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                uint64_t t500 = gm_l60;
                                                gm_t88rcucg1b7uk t501 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t498) + (uintptr_t)t500 * 40) + 16));
                                                void * t502 = gm_f_field_default_4ssmakm((void *)NULL, cx, NULL, t495, t496, t497, t501);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l94 = t502;
                                                uint8_t t503 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l94)) != 0);
                                                if (!t503) {
                                                    goto gm_lb71;
                                                }
                                                void * t504 = GM_LD(void *, ((uint8_t *)&gm_l94));
                                                gm_l63 = t504;
                                                void * t505 = (void *)((uint8_t *)&F.l57);
                                                void * t506 = gm_l63;
                                                gm_tftb1odc739t5 t507 = gm_f_push_d943vgp_2(t505, &(*(gm_tcgrjq9toscke *)((uint8_t *)t506)));
                                                gm_l95 = t507;
                                                uint8_t t508 = (uint8_t)!((((uint8_t *)&gm_l95))[0] != 0);
                                                if (!t508) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l95) + 1, gm_src7nt72u848oha, 300, 13);
                                                goto gm_lb70;
                                            }
gm_lb71:;
                                            uint8_t t509 = gm_l15;
                                            uint8_t t510 = (uint8_t)!t509;
                                            if (t510) {
                                                void * t511 = gm_l0;
                                                gm_tl874t2atnlh3 t512 = gm_l3;
                                                void * t513 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                                uint64_t t514 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                                uint64_t t515 = gm_l60;
                                                gm_b16_8 t516 = GM_LD(gm_b16_8, (((uint8_t *)t513) + (uintptr_t)t515 * 40));
                                                gm_tnpifv20irnph t517;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t517 + 0), t516);
                                                gm_f_format_843cvhd((void *)&gm_l96, (void *)NULL, cx, NULL, t517);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t518 = (uint8_t)!((((uint8_t *)&gm_l96))[0] != 0);
                                                if (!t518) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l96) + 8, gm_src7nt72u848oha, 302, 46);
                                                gm_b16_8 t519 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l96) + 8));
                                                gm_b16_8 t520 = GM_LD(gm_b16_8, ((uint8_t *)&t519));
                                                gm_b0_1 t521 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t511, t512, t520);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                            void * t522 = (void *)((uint8_t *)&F.l57);
                                            void * t523 = gm_l0;
                                            void * t524 = GM_LD(void *, ((uint8_t *)t523));
                                            gm_tl874t2atnlh3 t525 = gm_l3;
                                            void * t526 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t524, t525);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_tftb1odc739t5 t527 = gm_f_push_d943vgp_2(t522, &(*(gm_tcgrjq9toscke *)((uint8_t *)t526)));
                                            gm_l97 = t527;
                                            uint8_t t528 = (uint8_t)!((((uint8_t *)&gm_l97))[0] != 0);
                                            if (!t528) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l97) + 1, gm_src7nt72u848oha, 303, 13);
                                        }
gm_lb70:;
                                        goto gm_lb61;
                                    }
gm_lb62:;
                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 288, 5);
                                }
gm_lb61:;
                            }
gm_lb60:;
                            uint64_t t529 = gm_l59;
                            uint64_t r530; bool o531 = __builtin_add_overflow(t529, ((uint64_t)1ULL), &r530);
                            uint64_t t532 = r530;
                            gm_l59 = t532;
                        }
                    }
gm_lb58:;
                }
                {
                    gm_t88rcucg1b7uk t533 = gm_l1;
                    gm_b16_8 t534 = GM_LD(gm_b16_8, ((uint8_t *)&F.l57));
                    gm_tl874t2atnlh3 t535 = gm_l3;
                    gm_t2b6tm8c0vgo1 t536;
                    memset(&t536, 0, sizeof t536);
                    GM_ST(uint8_t, ((uint8_t *)&t536 + 0), ((uint8_t)35ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t536 + 8), t534);
                    void * t537 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t533, &t536, t535);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l64 = t537;
                    uint64_t t538 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                    uint8_t t539 = ((t538 == ((uint64_t)0ULL)));
                    if (t539) {
                        void * t540 = gm_l64;
                        F.ret = t540;
                        goto gm_fret;
                    }
                    gm_t88rcucg1b7uk t541 = gm_l1;
                    gm_b16_8 t542 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                    void * t543 = gm_l64;
                    gm_tl874t2atnlh3 t544 = gm_l3;
                    void * t545 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t541, t542, t543, t544);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t545;
                    goto gm_fret;
                }
            }
gm_lb2:;
        }
        {
            void * t546 = gm_l0;
            void * t547 = GM_LD(void *, ((uint8_t *)t546));
            gm_tl874t2atnlh3 t548 = gm_l3;
            void * t549 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t547, t548);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t549;
            goto gm_fret;
        }
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
__attribute__((aligned(4))) void * gm_f_construct_tuple_6ee5ejf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_construct_tuple_6ee5ejf F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    gm_b16_8 gm_l3; /* names */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* next */
    uint8_t gm_l10; /* spreading */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint8_t gm_l15; /* spread_failed */
    void * gm_l17; /* v */
    gm_t88rcucg1b7uk gm_l18; /* vt */
    gm_tnnjeerolos0a gm_l19; /* kind */
    gm_th2lqbo77pvsc gm_l20; /* what */
    gm_b16_8 gm_l21; /* w */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_t08olq97rscn3 gm_l27; /* n */
    uint64_t gm_l29; /* i */
    gm_b16_8 gm_l31; /* ps */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_tpouv9r7lrri2 gm_l34; /* p */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l38; /* i */
    gm_b16_8 gm_l39; /* n */
    void * gm_l40; /* v */
    uint64_t gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    uint64_t gm_l44; /* i */
    void * gm_l45; /* v */
    void * gm_l46; /* agg */
    GM_STACK_CHECK(&F, 3873u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tftb1odc739t5 gm_l47;
    uint8_t gm_l48;
    gm_th2lqbo77pvsc gm_l49;
    gm_t53at80pfdus3 gm_l50;
    gm_th2lqbo77pvsc gm_l51;
    gm_tabb710d9mq2e gm_l52;
    gm_tabb710d9mq2e gm_l53;
    gm_tftb1odc739t5 gm_l54;
    gm_tf5srh4mp4b3i gm_l55;
    uint64_t gm_l56;
    gm_tabb710d9mq2e gm_l57;
    gm_tftb1odc739t5 gm_l58;
    gm_th2lqbo77pvsc gm_l59;
    gm_b16_8 gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_tf5srh4mp4b3i gm_l62;
    uint64_t gm_l63;
    gm_tabb710d9mq2e gm_l64;
    gm_tabb710d9mq2e gm_l65;
    uint8_t gm_l66;
    gm_tabb710d9mq2e gm_l67;
    void * gm_l68;
    gm_tftb1odc739t5 gm_l69;
    gm_tabb710d9mq2e gm_l70;
    gm_tftb1odc739t5 gm_l71;
    {
        gm_trqfvmjtt4sij t54;
        gm_f_list_pmp06kq((void *)&t54, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t54;
        gm_b16_8 t55 = gm_l2;
        gm_l7 = t55;
        gm_l8 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t56 = gm_l8;
                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t58 = (t56 < t57);
                if (!t58) {
                    goto gm_lb1;
                }
                {
                    void * t59 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t61 = gm_l8;
                    {
                        {
                            void * t62 = (void *)((uint8_t *)&F.l6);
                            gm_tftb1odc739t5 t63 = gm_f_push_r2k6rjf(t62, ((void *)0));
                            gm_l47 = t63;
                            uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                            if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_src7nt72u848oha, 84, 22);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 84, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t65 = gm_l8;
                uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                uint64_t t68 = r66;
                gm_l8 = t68;
            }
        }
gm_lb1:;
        gm_l9 = ((uint64_t)0ULL);
        gm_l10 = ((uint8_t)0);
        gm_b16_8 t69 = gm_l4;
        gm_l11 = t69;
        gm_l12 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t70 = gm_l12;
                uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint8_t t72 = (t70 < t71);
                if (!t72) {
                    goto gm_lb6;
                }
                {
                    void * t73 = GM_LD(void *, ((uint8_t *)&gm_l11));
                    uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint64_t t75 = gm_l12;
                    {
                        {
                            memmove(((uint8_t *)&F.l13), (((uint8_t *)t73) + (uintptr_t)t75 * 144), 144);
                            {
                                {
                                    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
                                    uint8_t t77 = (uint8_t)(t76 == ((uint8_t)2ULL));
                                    if (!t77) {
                                        goto gm_lb12;
                                    }
                                    gm_l48 = ((uint8_t)1);
                                    goto gm_lb11;
                                }
gm_lb12:;
                                {
                                    gm_l48 = ((uint8_t)0);
                                    goto gm_lb11;
                                }
gm_lb13:;
                                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 89, 12);
                            }
gm_lb11:;
                            uint8_t t78 = gm_l48;
                            if (t78) {
                                gm_l10 = ((uint8_t)1);
                            }
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 88, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t79 = gm_l12;
                uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                uint64_t t82 = r80;
                gm_l12 = t82;
            }
        }
gm_lb6:;
    }
    {
        gm_trqfvmjtt4sij t83;
        gm_f_list_ir0h3tc_2((void *)&t83, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l14 = t83;
        gm_l15 = ((uint8_t)0);
        {
            {
                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint8_t t85 = ((t84 == ((uint64_t)1ULL)));
                if (!t85) {
                    goto gm_lb15;
                }
                uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint8_t t87 = (!(t86 == ((uint64_t)1ULL)));
                if (!t87) {
                    goto gm_lb15;
                }
                void * t88 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t89)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t89, gm_src7nt72u848oha, 96, 67);
                uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t88) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                uint8_t t91 = (uint8_t)(t90 == ((uint8_t)0ULL));
                if (!t91) {
                    goto gm_lb15;
                }
                memmove(((uint8_t *)&F.l16), ((((uint8_t *)t88) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                void * t92 = gm_l0;
                void * t93 = (void *)((uint8_t *)&F.l16);
                gm_tkn42u7na3t81 t94;
                memset(&t94, 0, sizeof t94);
                void * t95 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t92, t93, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t95;
                void * t96 = gm_l0;
                void * t97 = (void *)(((uint8_t *)t96) + 96);
                void * t98 = gm_l0;
                void * t99 = GM_LD(void *, ((uint8_t *)t98));
                void * t100 = gm_l17;
                gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t100));
                gm_t88rcucg1b7uk t102 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t97, t99, t101);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t102;
                void * t103 = gm_l0;
                void * t104 = GM_LD(void *, ((uint8_t *)t103));
                void * t105 = (void *)(((uint8_t *)t104) + 8);
                gm_t88rcucg1b7uk t106 = gm_l18;
                gm_f_kind_jcnt4hm((void *)&gm_l50, t105, GM_CONV(gm_csguankko4lmhd, t106));
                {
                    {
                        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                        uint8_t t108 = (uint8_t)(t107 == ((uint8_t)12ULL));
                        if (!t108) {
                            goto gm_lb17;
                        }
                        gm_b16_8 t109;
                        GM_ST(void *, (uint8_t *)&t109, (void *)(gm_sto8li44rv2ou));
                        GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(7));
                        gm_th2lqbo77pvsc t110;
                        GM_ST(uint8_t, (uint8_t *)&t110 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t110 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t110 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t110), t109);
                        ((uint8_t *)&t110)[16] = 1;
                        gm_l49 = t110;
                        goto gm_lb16;
                    }
gm_lb17:;
                    {
                        uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                        uint8_t t112 = (uint8_t)(t111 == ((uint8_t)11ULL));
                        if (!t112) {
                            goto gm_lb18;
                        }
                        gm_tnnjeerolos0a t113 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&gm_l50) + 8) + 12));
                        gm_l19 = t113;
                        gm_tnnjeerolos0a t114 = gm_l19;
                        gm_tnnjeerolos0a t115;
                        GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)0ULL));
                        gm_tnnjeerolos0a t116;
                        GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)2ULL));
                        gm_tcf2f71ra2nps t117;
                        GM_ST(gm_tnnjeerolos0a, ((uint8_t *)&t117 + 0), t115);
                        GM_ST(gm_tnnjeerolos0a, ((uint8_t *)&t117 + 8), t116);
                        uint8_t t118 = 0;
                        if (gm_eqb2ul45tidi(((uint8_t *)&t117) + 0, ((uint8_t *)&t114))) t118 = 1;
                        if (gm_eqb2ul45tidi(((uint8_t *)&t117) + 8, ((uint8_t *)&t114))) t118 = 1;
                        uint8_t t119 = (uint8_t)(t118);
                        if (t119) {
                            gm_b16_8 t120;
                            GM_ST(void *, (uint8_t *)&t120, (void *)(gm_srrqh3pev0igg));
                            GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(8));
                            gm_th2lqbo77pvsc t121;
                            GM_ST(uint8_t, (uint8_t *)&t121 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t121 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t121 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t121), t120);
                            ((uint8_t *)&t121)[16] = 1;
                            gm_l51 = t121;
                        } else {
                            gm_th2lqbo77pvsc t122;
                            memset(&t122, 0, sizeof t122);
                            gm_l51 = t122;
                        }
                        memmove(((uint8_t *)&gm_l49), ((uint8_t *)&gm_l51), 24);
                        goto gm_lb16;
                    }
gm_lb18:;
                    {
                        gm_th2lqbo77pvsc t123;
                        memset(&t123, 0, sizeof t123);
                        gm_l49 = t123;
                        goto gm_lb16;
                    }
gm_lb19:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 99, 25);
                }
gm_lb16:;
                memmove(((uint8_t *)&gm_l20), ((uint8_t *)&gm_l49), 24);
                {
                    {
                        uint8_t t124 = (uint8_t)((((uint8_t *)&gm_l20))[16] != 0);
                        if (!t124) {
                            goto gm_lb21;
                        }
                        gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                        gm_l21 = t125;
                        void * t126 = gm_l0;
                        gm_tl874t2atnlh3 t127 = gm_l5;
                        void * t128 = gm_l0;
                        gm_t88rcucg1b7uk t129 = gm_l1;
                        gm_b16_8 t130 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t128, t129);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t131 = gm_l21;
                        void * t132 = gm_l0;
                        gm_t88rcucg1b7uk t133 = gm_l1;
                        gm_b16_8 t134 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t132, t133);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tfeab279c16rr t135;
                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 0), t130);
                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 16), t131);
                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 32), t134);
                        gm_f_format_6nmb3uf((void *)&gm_l52, (void *)NULL, cx, NULL, &t135);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t136 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                        if (!t136) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 8, gm_src7nt72u848oha, 105, 28);
                        gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l52) + 8));
                        gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
                        gm_b0_1 t139 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t126, t127, t138);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t140 = gm_l0;
                        void * t141 = GM_LD(void *, ((uint8_t *)t140));
                        gm_tl874t2atnlh3 t142 = gm_l5;
                        void * t143 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t141, t142);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t143;
                        goto gm_fret;
                    }
gm_lb21:;
                }
gm_lb20:;
                uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint8_t t145 = (t144 > ((uint64_t)0ULL));
                if (t145) {
                    void * t146 = GM_LD(void *, ((uint8_t *)&F.l6));
                    uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t147)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t147, gm_src7nt72u848oha, 109, 13);
                    void * t148 = gm_l0;
                    void * t149 = gm_l17;
                    void * t150 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    gm_t88rcucg1b7uk t152 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t150) + (uintptr_t)((uint64_t)0ULL) * 4));
                    void * t153 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t148, t149, t152);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(void *, (((uint8_t *)t146) + (uintptr_t)((uint64_t)0ULL) * 8), t153);
                    gm_l9 = ((uint64_t)1ULL);
                }
                goto gm_lb14;
            }
gm_lb15:;
        }
gm_lb14:;
    }
    {
        void * t154 = GM_LD(void *, ((uint8_t *)&gm_l4));
        uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        uint64_t t156 = gm_l9;
        uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        uint64_t t158 = (t156 < t157 ? t156 : t157);
        uint64_t t159 = (uintptr_t)t158;
        uint64_t t160 = (uintptr_t)t155;
        if (t159 > t160 || t160 > (uintptr_t)t155) gm_trap_slice(t159, t160, t155, gm_src7nt72u848oha, 113, 16);
        uint64_t t161 = ((uintptr_t)t155) - (uintptr_t)t158;
        gm_b16_8 t162;
        GM_ST(void *, (uint8_t *)&t162, (void *)(((uint8_t *)t154 + (uintptr_t)t158 * 144)));
        GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(t161));
        gm_l22 = t162;
        gm_l23 = ((uint64_t)0ULL);
        {
            for (;;) {
                {
                    uint64_t t163 = gm_l23;
                    uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                    uint8_t t165 = (t163 < t164);
                    if (!t165) {
                        goto gm_lb22;
                    }
                    {
                        {
                            void * t166 = GM_LD(void *, ((uint8_t *)&gm_l22));
                            uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                            uint64_t t168 = gm_l23;
                            {
                                {
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l24), (((uint8_t *)t166) + (uintptr_t)t168 * 144), 144);
                                            gm_trqfvmjtt4sij t169;
                                            gm_f_list_5kthfl8((void *)&t169, (void *)NULL, cx, NULL);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            F.l25 = t169;
                                            {
                                                {
                                                    uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&F.l24) + 0));
                                                    uint8_t t171 = (uint8_t)(t170 == ((uint8_t)0ULL));
                                                    if (!t171) {
                                                        goto gm_lb28;
                                                    }
                                                    memmove(((uint8_t *)&F.l26), (((uint8_t *)&F.l24) + 8), 88);
                                                    uint64_t t172 = gm_l9;
                                                    uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                    uint8_t t174 = (t172 >= t173);
                                                    if (t174) {
                                                        void * t175 = gm_l0;
                                                        gm_tl874t2atnlh3 t176 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 72));
                                                        void * t177 = gm_l0;
                                                        gm_t88rcucg1b7uk t178 = gm_l1;
                                                        gm_b16_8 t179 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t177, t178);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_tnpifv20irnph t180;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t180 + 0), t179);
                                                        gm_f_format_bm4rhr0((void *)&gm_l53, (void *)NULL, cx, NULL, t180);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t181 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                                                        if (!t181) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 8, gm_src7nt72u848oha, 118, 38);
                                                        gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l53) + 8));
                                                        gm_b16_8 t183 = GM_LD(gm_b16_8, ((uint8_t *)&t182));
                                                        gm_b0_1 t184 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t175, t176, t183);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        goto gm_lb24;
                                                    }
                                                    void * t185 = (void *)((uint8_t *)&F.l25);
                                                    gm_th2lqbo77pvsc t186;
                                                    memset(&t186, 0, sizeof t186);
                                                    void * t187 = gm_l0;
                                                    void * t188 = (void *)((uint8_t *)&F.l26);
                                                    void * t189 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                                    uint64_t t190 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                    uint64_t t191 = gm_l9;
                                                    gm_t88rcucg1b7uk t192 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t189) + (uintptr_t)t191 * 4));
                                                    void * t193 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t187, t188, t192);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tl874t2atnlh3 t194 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 72));
                                                    gm_tm7a6rksvs6n5 t195;
                                                    GM_ST(uint32_t, (uint8_t *)&t195 + 44, 0);
                                                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t195 + 0), t186);
                                                    GM_ST(void *, ((uint8_t *)&t195 + 24), t193);
                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t195 + 32), t194);
                                                    gm_tftb1odc739t5 t196 = gm_f_push_9a0n4ld(t185, &t195);
                                                    gm_l54 = t196;
                                                    uint8_t t197 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                                                    if (!t197) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_src7nt72u848oha, 121, 17);
                                                    goto gm_lb27;
                                                }
gm_lb28:;
                                                {
                                                    uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)&F.l24) + 0));
                                                    uint8_t t199 = (uint8_t)(t198 == ((uint8_t)1ULL));
                                                    if (!t199) {
                                                        goto gm_lb29;
                                                    }
                                                    memmove(((uint8_t *)&gm_l27), (((uint8_t *)&F.l24) + 8), 32);
                                                    memmove(((uint8_t *)&F.l28), ((((uint8_t *)&F.l24) + 8) + 32), 88);
                                                    gm_b16_8 t200 = gm_l3;
                                                    gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l27));
                                                    gm_tf5srh4mp4b3i t202 = gm_f_index_of_o9r57ha(t200, t201);
                                                    gm_l55 = t202;
                                                    uint8_t t203 = (uint8_t)((((uint8_t *)&gm_l55))[8] != 0);
                                                    if (t203) {
                                                        uint64_t t204 = GM_LD(uint64_t, ((uint8_t *)&gm_l55));
                                                        gm_l56 = t204;
                                                    } else {
                                                        void * t205 = gm_l0;
                                                        gm_tl874t2atnlh3 t206 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l27) + 16));
                                                        void * t207 = gm_l0;
                                                        gm_t88rcucg1b7uk t208 = gm_l1;
                                                        gm_b16_8 t209 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t207, t208);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l27));
                                                        gm_t6j6smj7qoctu t211;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t211 + 0), t209);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t211 + 16), t210);
                                                        gm_f_format_3tnlp8q((void *)&gm_l57, (void *)NULL, cx, NULL, &t211);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t212 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                                                        if (!t212) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 8, gm_src7nt72u848oha, 125, 38);
                                                        gm_b16_8 t213 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l57) + 8));
                                                        gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&t213));
                                                        gm_b0_1 t215 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t205, t206, t214);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        goto gm_lb24;
                                                    }
                                                    uint64_t t216 = gm_l56;
                                                    gm_l29 = t216;
                                                    void * t217 = (void *)((uint8_t *)&F.l25);
                                                    gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l27));
                                                    gm_th2lqbo77pvsc t219;
                                                    GM_ST(uint8_t, (uint8_t *)&t219 + 17, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t219 + 18, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t219 + 20, 0);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t219), t218);
                                                    ((uint8_t *)&t219)[16] = 1;
                                                    void * t220 = gm_l0;
                                                    void * t221 = (void *)((uint8_t *)&F.l28);
                                                    void * t222 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                                    uint64_t t223 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                    uint64_t t224 = gm_l29;
                                                    if ((uintptr_t)t224 >= (uintptr_t)(t223)) gm_trap_index((uintptr_t)t224, t223, gm_src7nt72u848oha, 128, 78);
                                                    gm_t88rcucg1b7uk t225 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t222) + (uintptr_t)t224 * 4));
                                                    void * t226 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t220, t221, t225);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tl874t2atnlh3 t227 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l27) + 16));
                                                    gm_tm7a6rksvs6n5 t228;
                                                    GM_ST(uint32_t, (uint8_t *)&t228 + 44, 0);
                                                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t228 + 0), t219);
                                                    GM_ST(void *, ((uint8_t *)&t228 + 24), t226);
                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t228 + 32), t227);
                                                    gm_tftb1odc739t5 t229 = gm_f_push_9a0n4ld(t217, &t228);
                                                    gm_l58 = t229;
                                                    uint8_t t230 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                                    if (!t230) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_src7nt72u848oha, 128, 17);
                                                    goto gm_lb27;
                                                }
gm_lb29:;
                                                {
                                                    uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&F.l24) + 0));
                                                    uint8_t t232 = (uint8_t)(t231 == ((uint8_t)2ULL));
                                                    if (!t232) {
                                                        goto gm_lb30;
                                                    }
                                                    memmove(((uint8_t *)&F.l30), (((uint8_t *)&F.l24) + 8), 88);
                                                    void * t233 = gm_l0;
                                                    void * t234 = (void *)((uint8_t *)&F.l30);
                                                    void * t235 = (void *)((uint8_t *)&F.l14);
                                                    gm_f_spread_parts_e62q2l2((void *)&gm_l59, (void *)NULL, cx, NULL, t233, t234, t235);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t236 = (uint8_t)((((uint8_t *)&gm_l59))[16] != 0);
                                                    if (t236) {
                                                        gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l59));
                                                        gm_l60 = t237;
                                                    } else {
                                                        gm_l15 = ((uint8_t)1);
                                                        goto gm_lb24;
                                                    }
                                                    gm_b16_8 t238 = gm_l60;
                                                    gm_l31 = t238;
                                                    gm_b16_8 t239 = gm_l31;
                                                    gm_l32 = t239;
                                                    gm_l33 = ((uint64_t)0ULL);
                                                    {
                                                        for (;;) {
                                                            uint64_t t240 = gm_l33;
                                                            uint64_t t241 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                                            uint8_t t242 = (t240 < t241);
                                                            if (!t242) {
                                                                goto gm_lb31;
                                                            }
                                                            {
                                                                void * t243 = GM_LD(void *, ((uint8_t *)&gm_l32));
                                                                uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                                                uint64_t t245 = gm_l33;
                                                                {
                                                                    {
                                                                        memmove(((uint8_t *)&gm_l34), (((uint8_t *)t243) + (uintptr_t)t245 * 32), 32);
                                                                        void * t246 = (void *)((uint8_t *)&F.l25);
                                                                        void * t247 = GM_LD(void *, (((uint8_t *)&gm_l34) + 24));
                                                                        gm_tl874t2atnlh3 t248 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l30) + 72));
                                                                        gm_th2lqbo77pvsc t249 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l34));
                                                                        gm_tm7a6rksvs6n5 t250;
                                                                        GM_ST(uint32_t, (uint8_t *)&t250 + 44, 0);
                                                                        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t250 + 0), t249);
                                                                        GM_ST(void *, ((uint8_t *)&t250 + 24), t247);
                                                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t250 + 32), t248);
                                                                        gm_tftb1odc739t5 t251 = gm_f_push_9a0n4ld(t246, &t250);
                                                                        gm_l61 = t251;
                                                                        uint8_t t252 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                                                        if (!t252) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_src7nt72u848oha, 135, 31);
                                                                        goto gm_lb34;
                                                                    }
gm_lb35:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 135, 17);
                                                                }
gm_lb34:;
                                                            }
gm_lb33:;
                                                            uint64_t t253 = gm_l33;
                                                            uint64_t r254; bool o255 = __builtin_add_overflow(t253, ((uint64_t)1ULL), &r254);
                                                            uint64_t t256 = r254;
                                                            gm_l33 = t256;
                                                        }
                                                    }
gm_lb31:;
                                                    goto gm_lb27;
                                                }
gm_lb30:;
                                                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 115, 9);
                                            }
gm_lb27:;
                                        }
                                        {
                                            gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
                                            gm_l35 = t257;
                                            gm_l36 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t258 = gm_l36;
                                                    uint64_t t259 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                                    uint8_t t260 = (t258 < t259);
                                                    if (!t260) {
                                                        goto gm_lb36;
                                                    }
                                                    {
                                                        void * t261 = GM_LD(void *, ((uint8_t *)&gm_l35));
                                                        uint64_t t262 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                                        uint64_t t263 = gm_l36;
                                                        {
                                                            {
                                                                memmove(((uint8_t *)&F.l37), (((uint8_t *)t261) + (uintptr_t)t263 * 48), 48);
                                                                gm_l38 = ((uint64_t)0ULL);
                                                                {
                                                                    {
                                                                        uint8_t t264 = (uint8_t)((((uint8_t *)&F.l37))[16] != 0);
                                                                        if (!t264) {
                                                                            goto gm_lb42;
                                                                        }
                                                                        gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&F.l37));
                                                                        gm_l39 = t265;
                                                                        gm_b16_8 t266 = gm_l3;
                                                                        gm_b16_8 t267 = gm_l39;
                                                                        gm_tf5srh4mp4b3i t268 = gm_f_index_of_o9r57ha(t266, t267);
                                                                        gm_l62 = t268;
                                                                        uint8_t t269 = (uint8_t)((((uint8_t *)&gm_l62))[8] != 0);
                                                                        if (t269) {
                                                                            uint64_t t270 = GM_LD(uint64_t, ((uint8_t *)&gm_l62));
                                                                            gm_l63 = t270;
                                                                        } else {
                                                                            void * t271 = gm_l0;
                                                                            gm_tl874t2atnlh3 t272 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l37) + 32));
                                                                            void * t273 = gm_l0;
                                                                            gm_t88rcucg1b7uk t274 = gm_l1;
                                                                            gm_b16_8 t275 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t273, t274);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            gm_b16_8 t276 = gm_l39;
                                                                            gm_t6j6smj7qoctu t277;
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t277 + 0), t275);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t277 + 16), t276);
                                                                            gm_f_format_3tnlp8q((void *)&gm_l64, (void *)NULL, cx, NULL, &t277);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            uint8_t t278 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                                                                            if (!t278) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 8, gm_src7nt72u848oha, 142, 39);
                                                                            gm_b16_8 t279 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l64) + 8));
                                                                            gm_b16_8 t280 = GM_LD(gm_b16_8, ((uint8_t *)&t279));
                                                                            gm_b0_1 t281 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t271, t272, t280);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            goto gm_lb38;
                                                                        }
                                                                        uint64_t t282 = gm_l63;
                                                                        gm_l38 = t282;
                                                                        goto gm_lb41;
                                                                    }
gm_lb42:;
                                                                    uint64_t t283 = gm_l9;
                                                                    uint64_t t284 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                                    uint8_t t285 = (t283 >= t284);
                                                                    if (t285) {
                                                                        void * t286 = gm_l0;
                                                                        gm_tl874t2atnlh3 t287 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l37) + 32));
                                                                        void * t288 = gm_l0;
                                                                        gm_t88rcucg1b7uk t289 = gm_l1;
                                                                        gm_b16_8 t290 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t288, t289);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        gm_tnpifv20irnph t291;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t291 + 0), t290);
                                                                        gm_f_format_bm4rhr0((void *)&gm_l65, (void *)NULL, cx, NULL, t291);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        uint8_t t292 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                                                                        if (!t292) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 8, gm_src7nt72u848oha, 147, 39);
                                                                        gm_b16_8 t293 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
                                                                        gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)&t293));
                                                                        gm_b0_1 t295 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t286, t287, t294);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        goto gm_lb36;
                                                                    }
                                                                    uint64_t t296 = gm_l9;
                                                                    gm_l38 = t296;
                                                                    uint64_t t297 = gm_l9;
                                                                    uint64_t r298; bool o299 = __builtin_add_overflow(t297, ((uint64_t)1ULL), &r298);
                                                                    if (o299) gm_trap("integer overflow in `+`", gm_src7nt72u848oha, 151, 17);
                                                                    uint64_t t300 = r298;
                                                                    gm_l9 = t300;
                                                                }
gm_lb41:;
                                                                void * t301 = GM_LD(void *, ((uint8_t *)&F.l6));
                                                                uint64_t t302 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                                                                uint64_t t303 = gm_l38;
                                                                if ((uintptr_t)t303 >= (uintptr_t)(t302)) gm_trap_index((uintptr_t)t303, t302, gm_src7nt72u848oha, 153, 16);
                                                                {
                                                                    {
                                                                        uint8_t t304 = (uint8_t)(GM_LD(void *, (((uint8_t *)t301) + (uintptr_t)t303 * 8)) != 0);
                                                                        if (t304) {
                                                                            goto gm_lb44;
                                                                        }
                                                                        gm_l66 = ((uint8_t)0);
                                                                        goto gm_lb43;
                                                                    }
gm_lb44:;
                                                                    {
                                                                        gm_l66 = ((uint8_t)1);
                                                                        goto gm_lb43;
                                                                    }
gm_lb45:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 153, 16);
                                                                }
gm_lb43:;
                                                                uint8_t t305 = gm_l66;
                                                                if (t305) {
                                                                    void * t306 = gm_l0;
                                                                    gm_tl874t2atnlh3 t307 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l37) + 32));
                                                                    uint64_t t308 = gm_l38;
                                                                    void * t309 = gm_l0;
                                                                    gm_t88rcucg1b7uk t310 = gm_l1;
                                                                    gm_b16_8 t311 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t309, t310);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_tf1bjg81u0glv t312;
                                                                    GM_ST(uint64_t, ((uint8_t *)&t312 + 0), t308);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t312 + 8), t311);
                                                                    gm_f_format_t8pf89n((void *)&gm_l67, (void *)NULL, cx, NULL, &t312);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    uint8_t t313 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                                                                    if (!t313) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 8, gm_src7nt72u848oha, 153, 63);
                                                                    gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                                                                    gm_b16_8 t315 = GM_LD(gm_b16_8, ((uint8_t *)&t314));
                                                                    gm_b0_1 t316 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t306, t307, t315);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                }
                                                                void * t317 = gm_l0;
                                                                void * t318 = GM_LD(void *, (((uint8_t *)&F.l37) + 24));
                                                                void * t319 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                                                uint64_t t320 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                                                uint64_t t321 = gm_l38;
                                                                if ((uintptr_t)t321 >= (uintptr_t)(t320)) gm_trap_index((uintptr_t)t321, t320, gm_src7nt72u848oha, 154, 40);
                                                                gm_t88rcucg1b7uk t322 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t319) + (uintptr_t)t321 * 4));
                                                                void * t323 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t317, t318, t322);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                gm_l40 = t323;
                                                                void * t324 = GM_LD(void *, ((uint8_t *)&F.l6));
                                                                uint64_t t325 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                                                                uint64_t t326 = gm_l38;
                                                                if ((uintptr_t)t326 >= (uintptr_t)(t325)) gm_trap_index((uintptr_t)t326, t325, gm_src7nt72u848oha, 155, 13);
                                                                uint8_t t327 = gm_l10;
                                                                if (t327) {
                                                                    void * t328 = gm_l0;
                                                                    void * t329 = gm_l40;
                                                                    void * t330 = (void *)((uint8_t *)&F.l14);
                                                                    void * t331 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t328, t329, t330);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l68 = t331;
                                                                } else {
                                                                    void * t332 = gm_l40;
                                                                    gm_l68 = t332;
                                                                }
                                                                void * t333 = gm_l68;
                                                                GM_ST(void *, (((uint8_t *)t324) + (uintptr_t)t326 * 8), t333);
                                                                goto gm_lb39;
                                                            }
gm_lb40:;
                                                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 138, 9);
                                                        }
gm_lb39:;
                                                    }
gm_lb38:;
                                                    uint64_t t334 = gm_l36;
                                                    uint64_t r335; bool o336 = __builtin_add_overflow(t334, ((uint64_t)1ULL), &r335);
                                                    uint64_t t337 = r335;
                                                    gm_l36 = t337;
                                                }
                                            }
gm_lb36:;
                                        }
                                        {
                                            goto gm_lb25;
                                        }
                                    }
gm_lb26:;
                                }
                                {
                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 113, 5);
                                }
                            }
gm_lb25:;
                        }
                    }
gm_lb24:;
                }
                {
                    uint64_t t338 = gm_l23;
                    uint64_t r339; bool o340 = __builtin_add_overflow(t338, ((uint64_t)1ULL), &r339);
                    uint64_t t341 = r339;
                    gm_l23 = t341;
                }
            }
        }
gm_lb22:;
    }
    {
        gm_trqfvmjtt4sij t342;
        gm_f_list_vp1djv1_2((void *)&t342, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l41 = t342;
        gm_l43 = ((uint64_t)0ULL);
        uint64_t t343 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        gm_l42 = t343;
        {
            for (;;) {
                uint64_t t344 = gm_l43;
                uint64_t t345 = gm_l42;
                uint8_t t346 = (t344 < t345);
                if (!t346) {
                    goto gm_lb46;
                }
                {
                    {
                        {
                            uint64_t t347 = gm_l43;
                            gm_l44 = t347;
                            {
                                {
                                    void * t348 = GM_LD(void *, ((uint8_t *)&F.l6));
                                    uint64_t t349 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                                    uint64_t t350 = gm_l44;
                                    if ((uintptr_t)t350 >= (uintptr_t)(t349)) gm_trap_index((uintptr_t)t350, t349, gm_src7nt72u848oha, 160, 17);
                                    uint8_t t351 = (uint8_t)(GM_LD(void *, (((uint8_t *)t348) + (uintptr_t)t350 * 8)) != 0);
                                    if (!t351) {
                                        goto gm_lb52;
                                    }
                                    void * t352 = GM_LD(void *, (((uint8_t *)t348) + (uintptr_t)t350 * 8));
                                    gm_l45 = t352;
                                    void * t353 = (void *)((uint8_t *)&F.l41);
                                    void * t354 = gm_l45;
                                    gm_tftb1odc739t5 t355 = gm_f_push_d943vgp_2(t353, &(*(gm_tcgrjq9toscke *)((uint8_t *)t354)));
                                    gm_l69 = t355;
                                    uint8_t t356 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                                    if (!t356) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 1, gm_src7nt72u848oha, 161, 13);
                                    goto gm_lb51;
                                }
gm_lb52:;
                                uint8_t t357 = gm_l15;
                                uint8_t t358 = (uint8_t)!t357;
                                if (t358) {
                                    void * t359 = gm_l0;
                                    gm_tl874t2atnlh3 t360 = gm_l5;
                                    uint64_t t361 = gm_l44;
                                    void * t362 = gm_l0;
                                    gm_t88rcucg1b7uk t363 = gm_l1;
                                    gm_b16_8 t364 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t362, t363);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tf1bjg81u0glv t365;
                                    GM_ST(uint64_t, ((uint8_t *)&t365 + 0), t361);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t365 + 8), t364);
                                    gm_f_format_j4le8o0((void *)&gm_l70, (void *)NULL, cx, NULL, &t365);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t366 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                                    if (!t366) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 8, gm_src7nt72u848oha, 163, 51);
                                    gm_b16_8 t367 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l70) + 8));
                                    gm_b16_8 t368 = GM_LD(gm_b16_8, ((uint8_t *)&t367));
                                    gm_b0_1 t369 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t359, t360, t368);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                void * t370 = (void *)((uint8_t *)&F.l41);
                                void * t371 = gm_l0;
                                void * t372 = GM_LD(void *, ((uint8_t *)t371));
                                gm_tl874t2atnlh3 t373 = gm_l5;
                                void * t374 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t372, t373);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t375 = gm_f_push_d943vgp_2(t370, &(*(gm_tcgrjq9toscke *)((uint8_t *)t374)));
                                gm_l71 = t375;
                                uint8_t t376 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                                if (!t376) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 1, gm_src7nt72u848oha, 164, 13);
                            }
gm_lb51:;
                            goto gm_lb49;
                        }
gm_lb50:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 159, 5);
                    }
gm_lb49:;
                }
gm_lb48:;
                uint64_t t377 = gm_l43;
                uint64_t r378; bool o379 = __builtin_add_overflow(t377, ((uint64_t)1ULL), &r378);
                uint64_t t380 = r378;
                gm_l43 = t380;
            }
        }
gm_lb46:;
    }
    {
        gm_t88rcucg1b7uk t381 = gm_l1;
        gm_b16_8 t382 = GM_LD(gm_b16_8, ((uint8_t *)&F.l41));
        gm_tl874t2atnlh3 t383 = gm_l5;
        gm_t2b6tm8c0vgo1 t384;
        memset(&t384, 0, sizeof t384);
        GM_ST(uint8_t, ((uint8_t *)&t384 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t384 + 8), t382);
        void * t385 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t381, &t384, t383);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l46 = t385;
        uint64_t t386 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
        uint8_t t387 = ((t386 == ((uint64_t)0ULL)));
        if (t387) {
            void * t388 = gm_l46;
            F.ret = t388;
            goto gm_fret;
        }
        gm_t88rcucg1b7uk t389 = gm_l1;
        gm_b16_8 t390 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
        void * t391 = gm_l46;
        gm_tl874t2atnlh3 t392 = gm_l5;
        void * t393 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t389, t390, t391, t392);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) void * gm_f_construct_variant_o6is81c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, const void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_construct_variant_o6is81c F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* u */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_th2lqbo77pvsc gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_tep2drsl9408b gm_l5; /* vs */
    gm_tf5srh4mp4b3i gm_l6; /* idx */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint64_t gm_l11; /* i */
    gm_tkn42u7na3t81 gm_l12; /* payload_ty */
    void * gm_l13; /* payload */
    gm_t88rcucg1b7uk gm_l14; /* pt */
    gm_b16_8 gm_l15; /* a */
    gm_b16_8 gm_l16; /* elems */
    gm_b16_8 gm_l17; /* names */
    GM_STACK_CHECK(&F, 2303u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l3, a3, sizeof gm_l3);
    gm_l4 = a4;
    gm_tdlpv0q69lov1 gm_l19;
    gm_tep2drsl9408b gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_t3l8idb2m6c1r gm_l22;
    uint64_t gm_l23;
    gm_tabb710d9mq2e gm_l24;
    gm_b16_8 gm_l25;
    gm_tabb710d9mq2e gm_l26;
    gm_t53at80pfdus3 gm_l27;
    uint8_t gm_l28;
    uint8_t gm_l29;
    uint8_t gm_l30;
    uint8_t gm_l31;
    uint8_t gm_l32;
    gm_tabb710d9mq2e gm_l33;
    uint8_t gm_l34;
    gm_tabb710d9mq2e gm_l35;
    {
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, ((uint8_t *)t22));
        gm_t88rcucg1b7uk t24 = gm_l1;
        gm_f_union_variants_a7khaqp((void *)&gm_l19, t23, GM_CONV(gm_csguankko4lmhd, t24));
        uint8_t t25 = (uint8_t)((((uint8_t *)&gm_l19))[24] != 0);
        if (t25) {
            memmove(((uint8_t *)&gm_l20), ((uint8_t *)&gm_l19), 24);
        } else {
            void * t26 = gm_l0;
            gm_tl874t2atnlh3 t27 = gm_l4;
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            void * t29 = gm_l0;
            gm_t88rcucg1b7uk t30 = gm_l1;
            gm_b16_8 t31 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t32;
            GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t28);
            GM_ST(gm_b16_8, ((uint8_t *)&t32 + 16), t31);
            gm_f_format_12rbmhd((void *)&gm_l21, (void *)NULL, cx, NULL, &t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
            if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_src7nt72u848oha, 20, 24);
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
            gm_b0_1 t36 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t26, t27, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t37 = gm_l0;
            void * t38 = GM_LD(void *, ((uint8_t *)t37));
            gm_tl874t2atnlh3 t39 = gm_l4;
            void * t40 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t40;
            goto gm_fret;
        }
        memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l20), 24);
        gm_tf5srh4mp4b3i t41;
        memset(&t41, 0, sizeof t41);
        gm_l6 = t41;
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
        gm_l7 = t43;
        gm_l8 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t44 = gm_l8;
                uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t46 = (t44 < t45);
                if (!t46) {
                    goto gm_lb1;
                }
                {
                    uint64_t t47 = gm_l8;
                    void * t48 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t50 = gm_l8;
                    gm_tckdobunaulee t51 = GM_LD(gm_tckdobunaulee, (((uint8_t *)t48) + (uintptr_t)t50 * 72));
                    gm_t3l8idb2m6c1r t52;
                    GM_ST(uint64_t, ((uint8_t *)&t52 + 0), t47);
                    GM_ST(gm_tckdobunaulee, ((uint8_t *)&t52 + 8), t51);
                    gm_l22 = t52;
                    {
                        {
                            uint64_t t53 = GM_LD(uint64_t, ((uint8_t *)&gm_l22));
                            gm_l9 = t53;
                            memmove(((uint8_t *)&F.l10), (((uint8_t *)&gm_l22) + 8), 72);
                            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                            uint8_t t56 = (gm_eq0iqiubfm7e(((uint8_t *)&t54), ((uint8_t *)&t55)));
                            if (t56) {
                                uint64_t t57 = gm_l9;
                                gm_tf5srh4mp4b3i t58;
                                GM_ST(uint8_t, (uint8_t *)&t58 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t58 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t58 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t58), t57);
                                ((uint8_t *)&t58)[8] = 1;
                                gm_l6 = t58;
                                goto gm_lb1;
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 24, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t59 = gm_l8;
                uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                uint64_t t62 = r60;
                gm_l8 = t62;
            }
        }
gm_lb1:;
    }
    {
        uint8_t t63 = (uint8_t)((((uint8_t *)&gm_l6))[8] != 0);
        if (t63) {
            uint64_t t64 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
            gm_l23 = t64;
        } else {
            void * t65 = gm_l0;
            gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
            void * t67 = gm_l0;
            gm_t88rcucg1b7uk t68 = gm_l1;
            gm_b16_8 t69 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            gm_t6j6smj7qoctu t71;
            GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t69);
            GM_ST(gm_b16_8, ((uint8_t *)&t71 + 16), t70);
            gm_f_format_l002mid((void *)&gm_l24, (void *)NULL, cx, NULL, &t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
            if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_src7nt72u848oha, 31, 29);
            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
            gm_b0_1 t75 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t65, t66, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t76 = gm_l0;
            void * t77 = GM_LD(void *, ((uint8_t *)t76));
            gm_tl874t2atnlh3 t78 = gm_l4;
            void * t79 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t79;
            goto gm_fret;
        }
        uint64_t t80 = gm_l23;
        gm_l11 = t80;
        void * t81 = GM_LD(void *, ((uint8_t *)&gm_l5));
        uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        uint64_t t83 = gm_l11;
        if ((uintptr_t)t83 >= (uintptr_t)(t82)) gm_trap_index((uintptr_t)t83, t82, gm_src7nt72u848oha, 34, 19);
        gm_tkn42u7na3t81 t84 = GM_LD(gm_tkn42u7na3t81, ((((uint8_t *)t81) + (uintptr_t)t83 * 72) + 16));
        gm_l12 = t84;
        gm_l13 = ((void *)0);
        {
            {
                uint8_t t85 = (uint8_t)((((uint8_t *)&gm_l12))[4] != 0);
                if (!t85) {
                    goto gm_lb7;
                }
                gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l12));
                gm_l14 = t86;
                uint8_t t87 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
                if (t87) {
                    gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_l25 = t88;
                } else {
                    void * t89 = gm_l0;
                    gm_tl874t2atnlh3 t90 = gm_l4;
                    gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                    gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                    gm_t6j6smj7qoctu t93;
                    GM_ST(gm_b16_8, ((uint8_t *)&t93 + 0), t91);
                    GM_ST(gm_b16_8, ((uint8_t *)&t93 + 16), t92);
                    gm_f_format_9i81nmj((void *)&gm_l26, (void *)NULL, cx, NULL, &t93);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                    if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_src7nt72u848oha, 38, 28);
                    gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                    gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
                    gm_b0_1 t97 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t89, t90, t96);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t98 = gm_l0;
                    void * t99 = GM_LD(void *, ((uint8_t *)t98));
                    gm_tl874t2atnlh3 t100 = gm_l4;
                    void * t101 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t99, t100);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t101;
                    goto gm_fret;
                }
                gm_b16_8 t102 = gm_l25;
                gm_l15 = t102;
                {
                    {
                        void * t103 = gm_l0;
                        void * t104 = GM_LD(void *, ((uint8_t *)t103));
                        void * t105 = (void *)(((uint8_t *)t104) + 8);
                        gm_t88rcucg1b7uk t106 = gm_l14;
                        gm_f_kind_jcnt4hm((void *)&gm_l27, t105, GM_CONV(gm_csguankko4lmhd, t106));
                        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                        uint8_t t108 = (uint8_t)(t107 == ((uint8_t)12ULL));
                        if (!t108) {
                            goto gm_lb9;
                        }
                        gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
                        gm_l16 = t109;
                        gm_b16_8 t110 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l27) + 8) + 16));
                        gm_l17 = t110;
                        uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                        uint8_t t112 = (!(t111 == ((uint64_t)1ULL)));
                        if (t112) {
                            gm_l29 = ((uint8_t)1);
                        } else {
                            uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                            uint8_t t114 = (!(t113 == ((uint64_t)1ULL)));
                            gm_l29 = t114;
                        }
                        uint8_t t115 = gm_l29;
                        if (t115) {
                            gm_l28 = ((uint8_t)1);
                        } else {
                            void * t116 = GM_LD(void *, ((uint8_t *)&gm_l15));
                            uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t117)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t117, gm_src7nt72u848oha, 43, 56);
                            {
                                {
                                    uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t116) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                                    uint8_t t119 = (uint8_t)(t118 == ((uint8_t)1ULL));
                                    if (!t119) {
                                        goto gm_lb11;
                                    }
                                    gm_l30 = ((uint8_t)1);
                                    goto gm_lb10;
                                }
gm_lb11:;
                                {
                                    gm_l30 = ((uint8_t)0);
                                    goto gm_lb10;
                                }
gm_lb12:;
                                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 43, 56);
                            }
gm_lb10:;
                            uint8_t t120 = gm_l30;
                            gm_l28 = t120;
                        }
                        uint8_t t121 = gm_l28;
                        if (t121) {
                            void * t122 = gm_l0;
                            gm_t88rcucg1b7uk t123 = gm_l14;
                            gm_b16_8 t124 = gm_l16;
                            gm_b16_8 t125 = gm_l17;
                            gm_b16_8 t126 = gm_l15;
                            gm_tl874t2atnlh3 t127 = gm_l4;
                            void * t128 = gm_f_construct_tuple_6ee5ejf((void *)NULL, cx, NULL, t122, t123, t124, t125, t126, t127);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l13 = t128;
                            gm_t88rcucg1b7uk t129 = gm_l1;
                            uint64_t t130 = gm_l11;
                            uint32_t t131 = (uint32_t)t130;
                            void * t132 = gm_l13;
                            gm_ttggq7ng4kp3p t133;
                            GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t133 + 0), t131);
                            GM_ST(void *, ((uint8_t *)&t133 + 8), t132);
                            gm_tl874t2atnlh3 t134 = gm_l4;
                            gm_t2b6tm8c0vgo1 t135;
                            memset(&t135, 0, sizeof t135);
                            GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)38ULL));
                            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t135 + 8), t133);
                            void * t136 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t129, &t135, t134);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t136;
                            goto gm_fret;
                        }
                        goto gm_lb8;
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                uint8_t t138 = (!(t137 == ((uint64_t)1ULL)));
                if (t138) {
                    gm_l31 = ((uint8_t)1);
                } else {
                    void * t139 = GM_LD(void *, ((uint8_t *)&gm_l15));
                    uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t140)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t140, gm_src7nt72u848oha, 48, 30);
                    {
                        {
                            uint8_t t141 = GM_LD(uint8_t, ((((uint8_t *)t139) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                            uint8_t t142 = (uint8_t)(t141 == ((uint8_t)0ULL));
                            if (!t142) {
                                goto gm_lb14;
                            }
                            gm_l32 = ((uint8_t)0);
                            goto gm_lb13;
                        }
gm_lb14:;
                        {
                            gm_l32 = ((uint8_t)1);
                            goto gm_lb13;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 48, 30);
                    }
gm_lb13:;
                    uint8_t t143 = gm_l32;
                    gm_l31 = t143;
                }
                uint8_t t144 = gm_l31;
                if (t144) {
                    void * t145 = gm_l0;
                    gm_tl874t2atnlh3 t146 = gm_l4;
                    void * t147 = gm_l0;
                    gm_t88rcucg1b7uk t148 = gm_l14;
                    gm_b16_8 t149 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t147, t148);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t150;
                    GM_ST(gm_b16_8, ((uint8_t *)&t150 + 0), t149);
                    gm_f_format_e1r9br2((void *)&gm_l33, (void *)NULL, cx, NULL, t150);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t151 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                    if (!t151) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_src7nt72u848oha, 49, 28);
                    gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                    gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
                    gm_b0_1 t154 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t145, t146, t153);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t155 = gm_l0;
                    void * t156 = GM_LD(void *, ((uint8_t *)t155));
                    gm_tl874t2atnlh3 t157 = gm_l4;
                    void * t158 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t156, t157);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t158;
                    goto gm_fret;
                }
                {
                    {
                        void * t159 = GM_LD(void *, ((uint8_t *)&gm_l15));
                        uint64_t t160 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t160)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t160, gm_src7nt72u848oha, 52, 27);
                        uint8_t t161 = GM_LD(uint8_t, ((((uint8_t *)t159) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                        uint8_t t162 = (uint8_t)(t161 == ((uint8_t)0ULL));
                        if (!t162) {
                            goto gm_lb17;
                        }
                        memmove(((uint8_t *)&F.l18), ((((uint8_t *)t159) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                        void * t163 = gm_l0;
                        void * t164 = (void *)((uint8_t *)&F.l18);
                        gm_t88rcucg1b7uk t165 = gm_l14;
                        void * t166 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t163, t164, t165);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l13 = t166;
                        goto gm_lb16;
                    }
gm_lb17:;
                    void * t167 = gm_l0;
                    void * t168 = GM_LD(void *, ((uint8_t *)t167));
                    gm_tl874t2atnlh3 t169 = gm_l4;
                    void * t170 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t168, t169);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t170;
                    goto gm_fret;
                }
gm_lb16:;
                goto gm_lb6;
            }
gm_lb7:;
            {
                {
                    uint8_t t171 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
                    if (t171) {
                        goto gm_lb19;
                    }
                    gm_l34 = ((uint8_t)0);
                    goto gm_lb18;
                }
gm_lb19:;
                {
                    gm_l34 = ((uint8_t)1);
                    goto gm_lb18;
                }
gm_lb20:;
                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 54, 15);
            }
gm_lb18:;
            uint8_t t172 = gm_l34;
            if (t172) {
                void * t173 = gm_l0;
                gm_tl874t2atnlh3 t174 = gm_l4;
                gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                gm_tnpifv20irnph t176;
                GM_ST(gm_b16_8, ((uint8_t *)&t176 + 0), t175);
                gm_f_format_ktbu9b9((void *)&gm_l35, (void *)NULL, cx, NULL, t176);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t177 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                if (!t177) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_src7nt72u848oha, 55, 24);
                gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
                gm_b0_1 t180 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t173, t174, t179);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t181 = gm_l0;
                void * t182 = GM_LD(void *, ((uint8_t *)t181));
                gm_tl874t2atnlh3 t183 = gm_l4;
                void * t184 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t182, t183);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t184;
                goto gm_fret;
            }
        }
gm_lb6:;
    }
    {
        gm_t88rcucg1b7uk t185 = gm_l1;
        uint64_t t186 = gm_l11;
        uint32_t t187 = (uint32_t)t186;
        void * t188 = gm_l13;
        gm_ttggq7ng4kp3p t189;
        GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t189 + 0), t187);
        GM_ST(void *, ((uint8_t *)&t189 + 8), t188);
        gm_tl874t2atnlh3 t190 = gm_l4;
        gm_t2b6tm8c0vgo1 t191;
        memset(&t191, 0, sizeof t191);
        GM_ST(uint8_t, ((uint8_t *)&t191 + 0), ((uint8_t)38ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t191 + 8), t189);
        void * t192 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t185, &t191, t190);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t192;
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
__attribute__((aligned(4))) void * gm_f_default_value_lo723gi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_default_value_lo723gi F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* d */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    void * gm_l4; /* tys */
    void * gm_l5; /* e */
    void * gm_l6; /* inner */
    uint8_t gm_l7; /* negative */
    void * gm_l8; /* x */
    uint8_t gm_l9; /* numeric */
    void * gm_l10; /* lit */
    void * gm_l12; /* lit */
    uint8_t gm_l14; /* b */
    GM_STACK_CHECK(&F, 1799u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    gm_t53at80pfdus3 gm_l18;
    uint8_t gm_l19;
    uint8_t gm_l20;
    gm_t53at80pfdus3 gm_l21;
    uint8_t gm_l22;
    uint8_t gm_l23;
    gm_t53at80pfdus3 gm_l24;
    {
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, ((uint8_t *)t36));
        void * t38 = (void *)(((uint8_t *)t37) + 8);
        gm_l4 = t38;
        void * t39 = gm_l2;
        gm_l5 = t39;
        {
            for (;;) {
                void * t40 = gm_l5;
                uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                uint8_t t42 = (uint8_t)(t41 == ((uint8_t)9ULL));
                if (!t42) {
                    goto gm_lb1;
                }
                void * t43 = GM_LD(void *, ((((uint8_t *)t40) + 8) + 8));
                gm_l6 = t43;
                {
                    void * t44 = gm_l6;
                    gm_l5 = t44;
                }
gm_lb3:;
            }
        }
gm_lb1:;
        gm_l7 = ((uint8_t)0);
        {
            {
                void * t45 = gm_l5;
                uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                uint8_t t47 = (uint8_t)(t46 == ((uint8_t)12ULL));
                if (!t47) {
                    goto gm_lb5;
                }
                uint8_t t48 = GM_LD(uint8_t, (((((uint8_t *)t45) + 8) + 8) + 0));
                uint8_t t49 = (uint8_t)(t48 == ((uint8_t)0ULL));
                if (!t49) {
                    goto gm_lb5;
                }
                void * t50 = GM_LD(void *, (((((uint8_t *)t45) + 8) + 8) + 8));
                gm_l8 = t50;
                void * t51 = gm_l8;
                {
                    {
                        uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t51) + 8) + 0));
                        uint8_t t53 = (uint8_t)(t52 == ((uint8_t)0ULL));
                        if (!t53) {
                            goto gm_lb7;
                        }
                        gm_l15 = ((uint8_t)1);
                        goto gm_lb6;
                    }
gm_lb7:;
                    {
                        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t51) + 8) + 0));
                        uint8_t t55 = (uint8_t)(t54 == ((uint8_t)1ULL));
                        if (!t55) {
                            goto gm_lb8;
                        }
                        gm_l15 = ((uint8_t)1);
                        goto gm_lb6;
                    }
gm_lb8:;
                    {
                        gm_l15 = ((uint8_t)0);
                        goto gm_lb6;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 338, 38);
                }
gm_lb6:;
                uint8_t t56 = gm_l15;
                if (!t56) {
                    goto gm_lb5;
                }
                gm_l7 = ((uint8_t)1);
                void * t57 = gm_l8;
                gm_l5 = t57;
                goto gm_lb4;
            }
gm_lb5:;
        }
gm_lb4:;
        void * t58 = gm_l4;
        gm_t88rcucg1b7uk t59 = gm_l3;
        uint8_t t60 = gm_f_is_integer_8d4f9ek(t58, GM_CONV(gm_csguankko4lmhd, t59));
        if (t60) {
            gm_l16 = ((uint8_t)1);
        } else {
            void * t61 = gm_l4;
            gm_t88rcucg1b7uk t62 = gm_l3;
            gm_f_kind_jcnt4hm((void *)&gm_l18, t61, GM_CONV(gm_csguankko4lmhd, t62));
            {
                {
                    uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                    uint8_t t64 = (uint8_t)(t63 == ((uint8_t)3ULL));
                    if (!t64) {
                        goto gm_lb11;
                    }
                    gm_l17 = ((uint8_t)1);
                    goto gm_lb10;
                }
gm_lb11:;
                {
                    gm_l17 = ((uint8_t)0);
                    goto gm_lb10;
                }
gm_lb12:;
                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 342, 38);
            }
gm_lb10:;
            uint8_t t65 = gm_l17;
            gm_l16 = t65;
        }
    }
    {
        uint8_t t66 = gm_l16;
        gm_l9 = t66;
        void * t67 = gm_l5;
        {
            {
                uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t67) + 8) + 0));
                uint8_t t69 = (uint8_t)(t68 == ((uint8_t)0ULL));
                if (!t69) {
                    goto gm_lb14;
                }
                void * t70 = GM_LD(void *, ((((uint8_t *)t67) + 8) + 8));
                gm_l10 = t70;
                {
                    {
                        uint8_t t71 = gm_l9;
                        if (!t71) {
                            goto gm_lb16;
                        }
                        void * t72 = gm_l10;
                        {
                            {
                                uint8_t t73 = (uint8_t)(((((uint8_t *)t72) + 48))[8] != 0);
                                if (t73) {
                                    goto gm_lb18;
                                }
                                gm_l19 = ((uint8_t)1);
                                goto gm_lb17;
                            }
gm_lb18:;
                            {
                                gm_l19 = ((uint8_t)0);
                                goto gm_lb17;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 344, 37);
                        }
gm_lb17:;
                        uint8_t t74 = gm_l19;
                        if (!t74) {
                            goto gm_lb16;
                        }
                        void * t75 = gm_l10;
                        memmove(((uint8_t *)&F.l11), ((uint8_t *)t75), 48);
                        uint8_t t76 = gm_l7;
                        if (t76) {
                            gm_tbci5ospk5nnn t77 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l11));
                            gm_f_neg_agd5c5k((void *)&F.l11, (void *)NULL, cx, NULL, &t77);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        void * t78 = gm_l4;
                        gm_t88rcucg1b7uk t79 = gm_l3;
                        uint8_t t80 = gm_f_is_integer_8d4f9ek(t78, GM_CONV(gm_csguankko4lmhd, t79));
                        if (t80) {
                            gm_t88rcucg1b7uk t81 = gm_l3;
                            gm_tbci5ospk5nnn t82 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l11));
                            void * t83 = gm_l2;
                            gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t83) + 72));
                            gm_t2b6tm8c0vgo1 t85;
                            GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t85 + 56, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t85 + 8), t82);
                            void * t86 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t81, &t85, t84);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t86;
                            goto gm_fret;
                        }
                        void * t87 = gm_l0;
                        gm_tngtuiuskn938 t88;
                        gm_f_from_int_ofjuoqh((void *)&t88, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t88rcucg1b7uk t89 = gm_l3;
                        void * t90 = gm_l2;
                        gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t90) + 72));
                        void * t92 = gm_f_float_value_cr4h9gi((void *)NULL, cx, NULL, t87, &t88, t89, t91);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t92;
                        goto gm_fret;
                    }
gm_lb16:;
                }
gm_lb15:;
                goto gm_lb13;
            }
gm_lb14:;
            {
                uint8_t t93 = GM_LD(uint8_t, ((((uint8_t *)t67) + 8) + 0));
                uint8_t t94 = (uint8_t)(t93 == ((uint8_t)1ULL));
                if (!t94) {
                    goto gm_lb20;
                }
                void * t95 = GM_LD(void *, ((((uint8_t *)t67) + 8) + 8));
                gm_l12 = t95;
                {
                    {
                        void * t96 = gm_l4;
                        gm_t88rcucg1b7uk t97 = gm_l3;
                        gm_f_kind_jcnt4hm((void *)&gm_l21, t96, GM_CONV(gm_csguankko4lmhd, t97));
                        {
                            {
                                uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                uint8_t t99 = (uint8_t)(t98 == ((uint8_t)3ULL));
                                if (!t99) {
                                    goto gm_lb24;
                                }
                                gm_l20 = ((uint8_t)1);
                                goto gm_lb23;
                            }
gm_lb24:;
                            {
                                gm_l20 = ((uint8_t)0);
                                goto gm_lb23;
                            }
gm_lb25:;
                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 350, 27);
                        }
gm_lb23:;
                        uint8_t t100 = gm_l20;
                        if (!t100) {
                            goto gm_lb22;
                        }
                        void * t101 = gm_l12;
                        {
                            {
                                uint8_t t102 = (uint8_t)(((((uint8_t *)t101) + 96))[1] != 0);
                                if (t102) {
                                    goto gm_lb27;
                                }
                                gm_l22 = ((uint8_t)1);
                                goto gm_lb26;
                            }
gm_lb27:;
                            {
                                gm_l22 = ((uint8_t)0);
                                goto gm_lb26;
                            }
gm_lb28:;
                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 350, 57);
                        }
gm_lb26:;
                        uint8_t t103 = gm_l22;
                        if (!t103) {
                            goto gm_lb22;
                        }
                        void * t104 = gm_l12;
                        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)t104) + 98));
                        uint8_t t106 = (uint8_t)!t105;
                        if (!t106) {
                            goto gm_lb22;
                        }
                        void * t107 = gm_l12;
                        memmove(((uint8_t *)&F.l13), ((uint8_t *)t107), 96);
                        uint8_t t108 = gm_l7;
                        if (t108) {
                            uint8_t t109 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l13)));
                            if (t109) {
                                void * t110 = gm_l0;
                                void * t111 = GM_LD(void *, ((uint8_t *)t110));
                                uint64_t t112 = gm_l1;
                                void * t113 = gm_l2;
                                gm_t88rcucg1b7uk t114 = gm_l3;
                                uint8_t t115 = gm_f_default_const_k60bpu4((void *)NULL, cx, NULL, t111, t112, t113, t114);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t116 = (uint8_t)!t115;
                                if (t116) {
                                    void * t117 = gm_l0;
                                    void * t118 = GM_LD(void *, ((uint8_t *)t117));
                                    void * t119 = gm_l2;
                                    gm_tl874t2atnlh3 t120 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t119) + 72));
                                    void * t121 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t118, t120);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t121;
                                    goto gm_fret;
                                }
                                gm_t88rcucg1b7uk t122 = gm_l3;
                                gm_b16_8 t123;
                                GM_ST(void *, (uint8_t *)&t123, (void *)(gm_sna4vk4o059d0));
                                GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(9));
                                void * t124 = gm_l2;
                                gm_tl874t2atnlh3 t125 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t124) + 72));
                                gm_t08olq97rscn3 t126;
                                GM_ST(uint32_t, (uint8_t *)&t126 + 28, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t126 + 0), t123);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t126 + 16), t125);
                                void * t127 = gm_l2;
                                gm_tl874t2atnlh3 t128 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t127) + 72));
                                gm_t2b6tm8c0vgo1 t129;
                                GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t129 + 40, 0);
                                GM_ST(uint64_t, (uint8_t *)&t129 + 48, 0);
                                GM_ST(uint64_t, (uint8_t *)&t129 + 56, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)14ULL));
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t129 + 8), t126);
                                void * t130 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t122, &t129, t128);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t130;
                                goto gm_fret;
                            }
                            gm_tngtuiuskn938 t131 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)&F.l13));
                            gm_f_neg_0u9uqg6((void *)&F.l13, (void *)NULL, cx, NULL, &t131);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        void * t132 = gm_l0;
                        gm_t88rcucg1b7uk t133 = gm_l3;
                        void * t134 = gm_l2;
                        gm_tl874t2atnlh3 t135 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t134) + 72));
                        void * t136 = gm_f_float_value_cr4h9gi((void *)NULL, cx, NULL, t132, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l13)), t133, t135);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t136;
                        goto gm_fret;
                    }
gm_lb22:;
                }
gm_lb21:;
                goto gm_lb13;
            }
gm_lb20:;
            {
                uint8_t t137 = GM_LD(uint8_t, ((((uint8_t *)t67) + 8) + 0));
                uint8_t t138 = (uint8_t)(t137 == ((uint8_t)3ULL));
                if (!t138) {
                    goto gm_lb29;
                }
                uint8_t t139 = GM_LD(uint8_t, ((((uint8_t *)t67) + 8) + 8));
                gm_l14 = t139;
                void * t140 = gm_l4;
                gm_t88rcucg1b7uk t141 = gm_l3;
                gm_t53at80pfdus3 t142;
                gm_f_kind_jcnt4hm((void *)&t142, t140, GM_CONV(gm_csguankko4lmhd, t141));
                uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&t142) + 0));
                uint8_t t144 = (uint8_t)((t143 == ((uint8_t)4ULL)));
                gm_t53at80pfdus3 t145;
                memset(&t145, 0, sizeof t145);
                GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)4ULL));
                if (t144) {
                    gm_t88rcucg1b7uk t146 = gm_l3;
                    uint8_t t147 = gm_l14;
                    void * t148 = gm_l2;
                    gm_tl874t2atnlh3 t149 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t148) + 72));
                    gm_t2b6tm8c0vgo1 t150;
                    memset(&t150, 0, sizeof t150);
                    GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)4ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t150 + 8), t147);
                    void * t151 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t146, &t150, t149);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t151;
                    goto gm_fret;
                }
                goto gm_lb13;
            }
gm_lb29:;
            {
                uint8_t t152 = GM_LD(uint8_t, ((((uint8_t *)t67) + 8) + 0));
                uint8_t t153 = (uint8_t)(t152 == ((uint8_t)4ULL));
                if (!t153) {
                    goto gm_lb30;
                }
                void * t154 = gm_l4;
                gm_t88rcucg1b7uk t155 = gm_l3;
                gm_f_kind_jcnt4hm((void *)&gm_l24, t154, GM_CONV(gm_csguankko4lmhd, t155));
                {
                    {
                        uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        uint8_t t157 = (uint8_t)(t156 == ((uint8_t)13ULL));
                        if (!t157) {
                            goto gm_lb32;
                        }
                        gm_l23 = ((uint8_t)1);
                        goto gm_lb31;
                    }
gm_lb32:;
                    {
                        gm_l23 = ((uint8_t)0);
                        goto gm_lb31;
                    }
gm_lb33:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 362, 21);
                }
gm_lb31:;
                uint8_t t158 = gm_l23;
                if (t158) {
                    gm_t88rcucg1b7uk t159 = gm_l3;
                    void * t160 = gm_l2;
                    gm_tl874t2atnlh3 t161 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t160) + 72));
                    gm_t2b6tm8c0vgo1 t162;
                    memset(&t162, 0, sizeof t162);
                    GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)7ULL));
                    void * t163 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t159, &t162, t161);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t163;
                    goto gm_fret;
                }
                goto gm_lb13;
            }
gm_lb30:;
            {
                goto gm_lb13;
            }
gm_lb34:;
            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 343, 5);
        }
gm_lb13:;
    }
    {
        void * t164 = gm_l0;
        void * t165 = GM_LD(void *, ((uint8_t *)t164));
        uint64_t t166 = gm_l1;
        void * t167 = gm_l2;
        gm_t88rcucg1b7uk t168 = gm_l3;
        uint8_t t169 = gm_f_default_const_k60bpu4((void *)NULL, cx, NULL, t165, t166, t167, t168);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t170 = (uint8_t)!t169;
        if (t170) {
            void * t171 = gm_l0;
            void * t172 = GM_LD(void *, ((uint8_t *)t171));
            void * t173 = gm_l2;
            gm_tl874t2atnlh3 t174 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t173) + 72));
            void * t175 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t172, t174);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t175;
            goto gm_fret;
        }
        gm_t88rcucg1b7uk t176 = gm_l3;
        gm_b16_8 t177;
        GM_ST(void *, (uint8_t *)&t177, (void *)(gm_sna4vk4o059d0));
        GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(9));
        void * t178 = gm_l2;
        gm_tl874t2atnlh3 t179 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t178) + 72));
        gm_t08olq97rscn3 t180;
        GM_ST(uint32_t, (uint8_t *)&t180 + 28, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t180 + 0), t177);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t180 + 16), t179);
        void * t181 = gm_l2;
        gm_tl874t2atnlh3 t182 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t181) + 72));
        gm_t2b6tm8c0vgo1 t183;
        GM_ST(uint8_t, (uint8_t *)&t183 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t183 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t183 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t183 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t183 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t183 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)14ULL));
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t183 + 8), t180);
        void * t184 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t176, &t183, t182);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t184;
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
__attribute__((aligned(4))) void * gm_f_field_default_4ssmakm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_field_default_4ssmakm F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* id */
    uint64_t gm_l2; /* i */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_b16_8 gm_l4; /* defaults */
    void * gm_l5; /* d */
    uint32_t gm_l6; /* tid */
    void * gm_l8; /* v */
    GM_STACK_CHECK(&F, 676u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l9;
    void * gm_l10;
    void * gm_l11;
    void * gm_l12;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = GM_LD(void *, (((uint8_t *)t7) + 1088));
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t7) + 1088) + 8));
    uint32_t t10 = gm_l1;
    uint64_t t11 = (uint64_t)t10;
    if ((uintptr_t)t11 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t11, t9, gm_src7nt72u848oha, 315, 17);
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + (uintptr_t)t11 * 184) + 80));
    gm_l4 = t12;
    uint64_t t13 = gm_l2;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    uint8_t t15 = (t13 >= t14);
    if (t15) {
        gm_l9 = ((uint8_t)1);
    } else {
        void * t16 = GM_LD(void *, ((uint8_t *)&gm_l4));
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        uint64_t t18 = gm_l2;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t16) + (uintptr_t)t18 * 1));
        uint8_t t20 = (uint8_t)!t19;
        gm_l9 = t20;
    }
    uint8_t t21 = gm_l9;
    if (t21) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t22 = gm_l0;
    void * t23 = GM_LD(void *, ((uint8_t *)t22));
    uint32_t t24 = gm_l1;
    uint64_t t25 = gm_l2;
    void * t26 = gm_f_field_default_expr_tqfuiff(t23, t24, t25);
    gm_l10 = t26;
    uint8_t t27 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l10)) != 0);
    if (t27) {
        void * t28 = GM_LD(void *, ((uint8_t *)&gm_l10));
        gm_l11 = t28;
    } else {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t29 = gm_l11;
    gm_l5 = t29;
    void * t30 = gm_l0;
    void * t31 = GM_LD(void *, ((uint8_t *)t30));
    void * t32 = (void *)(((uint8_t *)t31) + 8);
    uint32_t t33 = gm_l1;
    uint32_t t34 = gm_f_template_9o92cvv(t32, t33);
    gm_l6 = t34;
    uint32_t t35 = gm_l6;
    uint32_t t36 = gm_l1;
    uint8_t t37 = (!(t35 == t36));
    if (t37) {
        {
            {
                void * t38 = gm_l0;
                void * t39 = GM_LD(void *, ((uint8_t *)t38));
                void * t40 = GM_LD(void *, (((uint8_t *)t39) + 1088));
                uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t39) + 1088) + 8));
                uint32_t t42 = gm_l6;
                uint64_t t43 = (uint64_t)t42;
                if ((uintptr_t)t43 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t43, t41, gm_src7nt72u848oha, 322, 28);
                uint8_t t44 = GM_LD(uint8_t, (((((uint8_t *)t40) + (uintptr_t)t43 * 184) + 24) + 0));
                uint8_t t45 = (uint8_t)(t44 == ((uint8_t)0ULL));
                if (!t45) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&F.l7), (((((uint8_t *)t40) + (uintptr_t)t43 * 184) + 24) + 8), 48);
                {
                    {
                        uint64_t t46 = gm_l2;
                        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                        uint8_t t48 = (t46 < t47);
                        if (!t48) {
                            goto gm_lb4;
                        }
                        void * t49 = gm_l0;
                        void * t50 = GM_LD(void *, ((uint8_t *)t49));
                        void * t51 = (void *)(((uint8_t *)t50) + 8);
                        void * t52 = GM_LD(void *, ((uint8_t *)&F.l7));
                        uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                        uint64_t t54 = gm_l2;
                        gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t52) + (uintptr_t)t54 * 40) + 16));
                        uint8_t t56 = gm_f_has_param_u9t0efs(t51, GM_CONV(gm_csguankko4lmhd, t55));
                        if (!t56) {
                            goto gm_lb4;
                        }
                        void * t57 = gm_l0;
                        void * t58 = GM_LD(void *, ((uint8_t *)t57));
                        void * t59 = gm_l0;
                        void * t60 = GM_LD(void *, ((uint8_t *)t59));
                        void * t61 = GM_LD(void *, (((uint8_t *)t60) + 1088));
                        uint64_t t62 = GM_LD(uint64_t, ((((uint8_t *)t60) + 1088) + 8));
                        uint32_t t63 = gm_l1;
                        uint64_t t64 = (uint64_t)t63;
                        if ((uintptr_t)t64 >= (uintptr_t)(t62)) gm_trap_index((uintptr_t)t64, t62, gm_src7nt72u848oha, 324, 45);
                        uint64_t t65 = GM_LD(uint64_t, ((((uint8_t *)t61) + (uintptr_t)t64 * 184) + 16));
                        void * t66 = gm_l5;
                        void * t67 = GM_LD(void *, ((uint8_t *)&F.l7));
                        uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                        uint64_t t69 = gm_l2;
                        gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t67) + (uintptr_t)t69 * 40) + 16));
                        gm_tkn42u7na3t81 t71;
                        GM_ST(uint8_t, (uint8_t *)&t71 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t71 + 6, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t71), t70);
                        ((uint8_t *)&t71)[4] = 1;
                        void * t72 = gm_f_check_const_expr_2ot4q9v((void *)NULL, cx, NULL, t58, t65, t66, t71);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l8 = t72;
                        uint8_t t73 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l8)) != 0);
                        if (t73) {
                            void * t74 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            gm_l12 = t74;
                        } else {
                            void * t75 = gm_l0;
                            void * t76 = GM_LD(void *, ((uint8_t *)t75));
                            void * t77 = gm_l5;
                            gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t77) + 72));
                            void * t79 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t76, t78);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l12 = t79;
                        }
                        void * t80 = gm_l12;
                        F.ret = t80;
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
    }
    void * t81 = gm_l0;
    void * t82 = gm_l0;
    void * t83 = GM_LD(void *, ((uint8_t *)t82));
    void * t84 = GM_LD(void *, (((uint8_t *)t83) + 1088));
    uint64_t t85 = GM_LD(uint64_t, ((((uint8_t *)t83) + 1088) + 8));
    uint32_t t86 = gm_l1;
    uint64_t t87 = (uint64_t)t86;
    if ((uintptr_t)t87 >= (uintptr_t)(t85)) gm_trap_index((uintptr_t)t87, t85, gm_src7nt72u848oha, 329, 29);
    uint64_t t88 = GM_LD(uint64_t, ((((uint8_t *)t84) + (uintptr_t)t87 * 184) + 16));
    void * t89 = gm_l5;
    gm_t88rcucg1b7uk t90 = gm_l3;
    void * t91 = gm_f_default_value_lo723gi((void *)NULL, cx, NULL, t81, t88, t89, t90);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t91;
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
inline __attribute__((aligned(4))) void * gm_f_field_value_19vleud(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_field_value_19vleud F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    void * gm_l3; /* v */
    GM_STACK_CHECK(&F, 420u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l1;
    uint8_t t3 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
    uint8_t t4 = (uint8_t)((t3 == ((uint8_t)5ULL)));
    gm_t2gtbh39ptekd t5 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)t2) + 8));
    gm_t2gtbh39ptekd t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)5ULL));
    if (t4) {
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l2;
        uint8_t t9 = gm_f_is_plain_data_ej4mh2m((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (uint8_t)!t9;
        if (t10) {
            void * t11 = gm_l0;
            void * t12 = gm_l1;
            gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t12) + 72));
            void * t14 = gm_l0;
            void * t15 = GM_LD(void *, ((uint8_t *)t14));
            gm_t88rcucg1b7uk t16 = gm_l2;
            gm_b16_8 t17 = gm_f_uninit_message_u2c3peu((void *)NULL, cx, NULL, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t18 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t11, t13, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_t88rcucg1b7uk t19 = gm_l2;
        void * t20 = gm_l1;
        gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 72));
        gm_t2b6tm8c0vgo1 t22;
        memset(&t22, 0, sizeof t22);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)8ULL));
        void * t23 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t19, &t22, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t23;
        goto gm_fret;
    }
    void * t24 = gm_l0;
    void * t25 = gm_l1;
    gm_t88rcucg1b7uk t26 = gm_l2;
    gm_tkn42u7na3t81 t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 6, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t27), t26);
    ((uint8_t *)&t27)[4] = 1;
    void * t28 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t24, t25, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t28;
    void * t29 = gm_l0;
    void * t30 = gm_l3;
    gm_t88rcucg1b7uk t31 = gm_l2;
    void * t32 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t29, t30, t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void * gm_f_float_value_cr4h9gi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_float_value_cr4h9gi F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint16_t gm_l4; /* bits */
    double gm_l5; /* x */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l6;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((uint8_t *)t4));
            void * t6 = (void *)(((uint8_t *)t5) + 8);
            gm_t88rcucg1b7uk t7 = gm_l2;
            gm_f_kind_jcnt4hm((void *)&gm_l6, t6, GM_CONV(gm_csguankko4lmhd, t7));
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)3ULL));
            if (!t9) {
                goto gm_lb2;
            }
            uint16_t t10 = GM_LD(uint16_t, (((uint8_t *)&gm_l6) + 8));
            gm_l4 = t10;
            uint16_t t11 = gm_l4;
            double t12 = gm_f_to_float_o1iop8t((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l1)), t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t12;
            gm_t88rcucg1b7uk t13 = gm_l2;
            double t14 = gm_l5;
            gm_tl874t2atnlh3 t15 = gm_l3;
            gm_t2b6tm8c0vgo1 t16;
            memset(&t16, 0, sizeof t16);
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)1ULL));
            GM_ST(double, ((uint8_t *)&t16 + 8), t14);
            void * t17 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t13, &t16, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t17;
            goto gm_fret;
        }
gm_lb2:;
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, ((uint8_t *)t18));
        gm_tl874t2atnlh3 t20 = gm_l3;
        void * t21 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t21;
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
__attribute__((aligned(4))) void gm_f_spread_source_fields_5csral7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2) {
    struct gm_fr_spread_source_fields_5csral7 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* vt */
    void * gm_l2; /* e */
    gm_b16_8 gm_l4; /* elems */
    gm_b16_8 gm_l5; /* names */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l9; /* n */
    gm_b16_8 gm_l10; /* fs */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    GM_STACK_CHECK(&F, 1043u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l14;
    gm_tf1bjg81u0glv gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_th2lqbo77pvsc gm_l17;
    gm_b16_8 gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_trqfvmjtt4sij t14;
    gm_f_list_7gsrufb((void *)&t14, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t14;
    {
        {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, ((uint8_t *)t15));
            void * t17 = (void *)(((uint8_t *)t16) + 8);
            gm_t88rcucg1b7uk t18 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l14, t17, GM_CONV(gm_csguankko4lmhd, t18));
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)12ULL));
            if (!t20) {
                goto gm_lb2;
            }
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
            gm_l4 = t21;
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l14) + 8) + 16));
            gm_l5 = t22;
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t24 = (t23 > ((uint64_t)0ULL));
            if (!t24) {
                goto gm_lb2;
            }
            gm_b16_8 t25 = gm_l5;
            gm_l6 = t25;
            gm_l7 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t26 = gm_l7;
                    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint8_t t28 = (t26 < t27);
                    if (!t28) {
                        goto gm_lb3;
                    }
                    {
                        uint64_t t29 = gm_l7;
                        void * t30 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t32 = gm_l7;
                        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t30) + (uintptr_t)t32 * 16));
                        gm_tf1bjg81u0glv t34;
                        GM_ST(uint64_t, ((uint8_t *)&t34 + 0), t29);
                        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 8), t33);
                        gm_l15 = t34;
                        {
                            {
                                uint64_t t35 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                gm_l8 = t35;
                                gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                                gm_l9 = t36;
                                void * t37 = (void *)((uint8_t *)&F.l3);
                                gm_b16_8 t38 = gm_l9;
                                void * t39 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                uint64_t t41 = gm_l8;
                                if ((uintptr_t)t41 >= (uintptr_t)(t40)) gm_trap_index((uintptr_t)t41, t40, gm_src7nt72u848oha, 65, 74);
                                gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t39) + (uintptr_t)t41 * 4));
                                gm_tep2drsl9408b t43;
                                GM_ST(uint32_t, (uint8_t *)&t43 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t38);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t43 + 16), t42);
                                gm_tftb1odc739t5 t44 = gm_f_push_lnebar3(t37, &t43);
                                gm_l16 = t44;
                                uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_src7nt72u848oha, 65, 40);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 65, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t46 = gm_l7;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l7 = t49;
                }
            }
gm_lb3:;
            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
            gm_th2lqbo77pvsc t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t51), t50);
            ((uint8_t *)&t51)[16] = 1;
            memmove(gm_out, ((uint8_t *)&t51), 24);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t52 = gm_l0;
    void * t53 = GM_LD(void *, ((uint8_t *)t52));
    gm_t88rcucg1b7uk t54 = gm_l1;
    gm_f_struct_fields_chqu3it((void *)&gm_l17, t53, GM_CONV(gm_csguankko4lmhd, t54));
    uint8_t t55 = (uint8_t)((((uint8_t *)&gm_l17))[16] != 0);
    if (t55) {
        gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
        gm_l18 = t56;
    } else {
        void * t57 = gm_l0;
        void * t58 = GM_LD(void *, ((uint8_t *)t57));
        void * t59 = (void *)(((uint8_t *)t58) + 8);
        gm_t88rcucg1b7uk t60 = gm_l1;
        uint8_t t61 = gm_f_is_error_8k27gd1(t59, GM_CONV(gm_csguankko4lmhd, t60));
        uint8_t t62 = (uint8_t)!t61;
        if (t62) {
            void * t63 = gm_l0;
            void * t64 = gm_l2;
            gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t64) + 72));
            void * t66 = gm_l0;
            gm_t88rcucg1b7uk t67 = gm_l1;
            gm_b16_8 t68 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t69;
            GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t68);
            gm_f_format_a07tfib((void *)&gm_l19, (void *)NULL, cx, NULL, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
            if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_src7nt72u848oha, 69, 57);
            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
            gm_b0_1 t73 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t63, t65, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_th2lqbo77pvsc t74;
        memset(&t74, 0, sizeof t74);
        memmove(gm_out, ((uint8_t *)&t74), 24);
        goto gm_fdone;
    }
    gm_b16_8 t75 = gm_l18;
    gm_l10 = t75;
    gm_b16_8 t76 = gm_l10;
    gm_l11 = t76;
    gm_l12 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t77 = gm_l12;
            uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
            uint8_t t79 = (t77 < t78);
            if (!t79) {
                goto gm_lb8;
            }
            {
                void * t80 = GM_LD(void *, ((uint8_t *)&gm_l11));
                uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint64_t t82 = gm_l12;
                {
                    {
                        memmove(((uint8_t *)&F.l13), (((uint8_t *)t80) + (uintptr_t)t82 * 40), 40);
                        void * t83 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                        gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 16));
                        gm_tep2drsl9408b t86;
                        GM_ST(uint32_t, (uint8_t *)&t86 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t86 + 0), t84);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t86 + 16), t85);
                        gm_tftb1odc739t5 t87 = gm_f_push_lnebar3(t83, &t86);
                        gm_l20 = t87;
                        uint8_t t88 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                        if (!t88) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_src7nt72u848oha, 72, 19);
                        goto gm_lb11;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 72, 5);
                }
gm_lb11:;
            }
gm_lb10:;
            uint64_t t89 = gm_l12;
            uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
            uint64_t t92 = r90;
            gm_l12 = t92;
        }
    }
gm_lb8:;
    gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_th2lqbo77pvsc t94;
    GM_ST(uint8_t, (uint8_t *)&t94 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t94 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t94 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t94), t93);
    ((uint8_t *)&t94)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t94), 24);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_strip_wrappers_e5thpib(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_strip_wrappers_e5thpib F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t0 */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_t88rcucg1b7uk gm_l3; /* i */
    gm_t88rcucg1b7uk gm_l4; /* ok */
    GM_STACK_CHECK_SHALLOW(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l5;
    gm_t88rcucg1b7uk t9 = gm_l1;
    gm_l2 = t9;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t10 = gm_l0;
                void * t11 = GM_LD(void *, ((uint8_t *)t10));
                void * t12 = (void *)(((uint8_t *)t11) + 8);
                gm_t88rcucg1b7uk t13 = gm_l2;
                gm_f_kind_jcnt4hm((void *)&gm_l5, t12, GM_CONV(gm_csguankko4lmhd, t13));
                {
                    {
                        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        uint8_t t15 = (uint8_t)(t14 == ((uint8_t)13ULL));
                        if (!t15) {
                            goto gm_lb5;
                        }
                        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
                        gm_l3 = t16;
                        gm_t88rcucg1b7uk t17 = gm_l3;
                        gm_l2 = t17;
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        uint8_t t19 = (uint8_t)(t18 == ((uint8_t)14ULL));
                        if (!t19) {
                            goto gm_lb6;
                        }
                        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l5) + 8) + 4));
                        gm_l4 = t20;
                        gm_t88rcucg1b7uk t21 = gm_l4;
                        gm_l2 = t21;
                        goto gm_lb4;
                    }
gm_lb6:;
                    {
                        gm_t88rcucg1b7uk t22 = gm_l2;
                        F.ret = t22;
                        goto gm_fret;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 9, 9);
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
