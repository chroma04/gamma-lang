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
typedef struct gm_cskjv6t053tkje { uint32_t f0; uint64_t f1; } gm_cskjv6t053tkje;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_cskjv6t053tkje) == 16 && _Alignof(gm_cskjv6t053tkje) == 8 && offsetof(gm_cskjv6t053tkje, f0) == 0 && offsetof(gm_cskjv6t053tkje, f1) == 8, "layout of `Char`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_tiilmfd16v3fc f0; gm_b16_8 f1; } gm_t1cs2fjnrqap1;
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; } gm_tb8ondtf8obk4;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
typedef struct { gm_tdgfduqjheqdm f0; uint8_t f1; uint8_t pe[7]; } gm_tdn4erebgflsv;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbgd14esf0jvi v0; } f1; } gm_tgeh6n0c7c8kb;
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; } gm_tha5675rv512q;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_tiilmfd16v3fc f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; gm_b16_8 f3; } gm_tvc83pqnepc2e;
_Static_assert(sizeof(gm_t1cs2fjnrqap1) == 40 && _Alignof(gm_t1cs2fjnrqap1) == 8 && offsetof(gm_t1cs2fjnrqap1, f0) == 0 && offsetof(gm_t1cs2fjnrqap1, f1) == 24, "layout of `HandlerLit`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tb8ondtf8obk4) == 168 && _Alignof(gm_tb8ondtf8obk4) == 8 && offsetof(gm_tb8ondtf8obk4, f0) == 0 && offsetof(gm_tb8ondtf8obk4, f1) == 8 && offsetof(gm_tb8ondtf8obk4, f2) == 48 && offsetof(gm_tb8ondtf8obk4, f3) == 88 && offsetof(gm_tb8ondtf8obk4, f4) == 128, "layout of `StackAnalysis`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdn4erebgflsv) == 72 && _Alignof(gm_tdn4erebgflsv) == 8 && offsetof(gm_tdn4erebgflsv, f0) == 0 && offsetof(gm_tdn4erebgflsv, f1) == 64, "layout of `?FnDef`");
_Static_assert(sizeof(gm_tgeh6n0c7c8kb) == 40 && _Alignof(gm_tgeh6n0c7c8kb) == 8 && offsetof(gm_tgeh6n0c7c8kb, f0) == 0 && offsetof(gm_tgeh6n0c7c8kb, f1) == 8, "layout of `StackFail`");
_Static_assert(sizeof(gm_tha5675rv512q) == 48 && _Alignof(gm_tha5675rv512q) == 8 && offsetof(gm_tha5675rv512q, f0) == 0 && offsetof(gm_tha5675rv512q, f1) == 8, "layout of `StackFrame`");
typedef struct { gm_tha5675rv512q f0; uint8_t f1; uint8_t pe[7]; } gm_tijr008e33o2m;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tgeh6n0c7c8kb v0; uint64_t v1; } f1; } gm_toabfckqu3onu;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tvc83pqnepc2e) == 64 && _Alignof(gm_tvc83pqnepc2e) == 8 && offsetof(gm_tvc83pqnepc2e, f0) == 0 && offsetof(gm_tvc83pqnepc2e, f1) == 24 && offsetof(gm_tvc83pqnepc2e, f2) == 32 && offsetof(gm_tvc83pqnepc2e, f3) == 48, "layout of `HandlerDeclInfo`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tijr008e33o2m) == 56 && _Alignof(gm_tijr008e33o2m) == 8 && offsetof(gm_tijr008e33o2m, f0) == 0 && offsetof(gm_tijr008e33o2m, f1) == 48, "layout of `?StackFrame`");
_Static_assert(sizeof(gm_toabfckqu3onu) == 48 && _Alignof(gm_toabfckqu3onu) == 8 && offsetof(gm_toabfckqu3onu, f0) == 0 && offsetof(gm_toabfckqu3onu, f1) == 8, "layout of `StackFail!u64`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_bind_fn_pefln9r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_bound_of_sn02ojm {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_def_of_m512sbu {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fv6i5s8 {
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
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_laia5g6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_pp58tj3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uk3cnn9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vndn3a4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_extend_2kurgou {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_t3eo0l3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_845n386 {
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
struct gm_fr_format_hp1rthv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_koh61mc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n18n34e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t68frbc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_msjtuv4 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_o00nc4s {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pe70gj7 {
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
struct gm_fr_list_vk99g9b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mentions_error_37knu7r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_pop_d7svgs5 {
    gm_frame h;
    gm_tha5675rv512q l2; /* x */
    gm_tijr008e33o2m ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_12ij5jo {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_1mcf4ec {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_9gqhmau {
    gm_frame h;
    gm_tha5675rv512q l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_njfep1h {
    gm_frame h;
    gm_t1cs2fjnrqap1 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pf02nv6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_1jnet6e {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5719f1b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_6u2ahnp {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_gjdu58l {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k2r4f5o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_stack_call_poqmlno {
    gm_frame h;
    gm_tha5675rv512q l8; /* fr */
    gm_tdgfduqjheqdm l9; /* def */
    gm_trqfvmjtt4sij l11; /* bound */
    gm_trqfvmjtt4sij l21; /* saved */
    gm_toabfckqu3onu l22; /* r */
    gm_toabfckqu3onu ret;
};
struct gm_fr_stack_children_dedraea {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* kids */
    gm_b16_8 ret;
};
struct gm_fr_stack_expr_33aekgt {
    gm_frame h;
    gm_tsth9qu1dv0g3 l3; /* callee */
    gm_t710lsrq1m8gk l23; /* w */
    gm_trqfvmjtt4sij l24; /* installed */
    gm_t1cs2fjnrqap1 l30; /* lit */
    gm_tvc83pqnepc2e l36; /* info */
    gm_t1cs2fjnrqap1 l40; /* h */
    gm_toabfckqu3onu l47; /* r */
    gm_tha5675rv512q l49; /* top */
    gm_tdgfduqjheqdm l50; /* def */
    gm_toabfckqu3onu ret;
};
struct gm_fr_stack_frame_68512ca {
    gm_frame h;
    gm_tdgfduqjheqdm l5; /* def */
    uint64_t ret;
};
struct gm_fr_stack_perform_soivemm {
    gm_frame h;
    gm_t1cs2fjnrqap1 l8; /* h */
    gm_toabfckqu3onu ret;
};
struct gm_fr_stack_push_expr_bdl56rb {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* bound */
    gm_toabfckqu3onu l4; /* r */
    gm_toabfckqu3onu ret;
};
struct gm_fr_stack_size_0gijrqq {
    gm_frame h;
    gm_tb8ondtf8obk4 l3; /* a */
    gm_toabfckqu3onu l4; /* r */
    gm_trqfvmjtt4sij l7; /* chain */
    gm_tha5675rv512q l10; /* fr */
    gm_t4gut2c9fj1kt l12; /* d */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_stack_target_7vr78v6 {
    gm_frame h;
    gm_tha5675rv512q l6; /* top */
    gm_tha5675rv512q l9; /* top */
    gm_t9805qtonbsag ret;
};
struct gm_fr_truncate_31h1fac {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_fn_pefln9r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_bound_of_sn02ojm(void * a0, uint32_t a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_laia5g6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pp58tj3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uk3cnn9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vndn3a4(void);
__attribute__((aligned(4))) uint8_t gm_f_ensure_body_tbtojbv(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_2kurgou(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t3eo0l3(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_format_845n386(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_hp1rthv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_koh61mc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_n18n34e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_t68frbc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_frame_cost_oei64tv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) void gm_f_get_mk00hfd(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_msjtuv4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_o00nc4s(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pe70gj7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vk99g9b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_error_37knu7r(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_d7svgs5(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_12ij5jo(void * a0, gm_cs9ufftihtbpo2 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1mcf4ec(void * a0, gm_cskjv6t053tkje a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9gqhmau(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_njfep1h(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1jnet6e(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_6u2ahnp(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gjdu58l(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k2r4f5o(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_stack_call_poqmlno(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_stack_children_dedraea(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_stack_expr_33aekgt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint64_t gm_f_stack_frame_68512ca(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_stack_perform_soivemm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_stack_push_expr_bdl56rb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_stack_size_0gijrqq(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_stack_target_7vr78v6(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_truncate_31h1fac(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_cb7r52n9k7f[8] __attribute__((aligned(8)));
extern uint8_t gm_cdlh2b67kra[8] __attribute__((aligned(8)));
extern uint8_t gm_cilec57f4ei[8] __attribute__((aligned(8)));
extern const uint8_t gm_s2sodn6uclrlt[63];
extern const uint8_t gm_s30rt0lbjn52b[19];
extern const uint8_t gm_s9impgj2b5t1c[50];
extern const uint8_t gm_s9jqgpofojve4[35];
extern const uint8_t gm_sdscrog2npict[23];
extern const uint8_t gm_sdsrtcf8ng48d[6];
extern const uint8_t gm_sg0997vvtiund[65];
extern const uint8_t gm_sg9vhq4ehlfsf[18];
extern const uint8_t gm_sig8ahbvm8eb8[10];
extern const uint8_t gm_sne1lgqg4j81f[27];
extern const uint8_t gm_sqa5rf17c57e4[5];
extern const uint8_t gm_squ1rl0dbtrtr[59];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4tam33qo20j7[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_tmpl gm_tpl2kgmgv2ueqo4;
extern const gm_tmpl gm_tpl5qkek6smv4rg;
extern const gm_tmpl gm_tpl9ljuo8s7738q;
extern const gm_tmpl gm_tplaahjt316h1v3;
extern const gm_tmpl gm_tplggs57b0p2or4;
extern const gm_tmpl gm_tpll8q455cp9mhi;
extern const gm_tmpl gm_tplsbqgr9k4itq0;
extern const gm_type gm_tyo86ks9qkvt;
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
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_def_of_m512sbu F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tdn4erebgflsv gm_l3;
    {
        {
            void * t4 = gm_l0;
            void * t5 = (void *)(((uint8_t *)t4) + 2184);
            uint32_t t6 = gm_l1;
            gm_f_get_mk00hfd((void *)&gm_l3, t5, t6);
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[64] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l3), 64);
            memmove(gm_out, ((uint8_t *)&F.l2), 64);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 384));
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t8) + 384) + 8));
    uint32_t t11 = gm_l1;
    uint64_t t12 = (uint64_t)t11;
    if ((uintptr_t)t12 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t12, t10, gm_srclv0dcbbfhves, 589, 12);
    memmove(gm_out, (((uint8_t *)t9) + (uintptr_t)t12 * 64), 64);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_laia5g6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_laia5g6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pp58tj3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_pp58tj3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uk3cnn9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_uk3cnn9 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vndn3a4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vndn3a4 F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_2kurgou(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_2kurgou F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_6u2ahnp(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 40);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 40)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t3eo0l3(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_t3eo0l3 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 29u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l1;
    void * t2 = (void *)((uint8_t *)&((struct gm_fr_stack_children_dedraea *)((&F.h)->up))->l1);
    void * t3 = gm_l0;
    gm_tftb1odc739t5 t4 = gm_f_push_pf02nv6(t2, t3);
    gm_l1 = t4;
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 1, gm_src4tam33qo20j7, 283, 42);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_845n386(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_845n386 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_squ1rl0dbtrtr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(58));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplggs57b0p2or4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_hp1rthv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hp1rthv F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s30rt0lbjn52b));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplaahjt316h1v3), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_koh61mc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_koh61mc F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sne1lgqg4j81f));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9ljuo8s7738q), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_n18n34e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_n18n34e F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg9vhq4ehlfsf));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5qkek6smv4rg), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_t68frbc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t68frbc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s9jqgpofojve4));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(34));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2kgmgv2ueqo4), t2);
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
inline __attribute__((aligned(4))) void gm_f_list_msjtuv4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_msjtuv4 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_pp58tj3();
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
inline __attribute__((aligned(4))) void gm_f_list_o00nc4s(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_o00nc4s F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vndn3a4();
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
inline __attribute__((aligned(4))) void gm_f_list_pe70gj7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pe70gj7 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_laia5g6();
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
inline __attribute__((aligned(4))) void gm_f_list_vk99g9b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vk99g9b F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_uk3cnn9();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_d7svgs5(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_d7svgs5 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
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
        gm_tijr008e33o2m t6;
        memset(&t6, 0, sizeof t6);
        memmove(gm_out, ((uint8_t *)&t6), 56);
        goto gm_fdone;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = gm_l1;
    uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
    uint64_t t13 = r11;
    if ((uintptr_t)t13 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t13, t9, gm_srcrgc9bkquf6nr, 133, 10);
    memmove(((uint8_t *)&F.l2), (((uint8_t *)t8) + (uintptr_t)t13 * 48), 48);
    void * t14 = gm_l0;
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t15) + 8));
    uint64_t t18 = gm_l1;
    uint64_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint64_t)1ULL), &r19);
    if (o20) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t21 = r19;
    uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t23 = (uintptr_t)t21;
    if (t22 > t23 || t23 > (uintptr_t)t17) gm_trap_slice(t22, t23, t17, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t24 = ((uintptr_t)t21) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t16 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    GM_ST(gm_b16_8, ((uint8_t *)t14), t25);
    gm_tha5675rv512q t26 = GM_LD(gm_tha5675rv512q, ((uint8_t *)&F.l2));
    gm_tijr008e33o2m t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 49, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 50, 0);
    GM_ST(uint32_t, (uint8_t *)&t27 + 52, 0);
    GM_ST(gm_tha5675rv512q, ((uint8_t *)&t27), t26);
    ((uint8_t *)&t27)[48] = 1;
    memmove(gm_out, ((uint8_t *)&t27), 56);
    goto gm_fdone;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_12ij5jo(void * a0, gm_cs9ufftihtbpo2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_12ij5jo F;
    void * gm_l0; /* l */
    gm_tcc1rq4n1avb2 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tcc1rq4n1avb2, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_uk3cnn9();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_1jnet6e(t8, ((uint64_t)1ULL), t10);
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
    gm_tcc1rq4n1avb2 t18 = gm_l1;
    GM_ST(gm_tcc1rq4n1avb2, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1mcf4ec(void * a0, gm_cskjv6t053tkje a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1mcf4ec F;
    void * gm_l0; /* l */
    gm_tb31k5l794t7p gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tb31k5l794t7p, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_pp58tj3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_k2r4f5o(t8, ((uint64_t)1ULL), t10);
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
    gm_tb31k5l794t7p t18 = gm_l1;
    GM_ST(gm_tb31k5l794t7p, (((uint8_t *)t16) + (uintptr_t)t17 * 16), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9gqhmau(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_9gqhmau F;
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
        gm_b16_8 t9 = gm_f_empty_slice_laia5g6();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_gjdu58l(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_njfep1h(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_njfep1h F;
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
        gm_b16_8 t9 = gm_f_empty_slice_vndn3a4();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_6u2ahnp(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1jnet6e(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_1jnet6e F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_6u2ahnp(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_6u2ahnp F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gjdu58l(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_gjdu58l F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k2r4f5o(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_k2r4f5o F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_stack_children_dedraea(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_stack_children_dedraea F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_trqfvmjtt4sij t2;
    gm_f_list_s1kpkpr((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = gm_l0;
    gm_closure t4 = ((gm_closure){(void *)gm_f_fn_t3eo0l3, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
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
inline __attribute__((aligned(4))) void gm_f_truncate_31h1fac(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_31h1fac F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcrgc9bkquf6nr, 125, 36);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        GM_ST(gm_b16_8, ((uint8_t *)t6), t14);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_bind_fn_pefln9r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_bind_fn_pefln9r F;
    void * gm_l0; /* bound */
    uint32_t gm_l1; /* local */
    uint32_t gm_l2; /* f */
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
                        if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_src4tam33qo20j7, 272, 12);
                        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)t14) + (uintptr_t)t16 * 8));
                        uint32_t t18 = gm_l1;
                        uint8_t t19 = ((t17 == t18));
                        if (t19) {
                            void * t20 = gm_l0;
                            void * t21 = GM_LD(void *, ((uint8_t *)t20));
                            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t20) + 8));
                            uint64_t t23 = gm_l5;
                            if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_src4tam33qo20j7, 273, 13);
                            uint32_t t24 = gm_l2;
                            GM_ST(uint32_t, ((((uint8_t *)t21) + (uintptr_t)t23 * 8) + 4), t24);
                            F.ret = GM_ZERO(gm_b0_1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 271, 5);
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
    uint32_t t31 = gm_l2;
    gm_tcc1rq4n1avb2 t32;
    GM_ST(uint32_t, ((uint8_t *)&t32 + 0), t30);
    GM_ST(uint32_t, ((uint8_t *)&t32 + 4), t31);
    gm_tftb1odc739t5 t33 = gm_f_push_12ij5jo(t29, GM_CONV(gm_cs9ufftihtbpo2, t32));
    gm_l6 = t33;
    uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src4tam33qo20j7, 277, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_bound_of_sn02ojm(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bound_of_sn02ojm F;
    void * gm_l0; /* bound */
    uint32_t gm_l1; /* local */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tcc1rq4n1avb2 gm_l4; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 178u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)t7));
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
                        gm_tcc1rq4n1avb2 t15 = GM_LD(gm_tcc1rq4n1avb2, (((uint8_t *)t12) + (uintptr_t)t14 * 8));
                        gm_l4 = t15;
                        uint32_t t16 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                        uint32_t t17 = gm_l1;
                        uint8_t t18 = ((t16 == t17));
                        if (t18) {
                            uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
                            gm_t9805qtonbsag t20;
                            GM_ST(uint8_t, (uint8_t *)&t20 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t20 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t20), t19);
                            ((uint8_t *)&t20)[4] = 1;
                            F.ret = t20;
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 168, 5);
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
    gm_t9805qtonbsag t25;
    memset(&t25, 0, sizeof t25);
    F.ret = t25;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t3eo0l3(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_stack_call_poqmlno(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_stack_call_poqmlno F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* g */
    gm_b16_8 gm_l2; /* args */
    gm_b16_8 gm_l3; /* env */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* name */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l10; /* body */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    uint32_t gm_l15; /* p */
    uint32_t gm_l16; /* t */
    uint8_t gm_l17; /* memoizable */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_tb31k5l794t7p gm_l20; /* m */
    uint64_t gm_l23; /* c */
    GM_STACK_CHECK(&F, 1960u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l24;
    void * gm_l25;
    gm_tabb710d9mq2e gm_l26;
    gm_tn68el8d14vku gm_l27;
    gm_t9805qtonbsag gm_l28;
    gm_tftb1odc739t5 gm_l29;
    uint8_t gm_l30;
    uint8_t gm_l31;
    uint8_t gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tftb1odc739t5 gm_l34;
    void * t22 = gm_l0;
    void * t23 = GM_LD(void *, ((uint8_t *)t22));
    void * t24 = GM_LD(void *, (((uint8_t *)t23) + 344));
    uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t23) + 344) + 8));
    uint32_t t26 = gm_l1;
    uint64_t t27 = (uint64_t)t26;
    if ((uintptr_t)t27 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t27, t25, gm_src4tam33qo20j7, 86, 13);
    gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)t24) + (uintptr_t)t27 * 136));
    gm_l5 = t28;
    void * t29 = gm_l0;
    gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)t29) + 8));
    gm_l6 = t30;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t31 = gm_l7;
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t33 = (t31 < t32);
            if (!t33) {
                goto gm_lb1;
            }
            {
                void * t34 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t36 = gm_l7;
                {
                    {
                        memmove(((uint8_t *)&F.l8), (((uint8_t *)t34) + (uintptr_t)t36 * 48), 48);
                        uint32_t t37 = GM_LD(uint32_t, ((uint8_t *)&F.l8));
                        uint32_t t38 = gm_l1;
                        uint8_t t39 = ((t37 == t38));
                        if (t39) {
                            gm_tl874t2atnlh3 t40 = gm_l4;
                            gm_b16_8 t41 = gm_l5;
                            gm_tnpifv20irnph t42;
                            GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
                            gm_f_format_n18n34e((void *)&gm_l24, (void *)NULL, cx, NULL, t42);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                            if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_src4tam33qo20j7, 88, 51);
                            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                            gm_tbgd14esf0jvi t46;
                            GM_ST(uint32_t, (uint8_t *)&t46 + 12, 0);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t46 + 0), t40);
                            GM_ST(gm_b16_8, ((uint8_t *)&t46 + 16), t45);
                            gm_tgeh6n0c7c8kb t47;
                            GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t47 + 8), t46);
                            gm_toabfckqu3onu t48;
                            GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                            ((uint8_t *)&t48)[0] = 1;
                            GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t48 + 8), t47);
                            memmove(gm_out, ((uint8_t *)&t48), 48);
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 87, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t49 = gm_l7;
            uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
            uint64_t t52 = r50;
            gm_l7 = t52;
        }
    }
gm_lb1:;
    void * t53 = gm_l0;
    void * t54 = GM_LD(void *, ((uint8_t *)t53));
    uint32_t t55 = gm_l1;
    gm_tl874t2atnlh3 t56 = gm_l4;
    uint8_t t57 = gm_f_ensure_body_tbtojbv((void *)NULL, cx, NULL, t54, t55, t56);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t58 = (uint8_t)!t57;
    if (t58) {
        gm_tgeh6n0c7c8kb t59;
        memset(&t59, 0, sizeof t59);
        GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)0ULL));
        gm_toabfckqu3onu t60;
        GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
        ((uint8_t *)&t60)[0] = 1;
        GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t60 + 8), t59);
        memmove(gm_out, ((uint8_t *)&t60), 48);
        goto gm_fdone;
    }
    void * t61 = gm_l0;
    void * t62 = GM_LD(void *, ((uint8_t *)t61));
    uint32_t t63 = gm_l1;
    gm_f_def_of_m512sbu((void *)&F.l9, t62, t63);
    uint8_t t64 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l9) + 40)) != 0);
    if (t64) {
        void * t65 = GM_LD(void *, (((uint8_t *)&F.l9) + 40));
        gm_l25 = t65;
    } else {
        gm_t9qkh0rsooc8d t66 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l9));
        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&t66) + 0));
        uint8_t t68 = (uint8_t)((t67 == ((uint8_t)3ULL)));
        gm_t9qkh0rsooc8d t69;
        GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)3ULL));
        if (t68) {
            gm_tl874t2atnlh3 t70 = gm_l4;
            gm_b16_8 t71 = gm_l5;
            gm_tnpifv20irnph t72;
            GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t71);
            gm_f_format_koh61mc((void *)&gm_l26, (void *)NULL, cx, NULL, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
            if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_src4tam33qo20j7, 93, 62);
            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
            gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
            gm_tbgd14esf0jvi t76;
            GM_ST(uint32_t, (uint8_t *)&t76 + 12, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t76 + 0), t70);
            GM_ST(gm_b16_8, ((uint8_t *)&t76 + 16), t75);
            gm_tgeh6n0c7c8kb t77;
            GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t77 + 8), t76);
            gm_toabfckqu3onu t78;
            GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
            ((uint8_t *)&t78)[0] = 1;
            GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t78 + 8), t77);
            memmove(gm_out, ((uint8_t *)&t78), 48);
            goto gm_fdone;
        }
        void * t79 = gm_l0;
        void * t80 = GM_LD(void *, ((uint8_t *)t79));
        gm_tl874t2atnlh3 t81 = gm_l4;
        gm_b16_8 t82;
        GM_ST(void *, (uint8_t *)&t82, (void *)(gm_s9impgj2b5t1c));
        GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(49));
        gm_b0_1 t83 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t80, t81, t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgeh6n0c7c8kb t84;
        memset(&t84, 0, sizeof t84);
        GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)0ULL));
        gm_toabfckqu3onu t85;
        GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
        ((uint8_t *)&t85)[0] = 1;
        GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t85 + 8), t84);
        memmove(gm_out, ((uint8_t *)&t85), 48);
        goto gm_fdone;
    }
    void * t86 = gm_l25;
    gm_l10 = t86;
    gm_trqfvmjtt4sij t87;
    gm_f_list_vk99g9b((void *)&t87, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l11 = t87;
    gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 8));
    gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
    gm_l12 = t89;
    gm_l13 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t90 = gm_l13;
            uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
            uint8_t t92 = (t90 < t91);
            if (!t92) {
                goto gm_lb6;
            }
            {
                uint64_t t93 = gm_l13;
                void * t94 = GM_LD(void *, ((uint8_t *)&gm_l12));
                uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint64_t t96 = gm_l13;
                uint32_t t97 = GM_LD(uint32_t, (((uint8_t *)t94) + (uintptr_t)t96 * 4));
                gm_tn68el8d14vku t98;
                GM_ST(uint32_t, (uint8_t *)&t98 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t98 + 0), t93);
                GM_ST(uint32_t, ((uint8_t *)&t98 + 8), t97);
                gm_l27 = t98;
                {
                    {
                        uint64_t t99 = GM_LD(uint64_t, ((uint8_t *)&gm_l27));
                        gm_l14 = t99;
                        uint32_t t100 = GM_LD(uint32_t, (((uint8_t *)&gm_l27) + 8));
                        gm_l15 = t100;
                        uint64_t t101 = gm_l14;
                        uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint8_t t103 = (t101 < t102);
                        if (t103) {
                            {
                                {
                                    void * t104 = gm_l0;
                                    void * t105 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                    uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                    uint64_t t107 = gm_l14;
                                    void * t108 = (void *)(((uint8_t *)t105) + (uintptr_t)t107 * 88);
                                    gm_t9805qtonbsag t109 = gm_f_stack_target_7vr78v6(t104, t108);
                                    gm_l28 = t109;
                                    uint8_t t110 = (uint8_t)((((uint8_t *)&gm_l28))[4] != 0);
                                    if (!t110) {
                                        goto gm_lb12;
                                    }
                                    uint32_t t111 = GM_LD(uint32_t, ((uint8_t *)&gm_l28));
                                    gm_l16 = t111;
                                    void * t112 = (void *)((uint8_t *)&F.l11);
                                    uint32_t t113 = gm_l15;
                                    uint32_t t114 = gm_l16;
                                    gm_tcc1rq4n1avb2 t115;
                                    GM_ST(uint32_t, ((uint8_t *)&t115 + 0), t113);
                                    GM_ST(uint32_t, ((uint8_t *)&t115 + 4), t114);
                                    gm_tftb1odc739t5 t116 = gm_f_push_12ij5jo(t112, GM_CONV(gm_cs9ufftihtbpo2, t115));
                                    gm_l29 = t116;
                                    uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                    if (!t117) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_src4tam33qo20j7, 100, 49);
                                    goto gm_lb11;
                                }
gm_lb12:;
                            }
gm_lb11:;
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 98, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t118 = gm_l13;
            uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
            uint64_t t121 = r119;
            gm_l13 = t121;
        }
    }
