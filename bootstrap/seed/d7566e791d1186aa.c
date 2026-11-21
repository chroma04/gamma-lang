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
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint32_t f0; uint32_t f1; uint8_t f2; uint8_t pe[3]; } gm_tilqfqdpj446h;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_t9805qtonbsag f0; void * f1; } gm_t12ib7gg43lh0;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_b16_8 f1; } gm_thervm8j23qet;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tilqfqdpj446h) == 12 && _Alignof(gm_tilqfqdpj446h) == 4 && offsetof(gm_tilqfqdpj446h, f0) == 0 && offsetof(gm_tilqfqdpj446h, f1) == 4 && offsetof(gm_tilqfqdpj446h, f2) == 8, "layout of `(dst: u32, f: u32, second: bool)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[12]; unsigned __int128 f1; } gm_tl5jbgvjjt15g;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; } gm_tpfol4ocdvoij;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_trt0mvq732stt;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint32_t f0; uint32_t f1; uint64_t f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tuipckodmso90;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t12ib7gg43lh0) == 16 && _Alignof(gm_t12ib7gg43lh0) == 8 && offsetof(gm_t12ib7gg43lh0, f0) == 0 && offsetof(gm_t12ib7gg43lh0, f1) == 8, "layout of `(dst: ?u32, expr: ^Expr)`");
typedef struct { void * f0; void * f1; gm_b16_8 f2; gm_trqfvmjtt4sij f3; gm_b16_8 f4; gm_b16_8 f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; uint32_t f8; uint32_t f9; } gm_t3k59klpjql8o;
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_tfbnciphf3261 v1; gm_tl5jbgvjjt15g v2; gm_t88rcucg1b7uk v3; gm_t88rcucg1b7uk v4; } f1; } gm_t7b7esi3n9r90;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_te9hrk0u35rev v0; gm_te9hrk0u35rev v1; uint32_t v2; } f1; } gm_t7pnmbsgd9mnq;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, bits: u64)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_t9805qtonbsag f0; gm_tvdg94bk7aa22 f1; uint8_t p2[4]; gm_b16_8 f2; } gm_th622ku78unet;
_Static_assert(sizeof(gm_thervm8j23qet) == 24 && _Alignof(gm_thervm8j23qet) == 8 && offsetof(gm_thervm8j23qet, f0) == 0 && offsetof(gm_thervm8j23qet, f1) == 8, "layout of `Slot(string, ())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tl5jbgvjjt15g) == 32 && _Alignof(gm_tl5jbgvjjt15g) == 16 && offsetof(gm_tl5jbgvjjt15g, f0) == 0 && offsetof(gm_tl5jbgvjjt15g, f1) == 16, "layout of `(ty: Ty, bits: u128)`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_tuipckodmso90 v1; } f1; } gm_tm7ecnors0fq6;
_Static_assert(sizeof(gm_tpfol4ocdvoij) == 32 && _Alignof(gm_tpfol4ocdvoij) == 8 && offsetof(gm_tpfol4ocdvoij, f0) == 0 && offsetof(gm_tpfol4ocdvoij, f1) == 16 && offsetof(gm_tpfol4ocdvoij, f2) == 28, "layout of `(name: string, span: Span, ty: Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_trt0mvq732stt) == 32 && _Alignof(gm_trt0mvq732stt) == 8 && offsetof(gm_trt0mvq732stt, f0) == 0 && offsetof(gm_trt0mvq732stt, f1) == 8 && offsetof(gm_trt0mvq732stt, f2) == 24, "layout of `(dst: u32, bytes: string, c: bool)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tcc1rq4n1avb2 v1; uint64_t v2; gm_tpfol4ocdvoij v3; uint32_t v4; } f1; } gm_tu78tssgij60g;
_Static_assert(sizeof(gm_tuipckodmso90) == 24 && _Alignof(gm_tuipckodmso90) == 8 && offsetof(gm_tuipckodmso90, f0) == 0 && offsetof(gm_tuipckodmso90, f1) == 4 && offsetof(gm_tuipckodmso90, f2) == 8 && offsetof(gm_tuipckodmso90, f3) == 16, "layout of `(local: u32, gen: u32, off: u64, ty: Ty)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
typedef struct { uint32_t f0; gm_ts6oqppjh0ba3 f1; uint8_t p2[10]; gm_t7b7esi3n9r90 f2; gm_t7b7esi3n9r90 f3; } gm_t1ctgp8j1v2as;
typedef struct { gm_t7b7esi3n9r90 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t1s3s9ds8udn4;
_Static_assert(sizeof(gm_t3k59klpjql8o) == 192 && _Alignof(gm_t3k59klpjql8o) == 8 && offsetof(gm_t3k59klpjql8o, f0) == 0 && offsetof(gm_t3k59klpjql8o, f1) == 8 && offsetof(gm_t3k59klpjql8o, f2) == 16 && offsetof(gm_t3k59klpjql8o, f3) == 32 && offsetof(gm_t3k59klpjql8o, f4) == 72 && offsetof(gm_t3k59klpjql8o, f5) == 88 && offsetof(gm_t3k59klpjql8o, f6) == 104 && offsetof(gm_t3k59klpjql8o, f7) == 144 && offsetof(gm_t3k59klpjql8o, f8) == 184 && offsetof(gm_t3k59klpjql8o, f9) == 188, "layout of `Elider`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
typedef struct { gm_t7b7esi3n9r90 f0; uint64_t f1; uint8_t pe[8]; } gm_t592joge1j4uv;
typedef struct { uint32_t f0; gm_tmn7n0n9raou5 f1; uint8_t p2[4]; gm_t7b7esi3n9r90 f2; } gm_t5hdm76ui2uro;
_Static_assert(sizeof(gm_t7b7esi3n9r90) == 48 && _Alignof(gm_t7b7esi3n9r90) == 16 && offsetof(gm_t7b7esi3n9r90, f0) == 0 && offsetof(gm_t7b7esi3n9r90, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_t7pnmbsgd9mnq) == 16 && _Alignof(gm_t7pnmbsgd9mnq) == 4 && offsetof(gm_t7pnmbsgd9mnq, f0) == 0 && offsetof(gm_t7pnmbsgd9mnq, f1) == 4, "layout of `Exit`");
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; uint32_t f2; uint8_t p3[12]; gm_t7b7esi3n9r90 f3; } gm_t8rrrcunvsibs;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t419mo45g7j0a v0; gm_t7b7esi3n9r90 v1; } f1; } gm_t9i04n6j2d1gi;
typedef struct { uint32_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[11]; gm_t7b7esi3n9r90 f2; } gm_tbto4k5jg9fve;
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; uint32_t f2; uint8_t pe[12]; } gm_tcpisgbff1dqv;
typedef struct { gm_t7b7esi3n9r90 f0; uint8_t f1; uint8_t p2[3]; uint32_t f2; uint8_t pe[8]; } gm_tejc6vv5sqno9;
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; } gm_tf4lk4crue8vt;
typedef struct { gm_t7b7esi3n9r90 f0; uint8_t f1; uint8_t pe[15]; } gm_tf8kfhcnval78;
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; gm_t7b7esi3n9r90 f2; gm_t7b7esi3n9r90 f3; uint8_t f4; uint8_t p5[7]; uint64_t f5; } gm_tgroihbjsmdvm;
_Static_assert(sizeof(gm_th622ku78unet) == 40 && _Alignof(gm_th622ku78unet) == 8 && offsetof(gm_th622ku78unet, f0) == 0 && offsetof(gm_th622ku78unet, f1) == 8 && offsetof(gm_th622ku78unet, f2) == 24, "layout of `(dst: ?u32, op: Builtin, args: []Value)`");
typedef struct { gm_tu78tssgij60g f0; gm_b16_8 f1; } gm_tlg23kb3oj23s;
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_t7b7esi3n9r90 v1; gm_t419mo45g7j0a v2; } f1; } gm_tllthq21h2f0k;
_Static_assert(sizeof(gm_tm7ecnors0fq6) == 32 && _Alignof(gm_tm7ecnors0fq6) == 8 && offsetof(gm_tm7ecnors0fq6, f0) == 0 && offsetof(gm_tm7ecnors0fq6, f1) == 8, "layout of `Sym`");
typedef struct { gm_b16_8 f0; gm_t7b7esi3n9r90 f1; } gm_tmvmieab1acso;
typedef struct { uint32_t f0; uint8_t f1; uint8_t p2[11]; gm_t7b7esi3n9r90 f2; gm_t7b7esi3n9r90 f3; } gm_tr6ll1dg9imrl;
typedef struct { gm_t7b7esi3n9r90 f0; gm_t7b7esi3n9r90 f1; } gm_tthvfhsvm2g0s;
_Static_assert(sizeof(gm_tu78tssgij60g) == 40 && _Alignof(gm_tu78tssgij60g) == 8 && offsetof(gm_tu78tssgij60g, f0) == 0 && offsetof(gm_tu78tssgij60g, f1) == 8, "layout of `Base`");
typedef struct { gm_tm7ecnors0fq6 f0; gm_tm7ecnors0fq6 f1; uint8_t f2; uint8_t pe[7]; } gm_tv1gaqputuep6;
typedef struct { gm_t9805qtonbsag f0; uint8_t p1[8]; gm_tllthq21h2f0k f1; gm_b16_8 f2; } gm_t116nudii5c54;
_Static_assert(sizeof(gm_t1ctgp8j1v2as) == 112 && _Alignof(gm_t1ctgp8j1v2as) == 16 && offsetof(gm_t1ctgp8j1v2as, f0) == 0 && offsetof(gm_t1ctgp8j1v2as, f1) == 4 && offsetof(gm_t1ctgp8j1v2as, f2) == 16 && offsetof(gm_t1ctgp8j1v2as, f3) == 64, "layout of `(dst: u32, op: BinOp, a: Value, b: Value)`");
_Static_assert(sizeof(gm_t1s3s9ds8udn4) == 80 && _Alignof(gm_t1s3s9ds8udn4) == 16 && offsetof(gm_t1s3s9ds8udn4, f0) == 0 && offsetof(gm_t1s3s9ds8udn4, f1) == 48 && offsetof(gm_t1s3s9ds8udn4, f2) == 64, "layout of `(cond: Value, then: []Inst, else_: []Inst)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tv1gaqputuep6 f1; } gm_t2q86r30n2ngm;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tlg23kb3oj23s f1; uint8_t f2; uint8_t pe[7]; } gm_t3349g713hmcr;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tlg23kb3oj23s f1; uint32_t f2; uint8_t pe[4]; } gm_t4j77n1ssj077;
_Static_assert(sizeof(gm_t592joge1j4uv) == 64 && _Alignof(gm_t592joge1j4uv) == 16 && offsetof(gm_t592joge1j4uv, f0) == 0 && offsetof(gm_t592joge1j4uv, f1) == 48, "layout of `(index: Value, size: u64)`");
_Static_assert(sizeof(gm_t5hdm76ui2uro) == 64 && _Alignof(gm_t5hdm76ui2uro) == 16 && offsetof(gm_t5hdm76ui2uro, f0) == 0 && offsetof(gm_t5hdm76ui2uro, f1) == 4 && offsetof(gm_t5hdm76ui2uro, f2) == 16, "layout of `(dst: u32, kind: Coercion, v: Value)`");
typedef struct { gm_tlg23kb3oj23s f0; uint8_t p1[8]; gm_t7b7esi3n9r90 f1; } gm_t6fp3q71vjrmk;
typedef struct { gm_t7b7esi3n9r90 f0; gm_tlg23kb3oj23s f1; uint8_t f2; uint8_t pe[7]; } gm_t6j9ipvj0d6k5;
typedef struct { gm_t7pnmbsgd9mnq f0; gm_tf8kfhcnval78 f1; } gm_t7dcjsiqi95h3;
_Static_assert(sizeof(gm_t8rrrcunvsibs) == 128 && _Alignof(gm_t8rrrcunvsibs) == 16 && offsetof(gm_t8rrrcunvsibs, f0) == 0 && offsetof(gm_t8rrrcunvsibs, f1) == 16 && offsetof(gm_t8rrrcunvsibs, f2) == 64 && offsetof(gm_t8rrrcunvsibs, f3) == 80, "layout of `(dst: u32, whole: Value, index: u32, v: Value)`");
_Static_assert(sizeof(gm_t9i04n6j2d1gi) == 64 && _Alignof(gm_t9i04n6j2d1gi) == 16 && offsetof(gm_t9i04n6j2d1gi, f0) == 0 && offsetof(gm_t9i04n6j2d1gi, f1) == 16, "layout of `Sink`");
_Static_assert(sizeof(gm_tbto4k5jg9fve) == 64 && _Alignof(gm_tbto4k5jg9fve) == 16 && offsetof(gm_tbto4k5jg9fve, f0) == 0 && offsetof(gm_tbto4k5jg9fve, f1) == 4 && offsetof(gm_tbto4k5jg9fve, f2) == 16, "layout of `(dst: u32, op: UnOp, a: Value)`");
_Static_assert(sizeof(gm_tcpisgbff1dqv) == 80 && _Alignof(gm_tcpisgbff1dqv) == 16 && offsetof(gm_tcpisgbff1dqv, f0) == 0 && offsetof(gm_tcpisgbff1dqv, f1) == 16 && offsetof(gm_tcpisgbff1dqv, f2) == 64, "layout of `(dst: u32, v: Value, index: u32)`");
_Static_assert(sizeof(gm_tejc6vv5sqno9) == 64 && _Alignof(gm_tejc6vv5sqno9) == 16 && offsetof(gm_tejc6vv5sqno9, f0) == 0 && offsetof(gm_tejc6vv5sqno9, f1) == 48 && offsetof(gm_tejc6vv5sqno9, f2) == 52, "layout of `(cond: Value, negated: bool, label: u32)`");
_Static_assert(sizeof(gm_tf4lk4crue8vt) == 64 && _Alignof(gm_tf4lk4crue8vt) == 16 && offsetof(gm_tf4lk4crue8vt, f0) == 0 && offsetof(gm_tf4lk4crue8vt, f1) == 16, "layout of `(dst: u32, v: Value)`");
_Static_assert(sizeof(gm_tf8kfhcnval78) == 64 && _Alignof(gm_tf8kfhcnval78) == 16 && offsetof(gm_tf8kfhcnval78, f0) == 0 && offsetof(gm_tf8kfhcnval78, f1) == 48, "layout of `?Value`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_tb31k5l794t7p v0; gm_t592joge1j4uv v1; gm_t592joge1j4uv v2; gm_tlcm4lvu3g1a8 v3; } f1; } gm_tfqmtuprvunod;
typedef struct { uint32_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[11]; gm_tf8kfhcnval78 f2; } gm_tg85uffmq8b38;
_Static_assert(sizeof(gm_tgroihbjsmdvm) == 176 && _Alignof(gm_tgroihbjsmdvm) == 16 && offsetof(gm_tgroihbjsmdvm, f0) == 0 && offsetof(gm_tgroihbjsmdvm, f1) == 16 && offsetof(gm_tgroihbjsmdvm, f2) == 64 && offsetof(gm_tgroihbjsmdvm, f3) == 112 && offsetof(gm_tgroihbjsmdvm, f4) == 160 && offsetof(gm_tgroihbjsmdvm, f5) == 168, "layout of `(dst: u32, ptr: Value, lo: Value, hi: Value, inclusive: bool, size: u64)`");
typedef struct { gm_t7b7esi3n9r90 f0; gm_t7b7esi3n9r90 f1; gm_tf8kfhcnval78 f2; uint8_t f3; uint8_t pe[15]; } gm_thvv7nb91atip;
typedef struct { gm_t9i04n6j2d1gi f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_tf8kfhcnval78 f4; } gm_tihh2qes0hpd4;
typedef struct { gm_tv1gaqputuep6 f0; uint8_t f1; uint8_t pe[7]; } gm_tjdjicdhiqk6f;
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_tf4lk4crue8vt v1; gm_tmvmieab1acso v2; } f1; } gm_tkgci6o4gktbf;
_Static_assert(sizeof(gm_tlg23kb3oj23s) == 56 && _Alignof(gm_tlg23kb3oj23s) == 8 && offsetof(gm_tlg23kb3oj23s, f0) == 0 && offsetof(gm_tlg23kb3oj23s, f1) == 40, "layout of `Place`");
_Static_assert(sizeof(gm_tllthq21h2f0k) == 64 && _Alignof(gm_tllthq21h2f0k) == 16 && offsetof(gm_tllthq21h2f0k, f0) == 0 && offsetof(gm_tllthq21h2f0k, f1) == 16, "layout of `Callee`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tlg23kb3oj23s f1; } gm_tlpcrb3vu6o2d;
_Static_assert(sizeof(gm_tmvmieab1acso) == 64 && _Alignof(gm_tmvmieab1acso) == 16 && offsetof(gm_tmvmieab1acso, f0) == 0 && offsetof(gm_tmvmieab1acso, f1) == 16, "layout of `(decls: []u32, v: Value)`");
_Static_assert(sizeof(gm_tr6ll1dg9imrl) == 112 && _Alignof(gm_tr6ll1dg9imrl) == 16 && offsetof(gm_tr6ll1dg9imrl, f0) == 0 && offsetof(gm_tr6ll1dg9imrl, f1) == 4 && offsetof(gm_tr6ll1dg9imrl, f2) == 16 && offsetof(gm_tr6ll1dg9imrl, f3) == 64, "layout of `(dst: u32, negated: bool, a: Value, b: Value)`");
_Static_assert(sizeof(gm_tthvfhsvm2g0s) == 96 && _Alignof(gm_tthvfhsvm2g0s) == 16 && offsetof(gm_tthvfhsvm2g0s, f0) == 0 && offsetof(gm_tthvfhsvm2g0s, f1) == 48, "layout of `(index: Value, len: Value)`");
_Static_assert(sizeof(gm_tv1gaqputuep6) == 72 && _Alignof(gm_tv1gaqputuep6) == 8 && offsetof(gm_tv1gaqputuep6, f0) == 0 && offsetof(gm_tv1gaqputuep6, f1) == 32 && offsetof(gm_tv1gaqputuep6, f2) == 64, "layout of `Cmp`");
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; uint8_t p3[8]; gm_tf8kfhcnval78 f3; gm_b16_8 f4; } gm_tvdggnhat0k99;
typedef struct { uint32_t f0; uint32_t f1; uint8_t p2[8]; gm_tf8kfhcnval78 f2; } gm_tvv8lnu6a3ppj;
_Static_assert(sizeof(gm_t116nudii5c54) == 96 && _Alignof(gm_t116nudii5c54) == 16 && offsetof(gm_t116nudii5c54, f0) == 0 && offsetof(gm_t116nudii5c54, f1) == 16 && offsetof(gm_t116nudii5c54, f2) == 80, "layout of `(dst: ?u32, callee: Callee, args: []Value)`");
_Static_assert(sizeof(gm_t2q86r30n2ngm) == 80 && _Alignof(gm_t2q86r30n2ngm) == 8 && offsetof(gm_t2q86r30n2ngm, f0) == 0 && offsetof(gm_t2q86r30n2ngm, f1) == 8, "layout of `TempCmp`");
_Static_assert(sizeof(gm_t3349g713hmcr) == 72 && _Alignof(gm_t3349g713hmcr) == 8 && offsetof(gm_t3349g713hmcr, f0) == 0 && offsetof(gm_t3349g713hmcr, f1) == 8 && offsetof(gm_t3349g713hmcr, f2) == 64, "layout of `(dst: u32, place: Place, result: bool)`");
_Static_assert(sizeof(gm_t4j77n1ssj077) == 72 && _Alignof(gm_t4j77n1ssj077) == 8 && offsetof(gm_t4j77n1ssj077, f0) == 0 && offsetof(gm_t4j77n1ssj077, f1) == 8 && offsetof(gm_t4j77n1ssj077, f2) == 64, "layout of `(dst: u32, place: Place, variant: u32)`");
_Static_assert(sizeof(gm_t6fp3q71vjrmk) == 112 && _Alignof(gm_t6fp3q71vjrmk) == 16 && offsetof(gm_t6fp3q71vjrmk, f0) == 0 && offsetof(gm_t6fp3q71vjrmk, f1) == 64, "layout of `(place: Place, v: Value)`");
_Static_assert(sizeof(gm_t6j9ipvj0d6k5) == 112 && _Alignof(gm_t6j9ipvj0d6k5) == 16 && offsetof(gm_t6j9ipvj0d6k5, f0) == 0 && offsetof(gm_t6j9ipvj0d6k5, f1) == 48 && offsetof(gm_t6j9ipvj0d6k5, f2) == 104, "layout of `(present: Value, place: Place, result: bool)`");
_Static_assert(sizeof(gm_t7dcjsiqi95h3) == 80 && _Alignof(gm_t7dcjsiqi95h3) == 16 && offsetof(gm_t7dcjsiqi95h3, f0) == 0 && offsetof(gm_t7dcjsiqi95h3, f1) == 16, "layout of `(kind: Exit, v: ?Value)`");
_Static_assert(sizeof(gm_tfqmtuprvunod) == 80 && _Alignof(gm_tfqmtuprvunod) == 16 && offsetof(gm_tfqmtuprvunod, f0) == 0 && offsetof(gm_tfqmtuprvunod, f1) == 16, "layout of `Proj`");
_Static_assert(sizeof(gm_tg85uffmq8b38) == 80 && _Alignof(gm_tg85uffmq8b38) == 16 && offsetof(gm_tg85uffmq8b38, f0) == 0 && offsetof(gm_tg85uffmq8b38, f1) == 4 && offsetof(gm_tg85uffmq8b38, f2) == 16, "layout of `(dst: u32, kind: Wrap, v: ?Value)`");
_Static_assert(sizeof(gm_thvv7nb91atip) == 176 && _Alignof(gm_thvv7nb91atip) == 16 && offsetof(gm_thvv7nb91atip, f0) == 0 && offsetof(gm_thvv7nb91atip, f1) == 48 && offsetof(gm_thvv7nb91atip, f2) == 96 && offsetof(gm_thvv7nb91atip, f3) == 160, "layout of `(lo: Value, hi: Value, len: ?Value, inclusive: bool)`");
_Static_assert(sizeof(gm_tihh2qes0hpd4) == 176 && _Alignof(gm_tihh2qes0hpd4) == 16 && offsetof(gm_tihh2qes0hpd4, f0) == 0 && offsetof(gm_tihh2qes0hpd4, f1) == 64 && offsetof(gm_tihh2qes0hpd4, f2) == 80 && offsetof(gm_tihh2qes0hpd4, f3) == 96 && offsetof(gm_tihh2qes0hpd4, f4) == 112, "layout of `(sink: Sink, pieces: []const string, specs: []const FmtSpec, args: []Value, table: ?Value)`");
_Static_assert(sizeof(gm_tjdjicdhiqk6f) == 80 && _Alignof(gm_tjdjicdhiqk6f) == 8 && offsetof(gm_tjdjicdhiqk6f, f0) == 0 && offsetof(gm_tjdjicdhiqk6f, f1) == 72, "layout of `?Cmp`");
_Static_assert(sizeof(gm_tkgci6o4gktbf) == 80 && _Alignof(gm_tkgci6o4gktbf) == 16 && offsetof(gm_tkgci6o4gktbf, f0) == 0 && offsetof(gm_tkgci6o4gktbf, f1) == 16, "layout of `Handler`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_tthvfhsvm2g0s v0; gm_thvv7nb91atip v1; gm_t6j9ipvj0d6k5 v2; } f1; } gm_tkj0hg83g38nr;
_Static_assert(sizeof(gm_tlpcrb3vu6o2d) == 64 && _Alignof(gm_tlpcrb3vu6o2d) == 8 && offsetof(gm_tlpcrb3vu6o2d, f0) == 0 && offsetof(gm_tlpcrb3vu6o2d, f1) == 8, "layout of `(dst: u32, place: Place)`");
_Static_assert(sizeof(gm_tvdggnhat0k99) == 112 && _Alignof(gm_tvdggnhat0k99) == 16 && offsetof(gm_tvdggnhat0k99, f0) == 0 && offsetof(gm_tvdggnhat0k99, f1) == 4 && offsetof(gm_tvdggnhat0k99, f2) == 8 && offsetof(gm_tvdggnhat0k99, f3) == 32 && offsetof(gm_tvdggnhat0k99, f4) == 96, "layout of `(label: u32, tir_label: u32, install: []Inst, on: ?Value, body: []Inst)`");
_Static_assert(sizeof(gm_tvv8lnu6a3ppj) == 80 && _Alignof(gm_tvv8lnu6a3ppj) == 16 && offsetof(gm_tvv8lnu6a3ppj, f0) == 0 && offsetof(gm_tvv8lnu6a3ppj, f1) == 4 && offsetof(gm_tvv8lnu6a3ppj, f2) == 16, "layout of `(dst: u32, variant: u32, payload: ?Value)`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_tf4lk4crue8vt v0; gm_tlpcrb3vu6o2d v1; gm_tlpcrb3vu6o2d v2; gm_t1ctgp8j1v2as v3; gm_tbto4k5jg9fve v4; gm_tr6ll1dg9imrl v5; gm_t5hdm76ui2uro v6; gm_th622ku78unet v7; gm_t116nudii5c54 v8; gm_tiilmfd16v3fc v9; gm_trt0mvq732stt v10; gm_tf4lk4crue8vt v11; gm_tgroihbjsmdvm v12; gm_tihh2qes0hpd4 v13; gm_tvv8lnu6a3ppj v14; gm_tg85uffmq8b38 v15; gm_tilqfqdpj446h v16; gm_tcc1rq4n1avb2 v17; gm_tcc1rq4n1avb2 v18; gm_ttggq7ng4kp3p v19; gm_tr6ll1dg9imrl v20; gm_tcpisgbff1dqv v21; gm_t8rrrcunvsibs v22; gm_t3349g713hmcr v23; gm_t4j77n1ssj077 v24; gm_t6fp3q71vjrmk v25; gm_tkj0hg83g38nr v26; gm_tiilmfd16v3fc v27; gm_tiilmfd16v3fc v28; gm_t1s3s9ds8udn4 v29; uint32_t v30; gm_tejc6vv5sqno9 v31; gm_tf8kfhcnval78 v32; gm_thervm8j23qet v33; gm_t7b7esi3n9r90 v34; gm_tf8kfhcnval78 v35; gm_b16_8 v36; gm_tvdggnhat0k99 v37; gm_tkgci6o4gktbf v38; gm_t7dcjsiqi95h3 v39; gm_tthvfhsvm2g0s v40; gm_t7b7esi3n9r90 v41; gm_t12ib7gg43lh0 v42; } f1; } gm_th7badf4b6ee3;
_Static_assert(sizeof(gm_tkj0hg83g38nr) == 192 && _Alignof(gm_tkj0hg83g38nr) == 16 && offsetof(gm_tkj0hg83g38nr, f0) == 0 && offsetof(gm_tkj0hg83g38nr, f1) == 16, "layout of `Check`");
_Static_assert(sizeof(gm_th7badf4b6ee3) == 208 && _Alignof(gm_th7badf4b6ee3) == 16 && offsetof(gm_th7badf4b6ee3, f0) == 0 && offsetof(gm_th7badf4b6ee3, f1) == 16, "layout of `Op`");
typedef struct { gm_th7badf4b6ee3 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tqp4hhm6timm2;
_Static_assert(sizeof(gm_tqp4hhm6timm2) == 224 && _Alignof(gm_tqp4hhm6timm2) == 16 && offsetof(gm_tqp4hhm6timm2, f0) == 0 && offsetof(gm_tqp4hhm6timm2, f1) == 208, "layout of `Inst`");
struct gm_fr_cmp_of_27ao0j3 {
    gm_frame h;
    gm_tjdjicdhiqk6f ret;
};
struct gm_fr_comparison_c2aetd2 {
    gm_frame h;
    gm_tjdjicdhiqk6f ret;
};
struct gm_fr_deinit_5h14t04 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_k1nfkkq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_padgogk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_elide_checks_s9bgk65 {
    gm_frame h;
    gm_t3k59klpjql8o l2; /* e */
    uint32_t ret;
};
struct gm_fr_empty_slice_cq1ve68 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_nue4tn3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_followed_f4uav3s {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_forget_since_3v2e6hi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_havoc_all_psqvvsq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_havoc_fq0gr83 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_holds_qjf219d {
    gm_frame h;
    gm_tv1gaqputuep6 l6; /* f */
    gm_tv1gaqputuep6 l10; /* g */
    uint8_t ret;
};
struct gm_fr_is_unsigned_12b42v0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_learn_bg3djpd {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* cond */
    gm_tv1gaqputuep6 l4; /* c */
    gm_tv1gaqputuep6 l5; /* fact */
    gm_b0_1 ret;
};
struct gm_fr_leaves_2uhc28a {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_hoac6rk {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_jmqem24 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_load_sym_cf03dbc {
    gm_frame h;
    gm_tfqmtuprvunod l7; /* x */
    gm_tm7ecnors0fq6 ret;
};
struct gm_fr_nested_j61tpae {
    gm_frame h;
    gm_tf8kfhcnval78 l2; /* cond */
    gm_t7b7esi3n9r90 l6; /* c */
    gm_b16_8 ret;
};
struct gm_fr_note_stores_ijl61ps {
    gm_frame h;
    gm_tqp4hhm6timm2 l5; /* i */
    gm_tlg23kb3oj23s l6; /* p */
    uint8_t ret;
};
struct gm_fr_plain_t32bme8 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_proven_vei0gi0 {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* index */
    gm_t7b7esi3n9r90 l2; /* len */
    uint8_t ret;
};
struct gm_fr_push_ihqmmda {
    gm_frame h;
    gm_tv1gaqputuep6 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rcevpkn {
    gm_frame h;
    gm_t2q86r30n2ngm l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_a8851lm {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_p1hp1t2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_same_pen5lq3 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_step_6i5283s {
    gm_frame h;
    gm_tkj0hg83g38nr l2; /* c */
    gm_t7b7esi3n9r90 l3; /* i */
    gm_t7b7esi3n9r90 l4; /* n */
    gm_t7b7esi3n9r90 l6; /* v */
    gm_tlg23kb3oj23s l8; /* p */
    gm_t7b7esi3n9r90 l11; /* a */
    gm_t7b7esi3n9r90 l12; /* b */
    gm_tv1gaqputuep6 l15; /* c */
    gm_tlg23kb3oj23s l16; /* p */
    gm_t7b7esi3n9r90 l17; /* v */
    gm_t7b7esi3n9r90 l19; /* c */
    gm_t7b7esi3n9r90 l21; /* c */
    gm_t7b7esi3n9r90 l35; /* on */
    uint8_t ret;
};
struct gm_fr_stored_9gdkkdh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sym_of_q0v11rf {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* v */
    gm_tm7ecnors0fq6 ret;
};
struct gm_fr_truncate_p9n7n5b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_walk_i8nc7n2 {
    gm_frame h;
    gm_b16_8 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_cmp_of_27ao0j3(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tjdjicdhiqk6f gm_f_cmp_of_27ao0j3_c(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_comparison_c2aetd2(void *gm_out, gm_ts6oqppjh0ba3 a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_tjdjicdhiqk6f gm_f_comparison_c2aetd2_c(gm_ts6oqppjh0ba3 a0, gm_tm7ecnors0fq6 a1, gm_tm7ecnors0fq6 a2);
inline __attribute__((aligned(4))) void gm_f_deinit_5h14t04(void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_k1nfkkq(void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_padgogk(void * a0);
__attribute__((aligned(4))) uint32_t gm_f_elide_checks_s9bgk65(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cq1ve68(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nue4tn3(void);
__attribute__((aligned(4))) uint8_t gm_f_followed_f4uav3s(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_forget_since_3v2e6hi(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_5tbvv4o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_c7cv9n3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_havoc_all_psqvvsq(void * a0);
__attribute__((aligned(4))) void gm_f_havoc_fq0gr83(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_holds_qjf219d(void * a0, const void * a1, const void * a2, uint8_t a3);
__attribute__((aligned(4))) uint8_t gm_f_holds_qjf219d_c(void * a0, gm_tm7ecnors0fq6 a1, gm_tm7ecnors0fq6 a2, uint8_t a3);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unsigned_12b42v0(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_learn_bg3djpd(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) uint8_t gm_f_leaves_2uhc28a(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_hoac6rk(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_jmqem24(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_sym_cf03dbc(void *gm_out, void * a0, void * a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) gm_tm7ecnors0fq6 gm_f_load_sym_cf03dbc_c(void * a0, void * a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_make_fill_ah1cvnb(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_make_fill_bqbf5rp(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_nested_j61tpae(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, uint8_t a3);
__attribute__((aligned(4))) uint8_t gm_f_note_stores_ijl61ps(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_plain_t32bme8(const void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) uint8_t gm_f_plain_t32bme8_c(gm_tm7ecnors0fq6 a0, gm_tm7ecnors0fq6 a1, uint8_t a2);
__attribute__((aligned(4))) uint8_t gm_f_proven_vei0gi0(void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_proven_vei0gi0_c(void * a0, gm_t7b7esi3n9r90 a1, gm_t7b7esi3n9r90 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ihqmmda(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rcevpkn(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_a8851lm(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_p1hp1t2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) uint8_t gm_f_same_pen5lq3(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_same_pen5lq3_c(gm_tm7ecnors0fq6 a0, gm_tm7ecnors0fq6 a1);
__attribute__((aligned(4))) uint8_t gm_f_step_6i5283s(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_stored_9gdkkdh(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_sym_of_q0v11rf(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tm7ecnors0fq6 gm_f_sym_of_q0v11rf_c(void * a0, gm_t7b7esi3n9r90 a1);
inline __attribute__((aligned(4))) void gm_f_truncate_p9n7n5b(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_value_type_p69quvj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_walk_i8nc7n2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
extern uint8_t gm_k1ndla1vg9c[40] __attribute__((aligned(8)));
extern uint8_t gm_k2ua9ue91iv[40] __attribute__((aligned(8)));
extern uint8_t gm_k5upcce8mpd[40] __attribute__((aligned(8)));
extern uint8_t gm_k6m9oif038q[40] __attribute__((aligned(8)));
extern uint8_t gm_kaafb5o3idp[40] __attribute__((aligned(8)));
extern uint8_t gm_kmspjrkr36c[40] __attribute__((aligned(8)));
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcljn8aau5qd4v[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_deinit_5h14t04(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_5h14t04 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    if (t4) {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, ((((uint8_t *)t5) + 24) + 8));
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 16));
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 24));
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, ((uint8_t *)t10));
        void * t12 = t11;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 16));
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)80ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)8ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_nue4tn3();
    GM_ST(gm_b16_8, ((uint8_t *)t20), t21);
    void * t22 = gm_l0;
    GM_ST(uint64_t, (((uint8_t *)t22) + 16), ((uint64_t)0ULL));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_k1nfkkq(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_k1nfkkq F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    if (t4) {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, ((((uint8_t *)t5) + 24) + 8));
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 16));
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 24));
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, ((uint8_t *)t10));
        void * t12 = t11;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 16));
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)72ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)8ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_cq1ve68();
    GM_ST(gm_b16_8, ((uint8_t *)t20), t21);
    void * t22 = gm_l0;
    GM_ST(uint64_t, (((uint8_t *)t22) + 16), ((uint64_t)0ULL));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_padgogk(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_padgogk F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    if (t4) {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, ((((uint8_t *)t5) + 24) + 8));
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 16));
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 24));
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, ((uint8_t *)t10));
        void * t12 = t11;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 16));
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)4ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)4ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_d914i5u();
    GM_ST(gm_b16_8, ((uint8_t *)t20), t21);
    void * t22 = gm_l0;
    GM_ST(uint64_t, (((uint8_t *)t22) + 16), ((uint64_t)0ULL));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cq1ve68(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_cq1ve68 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nue4tn3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_nue4tn3 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 80)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unsigned_12b42v0(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unsigned_12b42v0 F;
    void * gm_l0; /* e */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_integer_8d4f9ek(t4, GM_CONV(gm_csguankko4lmhd, t5));
    if (t6) {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        gm_t88rcucg1b7uk t10 = gm_l1;
        uint8_t t11 = gm_f_is_signed_integer_s66djm7(t9, GM_CONV(gm_csguankko4lmhd, t10));
        uint8_t t12 = (uint8_t)!t11;
        gm_l2 = t12;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t13 = gm_l2;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_list_hoac6rk(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_hoac6rk F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_nue4tn3();
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
inline __attribute__((aligned(4))) void gm_f_list_jmqem24(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_jmqem24 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_cq1ve68();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ihqmmda(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ihqmmda F;
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
        gm_b16_8 t9 = gm_f_empty_slice_cq1ve68();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_a8851lm(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 72), ((uint8_t *)a1), 72);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rcevpkn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rcevpkn F;
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
        gm_b16_8 t9 = gm_f_empty_slice_nue4tn3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_p1hp1t2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 80), ((uint8_t *)a1), 80);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 80)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_a8851lm(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_a8851lm F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_p1hp1t2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_p1hp1t2 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)80ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_truncate_p9n7n5b(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_p9n7n5b F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 72)));
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
inline __attribute__((aligned(4))) void gm_f_cmp_of_27ao0j3(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_of_27ao0j3 F;
    void * gm_l0; /* e */
    uint32_t gm_l1; /* t */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 366u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 32) + 8));
    gm_l2 = t6;
    {
        for (;;) {
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) {
                goto gm_lb1;
            }
            {
                uint64_t t9 = gm_l2;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_srcljn8aau5qd4v, 137, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
                void * t13 = gm_l0;
                void * t14 = GM_LD(void *, (((uint8_t *)t13) + 32));
                uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 32) + 8));
                uint64_t t16 = gm_l2;
                if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcljn8aau5qd4v, 138, 12);
                uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)t14) + (uintptr_t)t16 * 80));
                uint32_t t18 = gm_l1;
                uint8_t t19 = ((t17 == t18));
                if (t19) {
                    void * t20 = gm_l0;
                    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 32));
                    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 32) + 8));
                    uint64_t t23 = gm_l2;
                    if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcljn8aau5qd4v, 138, 47);
                    gm_tv1gaqputuep6 t24 = GM_LD(gm_tv1gaqputuep6, ((((uint8_t *)t21) + (uintptr_t)t23 * 80) + 8));
                    gm_tjdjicdhiqk6f t25;
                    GM_ST(uint8_t, (uint8_t *)&t25 + 73, 0);
                    GM_ST(uint16_t, (uint8_t *)&t25 + 74, 0);
                    GM_ST(uint32_t, (uint8_t *)&t25 + 76, 0);
                    GM_ST(gm_tv1gaqputuep6, ((uint8_t *)&t25), t24);
                    ((uint8_t *)&t25)[72] = 1;
                    memmove(gm_out, ((uint8_t *)&t25), 80);
                    goto gm_fdone;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_tjdjicdhiqk6f t26;
    memset(&t26, 0, sizeof t26);
    memmove(gm_out, ((uint8_t *)&t26), 80);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tjdjicdhiqk6f gm_f_cmp_of_27ao0j3_c(void * a0, uint32_t a1) { gm_tjdjicdhiqk6f gm_r; gm_f_cmp_of_27ao0j3(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_comparison_c2aetd2(void *gm_out, gm_ts6oqppjh0ba3 a0, const void * a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_comparison_c2aetd2 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_tm7ecnors0fq6 gm_l1; /* a */
    gm_tm7ecnors0fq6 gm_l2; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 1098u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tjdjicdhiqk6f gm_l3;
    {
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)10ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_tm7ecnors0fq6 t10 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)a1));
            gm_tm7ecnors0fq6 t11 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)a2));
            gm_tv1gaqputuep6 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 65, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 66, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 68, 0);
            GM_ST(gm_tm7ecnors0fq6, ((uint8_t *)&t12 + 0), t10);
            GM_ST(gm_tm7ecnors0fq6, ((uint8_t *)&t12 + 32), t11);
            GM_ST(uint8_t, ((uint8_t *)&t12 + 64), ((uint8_t)1));
            gm_tjdjicdhiqk6f t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 73, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 74, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 76, 0);
            GM_ST(gm_tv1gaqputuep6, ((uint8_t *)&t13), t12);
            ((uint8_t *)&t13)[72] = 1;
            gm_l3 = t13;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)11ULL));
            if (!t15) {
                goto gm_lb3;
            }
            gm_tm7ecnors0fq6 t16 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)a1));
            gm_tm7ecnors0fq6 t17 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)a2));
            gm_tv1gaqputuep6 t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 65, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 66, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 68, 0);
            GM_ST(gm_tm7ecnors0fq6, ((uint8_t *)&t18 + 0), t16);
            GM_ST(gm_tm7ecnors0fq6, ((uint8_t *)&t18 + 32), t17);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 64), ((uint8_t)0));
            gm_tjdjicdhiqk6f t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 73, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 74, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 76, 0);
            GM_ST(gm_tv1gaqputuep6, ((uint8_t *)&t19), t18);
            ((uint8_t *)&t19)[72] = 1;
            gm_l3 = t19;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)12ULL));
            if (!t21) {
                goto gm_lb4;
            }
            gm_tm7ecnors0fq6 t22 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)a2));
            gm_tm7ecnors0fq6 t23 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)a1));
            gm_tv1gaqputuep6 t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 65, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 66, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 68, 0);
            GM_ST(gm_tm7ecnors0fq6, ((uint8_t *)&t24 + 0), t22);
            GM_ST(gm_tm7ecnors0fq6, ((uint8_t *)&t24 + 32), t23);
            GM_ST(uint8_t, ((uint8_t *)&t24 + 64), ((uint8_t)1));
            gm_tjdjicdhiqk6f t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 73, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 74, 0);
            GM_ST(uint32_t, (uint8_t *)&t25 + 76, 0);
            GM_ST(gm_tv1gaqputuep6, ((uint8_t *)&t25), t24);
            ((uint8_t *)&t25)[72] = 1;
            gm_l3 = t25;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)13ULL));
            if (!t27) {
                goto gm_lb5;
            }
            gm_tm7ecnors0fq6 t28 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)a2));
            gm_tm7ecnors0fq6 t29 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)a1));
            gm_tv1gaqputuep6 t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 65, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 66, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 68, 0);
            GM_ST(gm_tm7ecnors0fq6, ((uint8_t *)&t30 + 0), t28);
            GM_ST(gm_tm7ecnors0fq6, ((uint8_t *)&t30 + 32), t29);
            GM_ST(uint8_t, ((uint8_t *)&t30 + 64), ((uint8_t)0));
            gm_tjdjicdhiqk6f t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 73, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 74, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 76, 0);
            GM_ST(gm_tv1gaqputuep6, ((uint8_t *)&t31), t30);
            ((uint8_t *)&t31)[72] = 1;
            gm_l3 = t31;
            goto gm_lb1;
        }
gm_lb5:;
        {
            gm_tjdjicdhiqk6f t32;
            memset(&t32, 0, sizeof t32);
            gm_l3 = t32;
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcljn8aau5qd4v, 125, 55);
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&gm_l3), 80);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tjdjicdhiqk6f gm_f_comparison_c2aetd2_c(gm_ts6oqppjh0ba3 a0, gm_tm7ecnors0fq6 a1, gm_tm7ecnors0fq6 a2) { gm_tjdjicdhiqk6f gm_r; gm_f_comparison_c2aetd2(&gm_r, a0, &a1, &a2); return gm_r; }
inline __attribute__((aligned(4))) uint32_t gm_f_elide_checks_s9bgk65(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_elide_checks_s9bgk65 F;
    void * gm_l0; /* s */
    void * gm_l1; /* f */
    GM_STACK_CHECK(&F, 767u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l3;
    gm_tabb710d9mq2e gm_l4;
    gm_tabb710d9mq2e gm_l5;
    void * t2 = gm_l0;
    void * t3 = gm_l1;
    void * t4 = gm_l1;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 24) + 8));
    gm_tm7ecnors0fq6 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
    gm_f_make_fill_ah1cvnb((void *)&gm_l3, (void *)NULL, cx, NULL, t5, &t6, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k5upcce8mpd)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcljn8aau5qd4v, 18, 34);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_trqfvmjtt4sij t9;
    gm_f_list_hoac6rk((void *)&t9, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t10 = gm_l1;
    uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t10) + 8) + 8));
    gm_f_make_fill_bqbf5rp((void *)&gm_l4, (void *)NULL, cx, NULL, t11, ((uint32_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k6m9oif038q)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcljn8aau5qd4v, 18, 95);
    gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
    void * t14 = gm_l1;
    uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t14) + 8) + 8));
    gm_tm7ecnors0fq6 t16;
    GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t16 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t16 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t16 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
    gm_f_make_fill_ah1cvnb((void *)&gm_l5, (void *)NULL, cx, NULL, t15, &t16, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kaafb5o3idp)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcljn8aau5qd4v, 18, 135);
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    gm_trqfvmjtt4sij t19;
    gm_f_list_jmqem24((void *)&t19, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t20;
    gm_f_list_go2qg0b((void *)&t20, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t3k59klpjql8o t21;
    GM_ST(void *, ((uint8_t *)&t21 + 0), t2);
    GM_ST(void *, ((uint8_t *)&t21 + 8), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&t21 + 16), t8);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t21 + 32), t9);
    GM_ST(gm_b16_8, ((uint8_t *)&t21 + 72), t13);
    GM_ST(gm_b16_8, ((uint8_t *)&t21 + 88), t18);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t21 + 104), t19);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t21 + 144), t20);
    GM_ST(uint32_t, ((uint8_t *)&t21 + 184), ((uint32_t)1ULL));
    GM_ST(uint32_t, ((uint8_t *)&t21 + 188), ((uint32_t)0ULL));
    F.l2 = t21;
    void * t22 = gm_l1;
    void * t23 = (void *)((uint8_t *)&F.l2);
    void * t24 = gm_l1;
    gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)t24) + 48));
    gm_b16_8 t26 = gm_f_walk_i8nc7n2((void *)NULL, cx, NULL, t23, t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (((uint8_t *)t22) + 48), t26);
    gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 16));
    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
    gm_b0_1 t29 = gm_f_free_c7cv9n3((void *)NULL, cx, NULL, t28, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kmspjrkr36c)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 72));
    gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
    gm_b0_1 t32 = gm_f_free_5tbvv4o((void *)NULL, cx, NULL, t31, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k1ndla1vg9c)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 88));
    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
    gm_b0_1 t35 = gm_f_free_c7cv9n3((void *)NULL, cx, NULL, t34, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2ua9ue91iv)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t36 = (void *)(((uint8_t *)&F.l2) + 32);
    gm_f_deinit_5h14t04(t36);
    void * t37 = (void *)(((uint8_t *)&F.l2) + 104);
    gm_f_deinit_k1nfkkq(t37);
    void * t38 = (void *)(((uint8_t *)&F.l2) + 144);
    gm_f_deinit_padgogk(t38);
    uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 188));
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
inline __attribute__((aligned(4))) uint8_t gm_f_followed_f4uav3s(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_followed_f4uav3s F;
    void * gm_l0; /* e */
    uint32_t gm_l1; /* l */
    void * gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l3;
    uint32_t t2 = gm_l1;
    uint64_t t3 = (uint64_t)t2;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 8));
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 8) + 8));
    uint8_t t7 = (t3 >= t6);
    if (t7) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 8));
    void * t10 = GM_LD(void *, (((uint8_t *)t9) + 8));
    uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t9) + 8) + 8));
    uint32_t t12 = gm_l1;
    uint64_t t13 = (uint64_t)t12;
    if ((uintptr_t)t13 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t13, t11, gm_srcljn8aau5qd4v, 84, 11);
    void * t14 = (void *)(((uint8_t *)t10) + (uintptr_t)t13 * 32);
    gm_l2 = t14;
    void * t15 = gm_l2;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t15) + 25));
    uint8_t t17 = (uint8_t)!t16;
    if (t17) {
        void * t18 = gm_l2;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t18) + 26));
        uint8_t t20 = (uint8_t)!t19;
        gm_l3 = t20;
    } else {
        gm_l3 = ((uint8_t)0);
    }
    uint8_t t21 = gm_l3;
    F.ret = t21;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_forget_since_3v2e6hi(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_forget_since_3v2e6hi F;
    void * gm_l0; /* e */
    uint64_t gm_l1; /* log */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 149u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t7 = gm_l1;
    gm_l3 = t7;
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 144) + 8));
    gm_l2 = t9;
    {
        for (;;) {
            uint64_t t10 = gm_l3;
            uint64_t t11 = gm_l2;
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t13 = gm_l3;
                        gm_l4 = t13;
                        void * t14 = gm_l0;
                        void * t15 = gm_l0;
                        void * t16 = GM_LD(void *, (((uint8_t *)t15) + 144));
                        uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t15) + 144) + 8));
                        uint64_t t18 = gm_l4;
                        if ((uintptr_t)t18 >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)t18, t17, gm_srcljn8aau5qd4v, 213, 45);
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)t16) + (uintptr_t)t18 * 4));
                        gm_f_havoc_fq0gr83(t14, t19);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcljn8aau5qd4v, 213, 5);
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
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_havoc_all_psqvvsq(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_havoc_all_psqvvsq F;
    void * gm_l0; /* e */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = ((uint64_t)0ULL);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 72) + 8));
    gm_l1 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l2;
            uint64_t t10 = gm_l1;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t12 = gm_l2;
                        gm_l3 = t12;
                        void * t13 = gm_l0;
                        uint64_t t14 = gm_l3;
                        uint32_t t15 = (uint32_t)t14;
                        gm_f_havoc_fq0gr83(t13, t15);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcljn8aau5qd4v, 97, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t16 = gm_l2;
            uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
            uint64_t t19 = r17;
            gm_l2 = t19;
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
inline __attribute__((aligned(4))) void gm_f_havoc_fq0gr83(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_havoc_fq0gr83 F;
    void * gm_l0; /* e */
    uint32_t gm_l1; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 181u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t2 = gm_l1;
    uint64_t t3 = (uint64_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 72) + 8));
    uint8_t t6 = (t3 >= t5);
    if (t6) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, (((uint8_t *)t7) + 72));
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t7) + 72) + 8));
    uint32_t t10 = gm_l1;
    uint64_t t11 = (uint64_t)t10;
    if ((uintptr_t)t11 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t11, t9, gm_srcljn8aau5qd4v, 91, 5);
    void * t12 = gm_l0;
    uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)t12) + 184));
    GM_ST(uint32_t, (((uint8_t *)t8) + (uintptr_t)t11 * 4), t13);
    void * t14 = gm_l0;
    void * t15 = gm_l0;
    uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)t15) + 184));
    uint32_t r17; bool o18 = __builtin_add_overflow(t16, ((uint32_t)1ULL), &r17);
    if (o18) gm_trap("integer overflow in `+`", gm_srcljn8aau5qd4v, 92, 5);
    uint32_t t19 = r17;
    GM_ST(uint32_t, (((uint8_t *)t14) + 184), t19);
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 88));
    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 88) + 8));
    uint32_t t23 = gm_l1;
    uint64_t t24 = (uint64_t)t23;
    if ((uintptr_t)t24 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t24, t22, gm_srcljn8aau5qd4v, 93, 5);
    gm_tm7ecnors0fq6 t25;
    GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t25 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t25 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t25 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tm7ecnors0fq6, (((uint8_t *)t21) + (uintptr_t)t24 * 32), t25);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_holds_qjf219d(void * a0, const void * a1, const void * a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_holds_qjf219d F;
    void * gm_l0; /* e */
    gm_tm7ecnors0fq6 gm_l1; /* a */
    gm_tm7ecnors0fq6 gm_l2; /* b */
    uint8_t gm_l3; /* strict */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint8_t gm_l7; /* rest */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 368u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l3 = a3;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t t14 = gm_l3;
    uint8_t t15 = gm_f_plain_t32bme8(&(*(gm_tm7ecnors0fq6 *)((uint8_t *)a1)), &(*(gm_tm7ecnors0fq6 *)((uint8_t *)a2)), t14);
    if (t15) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    void * t16 = gm_l0;
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t16) + 104));
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
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t21) + (uintptr_t)t23 * 72), 72);
                        uint8_t t24 = gm_f_same_pen5lq3(&(*(gm_tm7ecnors0fq6 *)((uint8_t *)&F.l6)), &(*(gm_tm7ecnors0fq6 *)((uint8_t *)a1)));
                        uint8_t t25 = (uint8_t)!t24;
                        if (t25) {
                            goto gm_lb3;
                        }
                        uint8_t t26 = gm_l3;
                        if (t26) {
                            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 64));
                            uint8_t t28 = (uint8_t)!t27;
                            gm_l11 = t28;
                        } else {
                            gm_l11 = ((uint8_t)0);
                        }
                        uint8_t t29 = gm_l11;
                        gm_l7 = t29;
                        uint8_t t30 = gm_l7;
                        uint8_t t31 = gm_f_plain_t32bme8(&(*(gm_tm7ecnors0fq6 *)(((uint8_t *)&F.l6) + 32)), &(*(gm_tm7ecnors0fq6 *)((uint8_t *)a2)), t30);
                        if (t31) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        void * t32 = gm_l0;
                        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 104));
                        gm_l8 = t33;
                        gm_l9 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t34 = gm_l9;
                                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                uint8_t t36 = (t34 < t35);
                                if (!t36) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t37 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                    uint64_t t39 = gm_l9;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l10), (((uint8_t *)t37) + (uintptr_t)t39 * 72), 72);
                                            {
                                                {
                                                    uint8_t t40 = gm_f_same_pen5lq3(&(*(gm_tm7ecnors0fq6 *)((uint8_t *)&F.l10)), &(*(gm_tm7ecnors0fq6 *)(((uint8_t *)&F.l6) + 32)));
                                                    if (!t40) {
                                                        goto gm_lb12;
                                                    }
                                                    uint8_t t41 = gm_l7;
                                                    gm_tm7ecnors0fq6 t42 = GM_LD(gm_tm7ecnors0fq6, (((uint8_t *)&F.l10) + 32));
                                                    gm_tm7ecnors0fq6 t43 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)a2));
                                                    if (t41) {
                                                        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 64));
                                                        uint8_t t45 = (uint8_t)!t44;
                                                        gm_l12 = t45;
                                                    } else {
                                                        gm_l12 = ((uint8_t)0);
                                                    }
                                                    uint8_t t46 = gm_l12;
                                                    uint8_t t47 = gm_f_plain_t32bme8(&t42, &t43, t46);
                                                    if (!t47) {
                                                        goto gm_lb12;
                                                    }
                                                    F.ret = ((uint8_t)1);
                                                    goto gm_fret;
                                                }