gm_lb6:;
    {
        {
            uint8_t t122 = (uint8_t)(((((uint8_t *)&F.l9) + 48))[4] != 0);
            if (t122) {
                goto gm_lb14;
            }
            gm_l32 = ((uint8_t)1);
            goto gm_lb13;
        }
gm_lb14:;
        {
            gm_l32 = ((uint8_t)0);
            goto gm_lb13;
        }
gm_lb15:;
        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 105, 19);
    }
gm_lb13:;
    uint8_t t123 = gm_l32;
    if (t123) {
        uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t125 = ((t124 == ((uint64_t)0ULL)));
        gm_l31 = t125;
    } else {
        gm_l31 = ((uint8_t)0);
    }
    uint8_t t126 = gm_l31;
    if (t126) {
        uint64_t t127 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
        uint8_t t128 = ((t127 == ((uint64_t)0ULL)));
        gm_l30 = t128;
    } else {
        gm_l30 = ((uint8_t)0);
    }
    uint8_t t129 = gm_l30;
    gm_l17 = t129;
    uint8_t t130 = gm_l17;
    if (t130) {
        void * t131 = gm_l0;
        gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)t131) + 88));
        gm_l18 = t132;
        gm_l19 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t133 = gm_l19;
                uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                uint8_t t135 = (t133 < t134);
                if (!t135) {
                    goto gm_lb16;
                }
                {
                    void * t136 = GM_LD(void *, ((uint8_t *)&gm_l18));
                    uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint64_t t138 = gm_l19;
                    {
                        {
                            gm_tb31k5l794t7p t139 = GM_LD(gm_tb31k5l794t7p, (((uint8_t *)t136) + (uintptr_t)t138 * 16));
                            gm_l20 = t139;
                            uint32_t t140 = GM_LD(uint32_t, ((uint8_t *)&gm_l20));
                            uint32_t t141 = gm_l1;
                            uint8_t t142 = ((t140 == t141));
                            if (t142) {
                                uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                gm_toabfckqu3onu t144;
                                memset(&t144, 0, sizeof t144);
                                ((uint8_t *)&t144)[0] = 0;
                                GM_ST(uint64_t, ((uint8_t *)&t144 + 8), t143);
                                memmove(gm_out, ((uint8_t *)&t144), 48);
                                goto gm_fdone;
                            }
                            goto gm_lb19;
                        }
gm_lb20:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 107, 9);
                    }
gm_lb19:;
                }
gm_lb18:;
                uint64_t t145 = gm_l19;
                uint64_t r146; bool o147 = __builtin_add_overflow(t145, ((uint64_t)1ULL), &r146);
                uint64_t t148 = r146;
                gm_l19 = t148;
            }
        }
gm_lb16:;
    }
    void * t149 = gm_l0;
    memmove(((uint8_t *)&F.l21), (((uint8_t *)t149) + 48), 40);
    void * t150 = gm_l0;
    gm_trqfvmjtt4sij t151;
    gm_f_list_o00nc4s((void *)&t151, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t150) + 48), t151);
    void * t152 = gm_l0;
    void * t153 = (void *)(((uint8_t *)t152) + 48);
    gm_b16_8 t154 = gm_l3;
    gm_tftb1odc739t5 t155 = gm_f_extend_2kurgou(t153, t154);
    gm_l33 = t155;
    uint8_t t156 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
    if (!t156) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_src4tam33qo20j7, 113, 5);
    void * t157 = gm_l0;
    uint32_t t158 = gm_l1;
    void * t159 = gm_l10;
    gm_trqfvmjtt4sij t160 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l11));
    gm_f_stack_push_expr_bdl56rb((void *)&F.l22, (void *)NULL, cx, NULL, t157, t158, &t160, t159);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t161 = gm_l0;
    memmove((((uint8_t *)t161) + 48), ((uint8_t *)&F.l21), 40);
    uint8_t t162 = (uint8_t)!((((uint8_t *)&F.l22))[0] != 0);
    if (t162) {
    } else {
        gm_tgeh6n0c7c8kb t163 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&F.l22) + 8));
        gm_toabfckqu3onu t164;
        GM_ST(uint8_t, (uint8_t *)&t164 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t164 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t164 + 4, 0);
        ((uint8_t *)&t164)[0] = 1;
        GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t164 + 8), t163);
        memmove(gm_out, ((uint8_t *)&t164), 48);
        goto gm_fdone;
    }
    uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 8));
    void * t166 = gm_l0;
    uint32_t t167 = gm_l1;
    uint64_t t168 = gm_f_stack_frame_68512ca((void *)NULL, cx, NULL, t166, t167);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t r169; bool o170 = __builtin_add_overflow(t165, t168, &r169);
    uint64_t t171 = o170 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r169;
    gm_l23 = t171;
    uint8_t t172 = gm_l17;
    if (t172) {
        void * t173 = gm_l0;
        void * t174 = (void *)(((uint8_t *)t173) + 88);
        uint32_t t175 = gm_l1;
        uint64_t t176 = gm_l23;
        gm_tb31k5l794t7p t177;
        GM_ST(uint32_t, (uint8_t *)&t177 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t177 + 0), t175);
        GM_ST(uint64_t, ((uint8_t *)&t177 + 8), t176);
        gm_tftb1odc739t5 t178 = gm_f_push_1mcf4ec(t174, GM_CONV(gm_cskjv6t053tkje, t177));
        gm_l34 = t178;
        uint8_t t179 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
        if (!t179) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_src4tam33qo20j7, 117, 21);
    }
    uint64_t t180 = gm_l23;
    gm_toabfckqu3onu t181;
    memset(&t181, 0, sizeof t181);
    ((uint8_t *)&t181)[0] = 0;
    GM_ST(uint64_t, ((uint8_t *)&t181 + 8), t180);
    memmove(gm_out, ((uint8_t *)&t181), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b16_8 gm_f_stack_children_dedraea(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_stack_expr_33aekgt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stack_expr_33aekgt F;
    void * gm_l0; /* a */
    void * gm_l1; /* e */
    uint64_t gm_l2; /* m */
    gm_b16_8 gm_l4; /* args */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint64_t gm_l8; /* c */
    uint32_t gm_l9; /* g */
    void * gm_l10; /* f */
    uint32_t gm_l11; /* g */
    gm_tiilmfd16v3fc gm_l12; /* eref */
    uint32_t gm_l13; /* op */
    void * gm_l14; /* w */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    uint64_t gm_l18; /* c */
    void * gm_l19; /* f */
    uint32_t gm_l20; /* g */
    gm_tiilmfd16v3fc gm_l21; /* eref */
    uint32_t gm_l22; /* op */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    gm_t4lnf71mruqpt gm_l27; /* h */
    void * gm_l28; /* x */
    uint32_t gm_l29; /* id */
    gm_b16_8 gm_l31; /* decls */
    void * gm_l32; /* value */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint32_t gm_l35; /* d */
    void * gm_l37; /* stack */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_b16_8 gm_l41; /* ops */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    uint64_t gm_l44; /* op */
    uint32_t gm_l45; /* c */
    uint64_t gm_l46; /* depth */
    void * gm_l48; /* b */
    gm_b16_8 gm_l51; /* <tmp> */
    uint64_t gm_l52; /* <tmp> */
    gm_trcmmd12cefb1 gm_l53; /* st */
    uint32_t gm_l54; /* local */
    void * gm_l55; /* init */
    uint32_t gm_l56; /* t */
    gm_b16_8 gm_l57; /* <tmp> */
    uint64_t gm_l58; /* <tmp> */
    void * gm_l59; /* c */
    gm_b16_8 gm_l60; /* <tmp> */
    uint64_t gm_l61; /* <tmp> */
    void * gm_l62; /* c */
    GM_STACK_CHECK(&F, 6585u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_toabfckqu3onu gm_l63;
    gm_toabfckqu3onu gm_l64;
    gm_toabfckqu3onu gm_l65;
    gm_t9805qtonbsag gm_l66;
    uint32_t gm_l67;
    gm_toabfckqu3onu gm_l68;
    gm_toabfckqu3onu gm_l69;
    gm_toabfckqu3onu gm_l70;
    gm_toabfckqu3onu gm_l71;
    gm_t9805qtonbsag gm_l72;
    uint32_t gm_l73;
    gm_toabfckqu3onu gm_l74;
    gm_toabfckqu3onu gm_l75;
    gm_toabfckqu3onu gm_l76;
    gm_t53at80pfdus3 gm_l77;
    gm_tftb1odc739t5 gm_l78;
    gm_toabfckqu3onu gm_l79;
    gm_tftb1odc739t5 gm_l80;
    gm_toabfckqu3onu gm_l81;
    gm_tn68el8d14vku gm_l82;
    gm_toabfckqu3onu gm_l83;
    gm_tftb1odc739t5 gm_l84;
    gm_tijr008e33o2m gm_l85;
    gm_tijr008e33o2m gm_l86;
    gm_tha5675rv512q gm_l87;
    gm_t9805qtonbsag gm_l88;
    gm_toabfckqu3onu gm_l89;
    gm_toabfckqu3onu gm_l90;
    {
        void * t73 = gm_l0;
        void * t74 = GM_LD(void *, ((uint8_t *)t73));
        void * t75 = (void *)(((uint8_t *)t74) + 8);
        void * t76 = gm_l1;
        gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t76));
        uint8_t t78 = gm_f_mentions_error_37knu7r(t75, GM_CONV(gm_csguankko4lmhd, t77));
        if (t78) {
            gm_tgeh6n0c7c8kb t79;
            memset(&t79, 0, sizeof t79);
            GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)0ULL));
            gm_toabfckqu3onu t80;
            GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
            ((uint8_t *)&t80)[0] = 1;
            GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t80 + 8), t79);
            memmove(gm_out, ((uint8_t *)&t80), 48);
            goto gm_fdone;
        }
        gm_l2 = ((uint64_t)0ULL);
        void * t81 = gm_l1;
        {
            {
                {
                    uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t81) + 8) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)34ULL));
                    if (!t83) {
                        goto gm_lb2;
                    }
                    memmove(((uint8_t *)&F.l3), ((((uint8_t *)t81) + 8) + 8), 40);
                    gm_b16_8 t84 = GM_LD(gm_b16_8, (((((uint8_t *)t81) + 8) + 8) + 40));
                    gm_l4 = t84;
                    gm_l6 = ((uint64_t)0ULL);
                    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    gm_l5 = t85;
                    {
                        for (;;) {
                            uint64_t t86 = gm_l6;
                            uint64_t t87 = gm_l5;
                            uint8_t t88 = (t86 < t87);
                            if (!t88) {
                                goto gm_lb3;
                            }
                            {
                                {
                                    {
                                        uint64_t t89 = gm_l6;
                                        gm_l7 = t89;
                                        uint64_t t90 = gm_l2;
                                        void * t91 = gm_l0;
                                        void * t92 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                        uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                        uint64_t t94 = gm_l7;
                                        void * t95 = (void *)(((uint8_t *)t92) + (uintptr_t)t94 * 88);
                                        gm_f_stack_expr_33aekgt((void *)&gm_l63, (void *)NULL, cx, NULL, t91, t95);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                                        if (t96) {
                                        } else {
                                            gm_tgeh6n0c7c8kb t97 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&gm_l63) + 8));
                                            gm_toabfckqu3onu t98;
                                            GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                                            ((uint8_t *)&t98)[0] = 1;
                                            GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t98 + 8), t97);
                                            memmove(gm_out, ((uint8_t *)&t98), 48);
                                            goto gm_fdone;
                                        }
                                        uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l63) + 8));
                                        uint64_t t100 = (t90 > t99 ? t90 : t99);
                                        gm_l2 = t100;
                                        goto gm_lb6;
                                    }
gm_lb7:;
                                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 180, 13);
                                }
gm_lb6:;
                            }
gm_lb5:;
                            uint64_t t101 = gm_l6;
                            uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                            uint64_t t104 = r102;
                            gm_l6 = t104;
                        }
                    }
gm_lb3:;
                    gm_l8 = ((uint64_t)0ULL);
                    {
                        {
                            uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                            uint8_t t106 = (uint8_t)(t105 == ((uint8_t)0ULL));
                            if (!t106) {
                                goto gm_lb9;
                            }
                            uint32_t t107 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 8));
                            gm_l9 = t107;
                            void * t108 = gm_l0;
                            uint32_t t109 = gm_l9;
                            gm_b16_8 t110 = gm_l4;
                            gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                            void * t112 = gm_l0;
                            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)t112) + 48));
                            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                            void * t115 = gm_l1;
                            gm_tl874t2atnlh3 t116 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t115) + 72));
                            gm_f_stack_call_poqmlno((void *)&gm_l64, (void *)NULL, cx, NULL, t108, t109, t111, t114, t116);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                            if (t117) {
                            } else {
                                gm_tgeh6n0c7c8kb t118 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&gm_l64) + 8));
                                gm_toabfckqu3onu t119;
                                GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
                                ((uint8_t *)&t119)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t119 + 8), t118);
                                memmove(gm_out, ((uint8_t *)&t119), 48);
                                goto gm_fdone;
                            }
                            uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l64) + 8));
                            gm_l8 = t120;
                            goto gm_lb8;
                        }
gm_lb9:;
                        {
                            uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                            uint8_t t122 = (uint8_t)(t121 == ((uint8_t)2ULL));
                            if (!t122) {
                                goto gm_lb10;
                            }
                            void * t123 = GM_LD(void *, (((uint8_t *)&F.l3) + 8));
                            gm_l10 = t123;
                            uint64_t t124 = gm_l2;
                            void * t125 = gm_l0;
                            void * t126 = gm_l10;
                            gm_f_stack_expr_33aekgt((void *)&gm_l65, (void *)NULL, cx, NULL, t125, t126);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t127 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                            if (t127) {
                            } else {
                                gm_tgeh6n0c7c8kb t128 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&gm_l65) + 8));
                                gm_toabfckqu3onu t129;
                                GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
                                ((uint8_t *)&t129)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t129 + 8), t128);
                                memmove(gm_out, ((uint8_t *)&t129), 48);
                                goto gm_fdone;
                            }
                            uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&gm_l65) + 8));
                            uint64_t t131 = (t124 > t130 ? t124 : t130);
                            gm_l2 = t131;
                            void * t132 = gm_l0;
                            void * t133 = gm_l10;
                            gm_t9805qtonbsag t134 = gm_f_stack_target_7vr78v6(t132, t133);
                            gm_l66 = t134;
                            uint8_t t135 = (uint8_t)((((uint8_t *)&gm_l66))[4] != 0);
                            if (t135) {
                                uint32_t t136 = GM_LD(uint32_t, ((uint8_t *)&gm_l66));
                                gm_l67 = t136;
                            } else {
                                void * t137 = gm_l1;
                                gm_tl874t2atnlh3 t138 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t137) + 72));
                                gm_b16_8 t139;
                                GM_ST(void *, (uint8_t *)&t139, (void *)(gm_sg0997vvtiund));
                                GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(64));
                                gm_tbgd14esf0jvi t140;
                                GM_ST(uint32_t, (uint8_t *)&t140 + 12, 0);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t140 + 0), t138);
                                GM_ST(gm_b16_8, ((uint8_t *)&t140 + 16), t139);
                                gm_tgeh6n0c7c8kb t141;
                                GM_ST(uint8_t, (uint8_t *)&t141 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t141 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t141 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t141 + 8), t140);
                                gm_toabfckqu3onu t142;
                                GM_ST(uint8_t, (uint8_t *)&t142 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t142 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t142 + 4, 0);
                                ((uint8_t *)&t142)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t142 + 8), t141);
                                memmove(gm_out, ((uint8_t *)&t142), 48);
                                goto gm_fdone;
                            }
                            uint32_t t143 = gm_l67;
                            gm_l11 = t143;
                            void * t144 = gm_l0;
                            uint32_t t145 = gm_l11;
                            gm_b16_8 t146 = gm_l4;
                            gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
                            void * t148 = gm_l0;
                            gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)t148) + 48));
                            gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                            void * t151 = gm_l1;
                            gm_tl874t2atnlh3 t152 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t151) + 72));
                            gm_f_stack_call_poqmlno((void *)&gm_l68, (void *)NULL, cx, NULL, t144, t145, t147, t150, t152);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t153 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                            if (t153) {
                            } else {
                                gm_tgeh6n0c7c8kb t154 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&gm_l68) + 8));
                                gm_toabfckqu3onu t155;
                                GM_ST(uint8_t, (uint8_t *)&t155 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t155 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t155 + 4, 0);
                                ((uint8_t *)&t155)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t155 + 8), t154);
                                memmove(gm_out, ((uint8_t *)&t155), 48);
                                goto gm_fdone;
                            }
                            uint64_t t156 = GM_LD(uint64_t, (((uint8_t *)&gm_l68) + 8));
                            gm_l8 = t156;
                            goto gm_lb8;
                        }