gm_lb12:;
                                            }
gm_lb11:;
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcljn8aau5qd4v, 167, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t48 = gm_l9;
                                uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                                uint64_t t51 = r49;
                                gm_l9 = t51;
                            }
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcljn8aau5qd4v, 163, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t52 = gm_l5;
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            uint64_t t55 = r53;
            gm_l5 = t55;
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
__attribute__((aligned(4))) uint8_t gm_f_holds_qjf219d_c(void * a0, gm_tm7ecnors0fq6 a1, gm_tm7ecnors0fq6 a2, uint8_t a3) { return gm_f_holds_qjf219d(a0, &a1, &a2, a3); }
extern __attribute__((aligned(4))) uint8_t gm_f_is_unsigned_12b42v0(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_learn_bg3djpd(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2) {
    struct gm_fr_learn_bg3djpd F;
    void * gm_l0; /* e */
    uint8_t gm_l2; /* truth */
    uint32_t gm_l3; /* t */
    GM_STACK_CHECK(&F, 553u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_tjdjicdhiqk6f gm_l6;
    gm_tv1gaqputuep6 gm_l7;
    gm_tv1gaqputuep6 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    {
        {
            uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t5 = (uint8_t)(t4 == ((uint8_t)0ULL));
            if (!t5) {
                goto gm_lb2;
            }
            uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
            gm_l3 = t6;
            void * t7 = gm_l0;
            uint32_t t8 = gm_l3;
            gm_f_cmp_of_27ao0j3((void *)&gm_l6, t7, t8);
            uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l6))[72] != 0);
            if (t9) {
                memmove(((uint8_t *)&gm_l7), ((uint8_t *)&gm_l6), 72);
            } else {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            memmove(((uint8_t *)&F.l4), ((uint8_t *)&gm_l7), 72);
            uint8_t t10 = gm_l2;
            if (t10) {
                uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 64));
                gm_tm7ecnors0fq6 t12 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)&F.l4));
                gm_tm7ecnors0fq6 t13 = GM_LD(gm_tm7ecnors0fq6, (((uint8_t *)&F.l4) + 32));
                gm_tv1gaqputuep6 t14;
                GM_ST(uint8_t, (uint8_t *)&t14 + 65, 0);
                GM_ST(uint16_t, (uint8_t *)&t14 + 66, 0);
                GM_ST(uint32_t, (uint8_t *)&t14 + 68, 0);
                GM_ST(gm_tm7ecnors0fq6, ((uint8_t *)&t14 + 0), t12);
                GM_ST(gm_tm7ecnors0fq6, ((uint8_t *)&t14 + 32), t13);
                GM_ST(uint8_t, ((uint8_t *)&t14 + 64), t11);
                gm_l8 = t14;
            } else {
                uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 64));
                uint8_t t16 = (uint8_t)!t15;
                gm_tm7ecnors0fq6 t17 = GM_LD(gm_tm7ecnors0fq6, (((uint8_t *)&F.l4) + 32));
                gm_tm7ecnors0fq6 t18 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)&F.l4));
                gm_tv1gaqputuep6 t19;
                GM_ST(uint8_t, (uint8_t *)&t19 + 65, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 66, 0);
                GM_ST(uint32_t, (uint8_t *)&t19 + 68, 0);
                GM_ST(gm_tm7ecnors0fq6, ((uint8_t *)&t19 + 0), t17);
                GM_ST(gm_tm7ecnors0fq6, ((uint8_t *)&t19 + 32), t18);
                GM_ST(uint8_t, ((uint8_t *)&t19 + 64), t16);
                gm_l8 = t19;
            }
            memmove(((uint8_t *)&F.l5), ((uint8_t *)&gm_l8), 72);
            void * t20 = gm_l0;
            void * t21 = (void *)(((uint8_t *)t20) + 104);
            gm_tftb1odc739t5 t22 = gm_f_push_ihqmmda(t21, &(*(gm_tv1gaqputuep6 *)((uint8_t *)&F.l5)));
            gm_l9 = t22;
            uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcljn8aau5qd4v, 149, 5);
            goto gm_lb1;
        }
gm_lb2:;
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_leaves_2uhc28a(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_leaves_2uhc28a F;
    gm_b16_8 gm_l0; /* insts */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
    if (t14) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t15 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
    if (o19) gm_trap("integer overflow in `-`", gm_srcljn8aau5qd4v, 250, 24);
    uint64_t t20 = r18;
    if ((uintptr_t)t20 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t20, t16, gm_srcljn8aau5qd4v, 250, 18);
    {
        {
            {
                {
                    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t15) + (uintptr_t)t20 * 224) + 0));
                    uint8_t t22 = (uint8_t)(t21 == ((uint8_t)32ULL));
                    if (!t22) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t15) + (uintptr_t)t20 * 224) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)30ULL));
                    if (!t24) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t15) + (uintptr_t)t20 * 224) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)39ULL));
                    if (!t26) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t15) + (uintptr_t)t20 * 224) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)34ULL));
                    if (!t28) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t15) + (uintptr_t)t20 * 224) + 0));
                    uint8_t t30 = (uint8_t)(t29 == ((uint8_t)35ULL));
                    if (!t30) {
                        goto gm_lb8;
                    }
                    goto gm_lb3;
                }
gm_lb8:;
                {
                    uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t15) + (uintptr_t)t20 * 224) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)42ULL));
                    if (!t32) {
                        goto gm_lb9;
                    }
                    goto gm_lb3;
                }