gm_lb10:;
                        {
                            uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                            uint8_t t158 = (uint8_t)(t157 == ((uint8_t)1ULL));
                            if (!t158) {
                                goto gm_lb11;
                            }
                            memmove(((uint8_t *)&gm_l12), (((uint8_t *)&F.l3) + 8), 24);
                            uint32_t t159 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 8) + 24));
                            gm_l13 = t159;
                            void * t160 = gm_l0;
                            void * t161 = (void *)((uint8_t *)&gm_l12);
                            uint32_t t162 = gm_l13;
                            gm_b16_8 t163 = gm_l4;
                            gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)&t163));
                            void * t165 = gm_l1;
                            gm_tl874t2atnlh3 t166 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t165) + 72));
                            gm_f_stack_perform_soivemm((void *)&gm_l69, (void *)NULL, cx, NULL, t160, t161, t162, t164, t166);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t167 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                            if (t167) {
                            } else {
                                gm_tgeh6n0c7c8kb t168 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&gm_l69) + 8));
                                gm_toabfckqu3onu t169;
                                GM_ST(uint8_t, (uint8_t *)&t169 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t169 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t169 + 4, 0);
                                ((uint8_t *)&t169)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t169 + 8), t168);
                                memmove(gm_out, ((uint8_t *)&t169), 48);
                                goto gm_fdone;
                            }
                            uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)&gm_l69) + 8));
                            gm_l8 = t170;
                            goto gm_lb8;
                        }
gm_lb11:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 182, 13);
                    }
gm_lb8:;
                    uint64_t t171 = gm_l2;
                    uint64_t t172 = gm_l8;
                    uint64_t t173 = (t171 > t172 ? t171 : t172);
                    gm_l2 = t173;
                    goto gm_lb1;
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t174 = GM_LD(uint8_t, ((((uint8_t *)t81) + 8) + 0));
                    uint8_t t175 = (uint8_t)(t174 == ((uint8_t)55ULL));
                    if (!t175) {
                        goto gm_lb12;
                    }
                    void * t176 = GM_LD(void *, ((((uint8_t *)t81) + 8) + 8));
                    gm_l14 = t176;
                    gm_l16 = ((uint64_t)0ULL);
                    void * t177 = gm_l14;
                    uint64_t t178 = GM_LD(uint64_t, ((((uint8_t *)t177) + 72) + 8));
                    gm_l15 = t178;
                    {
                        for (;;) {
                            uint64_t t179 = gm_l16;
                            uint64_t t180 = gm_l15;
                            uint8_t t181 = (t179 < t180);
                            if (!t181) {
                                goto gm_lb13;
                            }
                            {
                                {
                                    {
                                        uint64_t t182 = gm_l16;
                                        gm_l17 = t182;
                                        uint64_t t183 = gm_l2;
                                        void * t184 = gm_l0;
                                        void * t185 = gm_l14;
                                        void * t186 = GM_LD(void *, (((uint8_t *)t185) + 72));
                                        uint64_t t187 = GM_LD(uint64_t, ((((uint8_t *)t185) + 72) + 8));
                                        uint64_t t188 = gm_l17;
                                        if ((uintptr_t)t188 >= (uintptr_t)(t187)) gm_trap_index((uintptr_t)t188, t187, gm_src4tam33qo20j7, 196, 67);
                                        void * t189 = (void *)(((uint8_t *)t186) + (uintptr_t)t188 * 88);
                                        gm_f_stack_expr_33aekgt((void *)&gm_l70, (void *)NULL, cx, NULL, t184, t189);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t190 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                                        if (t190) {
                                        } else {
                                            gm_tgeh6n0c7c8kb t191 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&gm_l70) + 8));
                                            gm_toabfckqu3onu t192;
                                            GM_ST(uint8_t, (uint8_t *)&t192 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t192 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t192 + 4, 0);
                                            ((uint8_t *)&t192)[0] = 1;
                                            GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t192 + 8), t191);
                                            memmove(gm_out, ((uint8_t *)&t192), 48);
                                            goto gm_fdone;
                                        }
                                        uint64_t t193 = GM_LD(uint64_t, (((uint8_t *)&gm_l70) + 8));
                                        uint64_t t194 = (t183 > t193 ? t183 : t193);
                                        gm_l2 = t194;
                                        goto gm_lb16;
                                    }
gm_lb17:;
                                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 196, 13);
                                }
gm_lb16:;
                            }
gm_lb15:;
                            uint64_t t195 = gm_l16;
                            uint64_t r196; bool o197 = __builtin_add_overflow(t195, ((uint64_t)1ULL), &r196);
                            uint64_t t198 = r196;
                            gm_l16 = t198;
                        }
                    }
gm_lb13:;
                    gm_l18 = ((uint64_t)0ULL);
                    void * t199 = gm_l14;
                    {
                        {
                            uint8_t t200 = GM_LD(uint8_t, (((uint8_t *)t199) + 0));
                            uint8_t t201 = (uint8_t)(t200 == ((uint8_t)1ULL));
                            if (!t201) {
                                goto gm_lb19;
                            }
                            void * t202 = GM_LD(void *, (((uint8_t *)t199) + 8));
                            gm_l19 = t202;
                            uint64_t t203 = gm_l2;
                            void * t204 = gm_l0;
                            void * t205 = gm_l19;
                            gm_f_stack_expr_33aekgt((void *)&gm_l71, (void *)NULL, cx, NULL, t204, t205);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t206 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                            if (t206) {
                            } else {
                                gm_tgeh6n0c7c8kb t207 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&gm_l71) + 8));
                                gm_toabfckqu3onu t208;
                                GM_ST(uint8_t, (uint8_t *)&t208 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t208 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t208 + 4, 0);
                                ((uint8_t *)&t208)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t208 + 8), t207);
                                memmove(gm_out, ((uint8_t *)&t208), 48);
                                goto gm_fdone;
                            }
                            uint64_t t209 = GM_LD(uint64_t, (((uint8_t *)&gm_l71) + 8));
                            uint64_t t210 = (t203 > t209 ? t203 : t209);
                            gm_l2 = t210;
                            void * t211 = gm_l0;
                            void * t212 = gm_l19;
                            gm_t9805qtonbsag t213 = gm_f_stack_target_7vr78v6(t211, t212);
                            gm_l72 = t213;
                            uint8_t t214 = (uint8_t)((((uint8_t *)&gm_l72))[4] != 0);
                            if (t214) {
                                uint32_t t215 = GM_LD(uint32_t, ((uint8_t *)&gm_l72));
                                gm_l73 = t215;
                            } else {
                                void * t216 = gm_l1;
                                gm_tl874t2atnlh3 t217 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t216) + 72));
                                gm_b16_8 t218;
                                GM_ST(void *, (uint8_t *)&t218, (void *)(gm_s2sodn6uclrlt));
                                GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(62));
                                gm_tbgd14esf0jvi t219;
                                GM_ST(uint32_t, (uint8_t *)&t219 + 12, 0);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t219 + 0), t217);
                                GM_ST(gm_b16_8, ((uint8_t *)&t219 + 16), t218);
                                gm_tgeh6n0c7c8kb t220;
                                GM_ST(uint8_t, (uint8_t *)&t220 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t220 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t220 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t220 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t220 + 8), t219);
                                gm_toabfckqu3onu t221;
                                GM_ST(uint8_t, (uint8_t *)&t221 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t221 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t221 + 4, 0);
                                ((uint8_t *)&t221)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t221 + 8), t220);
                                memmove(gm_out, ((uint8_t *)&t221), 48);
                                goto gm_fdone;
                            }
                            uint32_t t222 = gm_l73;
                            gm_l20 = t222;
                            void * t223 = gm_l0;
                            uint32_t t224 = gm_l20;
                            gm_b16_8 t225 = gm_f_empty_slice_g3tddrb_2();
                            gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                            void * t227 = gm_l0;
                            gm_b16_8 t228 = GM_LD(gm_b16_8, (((uint8_t *)t227) + 48));
                            gm_b16_8 t229 = GM_LD(gm_b16_8, ((uint8_t *)&t228));
                            void * t230 = gm_l1;
                            gm_tl874t2atnlh3 t231 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t230) + 72));
                            gm_f_stack_call_poqmlno((void *)&gm_l74, (void *)NULL, cx, NULL, t223, t224, t226, t229, t231);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t232 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                            if (t232) {
                            } else {
                                gm_tgeh6n0c7c8kb t233 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&gm_l74) + 8));
                                gm_toabfckqu3onu t234;
                                GM_ST(uint8_t, (uint8_t *)&t234 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t234 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t234 + 4, 0);
                                ((uint8_t *)&t234)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t234 + 8), t233);
                                memmove(gm_out, ((uint8_t *)&t234), 48);
                                goto gm_fdone;
                            }
                            uint64_t t235 = GM_LD(uint64_t, (((uint8_t *)&gm_l74) + 8));
                            gm_l18 = t235;
                            goto gm_lb18;
                        }
gm_lb19:;
                        {
                            uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)t199) + 0));
                            uint8_t t237 = (uint8_t)(t236 == ((uint8_t)0ULL));
                            if (!t237) {
                                goto gm_lb20;
                            }
                            memmove(((uint8_t *)&gm_l21), (((uint8_t *)t199) + 8), 24);
                            uint32_t t238 = GM_LD(uint32_t, ((((uint8_t *)t199) + 8) + 24));
                            gm_l22 = t238;
                            void * t239 = gm_l0;
                            void * t240 = (void *)((uint8_t *)&gm_l21);
                            uint32_t t241 = gm_l22;
                            gm_b16_8 t242 = gm_f_empty_slice_g3tddrb_2();
                            gm_b16_8 t243 = GM_LD(gm_b16_8, ((uint8_t *)&t242));
                            void * t244 = gm_l1;
                            gm_tl874t2atnlh3 t245 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t244) + 72));
                            gm_f_stack_perform_soivemm((void *)&gm_l75, (void *)NULL, cx, NULL, t239, t240, t241, t243, t245);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t246 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                            if (t246) {
                            } else {
                                gm_tgeh6n0c7c8kb t247 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&gm_l75) + 8));
                                gm_toabfckqu3onu t248;
                                GM_ST(uint8_t, (uint8_t *)&t248 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t248 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t248 + 4, 0);
                                ((uint8_t *)&t248)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t248 + 8), t247);
                                memmove(gm_out, ((uint8_t *)&t248), 48);
                                goto gm_fdone;
                            }
                            uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)&gm_l75) + 8));
                            gm_l18 = t249;
                            goto gm_lb18;
                        }
gm_lb20:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 198, 13);
                    }
gm_lb18:;
                    uint64_t t250 = gm_l2;
                    uint64_t t251 = gm_l18;
                    uint64_t t252 = (t250 > t251 ? t250 : t251);
                    gm_l2 = t252;
                    goto gm_lb1;
                }
gm_lb12:;
            }
            {
                {
                    {
                        uint8_t t253 = GM_LD(uint8_t, ((((uint8_t *)t81) + 8) + 0));
                        uint8_t t254 = (uint8_t)(t253 == ((uint8_t)59ULL));
                        if (!t254) {
                            goto gm_lb21;
                        }
                        memmove(((uint8_t *)&F.l23), ((((uint8_t *)t81) + 8) + 8), 40);
                        gm_trqfvmjtt4sij t255;
                        gm_f_list_o00nc4s((void *)&t255, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l24 = t255;
                        gm_b16_8 t256 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 8));
                        gm_l25 = t256;
                        gm_l26 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t257 = gm_l26;
                                uint64_t t258 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                uint8_t t259 = (t257 < t258);
                                if (!t259) {
                                    goto gm_lb22;
                                }
                                {
                                    void * t260 = GM_LD(void *, ((uint8_t *)&gm_l25));
                                    uint64_t t261 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                    uint64_t t262 = gm_l26;
                                    {
                                        {
                                            memmove(((uint8_t *)&gm_l27), (((uint8_t *)t260) + (uintptr_t)t262 * 32), 32);
                                            {
                                                {
                                                    uint8_t t263 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                                                    uint8_t t264 = (uint8_t)(t263 == ((uint8_t)0ULL));
                                                    if (!t264) {
                                                        goto gm_lb28;
                                                    }
                                                    void * t265 = GM_LD(void *, (((uint8_t *)&gm_l27) + 8));
                                                    gm_l28 = t265;
                                                    uint64_t t266 = gm_l2;
                                                    void * t267 = gm_l0;
                                                    void * t268 = gm_l28;
                                                    gm_f_stack_expr_33aekgt((void *)&gm_l76, (void *)NULL, cx, NULL, t267, t268);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t269 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                                                    if (t269) {
                                                    } else {
                                                        gm_tgeh6n0c7c8kb t270 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&gm_l76) + 8));
                                                        gm_toabfckqu3onu t271;
                                                        GM_ST(uint8_t, (uint8_t *)&t271 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t271 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t271 + 4, 0);
                                                        ((uint8_t *)&t271)[0] = 1;
                                                        GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t271 + 8), t270);
                                                        memmove(gm_out, ((uint8_t *)&t271), 48);
                                                        goto gm_fdone;
                                                    }
                                                    uint64_t t272 = GM_LD(uint64_t, (((uint8_t *)&gm_l76) + 8));
                                                    uint64_t t273 = (t266 > t272 ? t266 : t272);
                                                    gm_l2 = t273;
                                                    {
                                                        {
                                                            void * t274 = gm_l0;
                                                            void * t275 = GM_LD(void *, ((uint8_t *)t274));
                                                            void * t276 = (void *)(((uint8_t *)t275) + 8);
                                                            void * t277 = gm_l28;
                                                            gm_t88rcucg1b7uk t278 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t277));
                                                            gm_f_kind_jcnt4hm((void *)&gm_l77, t276, GM_CONV(gm_csguankko4lmhd, t278));
                                                            uint8_t t279 = GM_LD(uint8_t, (((uint8_t *)&gm_l77) + 0));
                                                            uint8_t t280 = (uint8_t)(t279 == ((uint8_t)25ULL));
                                                            if (!t280) {
                                                                goto gm_lb30;
                                                            }
                                                            uint32_t t281 = GM_LD(uint32_t, (((uint8_t *)&gm_l77) + 8));
                                                            gm_l29 = t281;
                                                            void * t282 = gm_l0;
                                                            void * t283 = GM_LD(void *, ((uint8_t *)t282));
                                                            void * t284 = GM_LD(void *, (((uint8_t *)t283) + 504));
                                                            uint64_t t285 = GM_LD(uint64_t, ((((uint8_t *)t283) + 504) + 8));
                                                            uint32_t t286 = gm_l29;
                                                            uint64_t t287 = (uint64_t)t286;
                                                            if ((uintptr_t)t287 >= (uintptr_t)(t285)) gm_trap_index((uintptr_t)t287, t285, gm_src4tam33qo20j7, 217, 32);
                                                            memmove(((uint8_t *)&F.l30), (((uint8_t *)t284) + (uintptr_t)t287 * 40), 40);
                                                            void * t288 = (void *)((uint8_t *)&F.l24);
                                                            gm_b16_8 t289 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 24));
                                                            gm_tiilmfd16v3fc t290 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&F.l30));
                                                            gm_t1cs2fjnrqap1 t291;
                                                            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t291 + 0), t290);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t291 + 24), t289);
                                                            gm_tftb1odc739t5 t292 = gm_f_push_njfep1h(t288, &t291);
                                                            gm_l78 = t292;
                                                            uint8_t t293 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                                                            if (!t293) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 1, gm_src4tam33qo20j7, 218, 25);
                                                            goto gm_lb29;
                                                        }