gm_lb9:;
                {
                    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t15) + (uintptr_t)t20 * 224) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)43ULL));
                    if (!t34) {
                        goto gm_lb10;
                    }
                    goto gm_lb3;
                }
gm_lb10:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l1 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l1 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb11:;
        gm_trap("no match arm matched the scrutinee", gm_srcljn8aau5qd4v, 250, 12);
    }
gm_lb1:;
    uint8_t t35 = gm_l1;
    F.ret = t35;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_load_sym_cf03dbc(void *gm_out, void * a0, void * a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_load_sym_cf03dbc F;
    void * gm_l0; /* e */
    void * gm_l1; /* p */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    uint32_t gm_l3; /* l */
    uint64_t gm_l4; /* off */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l8; /* o */
    uint64_t gm_l9; /* c */
    uint32_t gm_l10; /* l2 */
    uint32_t gm_l11; /* g2 */
    uint64_t gm_l12; /* o2 */
    GM_STACK_CHECK_SHALLOW(&F, 718u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    gm_tm7ecnors0fq6 gm_l13;
    {
        {
            void * t15 = gm_l1;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t15) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)0ULL));
            if (!t17) {
                goto gm_lb2;
            }
            uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)t15) + 8));
            gm_l3 = t18;
            void * t19 = gm_l0;
            uint32_t t20 = gm_l3;
            uint8_t t21 = gm_f_followed_f4uav3s(t19, t20);
            uint8_t t22 = (uint8_t)!t21;
            if (t22) {
                gm_tm7ecnors0fq6 t23;
                GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t23 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t23 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t23 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
                memmove(gm_out, ((uint8_t *)&t23), 32);
                goto gm_fdone;
            }
            gm_l4 = ((uint64_t)0ULL);
            void * t24 = gm_l1;
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)t24) + 40));
            gm_l5 = t25;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t26 = gm_l6;
                    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t28 = (t26 < t27);
                    if (!t28) {
                        goto gm_lb3;
                    }
                    {
                        void * t29 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t31 = gm_l6;
                        {
                            {
                                memmove(((uint8_t *)&F.l7), (((uint8_t *)t29) + (uintptr_t)t31 * 80), 80);
                                {
                                    {
                                        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                                        uint8_t t33 = (uint8_t)(t32 == ((uint8_t)0ULL));
                                        if (!t33) {
                                            goto gm_lb9;
                                        }
                                        uint64_t t34 = GM_LD(uint64_t, ((((uint8_t *)&F.l7) + 16) + 8));
                                        gm_l8 = t34;
                                        uint64_t t35 = gm_l4;
                                        uint64_t t36 = gm_l8;
                                        uint64_t r37; bool o38 = __builtin_add_overflow(t35, t36, &r37);
                                        if (o38) gm_trap("integer overflow in `+`", gm_srcljn8aau5qd4v, 113, 9);
                                        uint64_t t39 = r37;
                                        gm_l4 = t39;
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    gm_tm7ecnors0fq6 t40;
                                    GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t40 + 8, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t40 + 16, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t40 + 24, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)0ULL));
                                    memmove(gm_out, ((uint8_t *)&t40), 32);
                                    goto gm_fdone;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcljn8aau5qd4v, 111, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t41 = gm_l6;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l6 = t44;
                }
            }
gm_lb3:;
            void * t45 = gm_l0;
            void * t46 = GM_LD(void *, (((uint8_t *)t45) + 88));
            uint64_t t47 = GM_LD(uint64_t, ((((uint8_t *)t45) + 88) + 8));
            uint32_t t48 = gm_l3;
            uint64_t t49 = (uint64_t)t48;
            if ((uintptr_t)t49 >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)t49, t47, gm_srcljn8aau5qd4v, 115, 11);
            {
                {
                    uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t46) + (uintptr_t)t49 * 32) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)1ULL));
                    if (!t51) {
                        goto gm_lb11;
                    }
                    uint64_t t52 = GM_LD(uint64_t, ((((uint8_t *)t46) + (uintptr_t)t49 * 32) + 8));
                    gm_l9 = t52;
                    void * t53 = gm_l1;
                    uint64_t t54 = GM_LD(uint64_t, ((((uint8_t *)t53) + 40) + 8));
                    uint8_t t55 = ((t54 == ((uint64_t)0ULL)));
                    if (t55) {
                        uint64_t t56 = gm_l9;
                        gm_tm7ecnors0fq6 t57;
                        GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t57 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t57 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)1ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t57 + 8), t56);
                        gm_l13 = t57;
                    } else {
                        gm_tm7ecnors0fq6 t58;
                        GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t58 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t58 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t58 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)0ULL));
                        gm_l13 = t58;
                    }
                    memmove(gm_out, ((uint8_t *)&gm_l13), 32);
                    goto gm_fdone;
                }
gm_lb11:;
                {
                    uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t46) + (uintptr_t)t49 * 32) + 0));
                    uint8_t t60 = (uint8_t)(t59 == ((uint8_t)2ULL));
                    if (!t60) {
                        goto gm_lb12;
                    }
                    uint32_t t61 = GM_LD(uint32_t, ((((uint8_t *)t46) + (uintptr_t)t49 * 32) + 8));
                    gm_l10 = t61;
                    uint32_t t62 = GM_LD(uint32_t, (((((uint8_t *)t46) + (uintptr_t)t49 * 32) + 8) + 4));
                    gm_l11 = t62;
                    uint64_t t63 = GM_LD(uint64_t, (((((uint8_t *)t46) + (uintptr_t)t49 * 32) + 8) + 8));
                    gm_l12 = t63;
                    uint32_t t64 = gm_l10;
                    uint32_t t65 = gm_l11;
                    uint64_t t66 = gm_l12;
                    uint64_t t67 = gm_l4;
                    uint64_t r68; bool o69 = __builtin_add_overflow(t66, t67, &r68);
                    if (o69) gm_trap("integer overflow in `+`", gm_srcljn8aau5qd4v, 117, 54);
                    uint64_t t70 = r68;
                    gm_t88rcucg1b7uk t71 = gm_l2;
                    gm_tuipckodmso90 t72;
                    GM_ST(uint32_t, (uint8_t *)&t72 + 20, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t72 + 0), t64);
                    GM_ST(uint32_t, ((uint8_t *)&t72 + 4), t65);
                    GM_ST(uint64_t, ((uint8_t *)&t72 + 8), t70);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t72 + 16), t71);
                    gm_tm7ecnors0fq6 t73;
                    GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_tuipckodmso90, ((uint8_t *)&t73 + 8), t72);
                    memmove(gm_out, ((uint8_t *)&t73), 32);
                    goto gm_fdone;
                }
gm_lb12:;
                {
                    uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t46) + (uintptr_t)t49 * 32) + 0));
                    uint8_t t75 = (uint8_t)(t74 == ((uint8_t)0ULL));
                    if (!t75) {
                        goto gm_lb13;
                    }
                    uint32_t t76 = gm_l3;
                    void * t77 = gm_l0;
                    void * t78 = GM_LD(void *, (((uint8_t *)t77) + 72));
                    uint64_t t79 = GM_LD(uint64_t, ((((uint8_t *)t77) + 72) + 8));
                    uint32_t t80 = gm_l3;
                    uint64_t t81 = (uint64_t)t80;
                    if ((uintptr_t)t81 >= (uintptr_t)(t79)) gm_trap_index((uintptr_t)t81, t79, gm_srcljn8aau5qd4v, 118, 34);
                    uint32_t t82 = GM_LD(uint32_t, (((uint8_t *)t78) + (uintptr_t)t81 * 4));
                    uint64_t t83 = gm_l4;
                    gm_t88rcucg1b7uk t84 = gm_l2;
                    gm_tuipckodmso90 t85;
                    GM_ST(uint32_t, (uint8_t *)&t85 + 20, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t85 + 0), t76);
                    GM_ST(uint32_t, ((uint8_t *)&t85 + 4), t82);
                    GM_ST(uint64_t, ((uint8_t *)&t85 + 8), t83);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t85 + 16), t84);
                    gm_tm7ecnors0fq6 t86;
                    GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_tuipckodmso90, ((uint8_t *)&t86 + 8), t85);
                    memmove(gm_out, ((uint8_t *)&t86), 32);
                    goto gm_fdone;
                }
gm_lb13:;
                gm_trap("no match arm matched the scrutinee", gm_srcljn8aau5qd4v, 115, 5);
            }
gm_lb10:;
        }
gm_lb2:;
        gm_tm7ecnors0fq6 t87;
        GM_ST(uint8_t, (uint8_t *)&t87 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t87 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t87 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t87 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t87 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t87 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)0ULL));
        memmove(gm_out, ((uint8_t *)&t87), 32);
        goto gm_fdone;
    }
gm_lb1:;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tm7ecnors0fq6 gm_f_load_sym_cf03dbc_c(void * a0, void * a1, gm_csguankko4lmhd a2) { gm_tm7ecnors0fq6 gm_r; gm_f_load_sym_cf03dbc(&gm_r, a0, a1, a2); return gm_r; }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_nested_j61tpae(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, uint8_t a3) {
    struct gm_fr_nested_j61tpae F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l1; /* insts */
    uint8_t gm_l3; /* truth */
    uint64_t gm_l4; /* mark */
    uint64_t gm_l5; /* log */
    gm_b16_8 gm_l7; /* out */
    GM_STACK_CHECK(&F, 227u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 104) + 8));
    gm_l4 = t5;
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)t6) + 144) + 8));
    gm_l5 = t7;
    {
        {
            uint8_t t8 = (uint8_t)((((uint8_t *)&F.l2))[48] != 0);
            if (!t8) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l6), ((uint8_t *)&F.l2), 48);
            void * t9 = gm_l0;
            uint8_t t10 = gm_l3;
            gm_b0_1 t11 = gm_f_learn_bg3djpd((void *)NULL, cx, NULL, t9, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l6)), t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t12 = gm_l0;
    gm_b16_8 t13 = gm_l1;
    gm_b16_8 t14 = gm_f_walk_i8nc7n2((void *)NULL, cx, NULL, t12, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t14;
    void * t15 = gm_l0;
    void * t16 = (void *)(((uint8_t *)t15) + 104);
    uint64_t t17 = gm_l4;
    gm_f_truncate_p9n7n5b(t16, t17);
    void * t18 = gm_l0;
    uint64_t t19 = gm_l5;
    gm_f_forget_since_3v2e6hi(t18, t19);
    gm_b16_8 t20 = gm_l7;
    F.ret = t20;
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
__attribute__((aligned(4))) uint8_t gm_f_note_stores_ijl61ps(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_note_stores_ijl61ps F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l1; /* insts */
    uint8_t gm_l2; /* any */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l7; /* l */
    gm_b16_8 gm_l8; /* body */
    gm_b16_8 gm_l9; /* body */
    gm_b16_8 gm_l10; /* body */
    gm_b16_8 gm_l11; /* body */
    gm_b16_8 gm_l12; /* a */
    gm_b16_8 gm_l13; /* b */
    gm_b16_8 gm_l14; /* a */
    gm_b16_8 gm_l15; /* b */
    GM_STACK_CHECK(&F, 656u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = ((uint8_t)0);
    gm_b16_8 t21 = gm_l1;
    gm_l3 = t21;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t22 = gm_l4;
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t24 = (t22 < t23);
            if (!t24) {
                goto gm_lb1;
            }
            {
                void * t25 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t27 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t25) + (uintptr_t)t27 * 224), 224);
                        {
                            {
                                uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t29 = (uint8_t)(t28 == ((uint8_t)25ULL));
                                if (!t29) {
                                    goto gm_lb7;
                                }
                                memmove(((uint8_t *)&F.l6), (((uint8_t *)&F.l5) + 16), 56);
                                {
                                    {
                                        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                        uint8_t t31 = (uint8_t)(t30 == ((uint8_t)0ULL));
                                        if (!t31) {
                                            goto gm_lb9;
                                        }
                                        uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 8));
                                        gm_l7 = t32;
                                        void * t33 = gm_l0;
                                        uint32_t t34 = gm_l7;
                                        gm_tm7ecnors0fq6 t35;
                                        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t35 + 8, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t35 + 24, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)0ULL));
                                        gm_b0_1 t36 = gm_f_stored_9gdkkdh((void *)NULL, cx, NULL, t33, t34, &t35, ((uint8_t)0));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t38 = (uint8_t)(t37 == ((uint8_t)44ULL));
                                if (!t38) {
                                    goto gm_lb10;
                                }
                                gm_l2 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb10:;
                            {
                                {
                                    {
                                        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                        uint8_t t40 = (uint8_t)(t39 == ((uint8_t)27ULL));
                                        if (!t40) {
                                            goto gm_lb13;
                                        }
                                        gm_b16_8 t41 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l5) + 16) + 8));
                                        gm_l8 = t41;
                                        goto gm_lb12;
                                    }