gm_lb30:;
                                                        gm_tgeh6n0c7c8kb t294;
                                                        memset(&t294, 0, sizeof t294);
                                                        GM_ST(uint8_t, ((uint8_t *)&t294 + 0), ((uint8_t)0ULL));
                                                        gm_toabfckqu3onu t295;
                                                        GM_ST(uint8_t, (uint8_t *)&t295 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t295 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t295 + 4, 0);
                                                        ((uint8_t *)&t295)[0] = 1;
                                                        GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t295 + 8), t294);
                                                        memmove(gm_out, ((uint8_t *)&t295), 48);
                                                        goto gm_fdone;
                                                    }
gm_lb29:;
                                                    goto gm_lb27;
                                                }
gm_lb28:;
                                                {
                                                    uint8_t t296 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                                                    uint8_t t297 = (uint8_t)(t296 == ((uint8_t)1ULL));
                                                    if (!t297) {
                                                        goto gm_lb31;
                                                    }
                                                    gm_b16_8 t298 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
                                                    gm_l31 = t298;
                                                    void * t299 = GM_LD(void *, ((((uint8_t *)&gm_l27) + 8) + 16));
                                                    gm_l32 = t299;
                                                    uint64_t t300 = gm_l2;
                                                    void * t301 = gm_l0;
                                                    void * t302 = gm_l32;
                                                    gm_f_stack_expr_33aekgt((void *)&gm_l79, (void *)NULL, cx, NULL, t301, t302);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t303 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                                                    if (t303) {
                                                    } else {
                                                        gm_tgeh6n0c7c8kb t304 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&gm_l79) + 8));
                                                        gm_toabfckqu3onu t305;
                                                        GM_ST(uint8_t, (uint8_t *)&t305 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t305 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t305 + 4, 0);
                                                        ((uint8_t *)&t305)[0] = 1;
                                                        GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t305 + 8), t304);
                                                        memmove(gm_out, ((uint8_t *)&t305), 48);
                                                        goto gm_fdone;
                                                    }
                                                    uint64_t t306 = GM_LD(uint64_t, (((uint8_t *)&gm_l79) + 8));
                                                    uint64_t t307 = (t300 > t306 ? t300 : t306);
                                                    gm_l2 = t307;
                                                    gm_b16_8 t308 = gm_l31;
                                                    gm_l33 = t308;
                                                    gm_l34 = ((uint64_t)0ULL);
                                                    {
                                                        for (;;) {
                                                            uint64_t t309 = gm_l34;
                                                            uint64_t t310 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                                            uint8_t t311 = (t309 < t310);
                                                            if (!t311) {
                                                                goto gm_lb32;
                                                            }
                                                            {
                                                                void * t312 = GM_LD(void *, ((uint8_t *)&gm_l33));
                                                                uint64_t t313 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                                                uint64_t t314 = gm_l34;
                                                                {
                                                                    {
                                                                        uint32_t t315 = GM_LD(uint32_t, (((uint8_t *)t312) + (uintptr_t)t314 * 4));
                                                                        gm_l35 = t315;
                                                                        void * t316 = gm_l0;
                                                                        void * t317 = GM_LD(void *, ((uint8_t *)t316));
                                                                        void * t318 = GM_LD(void *, (((uint8_t *)t317) + 544));
                                                                        uint64_t t319 = GM_LD(uint64_t, ((((uint8_t *)t317) + 544) + 8));
                                                                        uint32_t t320 = gm_l35;
                                                                        uint64_t t321 = (uint64_t)t320;
                                                                        if ((uintptr_t)t321 >= (uintptr_t)(t319)) gm_trap_index((uintptr_t)t321, t319, gm_src4tam33qo20j7, 223, 37);
                                                                        memmove(((uint8_t *)&F.l36), (((uint8_t *)t318) + (uintptr_t)t321 * 64), 64);
                                                                        void * t322 = (void *)((uint8_t *)&F.l24);
                                                                        gm_b16_8 t323 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 48));
                                                                        gm_tiilmfd16v3fc t324 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&F.l36));
                                                                        gm_t1cs2fjnrqap1 t325;
                                                                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t325 + 0), t324);
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t325 + 24), t323);
                                                                        gm_tftb1odc739t5 t326 = gm_f_push_njfep1h(t322, &t325);
                                                                        gm_l80 = t326;
                                                                        uint8_t t327 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                                                                        if (!t327) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 1, gm_src4tam33qo20j7, 224, 29);
                                                                        goto gm_lb35;
                                                                    }
gm_lb36:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 222, 25);
                                                                }
gm_lb35:;
                                                            }
gm_lb34:;
                                                            uint64_t t328 = gm_l34;
                                                            uint64_t r329; bool o330 = __builtin_add_overflow(t328, ((uint64_t)1ULL), &r329);
                                                            uint64_t t331 = r329;
                                                            gm_l34 = t331;
                                                        }
                                                    }
gm_lb32:;
                                                    goto gm_lb27;
                                                }
gm_lb31:;
                                                gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 213, 17);
                                            }
gm_lb27:;
                                            goto gm_lb25;
                                        }
gm_lb26:;
                                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 212, 13);
                                    }
gm_lb25:;
                                }
gm_lb24:;
                                uint64_t t332 = gm_l26;
                                uint64_t r333; bool o334 = __builtin_add_overflow(t332, ((uint64_t)1ULL), &r333);
                                uint64_t t335 = r333;
                                gm_l26 = t335;
                            }
                        }
gm_lb22:;
                    }
                    {
                        {
                            {
                                uint8_t t336 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l23) + 24)) != 0);
                                if (!t336) {
                                    goto gm_lb38;
                                }
                                void * t337 = GM_LD(void *, (((uint8_t *)&F.l23) + 24));
                                gm_l37 = t337;
                                uint64_t t338 = gm_l2;
                                void * t339 = gm_l0;
                                void * t340 = gm_l37;
                                gm_f_stack_expr_33aekgt((void *)&gm_l81, (void *)NULL, cx, NULL, t339, t340);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t341 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                                if (t341) {
                                } else {
                                    gm_tgeh6n0c7c8kb t342 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&gm_l81) + 8));
                                    gm_toabfckqu3onu t343;
                                    GM_ST(uint8_t, (uint8_t *)&t343 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t343 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t343 + 4, 0);
                                    ((uint8_t *)&t343)[0] = 1;
                                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t343 + 8), t342);
                                    memmove(gm_out, ((uint8_t *)&t343), 48);
                                    goto gm_fdone;
                                }
                                uint64_t t344 = GM_LD(uint64_t, (((uint8_t *)&gm_l81) + 8));
                                uint64_t t345 = (t338 > t344 ? t338 : t344);
                                gm_l2 = t345;
                                gm_b16_8 t346 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
                                gm_l38 = t346;
                                gm_l39 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t347 = gm_l39;
                                        uint64_t t348 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                        uint8_t t349 = (t347 < t348);
                                        if (!t349) {
                                            goto gm_lb39;
                                        }
                                        {
                                            void * t350 = GM_LD(void *, ((uint8_t *)&gm_l38));
                                            uint64_t t351 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                            uint64_t t352 = gm_l39;
                                            {
                                                {
                                                    memmove(((uint8_t *)&F.l40), (((uint8_t *)t350) + (uintptr_t)t352 * 40), 40);
                                                    void * t353 = gm_l0;
                                                    void * t354 = GM_LD(void *, ((uint8_t *)t353));
                                                    void * t355 = GM_LD(void *, (((uint8_t *)t354) + 1048));
                                                    uint64_t t356 = GM_LD(uint64_t, ((((uint8_t *)t354) + 1048) + 8));
                                                    uint32_t t357 = GM_LD(uint32_t, ((uint8_t *)&F.l40));
                                                    uint64_t t358 = (uint64_t)t357;
                                                    if ((uintptr_t)t358 >= (uintptr_t)(t356)) gm_trap_index((uintptr_t)t358, t356, gm_src4tam33qo20j7, 234, 28);
                                                    gm_b16_8 t359 = GM_LD(gm_b16_8, ((((uint8_t *)t355) + (uintptr_t)t358 * 56) + 40));
                                                    gm_l41 = t359;
                                                    gm_b16_8 t360 = GM_LD(gm_b16_8, (((uint8_t *)&F.l40) + 24));
                                                    gm_b16_8 t361 = GM_LD(gm_b16_8, ((uint8_t *)&t360));
                                                    gm_l42 = t361;
                                                    gm_l43 = ((uint64_t)0ULL);
                                                    {
                                                        for (;;) {
                                                            uint64_t t362 = gm_l43;
                                                            uint64_t t363 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                                                            uint8_t t364 = (t362 < t363);
                                                            if (!t364) {
                                                                goto gm_lb44;
                                                            }
                                                            {
                                                                uint64_t t365 = gm_l43;
                                                                void * t366 = GM_LD(void *, ((uint8_t *)&gm_l42));
                                                                uint64_t t367 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                                                                uint64_t t368 = gm_l43;
                                                                uint32_t t369 = GM_LD(uint32_t, (((uint8_t *)t366) + (uintptr_t)t368 * 4));
                                                                gm_tn68el8d14vku t370;
                                                                GM_ST(uint32_t, (uint8_t *)&t370 + 12, 0);
                                                                GM_ST(uint64_t, ((uint8_t *)&t370 + 0), t365);
                                                                GM_ST(uint32_t, ((uint8_t *)&t370 + 8), t369);
                                                                gm_l82 = t370;
                                                                {
                                                                    {
                                                                        uint64_t t371 = GM_LD(uint64_t, ((uint8_t *)&gm_l82));
                                                                        gm_l44 = t371;
                                                                        uint32_t t372 = GM_LD(uint32_t, (((uint8_t *)&gm_l82) + 8));
                                                                        gm_l45 = t372;
                                                                        {
                                                                            {
                                                                                uint64_t t373 = gm_l44;
                                                                                uint64_t t374 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                                                                                uint8_t t375 = (t373 < t374);
                                                                                if (!t375) {
                                                                                    goto gm_lb50;
                                                                                }
                                                                                void * t376 = GM_LD(void *, ((uint8_t *)&gm_l41));
                                                                                uint64_t t377 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                                                                                uint64_t t378 = gm_l44;
                                                                                gm_t9qkh0rsooc8d t379 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t376) + (uintptr_t)t378 * 48) + 16));
                                                                                uint8_t t380 = GM_LD(uint8_t, (((uint8_t *)&t379) + 0));
                                                                                uint8_t t381 = (uint8_t)((t380 == ((uint8_t)2ULL)));
                                                                                if (!t381) {
                                                                                    goto gm_lb50;
                                                                                }
                                                                                uint64_t t382 = gm_l2;
                                                                                void * t383 = gm_l0;
                                                                                uint32_t t384 = gm_l45;
                                                                                gm_t9qkh0rsooc8d t385;
                                                                                GM_ST(uint8_t, ((uint8_t *)&t385 + 0), ((uint8_t)2ULL));
                                                                                gm_b16_8 t386 = gm_f_empty_slice_g3tddrb_2();
                                                                                gm_b16_8 t387 = GM_LD(gm_b16_8, ((uint8_t *)&t386));
                                                                                void * t388 = gm_l0;
                                                                                gm_b16_8 t389 = GM_LD(gm_b16_8, (((uint8_t *)t388) + 48));
                                                                                gm_b16_8 t390 = GM_LD(gm_b16_8, ((uint8_t *)&t389));
                                                                                void * t391 = gm_l1;
                                                                                gm_tl874t2atnlh3 t392 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t391) + 72));
                                                                                gm_f_stack_call_poqmlno((void *)&gm_l83, (void *)NULL, cx, NULL, t383, t384, t387, t390, t392);
                                                                                if (gm_unw.kind) {
                                                                                    goto gm_lad;
                                                                                }
                                                                                uint8_t t393 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
                                                                                if (t393) {
                                                                                } else {
                                                                                    gm_tgeh6n0c7c8kb t394 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&gm_l83) + 8));
                                                                                    gm_toabfckqu3onu t395;
                                                                                    GM_ST(uint8_t, (uint8_t *)&t395 + 1, 0);
                                                                                    GM_ST(uint16_t, (uint8_t *)&t395 + 2, 0);
                                                                                    GM_ST(uint32_t, (uint8_t *)&t395 + 4, 0);
                                                                                    ((uint8_t *)&t395)[0] = 1;
                                                                                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t395 + 8), t394);
                                                                                    memmove(gm_out, ((uint8_t *)&t395), 48);
                                                                                    goto gm_fdone;
                                                                                }
                                                                                uint64_t t396 = GM_LD(uint64_t, (((uint8_t *)&gm_l83) + 8));
                                                                                uint64_t t397 = (t382 > t396 ? t382 : t396);
                                                                                gm_l2 = t397;
                                                                                goto gm_lb49;
                                                                            }
gm_lb50:;
                                                                        }
gm_lb49:;
                                                                        goto gm_lb47;
                                                                    }
gm_lb48:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 235, 21);
                                                                }
gm_lb47:;
                                                            }
gm_lb46:;
                                                            uint64_t t398 = gm_l43;
                                                            uint64_t r399; bool o400 = __builtin_add_overflow(t398, ((uint64_t)1ULL), &r399);
                                                            uint64_t t401 = r399;
                                                            gm_l43 = t401;
                                                        }
                                                    }
gm_lb44:;
                                                    goto gm_lb42;
                                                }
gm_lb43:;
                                                gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 233, 17);
                                            }
gm_lb42:;
                                        }
gm_lb41:;
                                        uint64_t t402 = gm_l39;
                                        uint64_t r403; bool o404 = __builtin_add_overflow(t402, ((uint64_t)1ULL), &r403);
                                        uint64_t t405 = r403;
                                        gm_l39 = t405;
                                    }
                                }
gm_lb39:;
                                goto gm_lb37;
                            }