gm_lb13:;
                                    {
                                        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                        uint8_t t43 = (uint8_t)(t42 == ((uint8_t)28ULL));
                                        if (!t43) {
                                            goto gm_lb14;
                                        }
                                        gm_b16_8 t44 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l5) + 16) + 8));
                                        gm_l8 = t44;
                                        goto gm_lb12;
                                    }
gm_lb14:;
                                    {
                                        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                        uint8_t t46 = (uint8_t)(t45 == ((uint8_t)33ULL));
                                        if (!t46) {
                                            goto gm_lb15;
                                        }
                                        gm_b16_8 t47 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l5) + 16) + 8));
                                        gm_l8 = t47;
                                        goto gm_lb12;
                                    }
gm_lb15:;
                                    {
                                        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                        uint8_t t49 = (uint8_t)(t48 == ((uint8_t)36ULL));
                                        if (!t49) {
                                            goto gm_lb16;
                                        }
                                        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
                                        gm_l8 = t50;
                                        goto gm_lb12;
                                    }
gm_lb16:;
                                    goto gm_lb11;
                                }
gm_lb12:;
                                void * t51 = gm_l0;
                                gm_b16_8 t52 = gm_l8;
                                gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                                uint8_t t54 = gm_f_note_stores_ijl61ps((void *)NULL, cx, NULL, t51, t53);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t54) {
                                    gm_l2 = ((uint8_t)1);
                                }
                                goto gm_lb6;
                            }
gm_lb11:;
                            {
                                uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t56 = (uint8_t)(t55 == ((uint8_t)29ULL));
                                if (!t56) {
                                    goto gm_lb17;
                                }
                                gm_b16_8 t57 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l5) + 16) + 48));
                                gm_l12 = t57;
                                gm_b16_8 t58 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l5) + 16) + 64));
                                gm_l13 = t58;
                                void * t59 = gm_l0;
                                gm_b16_8 t60 = gm_l12;
                                gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                                uint8_t t62 = gm_f_note_stores_ijl61ps((void *)NULL, cx, NULL, t59, t61);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t62) {
                                    gm_l2 = ((uint8_t)1);
                                }
                                void * t63 = gm_l0;
                                gm_b16_8 t64 = gm_l13;
                                gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                                uint8_t t66 = gm_f_note_stores_ijl61ps((void *)NULL, cx, NULL, t63, t65);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t66) {
                                    gm_l2 = ((uint8_t)1);
                                }
                                goto gm_lb6;
                            }
gm_lb17:;
                            {
                                uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t68 = (uint8_t)(t67 == ((uint8_t)37ULL));
                                if (!t68) {
                                    goto gm_lb18;
                                }
                                gm_b16_8 t69 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l5) + 16) + 8));
                                gm_l14 = t69;
                                gm_b16_8 t70 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l5) + 16) + 96));
                                gm_l15 = t70;
                                void * t71 = gm_l0;
                                gm_b16_8 t72 = gm_l14;
                                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                                uint8_t t74 = gm_f_note_stores_ijl61ps((void *)NULL, cx, NULL, t71, t73);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t74) {
                                    gm_l2 = ((uint8_t)1);
                                }
                                void * t75 = gm_l0;
                                gm_b16_8 t76 = gm_l15;
                                gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                                uint8_t t78 = gm_f_note_stores_ijl61ps((void *)NULL, cx, NULL, t75, t77);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t78) {
                                    gm_l2 = ((uint8_t)1);
                                }
                                goto gm_lb6;
                            }
gm_lb18:;
                            {
                                goto gm_lb6;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_srcljn8aau5qd4v, 229, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcljn8aau5qd4v, 228, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t79 = gm_l4;
            uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
            uint64_t t82 = r80;
            gm_l4 = t82;
        }
    }
gm_lb1:;
    uint8_t t83 = gm_l2;
    F.ret = t83;
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
inline __attribute__((aligned(4))) uint8_t gm_f_plain_t32bme8(const void * a0, const void * a1, uint8_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_plain_t32bme8 F;
    gm_tm7ecnors0fq6 gm_l0; /* a */
    gm_tm7ecnors0fq6 gm_l1; /* b */
    uint8_t gm_l2; /* strict */
    uint64_t gm_l3; /* x */
    uint64_t gm_l4; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l2 = a2;
    uint8_t gm_l5;
    uint8_t gm_l6;
    {
        {
            uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t5 = (uint8_t)(t4 == ((uint8_t)1ULL));
            if (!t5) {
                goto gm_lb2;
            }
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
            gm_l3 = t6;
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)1ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
            gm_l4 = t9;
            uint8_t t10 = gm_l2;
            if (t10) {
                uint64_t t11 = gm_l3;
                uint64_t t12 = gm_l4;
                uint8_t t13 = (t11 < t12);
                gm_l5 = t13;
            } else {
                uint64_t t14 = gm_l3;
                uint64_t t15 = gm_l4;
                uint8_t t16 = (t14 <= t15);
                gm_l5 = t16;
            }
            uint8_t t17 = gm_l5;
            F.ret = t17;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    uint8_t t18 = gm_l2;
    uint8_t t19 = (uint8_t)!t18;
    if (t19) {
        uint8_t t20 = gm_f_same_pen5lq3(&(*(gm_tm7ecnors0fq6 *)((uint8_t *)a0)), &(*(gm_tm7ecnors0fq6 *)((uint8_t *)a1)));
        gm_l6 = t20;
    } else {
        gm_l6 = ((uint8_t)0);
    }
    uint8_t t21 = gm_l6;
    F.ret = t21;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_plain_t32bme8_c(gm_tm7ecnors0fq6 a0, gm_tm7ecnors0fq6 a1, uint8_t a2) { return gm_f_plain_t32bme8(&a0, &a1, a2); }
inline __attribute__((aligned(4))) uint8_t gm_f_proven_vei0gi0(void * a0, const void * a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_proven_vei0gi0 F;
    void * gm_l0; /* e */
    gm_tm7ecnors0fq6 gm_l3; /* i */
    gm_tm7ecnors0fq6 gm_l4; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 359u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l5;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
    gm_t88rcucg1b7uk t5 = GM_CONV(gm_t88rcucg1b7uk, gm_f_value_type_p69quvj(t4, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)a1))));
    uint8_t t6 = gm_f_is_unsigned_12b42v0(t2, GM_CONV(gm_csguankko4lmhd, t5));
    uint8_t t7 = (uint8_t)!t6;
    if (t7) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t8 = gm_l0;
    gm_t7b7esi3n9r90 t9 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)a1));
    gm_f_sym_of_q0v11rf((void *)&gm_l3, t8, &t9);
    void * t10 = gm_l0;
    gm_t7b7esi3n9r90 t11 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)a2));
    gm_f_sym_of_q0v11rf((void *)&gm_l4, t10, &t11);
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
    uint8_t t13 = (uint8_t)((t12 == ((uint8_t)0ULL)));
    gm_tm7ecnors0fq6 t14 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)&gm_l3));
    gm_tm7ecnors0fq6 t15;
    GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t15 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t15 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t15 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)0ULL));
    if (t13) {
        gm_l5 = ((uint8_t)1);
    } else {
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
        uint8_t t17 = (uint8_t)((t16 == ((uint8_t)0ULL)));
        gm_tm7ecnors0fq6 t18 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)&gm_l4));
        gm_tm7ecnors0fq6 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t19 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t19 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t19 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
        gm_l5 = t17;
    }
    uint8_t t20 = gm_l5;
    if (t20) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t21 = gm_l0;
    uint8_t t22 = gm_f_holds_qjf219d(t21, &(*(gm_tm7ecnors0fq6 *)((uint8_t *)&gm_l3)), &(*(gm_tm7ecnors0fq6 *)((uint8_t *)&gm_l4)), ((uint8_t)1));
    F.ret = t22;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_proven_vei0gi0_c(void * a0, gm_t7b7esi3n9r90 a1, gm_t7b7esi3n9r90 a2) { return gm_f_proven_vei0gi0(a0, &a1, &a2); }
inline __attribute__((aligned(4))) uint8_t gm_f_same_pen5lq3(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_pen5lq3 F;
    gm_tm7ecnors0fq6 gm_l0; /* a */
    gm_tm7ecnors0fq6 gm_l1; /* b */
    uint64_t gm_l2; /* x */
    uint64_t gm_l3; /* y */
    uint32_t gm_l4; /* l */
    uint32_t gm_l5; /* g */
    uint64_t gm_l6; /* o */
    gm_t88rcucg1b7uk gm_l7; /* t */
    uint32_t gm_l8; /* l2 */
    uint32_t gm_l9; /* g2 */
    uint64_t gm_l10; /* o2 */
    gm_t88rcucg1b7uk gm_l11; /* t2 */
    GM_STACK_CHECK_SHALLOW(&F, 261u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            gm_l12 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)1ULL));
            if (!t13) {
                goto gm_lb3;
            }
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
            gm_l2 = t14;
            {
                {
                    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t16 = (uint8_t)(t15 == ((uint8_t)1ULL));
                    if (!t16) {
                        goto gm_lb5;
                    }
                    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
                    gm_l3 = t17;
                    uint64_t t18 = gm_l2;
                    uint64_t t19 = gm_l3;
                    uint8_t t20 = ((t18 == t19));
                    gm_l13 = t20;
                    goto gm_lb4;
                }
gm_lb5:;
                gm_l13 = ((uint8_t)0);
            }
gm_lb4:;
            uint8_t t21 = gm_l13;
            gm_l12 = t21;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)2ULL));
            if (!t23) {
                goto gm_lb6;
            }
            uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)a0) + 8));
            gm_l4 = t24;
            uint32_t t25 = GM_LD(uint32_t, ((((uint8_t *)a0) + 8) + 4));
            gm_l5 = t25;
            uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
            gm_l6 = t26;
            gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)a0) + 8) + 16));
            gm_l7 = t27;
            {
                {
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)2ULL));
                    if (!t29) {
                        goto gm_lb8;
                    }
                    uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)a1) + 8));
                    gm_l8 = t30;
                    uint32_t t31 = GM_LD(uint32_t, ((((uint8_t *)a1) + 8) + 4));
                    gm_l9 = t31;
                    uint64_t t32 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 8));
                    gm_l10 = t32;
                    gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)a1) + 8) + 16));
                    gm_l11 = t33;
                    uint32_t t34 = gm_l4;
                    uint32_t t35 = gm_l8;
                    uint8_t t36 = ((t34 == t35));
                    if (t36) {
                        uint32_t t37 = gm_l5;
                        uint32_t t38 = gm_l9;
                        uint8_t t39 = ((t37 == t38));
                        gm_l17 = t39;
                    } else {
                        gm_l17 = ((uint8_t)0);
                    }
                    uint8_t t40 = gm_l17;
                    if (t40) {
                        uint64_t t41 = gm_l6;
                        uint64_t t42 = gm_l10;
                        uint8_t t43 = ((t41 == t42));
                        gm_l16 = t43;
                    } else {
                        gm_l16 = ((uint8_t)0);
                    }
                    uint8_t t44 = gm_l16;
                    if (t44) {
                        gm_t88rcucg1b7uk t45 = gm_l7;
                        gm_t88rcucg1b7uk t46 = gm_l11;
                        uint8_t t47 = (gm_eq9m6mvtmgde(((uint8_t *)&t45), ((uint8_t *)&t46)));
                        gm_l15 = t47;
                    } else {
                        gm_l15 = ((uint8_t)0);
                    }
                    uint8_t t48 = gm_l15;
                    gm_l14 = t48;
                    goto gm_lb7;
                }
gm_lb8:;
                gm_l14 = ((uint8_t)0);
            }
gm_lb7:;
            uint8_t t49 = gm_l14;
            gm_l12 = t49;
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcljn8aau5qd4v, 75, 34);
    }
gm_lb1:;
    uint8_t t50 = gm_l12;
    F.ret = t50;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_same_pen5lq3_c(gm_tm7ecnors0fq6 a0, gm_tm7ecnors0fq6 a1) { return gm_f_same_pen5lq3(&a0, &a1); }
__attribute__((aligned(4))) uint8_t gm_f_step_6i5283s(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_step_6i5283s F;
    void * gm_l0; /* e */
    void * gm_l1; /* inst */
    uint32_t gm_l5; /* d */
    uint32_t gm_l7; /* d */
    uint32_t gm_l9; /* d */
    gm_ts6oqppjh0ba3 gm_l10; /* op */
    gm_tm7ecnors0fq6 gm_l13; /* x */
    gm_tm7ecnors0fq6 gm_l14; /* y */
    uint32_t gm_l18; /* l */
    uint8_t gm_l20; /* negated */
    gm_b16_8 gm_l22; /* then */
    gm_b16_8 gm_l23; /* else_ */
    gm_b16_8 gm_l24; /* a */
    gm_b16_8 gm_l25; /* b */
    uint32_t gm_l26; /* l */
    gm_b16_8 gm_l27; /* body */
    uint32_t gm_l28; /* l */
    gm_b16_8 gm_l29; /* body */
    uint64_t gm_l30; /* log */
    gm_b16_8 gm_l31; /* body */
    uint32_t gm_l32; /* l */
    uint32_t gm_l33; /* tl */
    gm_b16_8 gm_l34; /* install */
    gm_b16_8 gm_l36; /* body */
    uint64_t gm_l37; /* log */
    gm_b16_8 gm_l38; /* a */
    gm_b16_8 gm_l39; /* b */
    gm_b16_8 gm_l40; /* body */
    GM_STACK_CHECK(&F, 4115u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tjdjicdhiqk6f gm_l41;
    gm_tftb1odc739t5 gm_l42;
    {
        void * t29 = gm_l1;
        {
            {
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)26ULL));
                    if (!t31) {
                        goto gm_lb2;
                    }
                    memmove(((uint8_t *)&F.l2), (((uint8_t *)t29) + 16), 192);
                    {
                        {
                            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)0ULL));
                            if (!t33) {
                                goto gm_lb4;
                            }
                            memmove(((uint8_t *)&F.l3), (((uint8_t *)&F.l2) + 16), 48);
                            memmove(((uint8_t *)&F.l4), ((((uint8_t *)&F.l2) + 16) + 48), 48);
                            void * t34 = gm_l0;
                            uint8_t t35 = gm_f_proven_vei0gi0(t34, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l3)), &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l4)));
                            if (!t35) {
                                goto gm_lb4;
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
                {
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)0ULL));
                    if (!t37) {
                        goto gm_lb5;
                    }
                    uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)t29) + 16));
                    gm_l5 = t38;
                    memmove(((uint8_t *)&F.l6), ((((uint8_t *)t29) + 16) + 16), 48);
                    void * t39 = gm_l0;
                    void * t40 = GM_LD(void *, (((uint8_t *)t39) + 16));
                    uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t39) + 16) + 8));
                    uint32_t t42 = gm_l5;
                    uint64_t t43 = (uint64_t)t42;
                    if ((uintptr_t)t43 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t43, t41, gm_srcljn8aau5qd4v, 260, 26);
                    void * t44 = gm_l0;
                    gm_tm7ecnors0fq6 t45;
                    gm_f_sym_of_q0v11rf((void *)&t45, t44, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l6)));
                    GM_ST(gm_tm7ecnors0fq6, (((uint8_t *)t40) + (uintptr_t)t43 * 32), t45);
                    goto gm_lb1;
                }