gm_lb38:;
                            void * t406 = gm_l0;
                            uint64_t t407 = GM_LD(uint64_t, ((((uint8_t *)t406) + 48) + 8));
                            gm_l46 = t407;
                            void * t408 = gm_l0;
                            void * t409 = (void *)(((uint8_t *)t408) + 48);
                            gm_b16_8 t410 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
                            gm_b16_8 t411 = GM_LD(gm_b16_8, ((uint8_t *)&t410));
                            gm_tftb1odc739t5 t412 = gm_f_extend_2kurgou(t409, t411);
                            gm_l84 = t412;
                            uint8_t t413 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
                            if (!t413) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l84) + 1, gm_src4tam33qo20j7, 241, 17);
                            void * t414 = gm_l0;
                            void * t415 = GM_LD(void *, (((uint8_t *)&F.l23) + 32));
                            gm_f_stack_expr_33aekgt((void *)&F.l47, (void *)NULL, cx, NULL, t414, t415);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t416 = gm_l0;
                            void * t417 = (void *)(((uint8_t *)t416) + 48);
                            uint64_t t418 = gm_l46;
                            gm_f_truncate_31h1fac(t417, t418);
                            uint64_t t419 = gm_l2;
                            uint8_t t420 = (uint8_t)!((((uint8_t *)&F.l47))[0] != 0);
                            if (t420) {
                            } else {
                                gm_tgeh6n0c7c8kb t421 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&F.l47) + 8));
                                gm_toabfckqu3onu t422;
                                GM_ST(uint8_t, (uint8_t *)&t422 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t422 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t422 + 4, 0);
                                ((uint8_t *)&t422)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t422 + 8), t421);
                                memmove(gm_out, ((uint8_t *)&t422), 48);
                                goto gm_fdone;
                            }
                            uint64_t t423 = GM_LD(uint64_t, (((uint8_t *)&F.l47) + 8));
                            uint64_t t424 = (t419 > t423 ? t419 : t423);
                            gm_l2 = t424;
                        }
gm_lb37:;
                    }
                    {
                        goto gm_lb1;
                    }
                }
gm_lb21:;
            }
            {
                {
                    uint8_t t425 = GM_LD(uint8_t, ((((uint8_t *)t81) + 8) + 0));
                    uint8_t t426 = (uint8_t)(t425 == ((uint8_t)46ULL));
                    if (!t426) {
                        goto gm_lb51;
                    }
                    void * t427 = GM_LD(void *, ((((uint8_t *)t81) + 8) + 8));
                    gm_l48 = t427;
                    void * t428 = gm_l0;
                    void * t429 = GM_LD(void *, (((uint8_t *)t428) + 8));
                    uint64_t t430 = GM_LD(uint64_t, ((((uint8_t *)t428) + 8) + 8));
                    uint64_t r431; bool o432 = __builtin_sub_overflow(t430, ((uint64_t)1ULL), &r431);
                    uint64_t t433 = r431;
                    uint8_t t434 = (t433 < t430);
                    if (t434) {
                        gm_tha5675rv512q t435 = GM_LD(gm_tha5675rv512q, (((uint8_t *)t429) + (uintptr_t)t433 * 48));
                        gm_tijr008e33o2m t436;
                        GM_ST(uint8_t, (uint8_t *)&t436 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t436 + 50, 0);
                        GM_ST(uint32_t, (uint8_t *)&t436 + 52, 0);
                        GM_ST(gm_tha5675rv512q, ((uint8_t *)&t436), t435);
                        ((uint8_t *)&t436)[48] = 1;
                        gm_l85 = t436;
                    } else {
                        gm_tijr008e33o2m t437;
                        memset(&t437, 0, sizeof t437);
                        gm_l85 = t437;
                    }
                    memmove(((uint8_t *)&gm_l86), ((uint8_t *)&gm_l85), 56);
                    uint8_t t438 = (uint8_t)((((uint8_t *)&gm_l86))[48] != 0);
                    if (t438) {
                        memmove(((uint8_t *)&gm_l87), ((uint8_t *)&gm_l86), 48);
                    } else {
                        gm_tgeh6n0c7c8kb t439;
                        memset(&t439, 0, sizeof t439);
                        GM_ST(uint8_t, ((uint8_t *)&t439 + 0), ((uint8_t)0ULL));
                        gm_toabfckqu3onu t440;
                        GM_ST(uint8_t, (uint8_t *)&t440 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t440 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t440 + 4, 0);
                        ((uint8_t *)&t440)[0] = 1;
                        GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t440 + 8), t439);
                        memmove(gm_out, ((uint8_t *)&t440), 48);
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&F.l49), ((uint8_t *)&gm_l87), 48);
                    void * t441 = gm_l0;
                    void * t442 = GM_LD(void *, ((uint8_t *)t441));
                    uint32_t t443 = GM_LD(uint32_t, ((uint8_t *)&F.l49));
                    gm_f_def_of_m512sbu((void *)&F.l50, t442, t443);
                    void * t444 = gm_l48;
                    gm_b16_8 t445 = GM_LD(gm_b16_8, ((uint8_t *)t444));
                    gm_l51 = t445;
                    gm_l52 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t446 = gm_l52;
                            uint64_t t447 = GM_LD(uint64_t, (((uint8_t *)&gm_l51) + 8));
                            uint8_t t448 = (t446 < t447);
                            if (!t448) {
                                goto gm_lb52;
                            }
                            {
                                void * t449 = GM_LD(void *, ((uint8_t *)&gm_l51));
                                uint64_t t450 = GM_LD(uint64_t, (((uint8_t *)&gm_l51) + 8));
                                uint64_t t451 = gm_l52;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l53), (((uint8_t *)t449) + (uintptr_t)t451 * 32), 32);
                                        {
                                            {
                                                uint8_t t452 = GM_LD(uint8_t, (((uint8_t *)&gm_l53) + 0));
                                                uint8_t t453 = (uint8_t)(t452 == ((uint8_t)0ULL));
                                                if (!t453) {
                                                    goto gm_lb58;
                                                }
                                                uint32_t t454 = GM_LD(uint32_t, (((uint8_t *)&gm_l53) + 8));
                                                gm_l54 = t454;
                                                uint8_t t455 = (uint8_t)(GM_LD(void *, ((((uint8_t *)&gm_l53) + 8) + 8)) != 0);
                                                if (!t455) {
                                                    goto gm_lb58;
                                                }
                                                void * t456 = GM_LD(void *, ((((uint8_t *)&gm_l53) + 8) + 8));
                                                gm_l55 = t456;
                                                void * t457 = GM_LD(void *, (((uint8_t *)&F.l50) + 24));
                                                uint64_t t458 = GM_LD(uint64_t, ((((uint8_t *)&F.l50) + 24) + 8));
                                                uint32_t t459 = gm_l54;
                                                uint64_t t460 = (uint64_t)t459;
                                                if ((uintptr_t)t460 >= (uintptr_t)(t458)) gm_trap_index((uintptr_t)t460, t458, gm_src4tam33qo20j7, 254, 28);
                                                uint8_t t461 = GM_LD(uint8_t, ((((uint8_t *)t457) + (uintptr_t)t460 * 40) + 20));
                                                uint8_t t462 = (uint8_t)!t461;
                                                if (t462) {
                                                    {
                                                        {
                                                            void * t463 = gm_l0;
                                                            void * t464 = gm_l55;
                                                            gm_t9805qtonbsag t465 = gm_f_stack_target_7vr78v6(t463, t464);
                                                            gm_l88 = t465;
                                                            uint8_t t466 = (uint8_t)((((uint8_t *)&gm_l88))[4] != 0);
                                                            if (!t466) {
                                                                goto gm_lb60;
                                                            }
                                                            uint32_t t467 = GM_LD(uint32_t, ((uint8_t *)&gm_l88));
                                                            gm_l56 = t467;
                                                            void * t468 = gm_l0;
                                                            void * t469 = GM_LD(void *, (((uint8_t *)t468) + 8));
                                                            uint64_t t470 = GM_LD(uint64_t, ((((uint8_t *)t468) + 8) + 8));
                                                            void * t471 = gm_l0;
                                                            uint64_t t472 = GM_LD(uint64_t, ((((uint8_t *)t471) + 8) + 8));
                                                            uint64_t r473; bool o474 = __builtin_sub_overflow(t472, ((uint64_t)1ULL), &r473);
                                                            if (o474) gm_trap("integer overflow in `-`", gm_src4tam33qo20j7, 256, 51);
                                                            uint64_t t475 = r473;
                                                            if ((uintptr_t)t475 >= (uintptr_t)(t470)) gm_trap_index((uintptr_t)t475, t470, gm_src4tam33qo20j7, 256, 38);
                                                            void * t476 = (void *)((((uint8_t *)t469) + (uintptr_t)t475 * 48) + 8);
                                                            uint32_t t477 = gm_l54;
                                                            uint32_t t478 = gm_l56;
                                                            gm_b0_1 t479 = gm_f_bind_fn_pefln9r((void *)NULL, cx, NULL, t476, t477, t478);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            goto gm_lb59;
                                                        }
gm_lb60:;
                                                    }
gm_lb59:;
                                                }
                                                goto gm_lb57;
                                            }
gm_lb58:;
                                        }
gm_lb57:;
                                        goto gm_lb55;
                                    }
gm_lb56:;
                                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 252, 13);
                                }
gm_lb55:;
                            }
gm_lb54:;
                            uint64_t t480 = gm_l52;
                            uint64_t r481; bool o482 = __builtin_add_overflow(t480, ((uint64_t)1ULL), &r481);
                            uint64_t t483 = r481;
                            gm_l52 = t483;
                        }
                    }
gm_lb52:;
                    void * t484 = gm_l1;
                    gm_b16_8 t485 = gm_f_stack_children_dedraea((void *)NULL, cx, NULL, t484);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l57 = t485;
                    gm_l58 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t486 = gm_l58;
                            uint64_t t487 = GM_LD(uint64_t, (((uint8_t *)&gm_l57) + 8));
                            uint8_t t488 = (t486 < t487);
                            if (!t488) {
                                goto gm_lb61;
                            }
                            {
                                void * t489 = GM_LD(void *, ((uint8_t *)&gm_l57));
                                uint64_t t490 = GM_LD(uint64_t, (((uint8_t *)&gm_l57) + 8));
                                uint64_t t491 = gm_l58;
                                {
                                    {
                                        void * t492 = GM_LD(void *, (((uint8_t *)t489) + (uintptr_t)t491 * 8));
                                        gm_l59 = t492;
                                        uint64_t t493 = gm_l2;
                                        void * t494 = gm_l0;
                                        void * t495 = gm_l59;
                                        gm_f_stack_expr_33aekgt((void *)&gm_l89, (void *)NULL, cx, NULL, t494, t495);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t496 = (uint8_t)!((((uint8_t *)&gm_l89))[0] != 0);
                                        if (t496) {
                                        } else {
                                            gm_tgeh6n0c7c8kb t497 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&gm_l89) + 8));
                                            gm_toabfckqu3onu t498;
                                            GM_ST(uint8_t, (uint8_t *)&t498 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t498 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t498 + 4, 0);
                                            ((uint8_t *)&t498)[0] = 1;
                                            GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t498 + 8), t497);
                                            memmove(gm_out, ((uint8_t *)&t498), 48);
                                            goto gm_fdone;
                                        }
                                        uint64_t t499 = GM_LD(uint64_t, (((uint8_t *)&gm_l89) + 8));
                                        uint64_t t500 = (t493 > t499 ? t493 : t499);
                                        gm_l2 = t500;
                                        goto gm_lb64;
                                    }
gm_lb65:;
                                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 261, 13);
                                }
gm_lb64:;
                            }
gm_lb63:;
                            uint64_t t501 = gm_l58;
                            uint64_t r502; bool o503 = __builtin_add_overflow(t501, ((uint64_t)1ULL), &r502);
                            uint64_t t504 = r502;
                            gm_l58 = t504;
                        }
                    }
gm_lb61:;
                    goto gm_lb1;
                }
gm_lb51:;
            }
            {
                {
                    void * t505 = gm_l1;
                    gm_b16_8 t506 = gm_f_stack_children_dedraea((void *)NULL, cx, NULL, t505);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l60 = t506;
                    gm_l61 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t507 = gm_l61;
                            uint64_t t508 = GM_LD(uint64_t, (((uint8_t *)&gm_l60) + 8));
                            uint8_t t509 = (t507 < t508);
                            if (!t509) {
                                goto gm_lb67;
                            }
                            {
                                void * t510 = GM_LD(void *, ((uint8_t *)&gm_l60));
                                uint64_t t511 = GM_LD(uint64_t, (((uint8_t *)&gm_l60) + 8));
                                uint64_t t512 = gm_l61;
                                {
                                    {
                                        void * t513 = GM_LD(void *, (((uint8_t *)t510) + (uintptr_t)t512 * 8));
                                        gm_l62 = t513;
                                        uint64_t t514 = gm_l2;
                                        void * t515 = gm_l0;
                                        void * t516 = gm_l62;
                                        gm_f_stack_expr_33aekgt((void *)&gm_l90, (void *)NULL, cx, NULL, t515, t516);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t517 = (uint8_t)!((((uint8_t *)&gm_l90))[0] != 0);
                                        if (t517) {
                                        } else {
                                            gm_tgeh6n0c7c8kb t518 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&gm_l90) + 8));
                                            gm_toabfckqu3onu t519;
                                            GM_ST(uint8_t, (uint8_t *)&t519 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t519 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t519 + 4, 0);
                                            ((uint8_t *)&t519)[0] = 1;
                                            GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t519 + 8), t518);
                                            memmove(gm_out, ((uint8_t *)&t519), 48);
                                            goto gm_fdone;
                                        }
                                        uint64_t t520 = GM_LD(uint64_t, (((uint8_t *)&gm_l90) + 8));
                                        uint64_t t521 = (t514 > t520 ? t514 : t520);
                                        gm_l2 = t521;
                                        goto gm_lb70;
                                    }
gm_lb71:;
                                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 264, 13);
                                }
gm_lb70:;
                            }
gm_lb69:;
                            uint64_t t522 = gm_l61;
                            uint64_t r523; bool o524 = __builtin_add_overflow(t522, ((uint64_t)1ULL), &r523);
                            uint64_t t525 = r523;
                            gm_l61 = t525;
                        }
                    }
gm_lb67:;
                    goto gm_lb1;
                }
gm_lb66:;
                gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 178, 5);
            }
        }