gm_lb5:;
                {
                    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)1ULL));
                    if (!t47) {
                        goto gm_lb6;
                    }
                    uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)t29) + 16));
                    gm_l7 = t48;
                    memmove(((uint8_t *)&F.l8), ((((uint8_t *)t29) + 16) + 8), 56);
                    void * t49 = gm_l0;
                    void * t50 = GM_LD(void *, (((uint8_t *)t49) + 16));
                    uint64_t t51 = GM_LD(uint64_t, ((((uint8_t *)t49) + 16) + 8));
                    uint32_t t52 = gm_l7;
                    uint64_t t53 = (uint64_t)t52;
                    if ((uintptr_t)t53 >= (uintptr_t)(t51)) gm_trap_index((uintptr_t)t53, t51, gm_srcljn8aau5qd4v, 261, 26);
                    void * t54 = gm_l0;
                    void * t55 = (void *)((uint8_t *)&F.l8);
                    void * t56 = gm_l0;
                    void * t57 = GM_LD(void *, (((uint8_t *)t56) + 8));
                    void * t58 = GM_LD(void *, (((uint8_t *)t57) + 24));
                    uint64_t t59 = GM_LD(uint64_t, ((((uint8_t *)t57) + 24) + 8));
                    uint32_t t60 = gm_l7;
                    uint64_t t61 = (uint64_t)t60;
                    if ((uintptr_t)t61 >= (uintptr_t)(t59)) gm_trap_index((uintptr_t)t61, t59, gm_srcljn8aau5qd4v, 261, 59);
                    gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t58) + (uintptr_t)t61 * 4));
                    gm_tm7ecnors0fq6 t63;
                    gm_f_load_sym_cf03dbc((void *)&t63, t54, t55, GM_CONV(gm_csguankko4lmhd, t62));
                    GM_ST(gm_tm7ecnors0fq6, (((uint8_t *)t50) + (uintptr_t)t53 * 32), t63);
                    goto gm_lb1;
                }
gm_lb6:;
            }
            {
                {
                    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                    uint8_t t65 = (uint8_t)(t64 == ((uint8_t)3ULL));
                    if (!t65) {
                        goto gm_lb7;
                    }
                    uint32_t t66 = GM_LD(uint32_t, (((uint8_t *)t29) + 16));
                    gm_l9 = t66;
                    gm_ts6oqppjh0ba3 t67 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t29) + 16) + 4));
                    gm_l10 = t67;
                    memmove(((uint8_t *)&F.l11), ((((uint8_t *)t29) + 16) + 16), 48);
                    memmove(((uint8_t *)&F.l12), ((((uint8_t *)t29) + 16) + 64), 48);
                    void * t68 = gm_l0;
                    void * t69 = gm_l0;
                    void * t70 = GM_LD(void *, (((uint8_t *)t69) + 8));
                    gm_t88rcucg1b7uk t71 = GM_CONV(gm_t88rcucg1b7uk, gm_f_value_type_p69quvj(t70, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l11))));
                    uint8_t t72 = gm_f_is_unsigned_12b42v0(t68, GM_CONV(gm_csguankko4lmhd, t71));
                    if (t72) {
                        void * t73 = gm_l0;
                        gm_t7b7esi3n9r90 t74 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l11));
                        gm_f_sym_of_q0v11rf((void *)&gm_l13, t73, &t74);
                        void * t75 = gm_l0;
                        gm_t7b7esi3n9r90 t76 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l12));
                        gm_f_sym_of_q0v11rf((void *)&gm_l14, t75, &t76);
                        {
                            {
                                uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                                uint8_t t78 = (uint8_t)(!(t77 == ((uint8_t)0ULL)));
                                if (!t78) {
                                    goto gm_lb9;
                                }
                                uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                                uint8_t t80 = (uint8_t)(!(t79 == ((uint8_t)0ULL)));
                                if (!t80) {
                                    goto gm_lb9;
                                }
                                gm_tm7ecnors0fq6 t81 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)&gm_l13));
                                gm_tm7ecnors0fq6 t82;
                                GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t82 + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&t82 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t82 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)0ULL));
                                gm_tm7ecnors0fq6 t83 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)&gm_l14));
                                gm_tm7ecnors0fq6 t84;
                                GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t84 + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&t84 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t84 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)0ULL));
                                {
                                    {
                                        gm_ts6oqppjh0ba3 t85 = gm_l10;
                                        gm_tm7ecnors0fq6 t86 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)&gm_l13));
                                        gm_tm7ecnors0fq6 t87 = GM_LD(gm_tm7ecnors0fq6, ((uint8_t *)&gm_l14));
                                        gm_f_comparison_c2aetd2((void *)&gm_l41, t85, &t86, &t87);
                                        uint8_t t88 = (uint8_t)((((uint8_t *)&gm_l41))[72] != 0);
                                        if (!t88) {
                                            goto gm_lb11;
                                        }
                                        memmove(((uint8_t *)&F.l15), ((uint8_t *)&gm_l41), 72);
                                        void * t89 = gm_l0;
                                        void * t90 = (void *)(((uint8_t *)t89) + 32);
                                        uint32_t t91 = gm_l9;
                                        gm_tv1gaqputuep6 t92 = GM_LD(gm_tv1gaqputuep6, ((uint8_t *)&F.l15));
                                        gm_t2q86r30n2ngm t93;
                                        GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
                                        GM_ST(uint32_t, ((uint8_t *)&t93 + 0), t91);
                                        GM_ST(gm_tv1gaqputuep6, ((uint8_t *)&t93 + 8), t92);
                                        gm_tftb1odc739t5 t94 = gm_f_push_rcevpkn(t90, &t93);
                                        gm_l42 = t94;
                                        uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                                        if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_srcljn8aau5qd4v, 266, 48);
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                }
gm_lb10:;
                                goto gm_lb8;
                            }
gm_lb9:;
                        }
gm_lb8:;
                    }
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                    uint8_t t97 = (uint8_t)(t96 == ((uint8_t)25ULL));
                    if (!t97) {
                        goto gm_lb12;
                    }
                    memmove(((uint8_t *)&F.l16), (((uint8_t *)t29) + 16), 56);
                    memmove(((uint8_t *)&F.l17), ((((uint8_t *)t29) + 16) + 64), 48);
                    {
                        {
                            uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                            uint8_t t99 = (uint8_t)(t98 == ((uint8_t)0ULL));
                            if (!t99) {
                                goto gm_lb14;
                            }
                            uint32_t t100 = GM_LD(uint32_t, (((uint8_t *)&F.l16) + 8));
                            gm_l18 = t100;
                            void * t101 = gm_l0;
                            uint32_t t102 = gm_l18;
                            void * t103 = gm_l0;
                            gm_tm7ecnors0fq6 t104;
                            gm_f_sym_of_q0v11rf((void *)&t104, t103, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l17)));
                            uint64_t t105 = GM_LD(uint64_t, ((((uint8_t *)&F.l16) + 40) + 8));
                            uint8_t t106 = ((t105 == ((uint64_t)0ULL)));
                            gm_b0_1 t107 = gm_f_stored_9gdkkdh((void *)NULL, cx, NULL, t101, t102, &t104, t106);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb13;
                        }
gm_lb14:;
                    }
gm_lb13:;
                    goto gm_lb1;
                }
gm_lb12:;
            }
            {
                {
                    uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                    uint8_t t109 = (uint8_t)(t108 == ((uint8_t)31ULL));
                    if (!t109) {
                        goto gm_lb15;
                    }
                    memmove(((uint8_t *)&F.l19), (((uint8_t *)t29) + 16), 48);
                    uint8_t t110 = GM_LD(uint8_t, ((((uint8_t *)t29) + 16) + 48));
                    gm_l20 = t110;
                    void * t111 = gm_l0;
                    uint8_t t112 = gm_l20;
                    gm_b0_1 t113 = gm_f_learn_bg3djpd((void *)NULL, cx, NULL, t111, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l19)), t112);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                    uint8_t t115 = (uint8_t)(t114 == ((uint8_t)29ULL));
                    if (!t115) {
                        goto gm_lb16;
                    }
                    memmove(((uint8_t *)&F.l21), (((uint8_t *)t29) + 16), 48);
                    gm_b16_8 t116 = GM_LD(gm_b16_8, ((((uint8_t *)t29) + 16) + 48));
                    gm_l22 = t116;
                    gm_b16_8 t117 = GM_LD(gm_b16_8, ((((uint8_t *)t29) + 16) + 64));
                    gm_l23 = t117;
                    void * t118 = gm_l0;
                    gm_b16_8 t119 = gm_l22;
                    gm_t7b7esi3n9r90 t120 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l21));
                    gm_tf8kfhcnval78 t121;
                    GM_ST(uint8_t, (uint8_t *)&t121 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t121 + 50, 0);
                    GM_ST(uint32_t, (uint8_t *)&t121 + 52, 0);
                    GM_ST(uint64_t, (uint8_t *)&t121 + 56, 0);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t121), t120);
                    ((uint8_t *)&t121)[48] = 1;
                    gm_b16_8 t122 = gm_f_nested_j61tpae((void *)NULL, cx, NULL, t118, t119, &t121, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l24 = t122;
                    void * t123 = gm_l0;
                    gm_b16_8 t124 = gm_l23;
                    gm_t7b7esi3n9r90 t125 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l21));
                    gm_tf8kfhcnval78 t126;
                    GM_ST(uint8_t, (uint8_t *)&t126 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t126 + 50, 0);
                    GM_ST(uint32_t, (uint8_t *)&t126 + 52, 0);
                    GM_ST(uint64_t, (uint8_t *)&t126 + 56, 0);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t126), t125);
                    ((uint8_t *)&t126)[48] = 1;
                    gm_b16_8 t127 = gm_f_nested_j61tpae((void *)NULL, cx, NULL, t123, t124, &t126, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l25 = t127;
                    void * t128 = gm_l1;
                    gm_b16_8 t129 = gm_l24;
                    gm_b16_8 t130 = gm_l25;
                    gm_t7b7esi3n9r90 t131 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l21));
                    gm_t1s3s9ds8udn4 t132;
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t132 + 0), t131);
                    GM_ST(gm_b16_8, ((uint8_t *)&t132 + 48), t129);
                    GM_ST(gm_b16_8, ((uint8_t *)&t132 + 64), t130);
                    gm_th7badf4b6ee3 t133;
                    memset(&t133, 0, sizeof t133);
                    GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)29ULL));
                    GM_ST(gm_t1s3s9ds8udn4, ((uint8_t *)&t133 + 16), t132);
                    GM_ST(gm_th7badf4b6ee3, ((uint8_t *)t128), t133);
                    {
                        {
                            gm_b16_8 t134 = gm_l24;
                            gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                            uint8_t t136 = gm_f_leaves_2uhc28a(t135);
                            if (!t136) {
                                goto gm_lb18;
                            }
                            gm_b16_8 t137 = gm_l25;
                            gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
                            uint8_t t139 = gm_f_leaves_2uhc28a(t138);
                            uint8_t t140 = (uint8_t)!t139;
                            if (!t140) {
                                goto gm_lb18;
                            }
                            void * t141 = gm_l0;
                            gm_b0_1 t142 = gm_f_learn_bg3djpd((void *)NULL, cx, NULL, t141, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l21)), ((uint8_t)0));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb17;
                        }
gm_lb18:;
                    }
gm_lb17:;
                    {
                        {
                            gm_b16_8 t143 = gm_l25;
                            gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                            uint8_t t145 = gm_f_leaves_2uhc28a(t144);
                            if (!t145) {
                                goto gm_lb20;
                            }
                            gm_b16_8 t146 = gm_l24;
                            gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
                            uint8_t t148 = gm_f_leaves_2uhc28a(t147);
                            uint8_t t149 = (uint8_t)!t148;
                            if (!t149) {
                                goto gm_lb20;
                            }
                            void * t150 = gm_l0;
                            gm_b0_1 t151 = gm_f_learn_bg3djpd((void *)NULL, cx, NULL, t150, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l21)), ((uint8_t)1));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb19;
                        }
gm_lb20:;
                    }
gm_lb19:;
                    goto gm_lb1;
                }
gm_lb16:;
            }
            {
                {
                    uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                    uint8_t t153 = (uint8_t)(t152 == ((uint8_t)27ULL));
                    if (!t153) {
                        goto gm_lb21;
                    }
                    uint32_t t154 = GM_LD(uint32_t, (((uint8_t *)t29) + 16));
                    gm_l26 = t154;
                    gm_b16_8 t155 = GM_LD(gm_b16_8, ((((uint8_t *)t29) + 16) + 8));
                    gm_l27 = t155;
                    void * t156 = gm_l1;
                    uint32_t t157 = gm_l26;
                    void * t158 = gm_l0;
                    gm_b16_8 t159 = gm_l27;
                    gm_tf8kfhcnval78 t160;
                    memset(&t160, 0, sizeof t160);
                    gm_b16_8 t161 = gm_f_nested_j61tpae((void *)NULL, cx, NULL, t158, t159, &t160, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tiilmfd16v3fc t162;
                    GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t162 + 0), t157);
                    GM_ST(gm_b16_8, ((uint8_t *)&t162 + 8), t161);
                    gm_th7badf4b6ee3 t163;
                    memset(&t163, 0, sizeof t163);
                    GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)27ULL));
                    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t163 + 16), t162);
                    GM_ST(gm_th7badf4b6ee3, ((uint8_t *)t156), t163);
                    goto gm_lb1;
                }