gm_lb1:;
    }
    {
        uint64_t t526 = gm_l2;
        gm_toabfckqu3onu t527;
        memset(&t527, 0, sizeof t527);
        ((uint8_t *)&t527)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t527 + 8), t526);
        memmove(gm_out, ((uint8_t *)&t527), 48);
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
inline __attribute__((aligned(4))) uint64_t gm_f_stack_frame_68512ca(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_stack_frame_68512ca F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* g */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tb31k5l794t7p gm_l4; /* c */
    uint64_t gm_l6; /* c */
    GM_STACK_CHECK(&F, 331u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l7;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 128));
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
                        gm_tb31k5l794t7p t15 = GM_LD(gm_tb31k5l794t7p, (((uint8_t *)t12) + (uintptr_t)t14 * 16));
                        gm_l4 = t15;
                        uint32_t t16 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                        uint32_t t17 = gm_l1;
                        uint8_t t18 = ((t16 == t17));
                        if (t18) {
                            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            F.ret = t19;
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 132, 5);
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
    void * t24 = gm_l0;
    void * t25 = GM_LD(void *, ((uint8_t *)t24));
    uint32_t t26 = gm_l1;
    gm_f_def_of_m512sbu((void *)&F.l5, t25, t26);
    void * t27 = gm_l0;
    void * t28 = GM_LD(void *, ((uint8_t *)t27));
    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 24));
    gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
    void * t31 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
    uint64_t t32 = gm_f_frame_cost_oei64tv((void *)NULL, cx, NULL, t28, t30, t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t32;
    void * t33 = gm_l0;
    void * t34 = (void *)(((uint8_t *)t33) + 128);
    uint32_t t35 = gm_l1;
    uint64_t t36 = gm_l6;
    gm_tb31k5l794t7p t37;
    GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t37 + 0), t35);
    GM_ST(uint64_t, ((uint8_t *)&t37 + 8), t36);
    gm_tftb1odc739t5 t38 = gm_f_push_1mcf4ec(t34, GM_CONV(gm_cskjv6t053tkje, t37));
    gm_l7 = t38;
    uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_src4tam33qo20j7, 137, 5);
    uint64_t t40 = gm_l6;
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
__attribute__((aligned(4))) void gm_f_stack_perform_soivemm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_stack_perform_soivemm F;
    void * gm_l0; /* a */
    void * gm_l1; /* eref */
    uint32_t gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* ops */
    gm_b16_8 gm_l6; /* unknown */
    uint64_t gm_l7; /* k */
    GM_STACK_CHECK(&F, 993u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l9;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 1048));
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t8) + 1048) + 8));
    void * t11 = gm_l1;
    uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)t11));
    uint64_t t13 = (uint64_t)t12;
    if ((uintptr_t)t13 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t13, t10, gm_src4tam33qo20j7, 289, 12);
    gm_b16_8 t14 = GM_LD(gm_b16_8, ((((uint8_t *)t9) + (uintptr_t)t13 * 56) + 40));
    gm_l5 = t14;
    {
        {
            uint32_t t15 = gm_l2;
            uint64_t t16 = (uint64_t)t15;
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t18 = (t16 < t17);
            if (!t18) {
                goto gm_lb2;
            }
            void * t19 = GM_LD(void *, ((uint8_t *)&gm_l5));
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint32_t t21 = gm_l2;
            uint64_t t22 = (uint64_t)t21;
            if ((uintptr_t)t22 >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)t22, t20, gm_src4tam33qo20j7, 291, 32);
            gm_t9qkh0rsooc8d t23 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t19) + (uintptr_t)t22 * 48) + 16));
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            uint8_t t25 = (uint8_t)((t24 == ((uint8_t)2ULL)));
            if (!t25) {
                goto gm_lb2;
            }
            gm_toabfckqu3onu t26;
            memset(&t26, 0, sizeof t26);
            ((uint8_t *)&t26)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t26 + 8), ((uint64_t)0ULL));
            gm_t9qkh0rsooc8d t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)2ULL));
            memmove(gm_out, ((uint8_t *)&t26), 48);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t28 = gm_l0;
    void * t29 = GM_LD(void *, ((uint8_t *)t28));
    void * t30 = (void *)(((uint8_t *)t29) + 8);
    void * t31 = gm_l1;
    gm_b16_8 t32 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t30, t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t33;
    GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
    gm_f_format_845n386((void *)&gm_l9, (void *)NULL, cx, NULL, t33);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src4tam33qo20j7, 292, 16);
    gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
    gm_l6 = t35;
    void * t36 = gm_l0;
    uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)t36) + 48) + 8));
    gm_l7 = t37;
    {
        for (;;) {
            uint64_t t38 = gm_l7;
            uint8_t t39 = (t38 > ((uint64_t)0ULL));
            if (!t39) {
                goto gm_lb3;
            }
            {
                uint64_t t40 = gm_l7;
                uint64_t r41; bool o42 = __builtin_sub_overflow(t40, ((uint64_t)1ULL), &r41);
                if (o42) gm_trap("integer overflow in `-`", gm_src4tam33qo20j7, 295, 9);
                uint64_t t43 = r41;
                gm_l7 = t43;
                void * t44 = gm_l0;
                void * t45 = GM_LD(void *, (((uint8_t *)t44) + 48));
                uint64_t t46 = GM_LD(uint64_t, ((((uint8_t *)t44) + 48) + 8));
                uint64_t t47 = gm_l7;
                if ((uintptr_t)t47 >= (uintptr_t)(t46)) gm_trap_index((uintptr_t)t47, t46, gm_src4tam33qo20j7, 296, 27);
                void * t48 = (void *)(((uint8_t *)t45) + (uintptr_t)t47 * 40);
                void * t49 = gm_l1;
                int32_t t50 = gm_f_cmp_ref_8ntrqll(t48, t49);
                uint8_t t51 = (!(t50 == ((int32_t)0LL)));
                if (t51) {
                    goto gm_lb5;
                }
                void * t52 = gm_l0;
                void * t53 = GM_LD(void *, (((uint8_t *)t52) + 48));
                uint64_t t54 = GM_LD(uint64_t, ((((uint8_t *)t52) + 48) + 8));
                uint64_t t55 = gm_l7;
                if ((uintptr_t)t55 >= (uintptr_t)(t54)) gm_trap_index((uintptr_t)t55, t54, gm_src4tam33qo20j7, 297, 14);
                memmove(((uint8_t *)&F.l8), (((uint8_t *)t53) + (uintptr_t)t55 * 40), 40);
                uint32_t t56 = gm_l2;
                uint64_t t57 = (uint64_t)t56;
                uint64_t t58 = GM_LD(uint64_t, ((((uint8_t *)&F.l8) + 24) + 8));
                uint8_t t59 = (t57 >= t58);
                if (t59) {
                    gm_tl874t2atnlh3 t60 = gm_l4;
                    gm_b16_8 t61 = gm_l6;
                    gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                    gm_tbgd14esf0jvi t63;
                    GM_ST(uint32_t, (uint8_t *)&t63 + 12, 0);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t63 + 0), t60);
                    GM_ST(gm_b16_8, ((uint8_t *)&t63 + 16), t62);
                    gm_tgeh6n0c7c8kb t64;
                    GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t64 + 8), t63);
                    gm_toabfckqu3onu t65;
                    GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
                    ((uint8_t *)&t65)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t65 + 8), t64);
                    memmove(gm_out, ((uint8_t *)&t65), 48);
                    goto gm_fdone;
                }
                void * t66 = gm_l0;
                void * t67 = GM_LD(void *, (((uint8_t *)&F.l8) + 24));
                uint64_t t68 = GM_LD(uint64_t, ((((uint8_t *)&F.l8) + 24) + 8));
                uint32_t t69 = gm_l2;
                uint64_t t70 = (uint64_t)t69;
                if ((uintptr_t)t70 >= (uintptr_t)(t68)) gm_trap_index((uintptr_t)t70, t68, gm_src4tam33qo20j7, 299, 30);
                uint32_t t71 = GM_LD(uint32_t, (((uint8_t *)t67) + (uintptr_t)t70 * 4));
                gm_b16_8 t72 = gm_l3;
                void * t73 = gm_l0;
                void * t74 = GM_LD(void *, (((uint8_t *)t73) + 48));
                uint64_t t75 = GM_LD(uint64_t, ((((uint8_t *)t73) + 48) + 8));
                uint64_t t76 = gm_l7;
                uint64_t t77 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t78 = (uintptr_t)t76;
                if (t77 > t78 || t78 > (uintptr_t)t75) gm_trap_slice(t77, t78, t75, gm_src4tam33qo20j7, 299, 58);
                uint64_t t79 = ((uintptr_t)t76) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t80;
                GM_ST(void *, (uint8_t *)&t80, (void *)(((uint8_t *)t74 + (uintptr_t)((uint64_t)0ULL) * 40)));
                GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(t79));
                gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
                gm_tl874t2atnlh3 t82 = gm_l4;
                gm_f_stack_call_poqmlno((void *)&(*(gm_toabfckqu3onu *)gm_out), (void *)NULL, cx, NULL, t66, t71, t72, t81, t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
gm_lb5:;
        }
    }
gm_lb3:;
    gm_tl874t2atnlh3 t83 = gm_l4;
    gm_b16_8 t84 = gm_l6;
    gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
    gm_tbgd14esf0jvi t86;
    GM_ST(uint32_t, (uint8_t *)&t86 + 12, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t86 + 0), t83);
    GM_ST(gm_b16_8, ((uint8_t *)&t86 + 16), t85);
    gm_tgeh6n0c7c8kb t87;
    GM_ST(uint8_t, (uint8_t *)&t87 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t87 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t87 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)1ULL));
    GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t87 + 8), t86);
    gm_toabfckqu3onu t88;
    GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
    ((uint8_t *)&t88)[0] = 1;
    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t88 + 8), t87);
    memmove(gm_out, ((uint8_t *)&t88), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_stack_push_expr_bdl56rb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2, void * a3) {
    struct gm_fr_stack_push_expr_bdl56rb F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* g */
    void * gm_l3; /* body */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l5;
    uint8_t gm_l6;
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 8);
    uint32_t t7 = gm_l1;
    gm_trqfvmjtt4sij t8 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
    gm_tha5675rv512q t9;
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t9 + 0), t7);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t9 + 8), t8);
    gm_tftb1odc739t5 t10 = gm_f_push_9gqhmau(t6, &t9);
    gm_l5 = t10;
    uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_src4tam33qo20j7, 124, 5);
    void * t12 = gm_l0;
    void * t13 = gm_l3;
    gm_f_stack_expr_33aekgt((void *)&F.l4, (void *)NULL, cx, NULL, t12, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t14 = (uint8_t)!((((uint8_t *)&F.l4))[0] != 0);
            if (t14) {
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
        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 126, 8);
    }
gm_lb1:;
    uint8_t t15 = gm_l6;
    if (t15) {
        void * t16 = gm_l0;
        void * t17 = (void *)(((uint8_t *)t16) + 8);
        gm_tijr008e33o2m t18;
        gm_f_pop_d7svgs5((void *)&t18, t17);
    }
    memmove(gm_out, ((uint8_t *)&F.l4), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_stack_size_0gijrqq(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_stack_size_0gijrqq F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_tl874t2atnlh3 gm_l5; /* at */
    gm_b16_8 gm_l6; /* why */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l11; /* name */
    uint64_t gm_l13; /* frames */
    GM_STACK_CHECK(&F, 1546u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    void * t11 = gm_l0;
    gm_trqfvmjtt4sij t12;
    gm_f_list_pe70gj7((void *)&t12, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t13;
    gm_f_list_o00nc4s((void *)&t13, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t14;
    gm_f_list_msjtuv4((void *)&t14, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t15;
    gm_f_list_msjtuv4((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tb8ondtf8obk4 t16;
    GM_ST(void *, ((uint8_t *)&t16 + 0), t11);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t16 + 8), t12);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t16 + 48), t13);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t16 + 88), t14);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t16 + 128), t15);
    F.l3 = t16;
    void * t17 = (void *)((uint8_t *)&F.l3);
    uint32_t t18 = gm_l1;
    gm_b16_8 t19 = gm_f_empty_slice_g3tddrb_2();
    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
    gm_b16_8 t21 = gm_f_empty_slice_vndn3a4();
    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
    gm_tl874t2atnlh3 t23 = gm_l2;
    gm_f_stack_call_poqmlno((void *)&F.l4, (void *)NULL, cx, NULL, t17, t18, t20, t22, t23);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t24 = (uint8_t)!((((uint8_t *)&F.l4))[0] != 0);
            if (t24) {
                goto gm_lb2;
            }
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)0ULL));
            if (!t26) {
                goto gm_lb2;
            }
            gm_tf5srh4mp4b3i t27;
            memset(&t27, 0, sizeof t27);
            F.ret = t27;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t28 = (uint8_t)!((((uint8_t *)&F.l4))[0] != 0);
            if (t28) {
                goto gm_lb3;
            }
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)1ULL));
            if (!t30) {
                goto gm_lb3;
            }
            gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l4) + 8) + 8));
            gm_l5 = t31;
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l4) + 8) + 8) + 16));
            gm_l6 = t32;
            gm_trqfvmjtt4sij t33;
            gm_f_list_epvducg((void *)&t33, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l7 = t33;
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
            gm_l8 = t34;
            gm_l9 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t35 = gm_l9;
                    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint8_t t37 = (t35 < t36);
                    if (!t37) {
                        goto gm_lb4;
                    }
                    {
                        void * t38 = GM_LD(void *, ((uint8_t *)&gm_l8));
                        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint64_t t40 = gm_l9;
                        {
                            {
                                memmove(((uint8_t *)&F.l10), (((uint8_t *)t38) + (uintptr_t)t40 * 48), 48);
                                void * t41 = (void *)((uint8_t *)&F.l7);
                                void * t42 = gm_l0;
                                void * t43 = GM_LD(void *, (((uint8_t *)t42) + 344));
                                uint64_t t44 = GM_LD(uint64_t, ((((uint8_t *)t42) + 344) + 8));
                                uint32_t t45 = GM_LD(uint32_t, ((uint8_t *)&F.l10));
                                uint64_t t46 = (uint64_t)t45;
                                if ((uintptr_t)t46 >= (uintptr_t)(t44)) gm_trap_index((uintptr_t)t46, t44, gm_src4tam33qo20j7, 71, 68);
                                gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)t43) + (uintptr_t)t46 * 136));
                                gm_tnpifv20irnph t48;
                                GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
                                gm_f_format_h4jcodi((void *)&gm_l14, (void *)NULL, cx, NULL, t48);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                                if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_src4tam33qo20j7, 71, 49);
                                gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                                gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                                gm_tftb1odc739t5 t52 = gm_f_push_3ef746b(t41, t51);
                                gm_l15 = t52;
                                uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                if (!t53) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_src4tam33qo20j7, 71, 38);
                                goto gm_lb7;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 71, 13);
                        }