gm_lb21:;
                {
                    uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                    uint8_t t165 = (uint8_t)(t164 == ((uint8_t)28ULL));
                    if (!t165) {
                        goto gm_lb22;
                    }
                    uint32_t t166 = GM_LD(uint32_t, (((uint8_t *)t29) + 16));
                    gm_l28 = t166;
                    gm_b16_8 t167 = GM_LD(gm_b16_8, ((((uint8_t *)t29) + 16) + 8));
                    gm_l29 = t167;
                    void * t168 = gm_l0;
                    uint64_t t169 = GM_LD(uint64_t, ((((uint8_t *)t168) + 144) + 8));
                    gm_l30 = t169;
                    void * t170 = gm_l0;
                    gm_b16_8 t171 = gm_l29;
                    gm_b16_8 t172 = GM_LD(gm_b16_8, ((uint8_t *)&t171));
                    uint8_t t173 = gm_f_note_stores_ijl61ps((void *)NULL, cx, NULL, t170, t172);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t173) {
                        void * t174 = gm_l0;
                        gm_f_havoc_all_psqvvsq(t174);
                    }
                    void * t175 = gm_l1;
                    uint32_t t176 = gm_l28;
                    void * t177 = gm_l0;
                    gm_b16_8 t178 = gm_l29;
                    gm_tf8kfhcnval78 t179;
                    memset(&t179, 0, sizeof t179);
                    gm_b16_8 t180 = gm_f_nested_j61tpae((void *)NULL, cx, NULL, t177, t178, &t179, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tiilmfd16v3fc t181;
                    GM_ST(uint32_t, (uint8_t *)&t181 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t181 + 0), t176);
                    GM_ST(gm_b16_8, ((uint8_t *)&t181 + 8), t180);
                    gm_th7badf4b6ee3 t182;
                    memset(&t182, 0, sizeof t182);
                    GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)28ULL));
                    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t182 + 16), t181);
                    GM_ST(gm_th7badf4b6ee3, ((uint8_t *)t175), t182);
                    void * t183 = gm_l0;
                    uint64_t t184 = gm_l30;
                    gm_f_forget_since_3v2e6hi(t183, t184);
                    goto gm_lb1;
                }
gm_lb22:;
                {
                    uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                    uint8_t t186 = (uint8_t)(t185 == ((uint8_t)36ULL));
                    if (!t186) {
                        goto gm_lb23;
                    }
                    gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)t29) + 16));
                    gm_l31 = t187;
                    void * t188 = gm_l1;
                    void * t189 = gm_l0;
                    gm_b16_8 t190 = gm_l31;
                    gm_tf8kfhcnval78 t191;
                    memset(&t191, 0, sizeof t191);
                    gm_b16_8 t192 = gm_f_nested_j61tpae((void *)NULL, cx, NULL, t189, t190, &t191, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_th7badf4b6ee3 t193;
                    memset(&t193, 0, sizeof t193);
                    GM_ST(uint8_t, ((uint8_t *)&t193 + 0), ((uint8_t)36ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t193 + 16), t192);
                    GM_ST(gm_th7badf4b6ee3, ((uint8_t *)t188), t193);
                    goto gm_lb1;
                }
gm_lb23:;
                {
                    uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                    uint8_t t195 = (uint8_t)(t194 == ((uint8_t)37ULL));
                    if (!t195) {
                        goto gm_lb24;
                    }
                    uint32_t t196 = GM_LD(uint32_t, (((uint8_t *)t29) + 16));
                    gm_l32 = t196;
                    uint32_t t197 = GM_LD(uint32_t, ((((uint8_t *)t29) + 16) + 4));
                    gm_l33 = t197;
                    gm_b16_8 t198 = GM_LD(gm_b16_8, ((((uint8_t *)t29) + 16) + 8));
                    gm_l34 = t198;
                    uint8_t t199 = (uint8_t)((((((uint8_t *)t29) + 16) + 32))[48] != 0);
                    if (!t199) {
                        goto gm_lb24;
                    }
                    memmove(((uint8_t *)&F.l35), ((((uint8_t *)t29) + 16) + 32), 48);
                    gm_b16_8 t200 = GM_LD(gm_b16_8, ((((uint8_t *)t29) + 16) + 96));
                    gm_l36 = t200;
                    void * t201 = gm_l0;
                    uint64_t t202 = GM_LD(uint64_t, ((((uint8_t *)t201) + 144) + 8));
                    gm_l37 = t202;
                    void * t203 = gm_l0;
                    gm_b16_8 t204 = gm_l34;
                    gm_tf8kfhcnval78 t205;
                    memset(&t205, 0, sizeof t205);
                    gm_b16_8 t206 = gm_f_nested_j61tpae((void *)NULL, cx, NULL, t203, t204, &t205, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l38 = t206;
                    void * t207 = gm_l0;
                    gm_b16_8 t208 = gm_l36;
                    gm_tf8kfhcnval78 t209;
                    memset(&t209, 0, sizeof t209);
                    gm_b16_8 t210 = gm_f_nested_j61tpae((void *)NULL, cx, NULL, t207, t208, &t209, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l39 = t210;
                    void * t211 = gm_l1;
                    uint32_t t212 = gm_l32;
                    uint32_t t213 = gm_l33;
                    gm_b16_8 t214 = gm_l38;
                    gm_t7b7esi3n9r90 t215 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l35));
                    gm_tf8kfhcnval78 t216;
                    GM_ST(uint8_t, (uint8_t *)&t216 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t216 + 50, 0);
                    GM_ST(uint32_t, (uint8_t *)&t216 + 52, 0);
                    GM_ST(uint64_t, (uint8_t *)&t216 + 56, 0);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t216), t215);
                    ((uint8_t *)&t216)[48] = 1;
                    gm_b16_8 t217 = gm_l39;
                    gm_tvdggnhat0k99 t218;
                    GM_ST(uint64_t, (uint8_t *)&t218 + 24, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t218 + 0), t212);
                    GM_ST(uint32_t, ((uint8_t *)&t218 + 4), t213);
                    GM_ST(gm_b16_8, ((uint8_t *)&t218 + 8), t214);
                    GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t218 + 32), t216);
                    GM_ST(gm_b16_8, ((uint8_t *)&t218 + 96), t217);
                    gm_th7badf4b6ee3 t219;
                    memset(&t219, 0, sizeof t219);
                    GM_ST(uint8_t, ((uint8_t *)&t219 + 0), ((uint8_t)37ULL));
                    GM_ST(gm_tvdggnhat0k99, ((uint8_t *)&t219 + 16), t218);
                    GM_ST(gm_th7badf4b6ee3, ((uint8_t *)t211), t219);
                    void * t220 = gm_l0;
                    uint64_t t221 = gm_l37;
                    gm_f_forget_since_3v2e6hi(t220, t221);
                    goto gm_lb1;
                }
gm_lb24:;
            }
            {
                {
                    uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                    uint8_t t223 = (uint8_t)(t222 == ((uint8_t)33ULL));
                    if (!t223) {
                        goto gm_lb25;
                    }
                    gm_b16_8 t224 = GM_LD(gm_b16_8, ((((uint8_t *)t29) + 16) + 8));
                    gm_l40 = t224;
                    void * t225 = gm_l0;
                    gm_b16_8 t226 = gm_l40;
                    gm_b16_8 t227 = GM_LD(gm_b16_8, ((uint8_t *)&t226));
                    uint8_t t228 = gm_f_note_stores_ijl61ps((void *)NULL, cx, NULL, t225, t227);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t228) {
                        void * t229 = gm_l0;
                        gm_f_havoc_all_psqvvsq(t229);
                    }
                    goto gm_lb1;
                }
gm_lb25:;
                {
                    uint8_t t230 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                    uint8_t t231 = (uint8_t)(t230 == ((uint8_t)44ULL));
                    if (!t231) {
                        goto gm_lb26;
                    }
                    void * t232 = gm_l0;
                    gm_f_havoc_all_psqvvsq(t232);
                    goto gm_lb1;
                }
gm_lb26:;
                {
                    goto gm_lb1;
                }
gm_lb27:;
                gm_trap("no match arm matched the scrutinee", gm_srcljn8aau5qd4v, 258, 5);
            }
        }
gm_lb1:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_stored_9gdkkdh(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2, uint8_t a3) {
    struct gm_fr_stored_9gdkkdh F;
    void * gm_l0; /* e */
    uint32_t gm_l1; /* l */
    gm_tm7ecnors0fq6 gm_l2; /* v */
    uint8_t gm_l3; /* whole */
    GM_STACK_CHECK(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l4;
    void * t2 = gm_l0;
    uint32_t t3 = gm_l1;
    uint8_t t4 = gm_f_followed_f4uav3s(t2, t3);
    uint8_t t5 = (uint8_t)!t4;
    if (t5) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t6 = gm_l0;
    uint32_t t7 = gm_l1;
    gm_f_havoc_fq0gr83(t6, t7);
    uint8_t t8 = gm_l3;
    if (t8) {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 88));
        uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t9) + 88) + 8));
        uint32_t t12 = gm_l1;
        uint64_t t13 = (uint64_t)t12;
        if ((uintptr_t)t13 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t13, t11, gm_srcljn8aau5qd4v, 220, 16);
        memmove((((uint8_t *)t10) + (uintptr_t)t13 * 32), ((uint8_t *)&gm_l2), 32);
    }
    void * t14 = gm_l0;
    void * t15 = (void *)(((uint8_t *)t14) + 144);
    uint32_t t16 = gm_l1;
    gm_tftb1odc739t5 t17 = gm_f_push_im9d4ro(t15, t16);
    gm_l4 = t17;
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcljn8aau5qd4v, 221, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sym_of_q0v11rf(void *gm_out, void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sym_of_q0v11rf F;
    void * gm_l0; /* e */
    uint32_t gm_l2; /* t */
    uint64_t gm_l3; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tm7ecnors0fq6 gm_l4;
    gm_tm7ecnors0fq6 gm_l5;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)0ULL));
            if (!t7) {
                goto gm_lb2;
            }
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
            gm_l2 = t8;
            uint32_t t9 = gm_l2;
            uint64_t t10 = (uint64_t)t9;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t11) + 16) + 8));
            uint8_t t13 = (t10 < t12);
            if (t13) {
                void * t14 = gm_l0;
                void * t15 = GM_LD(void *, (((uint8_t *)t14) + 16));
                uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 16) + 8));
                uint32_t t17 = gm_l2;
                uint64_t t18 = (uint64_t)t17;
                if ((uintptr_t)t18 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t18, t16, gm_srcljn8aau5qd4v, 101, 43);
                memmove(((uint8_t *)&gm_l5), (((uint8_t *)t15) + (uintptr_t)t18 * 32), 32);
            } else {
                gm_tm7ecnors0fq6 t19;
                GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t19 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t19 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t19 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
                gm_l5 = t19;
            }
            memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l5), 32);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)1ULL));
            if (!t21) {
                goto gm_lb3;
            }
            uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)a1) + 16) + 8));
            gm_l3 = t22;
            uint64_t t23 = gm_l3;
            gm_tm7ecnors0fq6 t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t24 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t24 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)1ULL));
            GM_ST(uint64_t, ((uint8_t *)&t24 + 8), t23);
            gm_l4 = t24;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_tm7ecnors0fq6 t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t25 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t25 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t25 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            gm_l4 = t25;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcljn8aau5qd4v, 100, 46);
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&gm_l4), 32);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tm7ecnors0fq6 gm_f_sym_of_q0v11rf_c(void * a0, gm_t7b7esi3n9r90 a1) { gm_tm7ecnors0fq6 gm_r; gm_f_sym_of_q0v11rf(&gm_r, a0, &a1); return gm_r; }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_walk_i8nc7n2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_walk_i8nc7n2 F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l1; /* insts */
    uint64_t gm_l2; /* mark */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 395u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 104) + 8));
    gm_l2 = t8;
    gm_l3 = ((uint64_t)0ULL);
    gm_l5 = ((uint64_t)0ULL);
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    gm_l4 = t9;
    {
        for (;;) {
            uint64_t t10 = gm_l5;
            uint64_t t11 = gm_l4;
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t13 = gm_l5;
                        gm_l6 = t13;
                        void * t14 = gm_l0;
                        void * t15 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t17 = gm_l6;
                        void * t18 = (void *)(((uint8_t *)t15) + (uintptr_t)t17 * 224);
                        uint8_t t19 = gm_f_step_6i5283s((void *)NULL, cx, NULL, t14, t18);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (t19) {
                            uint64_t t20 = gm_l3;
                            uint64_t t21 = gm_l6;
                            uint8_t t22 = (!(t20 == t21));
                            if (t22) {
                                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                uint64_t t25 = gm_l3;
                                if ((uintptr_t)t25 >= (uintptr_t)(t24)) gm_trap_index((uintptr_t)t25, t24, gm_srcljn8aau5qd4v, 189, 29);
                                void * t26 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                uint64_t t28 = gm_l6;
                                memmove((((uint8_t *)t23) + (uintptr_t)t25 * 224), (((uint8_t *)t26) + (uintptr_t)t28 * 224), 224);
                            }
                            uint64_t t29 = gm_l3;
                            uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                            if (o31) gm_trap("integer overflow in `+`", gm_srcljn8aau5qd4v, 190, 13);
                            uint64_t t32 = r30;
                            gm_l3 = t32;
                        } else {
                            void * t33 = gm_l0;
                            void * t34 = gm_l0;
                            uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)t34) + 188));
                            uint32_t r36; bool o37 = __builtin_add_overflow(t35, ((uint32_t)1ULL), &r36);
                            if (o37) gm_trap("integer overflow in `+`", gm_srcljn8aau5qd4v, 192, 13);
                            uint32_t t38 = r36;
                            GM_ST(uint32_t, (((uint8_t *)t33) + 188), t38);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcljn8aau5qd4v, 187, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t39 = gm_l5;
            uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
            uint64_t t42 = r40;
            gm_l5 = t42;
        }
    }
gm_lb1:;
    void * t43 = gm_l0;
    void * t44 = (void *)(((uint8_t *)t43) + 104);
    uint64_t t45 = gm_l2;
    gm_f_truncate_p9n7n5b(t44, t45);
    void * t46 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t48 = gm_l3;
    uint64_t t49 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t50 = (uintptr_t)t48;
    if (t49 > t50 || t50 > (uintptr_t)t47) gm_trap_slice(t49, t50, t47, gm_srcljn8aau5qd4v, 196, 12);
    uint64_t t51 = ((uintptr_t)t48) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t52;
    GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t46 + (uintptr_t)((uint64_t)0ULL) * 224)));
    GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(t51));
    F.ret = t52;
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