gm_lb7:;
                    }
gm_lb6:;
                    uint64_t t54 = gm_l9;
                    uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                    uint64_t t57 = r55;
                    gm_l9 = t57;
                }
            }
gm_lb4:;
            void * t58 = gm_l0;
            void * t59 = GM_LD(void *, (((uint8_t *)t58) + 344));
            uint64_t t60 = GM_LD(uint64_t, ((((uint8_t *)t58) + 344) + 8));
            uint32_t t61 = gm_l1;
            uint64_t t62 = (uint64_t)t61;
            if ((uintptr_t)t62 >= (uintptr_t)(t60)) gm_trap_index((uintptr_t)t62, t60, gm_src4tam33qo20j7, 72, 21);
            gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)t59) + (uintptr_t)t62 * 136));
            gm_l11 = t63;
            gm_tl874t2atnlh3 t64 = gm_l5;
            gm_b16_8 t65 = gm_l11;
            gm_b16_8 t66 = gm_l6;
            gm_t6j6smj7qoctu t67;
            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t65);
            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 16), t66);
            gm_f_format_t68frbc((void *)&gm_l16, (void *)NULL, cx, NULL, &t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
            if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_src4tam33qo20j7, 73, 33);
            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
            gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
            gm_t4gut2c9fj1kt t71;
            gm_f_error_s6hn871((void *)&t71, (void *)NULL, cx, NULL, t64, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l12 = t71;
            void * t72 = (void *)((uint8_t *)&F.l12);
            gm_tl874t2atnlh3 t73 = gm_l2;
            gm_b16_8 t74;
            GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sdscrog2npict));
            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(22));
            gm_f_add_label_50fj7vv(t72, GM_CONV(gm_cseahvg8v7p0vs, t73), t74);
            uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
            uint8_t t76 = (t75 > ((uint64_t)1ULL));
            if (t76) {
                void * t77 = (void *)((uint8_t *)&F.l12);
                gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                gm_b16_8 t80;
                GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sdsrtcf8ng48d));
                GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(5));
                gm_f_join_sfknljb((void *)&gm_l17, (void *)NULL, cx, NULL, t79, t80);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t81 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                if (!t81) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_src4tam33qo20j7, 75, 82);
                gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                gm_tnpifv20irnph t83;
                GM_ST(gm_b16_8, ((uint8_t *)&t83 + 0), t82);
                gm_f_format_hp1rthv((void *)&gm_l18, (void *)NULL, cx, NULL, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t84 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                if (!t84) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src4tam33qo20j7, 75, 49);
                gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
                gm_f_add_note_46v502s(t77, t86);
            }
            void * t87 = gm_l0;
            gm_b0_1 t88 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t87, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l12)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t89;
            memset(&t89, 0, sizeof t89);
            F.ret = t89;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t90 = (uint8_t)!((((uint8_t *)&F.l4))[0] != 0);
            if (!t90) {
                goto gm_lb9;
            }
            uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
            gm_l13 = t91;
            uint64_t t92 = GM_LD(uint64_t, ((uint8_t *)gm_cb7r52n9k7f));
            uint64_t t93 = GM_LD(uint64_t, ((uint8_t *)gm_cilec57f4ei));
            uint64_t r94; bool o95 = __builtin_add_overflow(t92, t93, &r94);
            if (o95) gm_trap("integer overflow in `+`", gm_src4tam33qo20j7, 79, 28);
            uint64_t t96 = r94;
            uint64_t t97 = gm_l13;
            uint64_t r98; bool o99 = __builtin_add_overflow(t96, t97, &r98);
            uint64_t t100 = o99 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r98;
            uint64_t t101 = GM_LD(uint64_t, ((uint8_t *)gm_cdlh2b67kra));
            uint64_t t102 = (t100 > t101 ? t100 : t101);
            gm_tf5srh4mp4b3i t103;
            GM_ST(uint8_t, (uint8_t *)&t103 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t103 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t103 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t103), t102);
            ((uint8_t *)&t103)[8] = 1;
            F.ret = t103;
            goto gm_fret;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 67, 5);
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_stack_target_7vr78v6(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stack_target_7vr78v6 F;
    void * gm_l0; /* a */
    void * gm_l1; /* e */
    uint32_t gm_l2; /* f */
    uint32_t gm_l3; /* f */
    void * gm_l4; /* x */
    uint32_t gm_l5; /* l */
    uint32_t gm_l7; /* levels */
    uint32_t gm_l8; /* local */
    uint32_t gm_l10; /* owner */
    uint32_t gm_l11; /* <tmp> */
    uint32_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* k */
    GM_STACK_CHECK(&F, 1202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tijr008e33o2m gm_l14;
    gm_tijr008e33o2m gm_l15;
    gm_tha5675rv512q gm_l16;
    gm_tijr008e33o2m gm_l17;
    gm_tijr008e33o2m gm_l18;
    gm_tha5675rv512q gm_l19;
    gm_tdgfduqjheqdm gm_l20;
    gm_t9805qtonbsag gm_l21;
    uint32_t gm_l22;
    void * t19 = gm_l1;
    {
        {
            {
                {
                    uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)26ULL));
                    if (!t21) {
                        goto gm_lb4;
                    }
                    uint32_t t22 = GM_LD(uint32_t, ((((uint8_t *)t19) + 8) + 8));
                    gm_l2 = t22;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)27ULL));
                    if (!t24) {
                        goto gm_lb5;
                    }
                    uint32_t t25 = GM_LD(uint32_t, ((((uint8_t *)t19) + 8) + 8));
                    gm_l2 = t25;
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            uint32_t t26 = gm_l2;
            gm_t9805qtonbsag t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t27), t26);
            ((uint8_t *)&t27)[4] = 1;
            F.ret = t27;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)32ULL));
            if (!t29) {
                goto gm_lb6;
            }
            void * t30 = GM_LD(void *, (((((uint8_t *)t19) + 8) + 8) + 8));
            gm_l4 = t30;
            void * t31 = gm_l0;
            void * t32 = gm_l4;
            gm_t9805qtonbsag t33 = gm_f_stack_target_7vr78v6(t31, t32);
            F.ret = t33;
            goto gm_fret;
        }
gm_lb6:;
        {
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)15ULL));
            if (!t35) {
                goto gm_lb7;
            }
            uint32_t t36 = GM_LD(uint32_t, ((((uint8_t *)t19) + 8) + 8));
            gm_l5 = t36;
            void * t37 = gm_l0;
            void * t38 = GM_LD(void *, (((uint8_t *)t37) + 8));
            uint64_t t39 = GM_LD(uint64_t, ((((uint8_t *)t37) + 8) + 8));
            uint64_t r40; bool o41 = __builtin_sub_overflow(t39, ((uint64_t)1ULL), &r40);
            uint64_t t42 = r40;
            uint8_t t43 = (t42 < t39);
            if (t43) {
                gm_tha5675rv512q t44 = GM_LD(gm_tha5675rv512q, (((uint8_t *)t38) + (uintptr_t)t42 * 48));
                gm_tijr008e33o2m t45;
                GM_ST(uint8_t, (uint8_t *)&t45 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&t45 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&t45 + 52, 0);
                GM_ST(gm_tha5675rv512q, ((uint8_t *)&t45), t44);
                ((uint8_t *)&t45)[48] = 1;
                gm_l14 = t45;
            } else {
                gm_tijr008e33o2m t46;
                memset(&t46, 0, sizeof t46);
                gm_l14 = t46;
            }
            memmove(((uint8_t *)&gm_l15), ((uint8_t *)&gm_l14), 56);
            uint8_t t47 = (uint8_t)((((uint8_t *)&gm_l15))[48] != 0);
            if (t47) {
                memmove(((uint8_t *)&gm_l16), ((uint8_t *)&gm_l15), 48);
            } else {
                gm_t9805qtonbsag t48;
                memset(&t48, 0, sizeof t48);
                F.ret = t48;
                goto gm_fret;
            }
            memmove(((uint8_t *)&F.l6), ((uint8_t *)&gm_l16), 48);
            void * t49 = (void *)(((uint8_t *)&F.l6) + 8);
            uint32_t t50 = gm_l5;
            gm_t9805qtonbsag t51 = gm_f_bound_of_sn02ojm(t49, t50);
            F.ret = t51;
            goto gm_fret;
        }
gm_lb7:;
        {
            uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)16ULL));
            if (!t53) {
                goto gm_lb8;
            }
            uint32_t t54 = GM_LD(uint32_t, ((((uint8_t *)t19) + 8) + 8));
            gm_l7 = t54;
            uint32_t t55 = GM_LD(uint32_t, (((((uint8_t *)t19) + 8) + 8) + 4));
            gm_l8 = t55;
            void * t56 = gm_l0;
            void * t57 = GM_LD(void *, (((uint8_t *)t56) + 8));
            uint64_t t58 = GM_LD(uint64_t, ((((uint8_t *)t56) + 8) + 8));
            uint64_t r59; bool o60 = __builtin_sub_overflow(t58, ((uint64_t)1ULL), &r59);
            uint64_t t61 = r59;
            uint8_t t62 = (t61 < t58);
            if (t62) {
                gm_tha5675rv512q t63 = GM_LD(gm_tha5675rv512q, (((uint8_t *)t57) + (uintptr_t)t61 * 48));
                gm_tijr008e33o2m t64;
                GM_ST(uint8_t, (uint8_t *)&t64 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&t64 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&t64 + 52, 0);
                GM_ST(gm_tha5675rv512q, ((uint8_t *)&t64), t63);
                ((uint8_t *)&t64)[48] = 1;
                gm_l17 = t64;
            } else {
                gm_tijr008e33o2m t65;
                memset(&t65, 0, sizeof t65);
                gm_l17 = t65;
            }
            memmove(((uint8_t *)&gm_l18), ((uint8_t *)&gm_l17), 56);
            uint8_t t66 = (uint8_t)((((uint8_t *)&gm_l18))[48] != 0);
            if (t66) {
                memmove(((uint8_t *)&gm_l19), ((uint8_t *)&gm_l18), 48);
            } else {
                gm_t9805qtonbsag t67;
                memset(&t67, 0, sizeof t67);
                F.ret = t67;
                goto gm_fret;
            }
            memmove(((uint8_t *)&F.l9), ((uint8_t *)&gm_l19), 48);
            uint32_t t68 = GM_LD(uint32_t, ((uint8_t *)&F.l9));
            gm_l10 = t68;
            gm_l12 = ((uint32_t)0ULL);
            uint32_t t69 = gm_l7;
            gm_l11 = t69;
            {
                for (;;) {
                    uint32_t t70 = gm_l12;
                    uint32_t t71 = gm_l11;
                    uint8_t t72 = (t70 < t71);
                    if (!t72) {
                        goto gm_lb9;
                    }
                    {
                        {
                            {
                                void * t73 = gm_l0;
                                void * t74 = GM_LD(void *, ((uint8_t *)t73));
                                uint32_t t75 = gm_l10;
                                gm_f_def_of_m512sbu((void *)&gm_l20, t74, t75);
                                gm_t9805qtonbsag t76 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l20) + 48));
                                gm_l21 = t76;
                                uint8_t t77 = (uint8_t)((((uint8_t *)&gm_l21))[4] != 0);
                                if (t77) {
                                    uint32_t t78 = GM_LD(uint32_t, ((uint8_t *)&gm_l21));
                                    gm_l22 = t78;
                                } else {
                                    gm_t9805qtonbsag t79;
                                    memset(&t79, 0, sizeof t79);
                                    F.ret = t79;
                                    goto gm_fret;
                                }
                                uint32_t t80 = gm_l22;
                                gm_l10 = t80;
                                goto gm_lb12;
                            }
gm_lb13:;
                            gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 153, 13);
                        }
gm_lb12:;
                    }
gm_lb11:;
                    uint32_t t81 = gm_l12;
                    uint32_t r82; bool o83 = __builtin_add_overflow(t81, ((uint32_t)1ULL), &r82);
                    uint32_t t84 = r82;
                    gm_l12 = t84;
                }
            }
gm_lb9:;
            void * t85 = gm_l0;
            uint64_t t86 = GM_LD(uint64_t, ((((uint8_t *)t85) + 8) + 8));
            gm_l13 = t86;
            {
                for (;;) {
                    uint64_t t87 = gm_l13;
                    uint8_t t88 = (t87 > ((uint64_t)0ULL));
                    if (!t88) {
                        goto gm_lb14;
                    }
                    {
                        uint64_t t89 = gm_l13;
                        uint64_t r90; bool o91 = __builtin_sub_overflow(t89, ((uint64_t)1ULL), &r90);
                        if (o91) gm_trap("integer overflow in `-`", gm_src4tam33qo20j7, 158, 17);
                        uint64_t t92 = r90;
                        gm_l13 = t92;
                        void * t93 = gm_l0;
                        void * t94 = GM_LD(void *, (((uint8_t *)t93) + 8));
                        uint64_t t95 = GM_LD(uint64_t, ((((uint8_t *)t93) + 8) + 8));
                        uint64_t t96 = gm_l13;
                        if ((uintptr_t)t96 >= (uintptr_t)(t95)) gm_trap_index((uintptr_t)t96, t95, gm_src4tam33qo20j7, 159, 20);
                        uint32_t t97 = GM_LD(uint32_t, (((uint8_t *)t94) + (uintptr_t)t96 * 48));
                        uint32_t t98 = gm_l10;
                        uint8_t t99 = ((t97 == t98));
                        if (t99) {
                            void * t100 = gm_l0;
                            void * t101 = GM_LD(void *, (((uint8_t *)t100) + 8));
                            uint64_t t102 = GM_LD(uint64_t, ((((uint8_t *)t100) + 8) + 8));
                            uint64_t t103 = gm_l13;
                            if ((uintptr_t)t103 >= (uintptr_t)(t102)) gm_trap_index((uintptr_t)t103, t102, gm_src4tam33qo20j7, 159, 66);
                            void * t104 = (void *)((((uint8_t *)t101) + (uintptr_t)t103 * 48) + 8);
                            uint32_t t105 = gm_l8;
                            gm_t9805qtonbsag t106 = gm_f_bound_of_sn02ojm(t104, t105);
                            F.ret = t106;
                            goto gm_fret;
                        }
                    }
gm_lb16:;
                }
            }
gm_lb14:;
            gm_t9805qtonbsag t107;
            memset(&t107, 0, sizeof t107);
            F.ret = t107;
            goto gm_fret;
        }
gm_lb8:;
        {
            gm_t9805qtonbsag t108;
            memset(&t108, 0, sizeof t108);
            F.ret = t108;
            goto gm_fret;
        }
gm_lb17:;
        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 143, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
