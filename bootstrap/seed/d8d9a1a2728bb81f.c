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
typedef struct gm_cskjv6t053tkje { uint32_t f0; uint64_t f1; } gm_cskjv6t053tkje;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; } gm_t2abnvcnt53ot;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; } f1; } gm_t8c5h7u09n10a;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t f6; uint8_t f7; uint8_t f8; uint8_t f9; uint8_t f10; uint8_t f11; uint8_t f12; uint8_t f13; uint8_t f14; uint8_t f15; uint8_t f16; uint8_t f17; } gm_ta72bg8o6meed;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_cskjv6t053tkje) == 16 && _Alignof(gm_cskjv6t053tkje) == 8 && offsetof(gm_cskjv6t053tkje, f0) == 0 && offsetof(gm_cskjv6t053tkje, f1) == 8, "layout of `Char`");
_Static_assert(sizeof(gm_t2abnvcnt53ot) == 4 && _Alignof(gm_t2abnvcnt53ot) == 1 && offsetof(gm_t2abnvcnt53ot, f0) == 0 && offsetof(gm_t2abnvcnt53ot, f1) == 1 && offsetof(gm_t2abnvcnt53ot, f2) == 2 && offsetof(gm_t2abnvcnt53ot, f3) == 3, "layout of `(u8, u8, u8, u8)`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t f6; uint8_t f7; uint8_t pe[3]; } gm_t88jm1dgsmkjs;
_Static_assert(sizeof(gm_t8c5h7u09n10a) == 16 && _Alignof(gm_t8c5h7u09n10a) == 8 && offsetof(gm_t8c5h7u09n10a, f0) == 0 && offsetof(gm_t8c5h7u09n10a, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta72bg8o6meed) == 18 && _Alignof(gm_ta72bg8o6meed) == 1 && offsetof(gm_ta72bg8o6meed, f0) == 0 && offsetof(gm_ta72bg8o6meed, f1) == 1 && offsetof(gm_ta72bg8o6meed, f2) == 2 && offsetof(gm_ta72bg8o6meed, f3) == 3 && offsetof(gm_ta72bg8o6meed, f4) == 4 && offsetof(gm_ta72bg8o6meed, f5) == 5 && offsetof(gm_ta72bg8o6meed, f6) == 6 && offsetof(gm_ta72bg8o6meed, f7) == 7 && offsetof(gm_ta72bg8o6meed, f8) == 8 && offsetof(gm_ta72bg8o6meed, f9) == 9 && offsetof(gm_ta72bg8o6meed, f10) == 10 && offsetof(gm_ta72bg8o6meed, f11) == 11 && offsetof(gm_ta72bg8o6meed, f12) == 12 && offsetof(gm_ta72bg8o6meed, f13) == 13 && offsetof(gm_ta72bg8o6meed, f14) == 14 && offsetof(gm_ta72bg8o6meed, f15) == 15 && offsetof(gm_ta72bg8o6meed, f16) == 16 && offsetof(gm_ta72bg8o6meed, f17) == 17, "layout of `(u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8)`");
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_t3mf58gv9kf3n f0; void * f1; uint64_t f2; uint64_t f3; uint64_t f4; } gm_tjt4ka19str13;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t2792tsg5eg72;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t88jm1dgsmkjs) == 40 && _Alignof(gm_t88jm1dgsmkjs) == 8 && offsetof(gm_t88jm1dgsmkjs, f0) == 0 && offsetof(gm_t88jm1dgsmkjs, f1) == 8 && offsetof(gm_t88jm1dgsmkjs, f2) == 16 && offsetof(gm_t88jm1dgsmkjs, f3) == 32 && offsetof(gm_t88jm1dgsmkjs, f4) == 33 && offsetof(gm_t88jm1dgsmkjs, f5) == 34 && offsetof(gm_t88jm1dgsmkjs, f6) == 35 && offsetof(gm_t88jm1dgsmkjs, f7) == 36, "layout of `Info`");
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t6j6smj7qoctu v1; gm_b16_8 v2; } f1; } gm_tj63kbqqj2092;
_Static_assert(sizeof(gm_tjt4ka19str13) == 48 && _Alignof(gm_tjt4ka19str13) == 8 && offsetof(gm_tjt4ka19str13, f0) == 0 && offsetof(gm_tjt4ka19str13, f1) == 16 && offsetof(gm_tjt4ka19str13, f2) == 24 && offsetof(gm_tjt4ka19str13, f3) == 32 && offsetof(gm_tjt4ka19str13, f4) == 40, "layout of `Arena`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint64_t f2; void * f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_tc0nbpdhofi37 f7; gm_trqfvmjtt4sij f8; gm_toceffb697p0u f9; gm_trqfvmjtt4sij f10; uint8_t f11; uint8_t pe[7]; } gm_trd6bg8is1v4n;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint64_t f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; gm_tc70a9dbbmgkg f3; uint8_t f4; uint8_t pe[7]; } gm_tsnno3vacsa4v;
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; } gm_t23tohc57ebhi;
_Static_assert(sizeof(gm_t2792tsg5eg72) == 64 && _Alignof(gm_t2792tsg5eg72) == 8 && offsetof(gm_t2792tsg5eg72, f0) == 0 && offsetof(gm_t2792tsg5eg72, f1) == 40 && offsetof(gm_t2792tsg5eg72, f2) == 48 && offsetof(gm_t2792tsg5eg72, f3) == 56, "layout of `Diagnostics`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
_Static_assert(sizeof(gm_tj63kbqqj2092) == 40 && _Alignof(gm_tj63kbqqj2092) == 8 && offsetof(gm_tj63kbqqj2092, f0) == 0 && offsetof(gm_tj63kbqqj2092, f1) == 8, "layout of `Item`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_trd6bg8is1v4n) == 288 && _Alignof(gm_trd6bg8is1v4n) == 8 && offsetof(gm_trd6bg8is1v4n, f0) == 0 && offsetof(gm_trd6bg8is1v4n, f1) == 8 && offsetof(gm_trd6bg8is1v4n, f2) == 24 && offsetof(gm_trd6bg8is1v4n, f3) == 32 && offsetof(gm_trd6bg8is1v4n, f4) == 40 && offsetof(gm_trd6bg8is1v4n, f5) == 80 && offsetof(gm_trd6bg8is1v4n, f6) == 120 && offsetof(gm_trd6bg8is1v4n, f7) == 160 && offsetof(gm_trd6bg8is1v4n, f8) == 184 && offsetof(gm_trd6bg8is1v4n, f9) == 224 && offsetof(gm_trd6bg8is1v4n, f10) == 240 && offsetof(gm_trd6bg8is1v4n, f11) == 280, "layout of `Lexer`");
_Static_assert(sizeof(gm_tsnno3vacsa4v) == 48 && _Alignof(gm_tsnno3vacsa4v) == 8 && offsetof(gm_tsnno3vacsa4v, f0) == 0 && offsetof(gm_tsnno3vacsa4v, f1) == 8 && offsetof(gm_tsnno3vacsa4v, f2) == 9 && offsetof(gm_tsnno3vacsa4v, f3) == 16 && offsetof(gm_tsnno3vacsa4v, f4) == 40, "layout of `Frame`");
typedef struct { gm_tsnno3vacsa4v f0; uint8_t f1; uint8_t pe[7]; } gm_ttlblounidicj;
_Static_assert(sizeof(gm_t23tohc57ebhi) == 64 && _Alignof(gm_t23tohc57ebhi) == 8 && offsetof(gm_t23tohc57ebhi, f0) == 0 && offsetof(gm_t23tohc57ebhi, f1) == 32, "layout of `(TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
_Static_assert(sizeof(gm_ttlblounidicj) == 56 && _Alignof(gm_ttlblounidicj) == 8 && offsetof(gm_ttlblounidicj, f0) == 0 && offsetof(gm_ttlblounidicj, f1) == 48, "layout of `?Frame`");
struct gm_fr_aligned_with_s024agj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_allocator_5m783ja {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_column_r91o38o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_continues_line_2cliit4 {
    gm_frame h;
    gm_t88jm1dgsmkjs l0; /* info */
    uint8_t ret;
};
struct gm_fr_continues_with_name_ejtvq2k {
    gm_frame h;
    gm_t88jm1dgsmkjs l1; /* info */
    uint8_t ret;
};
struct gm_fr_deinit_26uqnl1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_aii0n0l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_ectdopm_2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_diagnostics_n3kkhf4 {
    gm_frame h;
    gm_t2792tsg5eg72 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lpi80k4_4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ojmeji2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p5lo8ed_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rqd6vee {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_get_t1u93iu {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_ss6fp5u {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_is_punct_char_3bc3hom {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_word_kb7cdmu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_keeps_padding_m0v49h1 {
    gm_frame h;
    gm_t88jm1dgsmkjs l1; /* prev */
    gm_t88jm1dgsmkjs l2; /* cur */
    uint8_t ret;
};
struct gm_fr_layout_ufvu3cq {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* infos */
    gm_trqfvmjtt4sij l5; /* items */
    gm_t88jm1dgsmkjs l10; /* info */
    gm_t4c7ipjj9s3fp l11; /* merges */
    gm_trqfvmjtt4sij l12; /* out */
    gm_trqfvmjtt4sij l16; /* stack */
    gm_tj63kbqqj2092 l26; /* item */
    gm_ttlblounidicj l34; /* f */
    gm_tsnno3vacsa4v l36; /* fr */
    gm_tsnno3vacsa4v l44; /* f */
    gm_tsnno3vacsa4v l49; /* f */
    gm_t88jm1dgsmkjs l56; /* info */
    gm_t88jm1dgsmkjs l60; /* pinfo */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lex_dc8mbmf {
    gm_frame h;
    gm_trd6bg8is1v4n l3; /* lx */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_lexes_apart_9scqa7n {
    gm_frame h;
    gm_tjt4ka19str13 l2; /* scratch */
    gm_trqfvmjtt4sij l3; /* joined */
    gm_t2792tsg5eg72 l4; /* diags */
    gm_tr8dvlu9ik4gj l5; /* lexed */
    gm_tm4pc0emhrv1g l11; /* t */
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_line_end_jm825k5 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_list_f0gjjbh_4 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_g3vmkan_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_merges_with_tail_ijmh3nm {
    gm_frame h;
    gm_tjt4ka19str13 l4; /* scratch */
    gm_trqfvmjtt4sij l5; /* joined */
    gm_t2792tsg5eg72 l6; /* diags */
    gm_tr8dvlu9ik4gj l7; /* lexed */
    gm_tm4pc0emhrv1g l10; /* t */
    uint8_t ret;
    gm_b0_1 w1;
};
struct gm_fr_new_jr5ejs0 {
    gm_frame h;
    gm_tjt4ka19str13 ret;
};
struct gm_fr_next_tok_qap88op {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_out_column_hr4hgng {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_pop_f3uajlg_3 {
    gm_frame h;
    gm_tsnno3vacsa4v l2; /* x */
    gm_ttlblounidicj ret;
};
struct gm_fr_push_1e500nc_4 {
    gm_frame h;
    gm_tj63kbqqj2092 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_8h1r7s6_3 {
    gm_frame h;
    gm_tsnno3vacsa4v l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o86m6d7_4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_r51vrc8_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_relative_column_ctuu0pd {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_scan_gap_tkq0f9l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_starts_with_non_white_o0cd8c4 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_would_merge_44g1m96 {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b);
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b);
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b);
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_aligned_with_s024agj(gm_b16_8 a0, uint64_t a1, uint8_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_all_white_1b9moim(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0);
__attribute__((aligned(4))) uint64_t gm_f_char_count_76unqru(gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_char_offset_kr8000h(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_classify_e96658j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint64_t gm_f_column_r91o38o(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_continues_line_2cliit4(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_continues_line_2cliit4_c(gm_t88jm1dgsmkjs a0);
__attribute__((aligned(4))) uint8_t gm_f_continues_with_name_ejtvq2k(gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_continues_with_name_ejtvq2k_c(gm_b16_8 a0, gm_t88jm1dgsmkjs a1);
__attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_decode_jrk6b03(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_deinit_26uqnl1(void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_aii0n0l(void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_ectdopm_2(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_p1kmdo2(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ojmeji2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rqd6vee(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_h7591bf(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_t1u93iu(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_ss6fp5u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint64_t gm_f_indent_of_i911gpd(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_punct_char_3bc3hom(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_white_3njipsh(uint32_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_word_kb7cdmu(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_keeps_padding_m0v49h1(gm_b16_8 a0, const void * a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_keeps_padding_m0v49h1_c(gm_b16_8 a0, gm_t88jm1dgsmkjs a1, gm_t88jm1dgsmkjs a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_last_line_hk6q5l4(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_layout_ufvu3cq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_lexes_apart_9scqa7n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_line_binds_jm0pnul(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_line_end_jm825k5(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint64_t gm_f_line_start_pl10jt5(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_g3vmkan_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_merges_with_tail_ijmh3nm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_tok_qap88op(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_only_spaces_bvta5sq(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_out_column_hr4hgng(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_f3uajlg_3(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc_4(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_8h1r7s6_3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_spaces_732bua4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_fmu1emq(void * a0, const void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r51vrc8_3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) uint64_t gm_f_relative_column_ctuu0pd(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_replace_lf6hcmn(void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_scan_gap_tkq0f9l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_non_white_o0cd8c4(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_d3d35bq(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_would_merge_44g1m96(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2);
extern uint8_t gm_c3dq62rrli9[16] __attribute__((aligned(8)));
extern uint8_t gm_cjlj93gp0or[24] __attribute__((aligned(8)));
extern void *const gm_hd3t1u951rul[];
extern const uint8_t gm_s1c4sfff8ac39[7];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s920dpvvv6cej[4];
extern const uint8_t gm_s9es2tos1bhfp[5];
extern const uint8_t gm_sc8o4oaph9gbr[2];
extern const uint8_t gm_sccjki3qgi8m7[15];
extern const uint8_t gm_seccpktgmrevt[2];
extern const uint8_t gm_sf9a2kg3eo1gs[2];
extern const uint8_t gm_sfi2ha42r1nr2[2];
extern const uint8_t gm_sfqk3rro4g7t0[2];
extern const uint8_t gm_sgpdmvc9s0d2p[3];
extern const uint8_t gm_sh40na9qbga3l[2];
extern const uint8_t gm_sidtn67sdk4e0[7];
extern const uint8_t gm_sjfg2291kjugh[4];
extern const uint8_t gm_skmt2kt2p7v0u[3];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl9nvct67or45[3];
extern const uint8_t gm_slpo22sjs83b1[5];
extern const uint8_t gm_sm8obe455p2d9[3];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_soqt3jnmovq5l[4];
extern const uint8_t gm_spi47lg4tr80j[3];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srceoep1gquqdsq[];
extern const char gm_srcip69en21ht7v[];
extern const char gm_srcrchqfd7obmqe[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srh29phg8b30r[4];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_ss67f0gmfen1m[3];
extern const uint8_t gm_svbhcvlahd4n2[3];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b) {
    if (!gm_eqbihgc69jv3(a + 0, b + 0)) return false;
    if (!gm_eqk7n1cdvnss(a + 16, b + 16)) return false;
    return true;
}
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)2) return gm_eq2ceic3f8rp(a + 8, b + 8);
    if (ta == (uint8_t)3) return gm_eqrv7v3j3o54(a + 8, b + 8);
    if (ta == (uint8_t)4) return gm_eqpa2btif7eh(a + 8, b + 8);
    if (ta == (uint8_t)5) return gm_eqbdg26q4aml(a + 8, b + 8);
    if (ta == (uint8_t)6) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)7) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)8) return gm_eqv3tj1pu43g(a + 8, b + 8);
    return true;
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_allocator_5m783ja F;
    void * gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = t2;
    void * t4 = (void *)((uint8_t *)gm_cjlj93gp0or);
    gm_t3mf58gv9kf3n t5;
    GM_ST(void *, ((uint8_t *)&t5 + 0), t3);
    GM_ST(void *, ((uint8_t *)&t5 + 8), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
gm_fprop:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
}
inline __attribute__((aligned(4))) uint64_t gm_f_column_r91o38o(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_column_r91o38o F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* pos */
    GM_STACK_CHECK_SHALLOW(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t4 = gm_l1;
    uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t6 = (uintptr_t)t4;
    if (t5 > t6 || t6 > (uintptr_t)t3) gm_trap_slice(t5, t6, t3, gm_srcrchqfd7obmqe, 71, 80);
    uint64_t t7 = ((uintptr_t)t4) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t2 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7));
    gm_b16_8 t9 = gm_f_last_line_hk6q5l4(t8);
    uint64_t t10 = gm_f_char_count_76unqru(t9);
    F.ret = t10;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_deinit_26uqnl1(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_26uqnl1 F;
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
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)40ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)8ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_rqd6vee();
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
inline __attribute__((aligned(4))) void gm_f_deinit_aii0n0l(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_aii0n0l F;
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
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)40ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)8ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_lpi80k4_4();
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
inline __attribute__((aligned(4))) void gm_f_deinit_ectdopm_2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_ectdopm_2 F;
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
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)48ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)8ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_p5lo8ed_3();
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
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_diagnostics_n3kkhf4 F;
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_prbqo9j((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t2792tsg5eg72 t3;
    GM_ST(uint32_t, (uint8_t *)&t3 + 60, 0);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t3 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t3 + 40), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t3 + 48), ((uint64_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 56), ((uint32_t)0ULL));
    memmove(gm_out, ((uint8_t *)&t3), 64);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lpi80k4_4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ojmeji2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ojmeji2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p5lo8ed_3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rqd6vee(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rqd6vee F;
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_t1u93iu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_t1u93iu F;
    void * gm_l0; /* m */
    gm_t6j6smj7qoctu gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 162u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_tf5srh4mp4b3i gm_l3;
    uint64_t gm_l4;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_h7591bf(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l4 = t6;
    } else {
        gm_tqs5p4n2tq19v t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    uint64_t t8 = gm_l4;
    gm_l2 = t8;
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t9) + 8));
    uint64_t t12 = gm_l2;
    if ((uintptr_t)t12 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t12, t11, gm_srcbsc23is03ttm, 139, 12);
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t10) + (uintptr_t)t12 * 48) + 40));
    gm_tqs5p4n2tq19v t14;
    GM_ST(uint8_t, ((uint8_t *)&t14), t13);
    ((uint8_t *)&t14)[1] = 1;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_hash_map_ss6fp5u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_ss6fp5u F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ojmeji2();
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
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_lex_dc8mbmf F;
    uint32_t gm_l0; /* file */
    gm_b16_8 gm_l1; /* src */
    void * gm_l2; /* diags */
    GM_STACK_CHECK(&F, 352u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    void * t4 = gm_l2;
    gm_trd6bg8is1v4n t5;
    gm_f_new_lexer_dm12g12((void *)&t5, (void *)NULL, cx, NULL, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t5;
    void * t6 = (void *)((uint8_t *)&F.l3);
    gm_f_run_v26is74((void *)&(*(gm_tr8dvlu9ik4gj *)gm_out), (void *)NULL, cx, NULL, t6);
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
inline __attribute__((aligned(4))) uint64_t gm_f_line_end_jm825k5(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_line_end_jm825k5 F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* pos */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 123u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t5 = gm_l1;
    gm_l2 = t5;
    {
        for (;;) {
            uint64_t t6 = gm_l2;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t8 = (t6 < t7);
            if (!t8) {
                goto gm_lb1;
            }
            void * t9 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t11 = gm_l2;
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t9) + (uintptr_t)t11 * 1));
            uint8_t t13 = (!(t12 == ((uint8_t)10ULL)));
            if (!t13) {
                goto gm_lb1;
            }
            {
                uint64_t t14 = gm_l2;
                uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
                if (o16) gm_trap("integer overflow in `+`", gm_srcrchqfd7obmqe, 83, 50);
                uint64_t t17 = r15;
                gm_l2 = t17;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t18 = gm_l2;
    F.ret = t18;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_f0gjjbh_4 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lpi80k4_4();
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
inline __attribute__((aligned(4))) void gm_f_list_g3vmkan_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g3vmkan_3 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_p5lo8ed_3();
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
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_jr5ejs0 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_node *pn2 = gm_find(cx, 587034433);
    if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_srcip69en21ht7v, 41, 55);
    gm_t3mf58gv9kf3n t3;
    t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tjt4ka19str13 t4;
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t4 + 0), t3);
    GM_ST(void *, ((uint8_t *)&t4 + 16), ((void *)0));
    GM_ST(uint64_t, ((uint8_t *)&t4 + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t4 + 32), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t4 + 40), ((uint64_t)0ULL));
    memmove(gm_out, ((uint8_t *)&t4), 48);
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
inline __attribute__((aligned(4))) uint64_t gm_f_out_column_hr4hgng(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_out_column_hr4hgng F;
    void * gm_l0; /* out */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)t2));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    gm_b16_8 t5 = gm_f_last_line_hk6q5l4(t4);
    uint64_t t6 = gm_f_char_count_76unqru(t5);
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_f3uajlg_3(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_f3uajlg_3 F;
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
        gm_ttlblounidicj t6;
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
    gm_tsnno3vacsa4v t26 = GM_LD(gm_tsnno3vacsa4v, ((uint8_t *)&F.l2));
    gm_ttlblounidicj t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 49, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 50, 0);
    GM_ST(uint32_t, (uint8_t *)&t27 + 52, 0);
    GM_ST(gm_tsnno3vacsa4v, ((uint8_t *)&t27), t26);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc_4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1e500nc_4 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_lpi80k4_4();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_o86m6d7_4(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 40), ((uint8_t *)&F.l1), 40);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_8h1r7s6_3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_8h1r7s6_3 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_p5lo8ed_3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_r51vrc8_3(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o86m6d7_4 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r51vrc8_3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_r51vrc8_3 F;
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
inline __attribute__((aligned(4))) uint64_t gm_f_relative_column_ctuu0pd(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_relative_column_ctuu0pd F;
    gm_b16_8 gm_l0; /* line */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t gm_l3;
    gm_b16_8 t2 = gm_l0;
    uint64_t t3 = gm_f_char_count_76unqru(t2);
    gm_l1 = t3;
    gm_b16_8 t4 = gm_l0;
    uint64_t t5 = gm_f_indent_of_i911gpd(t4);
    gm_l2 = t5;
    uint64_t t6 = gm_l1;
    uint64_t t7 = gm_l2;
    uint8_t t8 = (t6 > t7);
    if (t8) {
        uint64_t t9 = gm_l1;
        uint64_t t10 = gm_l2;
        uint64_t r11; bool o12 = __builtin_sub_overflow(t9, t10, &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 450, 23);
        uint64_t t13 = r11;
        gm_l3 = t13;
    } else {
        gm_l3 = ((uint64_t)0ULL);
    }
    uint64_t t14 = gm_l3;
    F.ret = t14;
    goto gm_fret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_non_white_o0cd8c4(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_non_white_o0cd8c4 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tb31k5l794t7p gm_l1;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t4 = gm_l0;
    gm_tb31k5l794t7p t5 = GM_CONV(gm_tb31k5l794t7p, gm_f_decode_jrk6b03(t4, ((uint64_t)0ULL)));
    gm_l1 = t5;
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint8_t t7 = gm_f_is_white_3njipsh(t6);
    uint8_t t8 = (uint8_t)!t7;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_aligned_with_s024agj(gm_b16_8 a0, uint64_t a1, uint8_t a2, gm_b16_8 a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_with_s024agj F;
    gm_b16_8 gm_l0; /* line */
    uint64_t gm_l1; /* col */
    uint8_t gm_l2; /* op */
    gm_b16_8 gm_l3; /* text */
    uint64_t gm_l4; /* cut */
    gm_b16_8 gm_l5; /* rest */
    gm_b16_8 gm_l6; /* before */
    uint8_t gm_l7; /* spaced */
    GM_STACK_CHECK_SHALLOW(&F, 386u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l8;
    uint8_t gm_l9;
    uint8_t gm_l10;
    gm_b16_8 t2 = gm_l0;
    uint64_t t3 = gm_l1;
    gm_b16_8 t4 = gm_l0;
    uint64_t t5 = gm_f_indent_of_i911gpd(t4);
    uint64_t r6; bool o7 = __builtin_add_overflow(t3, t5, &r6);
    if (o7) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 198, 37);
    uint64_t t8 = r6;
    uint64_t t9 = gm_f_char_offset_kr8000h(t2, t8);
    gm_l4 = t9;
    void * t10 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t12 = gm_l4;
    uint64_t t13 = (uintptr_t)t12;
    uint64_t t14 = (uintptr_t)t11;
    if (t13 > t14 || t14 > (uintptr_t)t11) gm_trap_slice(t13, t14, t11, gm_srceoep1gquqdsq, 199, 13);
    uint64_t t15 = ((uintptr_t)t11) - (uintptr_t)t12;
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t10 + (uintptr_t)t12 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(t15));
    gm_l5 = t16;
    void * t17 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t19 = gm_l4;
    uint64_t t20 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t21 = (uintptr_t)t19;
    if (t20 > t21 || t21 > (uintptr_t)t18) gm_trap_slice(t20, t21, t18, gm_srceoep1gquqdsq, 200, 15);
    uint64_t t22 = ((uintptr_t)t19) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t23;
    GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t17 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22));
    gm_l6 = t23;
    gm_b16_8 t24 = gm_l6;
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s3p0rbs9hc3lg));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(1));
    uint8_t t26 = gm_f_ends_with_biriu5l(t24, t25);
    if (t26) {
        gm_b16_8 t27 = gm_l6;
        uint8_t t28 = gm_f_all_white_1b9moim(t27);
        uint8_t t29 = (uint8_t)!t28;
        gm_l8 = t29;
    } else {
        gm_l8 = ((uint8_t)0);
    }
    uint8_t t30 = gm_l8;
    gm_l7 = t30;
    uint8_t t31 = gm_l2;
    if (t31) {
        uint8_t t32 = gm_l7;
        if (t32) {
            gm_b16_8 t33 = gm_l5;
            gm_b16_8 t34 = gm_l3;
            uint8_t t35 = gm_f_starts_with_6vvt01e(t33, t34);
            gm_l9 = t35;
        } else {
            gm_l9 = ((uint8_t)0);
        }
        uint8_t t36 = gm_l9;
        F.ret = t36;
        goto gm_fret;
    }
    uint8_t t37 = gm_l7;
    if (t37) {
        gm_b16_8 t38 = gm_l5;
        uint8_t t39 = gm_f_starts_with_non_white_o0cd8c4(t38);
        gm_l10 = t39;
    } else {
        gm_l10 = ((uint8_t)0);
    }
    uint8_t t40 = gm_l10;
    F.ret = t40;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_continues_line_2cliit4(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_continues_line_2cliit4 F;
    gm_b16_8 gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 638u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l2;
    uint8_t gm_l3;
    uint8_t gm_l4;
    uint8_t gm_l5;
    gm_tf5srh4mp4b3i gm_l6;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)a0) + 34));
    if (t25) {
        gm_l2 = ((uint8_t)1);
    } else {
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)a0) + 35));
        gm_l2 = t26;
    }
    uint8_t t27 = gm_l2;
    if (t27) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 16));
    gm_l1 = t28;
    {
        {
            {
                {
                    gm_b16_8 t29 = gm_l1;
                    gm_b16_8 t30;
                    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sgpdmvc9s0d2p));
                    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(2));
                    uint8_t t31 = (gm_eq0iqiubfm7e(((uint8_t *)&t29), ((uint8_t *)&t30)));
                    if (!t31) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_b16_8 t32 = gm_l1;
                    gm_b16_8 t33;
                    GM_ST(void *, (uint8_t *)&t33, (void *)(gm_svbhcvlahd4n2));
                    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(2));
                    uint8_t t34 = (gm_eq0iqiubfm7e(((uint8_t *)&t32), ((uint8_t *)&t33)));
                    if (!t34) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    gm_b16_8 t35 = gm_l1;
                    gm_b16_8 t36;
                    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s920dpvvv6cej));
                    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(3));
                    uint8_t t37 = (gm_eq0iqiubfm7e(((uint8_t *)&t35), ((uint8_t *)&t36)));
                    if (!t37) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    gm_b16_8 t38 = gm_l1;
                    gm_b16_8 t39;
                    GM_ST(void *, (uint8_t *)&t39, (void *)(gm_spi47lg4tr80j));
                    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(2));
                    uint8_t t40 = (gm_eq0iqiubfm7e(((uint8_t *)&t38), ((uint8_t *)&t39)));
                    if (!t40) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    gm_b16_8 t41 = gm_l1;
                    gm_b16_8 t42;
                    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s9es2tos1bhfp));
                    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(4));
                    uint8_t t43 = (gm_eq0iqiubfm7e(((uint8_t *)&t41), ((uint8_t *)&t42)));
                    if (!t43) {
                        goto gm_lb8;
                    }
                    goto gm_lb3;
                }
gm_lb8:;
                goto gm_lb2;
            }
gm_lb3:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
        {
            {
                {
                    gm_b16_8 t44 = gm_l1;
                    gm_b16_8 t45;
                    GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sc8o4oaph9gbr));
                    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(1));
                    uint8_t t46 = (gm_eq0iqiubfm7e(((uint8_t *)&t44), ((uint8_t *)&t45)));
                    if (!t46) {
                        goto gm_lb11;
                    }
                    goto gm_lb10;
                }
gm_lb11:;
                {
                    gm_b16_8 t47 = gm_l1;
                    gm_b16_8 t48;
                    GM_ST(void *, (uint8_t *)&t48, (void *)(gm_spod3tmkh25up));
                    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(1));
                    uint8_t t49 = (gm_eq0iqiubfm7e(((uint8_t *)&t47), ((uint8_t *)&t48)));
                    if (!t49) {
                        goto gm_lb12;
                    }
                    goto gm_lb10;
                }
gm_lb12:;
                {
                    gm_b16_8 t50 = gm_l1;
                    gm_b16_8 t51;
                    GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sfqk3rro4g7t0));
                    GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(1));
                    uint8_t t52 = (gm_eq0iqiubfm7e(((uint8_t *)&t50), ((uint8_t *)&t51)));
                    if (!t52) {
                        goto gm_lb13;
                    }
                    goto gm_lb10;
                }
gm_lb13:;
                {
                    gm_b16_8 t53 = gm_l1;
                    gm_b16_8 t54;
                    GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sf9a2kg3eo1gs));
                    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(1));
                    uint8_t t55 = (gm_eq0iqiubfm7e(((uint8_t *)&t53), ((uint8_t *)&t54)));
                    if (!t55) {
                        goto gm_lb14;
                    }
                    goto gm_lb10;
                }
gm_lb14:;
                {
                    gm_b16_8 t56 = gm_l1;
                    gm_b16_8 t57;
                    GM_ST(void *, (uint8_t *)&t57, (void *)(gm_srs1vrn2uaams));
                    GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(1));
                    uint8_t t58 = (gm_eq0iqiubfm7e(((uint8_t *)&t56), ((uint8_t *)&t57)));
                    if (!t58) {
                        goto gm_lb15;
                    }
                    goto gm_lb10;
                }
gm_lb15:;
                {
                    gm_b16_8 t59 = gm_l1;
                    gm_b16_8 t60;
                    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sfi2ha42r1nr2));
                    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(1));
                    uint8_t t61 = (gm_eq0iqiubfm7e(((uint8_t *)&t59), ((uint8_t *)&t60)));
                    if (!t61) {
                        goto gm_lb16;
                    }
                    goto gm_lb10;
                }
gm_lb16:;
                {
                    gm_b16_8 t62 = gm_l1;
                    gm_b16_8 t63;
                    GM_ST(void *, (uint8_t *)&t63, (void *)(gm_seccpktgmrevt));
                    GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(1));
                    uint8_t t64 = (gm_eq0iqiubfm7e(((uint8_t *)&t62), ((uint8_t *)&t63)));
                    if (!t64) {
                        goto gm_lb17;
                    }
                    goto gm_lb10;
                }
gm_lb17:;
                {
                    gm_b16_8 t65 = gm_l1;
                    gm_b16_8 t66;
                    GM_ST(void *, (uint8_t *)&t66, (void *)(gm_soqt3jnmovq5l));
                    GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(3));
                    uint8_t t67 = (gm_eq0iqiubfm7e(((uint8_t *)&t65), ((uint8_t *)&t66)));
                    if (!t67) {
                        goto gm_lb18;
                    }
                    goto gm_lb10;
                }
gm_lb18:;
                {
                    gm_b16_8 t68 = gm_l1;
                    gm_b16_8 t69;
                    GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sjfg2291kjugh));
                    GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(3));
                    uint8_t t70 = (gm_eq0iqiubfm7e(((uint8_t *)&t68), ((uint8_t *)&t69)));
                    if (!t70) {
                        goto gm_lb19;
                    }
                    goto gm_lb10;
                }
gm_lb19:;
                goto gm_lb9;
            }
gm_lb10:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb9:;
        {
            goto gm_lb1;
        }
gm_lb20:;
        gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 156, 5);
    }
gm_lb1:;
    gm_b16_8 t71;
    GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sccjki3qgi8m7));
    GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(14));
    void * t72 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t73)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t73, gm_srceoep1gquqdsq, 161, 39);
    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)t72) + (uintptr_t)((uint64_t)0ULL) * 1));
    gm_tf5srh4mp4b3i t75 = gm_f_index_of_hdeg351(t71, t74);
    gm_l6 = t75;
    {
        {
            uint8_t t76 = (uint8_t)((((uint8_t *)&gm_l6))[8] != 0);
            if (t76) {
                goto gm_lb22;
            }
            gm_l5 = ((uint8_t)0);
            goto gm_lb21;
        }
gm_lb22:;
        {
            gm_l5 = ((uint8_t)1);
            goto gm_lb21;
        }
gm_lb23:;
        gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 161, 12);
    }
gm_lb21:;
    uint8_t t77 = gm_l5;
    if (t77) {
        gm_l4 = ((uint8_t)1);
    } else {
        gm_b16_8 t78 = gm_l1;
        gm_b16_8 t79;
        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_s1c4sfff8ac39));
        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(6));
        uint8_t t80 = (gm_eq0iqiubfm7e(((uint8_t *)&t78), ((uint8_t *)&t79)));
        gm_l4 = t80;
    }
    uint8_t t81 = gm_l4;
    if (t81) {
        gm_l3 = ((uint8_t)1);
    } else {
        gm_b16_8 t82 = gm_l1;
        gm_b16_8 t83;
        GM_ST(void *, (uint8_t *)&t83, (void *)(gm_sidtn67sdk4e0));
        GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(6));
        uint8_t t84 = (gm_eq0iqiubfm7e(((uint8_t *)&t82), ((uint8_t *)&t83)));
        gm_l3 = t84;
    }
    uint8_t t85 = gm_l3;
    F.ret = t85;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_continues_line_2cliit4_c(gm_t88jm1dgsmkjs a0) { return gm_f_continues_line_2cliit4(&a0); }
inline __attribute__((aligned(4))) uint8_t gm_f_continues_with_name_ejtvq2k(gm_b16_8 a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_continues_with_name_ejtvq2k F;
    gm_b16_8 gm_l0; /* src */
    uint8_t gm_l2; /* c */
    uint8_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l5;
    gm_tqs5p4n2tq19v gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 16));
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl3q52ep6aa53));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(1));
    uint8_t t4 = (!gm_eq0iqiubfm7e(((uint8_t *)&t2), ((uint8_t *)&t3)));
    if (t4) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t5 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
    uint8_t t8 = (t7 < t6);
    if (t8) {
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t5) + (uintptr_t)t7 * 1));
        gm_tqs5p4n2tq19v t10;
        GM_ST(uint8_t, ((uint8_t *)&t10), t9);
        ((uint8_t *)&t10)[1] = 1;
        gm_l5 = t10;
    } else {
        gm_tqs5p4n2tq19v t11;
        memset(&t11, 0, sizeof t11);
        gm_l5 = t11;
    }
    gm_tqs5p4n2tq19v t12 = gm_l5;
    gm_l6 = t12;
    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l6))[1] != 0);
    if (t13) {
        uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&gm_l6));
        gm_l7 = t14;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint8_t t15 = gm_l7;
    gm_l2 = t15;
    uint8_t t16 = gm_l2;
    gm_l3 = t16;
    uint8_t t17 = gm_l3;
    uint8_t t18 = (((uint8_t)97ULL) <= t17);
    if (t18) {
        uint8_t t19 = gm_l3;
        uint8_t t20 = (t19 <= ((uint8_t)122ULL));
        gm_l11 = t20;
    } else {
        gm_l11 = ((uint8_t)0);
    }
    uint8_t t21 = gm_l11;
    if (t21) {
        gm_l10 = ((uint8_t)1);
    } else {
        uint8_t t22 = gm_l2;
        gm_l4 = t22;
        uint8_t t23 = gm_l4;
        uint8_t t24 = (((uint8_t)65ULL) <= t23);
        if (t24) {
            uint8_t t25 = gm_l4;
            uint8_t t26 = (t25 <= ((uint8_t)90ULL));
            gm_l12 = t26;
        } else {
            gm_l12 = ((uint8_t)0);
        }
        uint8_t t27 = gm_l12;
        gm_l10 = t27;
    }
    uint8_t t28 = gm_l10;
    if (t28) {
        gm_l9 = ((uint8_t)1);
    } else {
        uint8_t t29 = gm_l2;
        uint8_t t30 = ((t29 == ((uint8_t)95ULL)));
        gm_l9 = t30;
    }
    uint8_t t31 = gm_l9;
    if (t31) {
        void * t32 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        uint64_t t35 = (uintptr_t)t34;
        uint64_t t36 = (uintptr_t)t33;
        if (t35 > t36 || t36 > (uintptr_t)t33) gm_trap_slice(t35, t36, t33, gm_srceoep1gquqdsq, 149, 85);
        uint64_t t37 = ((uintptr_t)t33) - (uintptr_t)t34;
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t32 + (uintptr_t)t34 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37));
        uint8_t t39 = gm_f_line_binds_jm0pnul(t38);
        uint8_t t40 = (uint8_t)!t39;
        gm_l8 = t40;
    } else {
        gm_l8 = ((uint8_t)0);
    }
    uint8_t t41 = gm_l8;
    F.ret = t41;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_continues_with_name_ejtvq2k_c(gm_b16_8 a0, gm_t88jm1dgsmkjs a1) { return gm_f_continues_with_name_ejtvq2k(a0, &a1); }
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_char_3bc3hom(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_punct_char_3bc3hom F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t2 = gm_l0;
    gm_ta72bg8o6meed t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)45ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)43ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)42ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 3), ((uint8_t)47ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 4), ((uint8_t)37ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 5), ((uint8_t)60ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 6), ((uint8_t)62ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 7), ((uint8_t)61ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 8), ((uint8_t)33ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 9), ((uint8_t)38ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 10), ((uint8_t)124ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 11), ((uint8_t)94ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 12), ((uint8_t)126ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 13), ((uint8_t)46ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 14), ((uint8_t)58ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 15), ((uint8_t)63ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 16), ((uint8_t)64ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 17), ((uint8_t)35ULL));
    uint8_t t4 = 0;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 0, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 1, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 2, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 3, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 4, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 5, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 6, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 7, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 8, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 9, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 10, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 11, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 12, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 13, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 14, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 15, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 16, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 17, ((uint8_t *)&t2))) t4 = 1;
    uint8_t t5 = (uint8_t)(t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_word_kb7cdmu(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_word_kb7cdmu F;
    uint8_t gm_l0; /* c */
    uint8_t gm_l1; /* <tmp> */
    uint8_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l4;
    uint8_t gm_l5;
    uint8_t gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    uint8_t t2 = gm_l0;
    gm_l1 = t2;
    uint8_t t3 = gm_l1;
    uint8_t t4 = (((uint8_t)97ULL) <= t3);
    if (t4) {
        uint8_t t5 = gm_l1;
        uint8_t t6 = (t5 <= ((uint8_t)122ULL));
        gm_l7 = t6;
    } else {
        gm_l7 = ((uint8_t)0);
    }
    uint8_t t7 = gm_l7;
    if (t7) {
        gm_l6 = ((uint8_t)1);
    } else {
        uint8_t t8 = gm_l0;
        gm_l2 = t8;
        uint8_t t9 = gm_l2;
        uint8_t t10 = (((uint8_t)65ULL) <= t9);
        if (t10) {
            uint8_t t11 = gm_l2;
            uint8_t t12 = (t11 <= ((uint8_t)90ULL));
            gm_l8 = t12;
        } else {
            gm_l8 = ((uint8_t)0);
        }
        uint8_t t13 = gm_l8;
        gm_l6 = t13;
    }
    uint8_t t14 = gm_l6;
    if (t14) {
        gm_l5 = ((uint8_t)1);
    } else {
        uint8_t t15 = gm_l0;
        gm_l3 = t15;
        uint8_t t16 = gm_l3;
        uint8_t t17 = (((uint8_t)48ULL) <= t16);
        if (t17) {
            uint8_t t18 = gm_l3;
            uint8_t t19 = (t18 <= ((uint8_t)57ULL));
            gm_l9 = t19;
        } else {
            gm_l9 = ((uint8_t)0);
        }
        uint8_t t20 = gm_l9;
        gm_l5 = t20;
    }
    uint8_t t21 = gm_l5;
    if (t21) {
        gm_l4 = ((uint8_t)1);
    } else {
        uint8_t t22 = gm_l0;
        gm_t2abnvcnt53ot t23;
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)95ULL));
        GM_ST(uint8_t, ((uint8_t *)&t23 + 1), ((uint8_t)34ULL));
        GM_ST(uint8_t, ((uint8_t *)&t23 + 2), ((uint8_t)39ULL));
        GM_ST(uint8_t, ((uint8_t *)&t23 + 3), ((uint8_t)96ULL));
        uint8_t t24 = 0;
        if (gm_eqjmccqdb90f(((uint8_t *)&t23) + 0, ((uint8_t *)&t22))) t24 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t23) + 1, ((uint8_t *)&t22))) t24 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t23) + 2, ((uint8_t *)&t22))) t24 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t23) + 3, ((uint8_t *)&t22))) t24 = 1;
        uint8_t t25 = (uint8_t)(t24);
        gm_l4 = t25;
    }
    uint8_t t26 = gm_l4;
    F.ret = t26;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_keeps_padding_m0v49h1(gm_b16_8 a0, const void * a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_keeps_padding_m0v49h1 F;
    gm_b16_8 gm_l0; /* src */
    uint8_t gm_l3; /* op */
    uint64_t gm_l4; /* start */
    uint64_t gm_l5; /* end */
    gm_th2lqbo77pvsc gm_l6; /* above */
    gm_th2lqbo77pvsc gm_l7; /* below */
    gm_b16_8 gm_l8; /* rest */
    gm_b16_8 gm_l9; /* own */
    uint64_t gm_l10; /* own_indent */
    uint64_t gm_l11; /* here */
    uint64_t gm_l12; /* col */
    gm_b16_8 gm_l13; /* line */
    gm_b16_8 gm_l14; /* line */
    GM_STACK_CHECK_SHALLOW(&F, 1017u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t gm_l15;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 16));
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_ss67f0gmfen1m));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(2));
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sm8obe455p2d9));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(2));
    gm_t6j6smj7qoctu t11;
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t9);
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 16), t10);
    uint8_t t12 = 0;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t11) + 0, ((uint8_t *)&t8))) t12 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t11) + 16, ((uint8_t *)&t8))) t12 = 1;
    uint8_t t13 = (uint8_t)(t12);
    gm_l3 = t13;
    {
        {
            uint8_t t14 = gm_l3;
            uint8_t t15 = (uint8_t)!t14;
            if (!t15) {
                goto gm_lb2;
            }
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 16));
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s39mljh669qgl));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(1));
            uint8_t t18 = (!gm_eq0iqiubfm7e(((uint8_t *)&t16), ((uint8_t *)&t17)));
            if (!t18) {
                goto gm_lb2;
            }
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t19 = gm_l0;
    uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)a2));
    uint64_t t21 = gm_f_line_start_pl10jt5(t19, t20);
    gm_l4 = t21;
    gm_b16_8 t22 = gm_l0;
    uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)a2));
    uint64_t t24 = gm_f_line_end_jm825k5(t22, t23);
    gm_l5 = t24;
    gm_th2lqbo77pvsc t25;
    memset(&t25, 0, sizeof t25);
    gm_l6 = t25;
    uint64_t t26 = gm_l4;
    uint8_t t27 = (t26 > ((uint64_t)0ULL));
    if (t27) {
        void * t28 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t30 = gm_l4;
        uint64_t r31; bool o32 = __builtin_sub_overflow(t30, ((uint64_t)1ULL), &r31);
        if (o32) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 176, 45);
        uint64_t t33 = r31;
        uint64_t t34 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t35 = (uintptr_t)t33;
        if (t34 > t35 || t35 > (uintptr_t)t29) gm_trap_slice(t34, t35, t29, gm_srceoep1gquqdsq, 176, 38);
        uint64_t t36 = ((uintptr_t)t33) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(((uint8_t *)t28 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(t36));
        gm_b16_8 t38 = gm_f_last_line_hk6q5l4(t37);
        gm_th2lqbo77pvsc t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t39 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t39), t38);
        ((uint8_t *)&t39)[16] = 1;
        gm_l6 = t39;
    }
    gm_th2lqbo77pvsc t40;
    memset(&t40, 0, sizeof t40);
    gm_l7 = t40;
    uint64_t t41 = gm_l5;
    uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t43 = (t41 < t42);
    if (t43) {
        void * t44 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t46 = gm_l5;
        uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
        if (o48) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 179, 21);
        uint64_t t49 = r47;
        uint64_t t50 = (uintptr_t)t49;
        uint64_t t51 = (uintptr_t)t45;
        if (t50 > t51 || t51 > (uintptr_t)t45) gm_trap_slice(t50, t51, t45, gm_srceoep1gquqdsq, 179, 17);
        uint64_t t52 = ((uintptr_t)t45) - (uintptr_t)t49;
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(((uint8_t *)t44 + (uintptr_t)t49 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(t52));
        gm_l8 = t53;
        void * t54 = GM_LD(void *, ((uint8_t *)&gm_l8));
        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
        gm_b16_8 t56 = gm_l8;
        uint64_t t57 = gm_f_line_end_jm825k5(t56, ((uint64_t)0ULL));
        uint64_t t58 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t59 = (uintptr_t)t57;
        if (t58 > t59 || t59 > (uintptr_t)t55) gm_trap_slice(t58, t59, t55, gm_srceoep1gquqdsq, 180, 17);
        uint64_t t60 = ((uintptr_t)t57) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t61;
        GM_ST(void *, (uint8_t *)&t61, (void *)(((uint8_t *)t54 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(t60));
        gm_th2lqbo77pvsc t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t62 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t62), t61);
        ((uint8_t *)&t62)[16] = 1;
        gm_l7 = t62;
    }
    void * t63 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t65 = gm_l4;
    uint64_t t66 = gm_l5;
    uint64_t t67 = (uintptr_t)t65;
    uint64_t t68 = (uintptr_t)t66;
    if (t67 > t68 || t68 > (uintptr_t)t64) gm_trap_slice(t67, t68, t64, gm_srceoep1gquqdsq, 182, 12);
    uint64_t t69 = ((uintptr_t)t66) - (uintptr_t)t65;
    gm_b16_8 t70;
    GM_ST(void *, (uint8_t *)&t70, (void *)(((uint8_t *)t63 + (uintptr_t)t65 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(t69));
    gm_l9 = t70;
    gm_b16_8 t71 = gm_l9;
    uint64_t t72 = gm_f_indent_of_i911gpd(t71);
    gm_l10 = t72;
    gm_b16_8 t73 = gm_l0;
    uint64_t t74 = GM_LD(uint64_t, ((uint8_t *)a2));
    uint64_t t75 = gm_f_column_r91o38o(t73, t74);
    gm_l11 = t75;
    uint64_t t76 = gm_l11;
    uint64_t t77 = gm_l10;
    uint8_t t78 = (t76 > t77);
    if (t78) {
        uint64_t t79 = gm_l11;
        uint64_t t80 = gm_l10;
        uint64_t r81; bool o82 = __builtin_sub_overflow(t79, t80, &r81);
        if (o82) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 185, 35);
        uint64_t t83 = r81;
        gm_l15 = t83;
    } else {
        gm_l15 = ((uint64_t)0ULL);
    }
    uint64_t t84 = gm_l15;
    gm_l12 = t84;
    {
        {
            uint8_t t85 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
            if (!t85) {
                goto gm_lb4;
            }
            gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
            gm_l13 = t86;
            gm_b16_8 t87 = gm_l13;
            uint64_t t88 = gm_l12;
            uint8_t t89 = gm_l3;
            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 16));
            uint8_t t91 = gm_f_aligned_with_s024agj(t87, t88, t89, t90);
            if (t91) {
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    {
        {
            uint8_t t92 = (uint8_t)((((uint8_t *)&gm_l7))[16] != 0);
            if (!t92) {
                goto gm_lb6;
            }
            gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
            gm_l14 = t93;
            gm_b16_8 t94 = gm_l14;
            uint64_t t95 = gm_l12;
            uint8_t t96 = gm_l3;
            gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 16));
            uint8_t t98 = gm_f_aligned_with_s024agj(t94, t95, t96, t97);
            if (t98) {
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto gm_lb5;
        }
gm_lb6:;
    }
gm_lb5:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_keeps_padding_m0v49h1_c(gm_b16_8 a0, gm_t88jm1dgsmkjs a1, gm_t88jm1dgsmkjs a2) { return gm_f_keeps_padding_m0v49h1(a0, &a1, &a2); }
__attribute__((aligned(4))) void gm_f_layout_ufvu3cq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_layout_ufvu3cq F;
    gm_b16_8 gm_l0; /* src */
    gm_b16_8 gm_l1; /* toks */
    uint8_t gm_l3; /* bom */
    uint64_t gm_l4; /* base */
    uint64_t gm_l6; /* prev_end */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* k */
    uint8_t gm_l13; /* line_open */
    uint64_t gm_l14; /* nl */
    gm_t8c5h7u09n10a gm_l15; /* prev */
    uint64_t gm_l17; /* cur_base */
    uint64_t gm_l18; /* min_len */
    uint8_t gm_l19; /* cont_next */
    uint8_t gm_l20; /* after_lbrace */
    gm_tf5srh4mp4b3i gm_l21; /* line_head */
    uint8_t gm_l22; /* after_chain */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* idx */
    uint8_t gm_l27; /* close_tok */
    uint8_t gm_l28; /* close_brace */
    uint64_t gm_l29; /* k */
    uint8_t gm_l30; /* trailing */
    uint8_t gm_l31; /* first */
    gm_tf5srh4mp4b3i gm_l32; /* head */
    uint64_t gm_l33; /* indent */
    uint64_t gm_l35; /* v */
    uint8_t gm_l37; /* plain_ctx */
    uint8_t gm_l38; /* starts_cont */
    uint8_t gm_l39; /* is_else */
    uint64_t gm_l40; /* h */
    uint8_t gm_l41; /* chain_else */
    uint8_t gm_l42; /* continues */
    gm_tf5srh4mp4b3i gm_l43; /* aligned */
    gm_t2lqu8jsj511d gm_l45; /* v */
    uint64_t gm_l46; /* h */
    uint64_t gm_l47; /* level */
    uint64_t gm_l48; /* col */
    uint8_t gm_l50; /* lbrace */
    gm_t6j6smj7qoctu gm_l51; /* l */
    gm_b16_8 gm_l52; /* text */
    uint8_t gm_l53; /* prev_ta */
    uint64_t gm_l54; /* p */
    uint64_t gm_l55; /* k */
    uint64_t gm_l57; /* pad */
    uint8_t gm_l58; /* space */
    uint64_t gm_l59; /* p */
    uint8_t gm_l61; /* tight */
    gm_b16_8 gm_l62; /* gap */
    uint64_t gm_l63; /* min_gap */
    uint64_t gm_l64; /* want */
    uint64_t gm_l65; /* have */
    uint8_t gm_l66; /* glued */
    uint64_t gm_l67; /* p */
    void * gm_l68; /* f */
    uint8_t gm_l69; /* brace */
    uint8_t gm_l70; /* chain */
    uint64_t gm_l71; /* h */
    GM_STACK_CHECK(&F, 3976u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l72;
    gm_tftb1odc739t5 gm_l73;
    gm_tftb1odc739t5 gm_l74;
    uint8_t gm_l75;
    uint8_t gm_l76;
    uint8_t gm_l77;
    uint8_t gm_l78;
    gm_tftb1odc739t5 gm_l79;
    gm_tftb1odc739t5 gm_l80;
    uint8_t gm_l81;
    uint64_t gm_l82;
    uint8_t gm_l83;
    uint8_t gm_l84;
    uint8_t gm_l85;
    uint8_t gm_l86;
    uint8_t gm_l87;
    uint8_t gm_l88;
    uint8_t gm_l89;
    uint8_t gm_l90;
    uint8_t gm_l91;
    uint64_t gm_l92;
    gm_ttlblounidicj gm_l93;
    uint8_t gm_l94;
    gm_tftb1odc739t5 gm_l95;
    gm_tftb1odc739t5 gm_l96;
    gm_tftb1odc739t5 gm_l97;
    gm_tftb1odc739t5 gm_l98;
    gm_tftb1odc739t5 gm_l99;
    gm_tftb1odc739t5 gm_l100;
    uint8_t gm_l101;
    uint64_t gm_l102;
    uint64_t gm_l103;
    uint8_t gm_l104;
    uint8_t gm_l105;
    uint8_t gm_l106;
    uint8_t gm_l107;
    uint8_t gm_l108;
    uint8_t gm_l109;
    gm_tftb1odc739t5 gm_l110;
    uint8_t gm_l111;
    gm_tftb1odc739t5 gm_l112;
    uint8_t gm_l113;
    uint8_t gm_l114;
    uint8_t gm_l115;
    gm_tftb1odc739t5 gm_l116;
    {
        {
            gm_b16_8 t65 = gm_l0;
            gm_b16_8 t66 = gm_l1;
            gm_trqfvmjtt4sij t67;
            gm_f_classify_e96658j((void *)&t67, (void *)NULL, cx, NULL, t65, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l2 = t67;
            gm_b16_8 t68 = gm_l0;
            gm_b16_8 t69;
            GM_ST(void *, (uint8_t *)&t69, (void *)(gm_srh29phg8b30r));
            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(3));
            uint8_t t70 = gm_f_starts_with_6vvt01e(t68, t69);
            gm_l3 = t70;
            uint8_t t71 = gm_l3;
            if (t71) {
                gm_l72 = ((uint64_t)3ULL);
            } else {
                gm_l72 = ((uint64_t)0ULL);
            }
            uint64_t t72 = gm_l72;
            gm_l4 = t72;
            gm_trqfvmjtt4sij t73;
            gm_f_list_f0gjjbh_4((void *)&t73, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld64_1;
            }
            F.l5 = t73;
            uint64_t t74 = gm_l4;
            gm_l6 = t74;
            gm_l8 = ((uint64_t)0ULL);
            uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
            gm_l7 = t75;
            {
                for (;;) {
                    uint64_t t76 = gm_l8;
                    uint64_t t77 = gm_l7;
                    uint8_t t78 = (t76 < t77);
                    if (!t78) {
                        goto gm_lb2;
                    }
                    {
                        {
                            {
                                uint64_t t79 = gm_l8;
                                gm_l9 = t79;
                                void * t80 = GM_LD(void *, ((uint8_t *)&F.l2));
                                uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                uint64_t t82 = gm_l9;
                                if ((uintptr_t)t82 >= (uintptr_t)(t81)) gm_trap_index((uintptr_t)t82, t81, gm_srceoep1gquqdsq, 247, 17);
                                memmove(((uint8_t *)&F.l10), (((uint8_t *)t80) + (uintptr_t)t82 * 40), 40);
                                void * t83 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t85 = gm_l6;
                                uint64_t t86 = GM_LD(uint64_t, ((uint8_t *)&F.l10));
                                uint64_t t87 = (uintptr_t)t85;
                                uint64_t t88 = (uintptr_t)t86;
                                if (t87 > t88 || t88 > (uintptr_t)t84) gm_trap_slice(t87, t88, t84, gm_srceoep1gquqdsq, 248, 18);
                                uint64_t t89 = ((uintptr_t)t86) - (uintptr_t)t85;
                                gm_b16_8 t90;
                                GM_ST(void *, (uint8_t *)&t90, (void *)(((uint8_t *)t83 + (uintptr_t)t85 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(t89));
                                void * t91 = (void *)((uint8_t *)&F.l5);
                                gm_b0_1 t92 = gm_f_scan_gap_tkq0f9l((void *)NULL, cx, NULL, t90, t91);
                                if (gm_unw.kind) {
                                    goto gm_ld64_2;
                                }
                                void * t93 = (void *)((uint8_t *)&F.l5);
                                uint64_t t94 = gm_l9;
                                gm_tj63kbqqj2092 t95;
                                GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t95 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t95 + 24, 0);
                                GM_ST(uint64_t, (uint8_t *)&t95 + 32, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)0ULL));
                                GM_ST(uint64_t, ((uint8_t *)&t95 + 8), t94);
                                gm_tftb1odc739t5 t96 = gm_f_push_1e500nc_4(t93, &t95);
                                gm_l73 = t96;
                                uint8_t t97 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
                                if (!t97) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l73) + 1, gm_srceoep1gquqdsq, 249, 9);
                                uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                gm_l6 = t98;
                                goto gm_lb5;
                            }
gm_lb6:;
                            gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 246, 5);
                        }
gm_lb5:;
                    }
gm_lb4:;
                    uint64_t t99 = gm_l8;
                    uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                    uint64_t t102 = r100;
                    gm_l8 = t102;
                }
            }
gm_lb2:;
        }
        {
            void * t103 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t105 = gm_l6;
            uint64_t t106 = (uintptr_t)t105;
            uint64_t t107 = (uintptr_t)t104;
            if (t106 > t107 || t107 > (uintptr_t)t104) gm_trap_slice(t106, t107, t104, gm_srceoep1gquqdsq, 252, 14);
            uint64_t t108 = ((uintptr_t)t104) - (uintptr_t)t105;
            gm_b16_8 t109;
            GM_ST(void *, (uint8_t *)&t109, (void *)(((uint8_t *)t103 + (uintptr_t)t105 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(t108));
            void * t110 = (void *)((uint8_t *)&F.l5);
            gm_b0_1 t111 = gm_f_scan_gap_tkq0f9l((void *)NULL, cx, NULL, t109, t110);
            if (gm_unw.kind) {
                goto gm_ld64_2;
            }
            gm_t4c7ipjj9s3fp t112;
            gm_f_hash_map_ss6fp5u((void *)&t112, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld64_2;
            }
            F.l11 = t112;
            gm_trqfvmjtt4sij t113;
            gm_f_list_uvfu1k1((void *)&t113, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld64_3;
            }
            F.l12 = t113;
            uint8_t t114 = gm_l3;
            if (t114) {
                void * t115 = (void *)((uint8_t *)&F.l12);
                gm_b16_8 t116;
                GM_ST(void *, (uint8_t *)&t116, (void *)(gm_srh29phg8b30r));
                GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(3));
                gm_tftb1odc739t5 t117 = gm_f_extend_0cs85gs(t115, t116);
                gm_l74 = t117;
                uint8_t t118 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                if (!t118) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 1, gm_srceoep1gquqdsq, 257, 14);
            }
            gm_l13 = ((uint8_t)0);
            gm_l14 = ((uint64_t)0ULL);
            gm_t8c5h7u09n10a t119;
            GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t119 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t119 + 0), ((uint8_t)0ULL));
            gm_l15 = t119;
            gm_trqfvmjtt4sij t120;
            gm_f_list_g3vmkan_3((void *)&t120, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld64_3;
            }
            F.l16 = t120;
            gm_l17 = ((uint64_t)0ULL);
            gm_l18 = ((uint64_t)0ULL);
            gm_l19 = ((uint8_t)0);
            gm_l20 = ((uint8_t)0);
            gm_tf5srh4mp4b3i t121;
            memset(&t121, 0, sizeof t121);
            gm_l21 = t121;
            gm_l22 = ((uint8_t)0);
            gm_l24 = ((uint64_t)0ULL);
            uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
            gm_l23 = t122;
            {
                for (;;) {
                    {
                        uint64_t t123 = gm_l24;
                        uint64_t t124 = gm_l23;
                        uint8_t t125 = (t123 < t124);
                        if (!t125) {
                            goto gm_lb7;
                        }
                        {
                            {
                                {
                                    {
                                        {
                                            uint64_t t126 = gm_l24;
                                            gm_l25 = t126;
                                            void * t127 = GM_LD(void *, ((uint8_t *)&F.l5));
                                            uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                            uint64_t t129 = gm_l25;
                                            if ((uintptr_t)t129 >= (uintptr_t)(t128)) gm_trap_index((uintptr_t)t129, t128, gm_srceoep1gquqdsq, 278, 17);
                                            memmove(((uint8_t *)&F.l26), (((uint8_t *)t127) + (uintptr_t)t129 * 40), 40);
                                            uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                                            uint8_t t131 = (uint8_t)((t130 == ((uint8_t)2ULL)));
                                            gm_tj63kbqqj2092 t132 = GM_LD(gm_tj63kbqqj2092, ((uint8_t *)&F.l26));
                                            gm_tj63kbqqj2092 t133;
                                            memset(&t133, 0, sizeof t133);
                                            GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)2ULL));
                                            if (t131) {
                                                uint64_t t134 = gm_l14;
                                                uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                                                if (o136) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 280, 13);
                                                uint64_t t137 = r135;
                                                gm_l14 = t137;
                                                goto gm_lb9;
                                            }
                                            gm_l27 = ((uint8_t)0);
                                            gm_l28 = ((uint8_t)0);
                                            {
                                                {
                                                    uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                                                    uint8_t t139 = (uint8_t)(t138 == ((uint8_t)0ULL));
                                                    if (!t139) {
                                                        goto gm_lb13;
                                                    }
                                                    uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)&F.l26) + 8));
                                                    gm_l29 = t140;
                                                    void * t141 = GM_LD(void *, ((uint8_t *)&F.l2));
                                                    uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                    uint64_t t143 = gm_l29;
                                                    if ((uintptr_t)t143 >= (uintptr_t)(t142)) gm_trap_index((uintptr_t)t143, t142, gm_srceoep1gquqdsq, 286, 25);
                                                    uint8_t t144 = GM_LD(uint8_t, ((((uint8_t *)t141) + (uintptr_t)t143 * 40) + 35));
                                                    gm_l27 = t144;
                                                    uint8_t t145 = gm_l27;
                                                    if (t145) {
                                                        void * t146 = GM_LD(void *, ((uint8_t *)&F.l2));
                                                        uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                        uint64_t t148 = gm_l29;
                                                        if ((uintptr_t)t148 >= (uintptr_t)(t147)) gm_trap_index((uintptr_t)t148, t147, gm_srceoep1gquqdsq, 287, 41);
                                                        gm_b16_8 t149 = GM_LD(gm_b16_8, ((((uint8_t *)t146) + (uintptr_t)t148 * 40) + 16));
                                                        gm_b16_8 t150;
                                                        GM_ST(void *, (uint8_t *)&t150, (void *)(gm_s2cuflhqkkuaq));
                                                        GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(1));
                                                        uint8_t t151 = (gm_eq0iqiubfm7e(((uint8_t *)&t149), ((uint8_t *)&t150)));
                                                        gm_l75 = t151;
                                                    } else {
                                                        gm_l75 = ((uint8_t)0);
                                                    }
                                                    uint8_t t152 = gm_l75;
                                                    gm_l28 = t152;
                                                    goto gm_lb12;
                                                }
gm_lb13:;
                                            }
gm_lb12:;
                                            uint8_t t153 = gm_l13;
                                            if (t153) {
                                                uint64_t t154 = gm_l14;
                                                uint8_t t155 = ((t154 == ((uint64_t)0ULL)));
                                                gm_l77 = t155;
                                            } else {
                                                gm_l77 = ((uint8_t)0);
                                            }
                                            uint8_t t156 = gm_l77;
                                            if (t156) {
                                                {
                                                    {
                                                        uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                                                        uint8_t t158 = (uint8_t)(t157 == ((uint8_t)3ULL));
                                                        if (!t158) {
                                                            goto gm_lb15;
                                                        }
                                                        gm_l78 = ((uint8_t)1);
                                                        goto gm_lb14;
                                                    }
gm_lb15:;
                                                    {
                                                        gm_l78 = ((uint8_t)0);
                                                        goto gm_lb14;
                                                    }
gm_lb16:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 289, 47);
                                                }
gm_lb14:;
                                                uint8_t t159 = gm_l78;
                                                gm_l76 = t159;
                                            } else {
                                                gm_l76 = ((uint8_t)0);
                                            }
                                        }
                                        {
                                            uint8_t t160 = gm_l76;
                                            gm_l30 = t160;
                                            {
                                                {
                                                    uint64_t t161 = gm_l14;
                                                    uint8_t t162 = (t161 > ((uint64_t)0ULL));
                                                    if (!t162) {
                                                        goto gm_lb18;
                                                    }
                                                    uint8_t t163 = gm_l13;
                                                    if (!t163) {
                                                        goto gm_lb18;
                                                    }
                                                    void * t164 = (void *)((uint8_t *)&F.l12);
                                                    gm_tftb1odc739t5 t165 = gm_f_push_t83rg2m(t164, ((uint8_t)10ULL));
                                                    gm_l79 = t165;
                                                    uint8_t t166 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                                                    if (!t166) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l79) + 1, gm_srceoep1gquqdsq, 291, 13);
                                                    {
                                                        {
                                                            uint64_t t167 = gm_l14;
                                                            uint8_t t168 = (t167 >= ((uint64_t)2ULL));
                                                            if (!t168) {
                                                                goto gm_lb20;
                                                            }
                                                            uint8_t t169 = gm_l20;
                                                            uint8_t t170 = (uint8_t)!t169;
                                                            if (!t170) {
                                                                goto gm_lb20;
                                                            }
                                                            uint8_t t171 = gm_l28;
                                                            uint8_t t172 = (uint8_t)!t171;
                                                            if (!t172) {
                                                                goto gm_lb20;
                                                            }
                                                            void * t173 = (void *)((uint8_t *)&F.l12);
                                                            gm_tftb1odc739t5 t174 = gm_f_push_t83rg2m(t173, ((uint8_t)10ULL));
                                                            gm_l80 = t174;
                                                            uint8_t t175 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                                                            if (!t175) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 1, gm_srceoep1gquqdsq, 292, 67);
                                                            goto gm_lb19;
                                                        }
gm_lb20:;
                                                    }
gm_lb19:;
                                                    gm_l13 = ((uint8_t)0);
                                                    goto gm_lb17;
                                                }
gm_lb18:;
                                            }
gm_lb17:;
                                            gm_l14 = ((uint64_t)0ULL);
                                            uint8_t t176 = gm_l13;
                                            uint8_t t177 = (uint8_t)!t176;
                                            gm_l31 = t177;
                                            uint8_t t178 = gm_l31;
                                            if (t178) {
                                                gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                                                gm_b16_8 t180 = GM_LD(gm_b16_8, ((uint8_t *)&t179));
                                                uint64_t t181 = gm_l25;
                                                gm_tf5srh4mp4b3i t182 = gm_f_next_tok_qap88op(t180, t181);
                                                gm_l32 = t182;
                                                gm_tf5srh4mp4b3i t183 = gm_l32;
                                                gm_l21 = t183;
                                                gm_l33 = ((uint64_t)0ULL);
                                                uint8_t t184 = gm_l27;
                                                if (t184) {
                                                    void * t185 = (void *)((uint8_t *)&F.l16);
                                                    gm_f_pop_f3uajlg_3((void *)&F.l34, t185);
                                                    gm_l22 = ((uint8_t)0);
                                                    gm_l35 = ((uint64_t)0ULL);
                                                    {
                                                        {
                                                            uint8_t t186 = (uint8_t)((((uint8_t *)&F.l34))[48] != 0);
                                                            if (!t186) {
                                                                goto gm_lb22;
                                                            }
                                                            memmove(((uint8_t *)&F.l36), ((uint8_t *)&F.l34), 48);
                                                            uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&F.l36) + 40));
                                                            if (t187) {
                                                                uint8_t t188 = gm_l28;
                                                                gm_l81 = t188;
                                                            } else {
                                                                gm_l81 = ((uint8_t)0);
                                                            }
                                                            uint8_t t189 = gm_l81;
                                                            gm_l22 = t189;
                                                            uint64_t t190 = GM_LD(uint64_t, ((uint8_t *)&F.l36));
                                                            gm_l35 = t190;
                                                            goto gm_lb21;
                                                        }
gm_lb22:;
                                                    }
gm_lb21:;
                                                    uint64_t t191 = gm_l35;
                                                    gm_l17 = t191;
                                                    uint64_t t192 = gm_l35;
                                                    uint64_t r193; bool o194 = __builtin_mul_overflow(t192, ((uint64_t)4ULL), &r193);
                                                    if (o194) gm_trap("integer overflow in `*`", gm_srceoep1gquqdsq, 310, 26);
                                                    uint64_t t195 = r193;
                                                    gm_l33 = t195;
                                                } else {
                                                    uint64_t t196 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                                    uint8_t t197 = (t196 > ((uint64_t)0ULL));
                                                    if (t197) {
                                                        void * t198 = GM_LD(void *, ((uint8_t *)&F.l16));
                                                        uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                                        uint64_t t200 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                                        uint64_t r201; bool o202 = __builtin_sub_overflow(t200, ((uint64_t)1ULL), &r201);
                                                        if (o202) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 312, 65);
                                                        uint64_t t203 = r201;
                                                        if ((uintptr_t)t203 >= (uintptr_t)(t199)) gm_trap_index((uintptr_t)t203, t199, gm_srceoep1gquqdsq, 312, 53);
                                                        uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)t198) + (uintptr_t)t203 * 48));
                                                        uint64_t r205; bool o206 = __builtin_add_overflow(t204, ((uint64_t)1ULL), &r205);
                                                        if (o206) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 312, 53);
                                                        uint64_t t207 = r205;
                                                        gm_l82 = t207;
                                                    } else {
                                                        gm_l82 = ((uint64_t)0ULL);
                                                    }
                                                    uint64_t t208 = gm_l82;
                                                    gm_l17 = t208;
                                                    uint64_t t209 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                                    uint8_t t210 = ((t209 == ((uint64_t)0ULL)));
                                                    if (t210) {
                                                        gm_l83 = ((uint8_t)1);
                                                    } else {
                                                        void * t211 = GM_LD(void *, ((uint8_t *)&F.l16));
                                                        uint64_t t212 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                                        uint64_t t213 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                                        uint64_t r214; bool o215 = __builtin_sub_overflow(t213, ((uint64_t)1ULL), &r214);
                                                        if (o215) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 313, 66);
                                                        uint64_t t216 = r214;
                                                        if ((uintptr_t)t216 >= (uintptr_t)(t212)) gm_trap_index((uintptr_t)t216, t212, gm_srceoep1gquqdsq, 313, 54);
                                                        uint8_t t217 = GM_LD(uint8_t, ((((uint8_t *)t211) + (uintptr_t)t216 * 48) + 8));
                                                        gm_l83 = t217;
                                                    }
                                                    uint8_t t218 = gm_l83;
                                                    gm_l37 = t218;
                                                    gm_l38 = ((uint8_t)0);
                                                    gm_l39 = ((uint8_t)0);
                                                    {
                                                        {
                                                            uint8_t t219 = (uint8_t)((((uint8_t *)&gm_l32))[8] != 0);
                                                            if (!t219) {
                                                                goto gm_lb24;
                                                            }
                                                            uint64_t t220 = GM_LD(uint64_t, ((uint8_t *)&gm_l32));
                                                            gm_l40 = t220;
                                                            void * t221 = GM_LD(void *, ((uint8_t *)&F.l2));
                                                            uint64_t t222 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                            uint64_t t223 = gm_l40;
                                                            if ((uintptr_t)t223 >= (uintptr_t)(t222)) gm_trap_index((uintptr_t)t223, t222, gm_srceoep1gquqdsq, 317, 35);
                                                            gm_b16_8 t224 = GM_LD(gm_b16_8, ((((uint8_t *)t221) + (uintptr_t)t223 * 40) + 16));
                                                            gm_b16_8 t225;
                                                            GM_ST(void *, (uint8_t *)&t225, (void *)(gm_s920dpvvv6cej));
                                                            GM_ST(uintptr_t, (uint8_t *)&t225 + 8, (uintptr_t)(3));
                                                            gm_b16_8 t226;
                                                            GM_ST(void *, (uint8_t *)&t226, (void *)(gm_spi47lg4tr80j));
                                                            GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(2));
                                                            gm_b16_8 t227;
                                                            GM_ST(void *, (uint8_t *)&t227, (void *)(gm_s9es2tos1bhfp));
                                                            GM_ST(uintptr_t, (uint8_t *)&t227 + 8, (uintptr_t)(4));
                                                            gm_tfeab279c16rr t228;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t228 + 0), t225);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t228 + 16), t226);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t228 + 32), t227);
                                                            uint8_t t229 = 0;
                                                            if (gm_eq0iqiubfm7e(((uint8_t *)&t228) + 0, ((uint8_t *)&t224))) t229 = 1;
                                                            if (gm_eq0iqiubfm7e(((uint8_t *)&t228) + 16, ((uint8_t *)&t224))) t229 = 1;
                                                            if (gm_eq0iqiubfm7e(((uint8_t *)&t228) + 32, ((uint8_t *)&t224))) t229 = 1;
                                                            uint8_t t230 = (uint8_t)(t229);
                                                            if (t230) {
                                                                gm_l84 = ((uint8_t)1);
                                                            } else {
                                                                gm_b16_8 t231 = gm_l0;
                                                                void * t232 = GM_LD(void *, ((uint8_t *)&F.l2));
                                                                uint64_t t233 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                                uint64_t t234 = gm_l40;
                                                                if ((uintptr_t)t234 >= (uintptr_t)(t233)) gm_trap_index((uintptr_t)t234, t233, gm_srceoep1gquqdsq, 317, 108);
                                                                uint8_t t235 = gm_f_continues_with_name_ejtvq2k(t231, &(*(gm_t88jm1dgsmkjs *)(((uint8_t *)t232) + (uintptr_t)t234 * 40)));
                                                                gm_l84 = t235;
                                                            }
                                                            uint8_t t236 = gm_l84;
                                                            gm_l38 = t236;
                                                            void * t237 = GM_LD(void *, ((uint8_t *)&F.l2));
                                                            uint64_t t238 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                            uint64_t t239 = gm_l40;
                                                            if ((uintptr_t)t239 >= (uintptr_t)(t238)) gm_trap_index((uintptr_t)t239, t238, gm_srceoep1gquqdsq, 318, 31);
                                                            gm_b16_8 t240 = GM_LD(gm_b16_8, ((((uint8_t *)t237) + (uintptr_t)t239 * 40) + 16));
                                                            gm_b16_8 t241;
                                                            GM_ST(void *, (uint8_t *)&t241, (void *)(gm_s9es2tos1bhfp));
                                                            GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(4));
                                                            uint8_t t242 = (gm_eq0iqiubfm7e(((uint8_t *)&t240), ((uint8_t *)&t241)));
                                                            gm_l39 = t242;
                                                            goto gm_lb23;
                                                        }
gm_lb24:;
                                                    }
gm_lb23:;
                                                    uint8_t t243 = gm_l22;
                                                    if (t243) {
                                                        {
                                                            {
                                                                uint8_t t244 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                                                                uint8_t t245 = (uint8_t)(t244 == ((uint8_t)0ULL));
                                                                if (!t245) {
                                                                    goto gm_lb26;
                                                                }
                                                                gm_l87 = ((uint8_t)1);
                                                                goto gm_lb25;
                                                            }
gm_lb26:;
                                                            {
                                                                gm_l87 = ((uint8_t)0);
                                                                goto gm_lb25;
                                                            }
gm_lb27:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 320, 47);
                                                        }
gm_lb25:;
                                                        uint8_t t246 = gm_l87;
                                                        gm_l86 = t246;
                                                    } else {
                                                        gm_l86 = ((uint8_t)0);
                                                    }
                                                    uint8_t t247 = gm_l86;
                                                    if (t247) {
                                                        uint8_t t248 = gm_l39;
                                                        gm_l85 = t248;
                                                    } else {
                                                        gm_l85 = ((uint8_t)0);
                                                    }
                                                    uint8_t t249 = gm_l85;
                                                    gm_l41 = t249;
                                                    uint8_t t250 = gm_l37;
                                                    if (t250) {
                                                        uint8_t t251 = gm_l41;
                                                        uint8_t t252 = (uint8_t)!t251;
                                                        gm_l90 = t252;
                                                    } else {
                                                        gm_l90 = ((uint8_t)0);
                                                    }
                                                    uint8_t t253 = gm_l90;
                                                    if (t253) {
                                                        uint8_t t254 = gm_l20;
                                                        uint8_t t255 = (uint8_t)!t254;
                                                        gm_l89 = t255;
                                                    } else {
                                                        gm_l89 = ((uint8_t)0);
                                                    }
                                                    uint8_t t256 = gm_l89;
                                                    if (t256) {
                                                        uint8_t t257 = gm_l19;
                                                        if (t257) {
                                                            gm_l91 = ((uint8_t)1);
                                                        } else {
                                                            uint8_t t258 = gm_l38;
                                                            gm_l91 = t258;
                                                        }
                                                        uint8_t t259 = gm_l91;
                                                        gm_l88 = t259;
                                                    } else {
                                                        gm_l88 = ((uint8_t)0);
                                                    }
                                                    uint8_t t260 = gm_l88;
                                                    gm_l42 = t260;
                                                    gm_tf5srh4mp4b3i t261;
                                                    memset(&t261, 0, sizeof t261);
                                                    gm_l43 = t261;
                                                    uint64_t t262 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                                    uint8_t t263 = (t262 > ((uint64_t)0ULL));
                                                    if (t263) {
                                                        void * t264 = GM_LD(void *, ((uint8_t *)&F.l16));
                                                        uint64_t t265 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                                        uint64_t t266 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                                        uint64_t r267; bool o268 = __builtin_sub_overflow(t266, ((uint64_t)1ULL), &r267);
                                                        if (o268) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 326, 38);
                                                        uint64_t t269 = r267;
                                                        if ((uintptr_t)t269 >= (uintptr_t)(t265)) gm_trap_index((uintptr_t)t269, t265, gm_srceoep1gquqdsq, 326, 26);
                                                        memmove(((uint8_t *)&F.l44), (((uint8_t *)t264) + (uintptr_t)t269 * 48), 48);
                                                        uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&F.l44) + 9));
                                                        if (t270) {
                                                            {
                                                                {
                                                                    uint8_t t271 = (uint8_t)(((((uint8_t *)&F.l44) + 16))[16] != 0);
                                                                    if (!t271) {
                                                                        goto gm_lb29;
                                                                    }
                                                                    gm_t2lqu8jsj511d t272 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)&F.l44) + 16));
                                                                    gm_l45 = t272;
                                                                    {
                                                                        {
                                                                            uint8_t t273 = (uint8_t)((((uint8_t *)&gm_l32))[8] != 0);
                                                                            if (!t273) {
                                                                                goto gm_lb31;
                                                                            }
                                                                            uint64_t t274 = GM_LD(uint64_t, ((uint8_t *)&gm_l32));
                                                                            gm_l46 = t274;
                                                                            gm_b16_8 t275 = gm_l0;
                                                                            void * t276 = GM_LD(void *, ((uint8_t *)&F.l2));
                                                                            uint64_t t277 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                                            uint64_t t278 = gm_l46;
                                                                            if ((uintptr_t)t278 >= (uintptr_t)(t277)) gm_trap_index((uintptr_t)t278, t277, gm_srceoep1gquqdsq, 330, 48);
                                                                            uint64_t t279 = GM_LD(uint64_t, (((uint8_t *)t276) + (uintptr_t)t278 * 40));
                                                                            uint64_t t280 = gm_f_column_r91o38o(t275, t279);
                                                                            uint64_t t281 = GM_LD(uint64_t, ((uint8_t *)&gm_l45));
                                                                            uint8_t t282 = ((t280 == t281));
                                                                            if (t282) {
                                                                                uint64_t t283 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                                                                                gm_tf5srh4mp4b3i t284;
                                                                                GM_ST(uint8_t, (uint8_t *)&t284 + 9, 0);
                                                                                GM_ST(uint16_t, (uint8_t *)&t284 + 10, 0);
                                                                                GM_ST(uint32_t, (uint8_t *)&t284 + 12, 0);
                                                                                GM_ST(uint64_t, ((uint8_t *)&t284), t283);
                                                                                ((uint8_t *)&t284)[8] = 1;
                                                                                gm_l43 = t284;
                                                                            }
                                                                            goto gm_lb30;
                                                                        }
gm_lb31:;
                                                                    }
gm_lb30:;
                                                                    goto gm_lb28;
                                                                }
gm_lb29:;
                                                            }
gm_lb28:;
                                                        }
                                                    }
                                                    uint64_t t285 = gm_l17;
                                                    gm_l47 = t285;
                                                    uint8_t t286 = gm_l42;
                                                    if (t286) {
                                                        uint64_t t287 = gm_l47;
                                                        uint64_t r288; bool o289 = __builtin_add_overflow(t287, ((uint64_t)1ULL), &r288);
                                                        if (o289) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 336, 32);
                                                        uint64_t t290 = r288;
                                                        gm_l47 = t290;
                                                    }
                                                    {
                                                        {
                                                            uint8_t t291 = (uint8_t)((((uint8_t *)&gm_l43))[8] != 0);
                                                            if (!t291) {
                                                                goto gm_lb33;
                                                            }
                                                            uint64_t t292 = GM_LD(uint64_t, ((uint8_t *)&gm_l43));
                                                            gm_l48 = t292;
                                                            uint64_t t293 = gm_l48;
                                                            gm_l92 = t293;
                                                            goto gm_lb32;
                                                        }
gm_lb33:;
                                                        uint64_t t294 = gm_l47;
                                                        uint64_t r295; bool o296 = __builtin_mul_overflow(t294, ((uint64_t)4ULL), &r295);
                                                        if (o296) gm_trap("integer overflow in `*`", gm_srceoep1gquqdsq, 337, 59);
                                                        uint64_t t297 = r295;
                                                        gm_l92 = t297;
                                                    }
gm_lb32:;
                                                    uint64_t t298 = gm_l92;
                                                    gm_l33 = t298;
                                                }
                                                uint64_t t299 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                                gm_l18 = t299;
                                                void * t300 = (void *)((uint8_t *)&F.l12);
                                                uint64_t t301 = gm_l33;
                                                gm_b0_1 t302 = gm_f_push_spaces_732bua4((void *)NULL, cx, NULL, t300, t301);
                                                if (gm_unw.kind) {
                                                    goto gm_ld64_4;
                                                }
                                                gm_l13 = ((uint8_t)1);
                                                gm_t8c5h7u09n10a t303;
                                                GM_ST(uint8_t, (uint8_t *)&t303 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t303 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t303 + 4, 0);
                                                GM_ST(uint64_t, (uint8_t *)&t303 + 8, 0);
                                                GM_ST(uint8_t, ((uint8_t *)&t303 + 0), ((uint8_t)0ULL));
                                                gm_l15 = t303;
                                            } else {
                                                uint8_t t304 = gm_l27;
                                                if (t304) {
                                                    {
                                                        {
                                                            void * t305 = (void *)((uint8_t *)&F.l16);
                                                            gm_f_pop_f3uajlg_3((void *)&gm_l93, t305);
                                                            uint8_t t306 = (uint8_t)((((uint8_t *)&gm_l93))[48] != 0);
                                                            if (!t306) {
                                                                goto gm_lb35;
                                                            }
                                                            memmove(((uint8_t *)&F.l49), ((uint8_t *)&gm_l93), 48);
                                                            uint8_t t307 = GM_LD(uint8_t, (((uint8_t *)&F.l49) + 40));
                                                            if (t307) {
                                                                uint8_t t308 = gm_l28;
                                                                gm_l94 = t308;
                                                            } else {
                                                                gm_l94 = ((uint8_t)0);
                                                            }
                                                            uint8_t t309 = gm_l94;
                                                            gm_l22 = t309;
                                                            uint64_t t310 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                                            uint64_t t311 = gm_l18;
                                                            uint8_t t312 = (t310 < t311);
                                                            if (t312) {
                                                                uint64_t t313 = GM_LD(uint64_t, ((uint8_t *)&F.l49));
                                                                gm_l17 = t313;
                                                                uint64_t t314 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                                                gm_l18 = t314;
                                                            }
                                                            goto gm_lb34;
                                                        }
gm_lb35:;
                                                    }
gm_lb34:;
                                                }
                                            }
                                        }
                                        {
                                            gm_l50 = ((uint8_t)0);
                                            {
                                                {
                                                    {
                                                        uint8_t t315 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                                                        uint8_t t316 = (uint8_t)(t315 == ((uint8_t)2ULL));
                                                        if (!t316) {
                                                            goto gm_lb37;
                                                        }
                                                        goto gm_lb36;
                                                    }
gm_lb37:;
                                                    {
                                                        uint8_t t317 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                                                        uint8_t t318 = (uint8_t)(t317 == ((uint8_t)1ULL));
                                                        if (!t318) {
                                                            goto gm_lb38;
                                                        }
                                                        void * t319 = (void *)((uint8_t *)&F.l12);
                                                        gm_tftb1odc739t5 t320 = gm_f_push_t83rg2m(t319, ((uint8_t)59ULL));
                                                        gm_l95 = t320;
                                                        uint8_t t321 = (uint8_t)!((((uint8_t *)&gm_l95))[0] != 0);
                                                        if (!t321) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l95) + 1, gm_srceoep1gquqdsq, 356, 17);
                                                        gm_t8c5h7u09n10a t322;
                                                        GM_ST(uint8_t, (uint8_t *)&t322 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t322 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t322 + 4, 0);
                                                        GM_ST(uint64_t, (uint8_t *)&t322 + 8, 0);
                                                        GM_ST(uint8_t, ((uint8_t *)&t322 + 0), ((uint8_t)2ULL));
                                                        gm_l15 = t322;
                                                        gm_l19 = ((uint8_t)0);
                                                        goto gm_lb36;
                                                    }
gm_lb38:;
                                                    {
                                                        uint8_t t323 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                                                        uint8_t t324 = (uint8_t)(t323 == ((uint8_t)3ULL));
                                                        if (!t324) {
                                                            goto gm_lb39;
                                                        }
                                                        memmove(((uint8_t *)&gm_l51), (((uint8_t *)&F.l26) + 8), 32);
                                                        uint8_t t325 = gm_l31;
                                                        uint8_t t326 = (uint8_t)!t325;
                                                        if (t326) {
                                                            {
                                                                {
                                                                    uint64_t t327 = GM_LD(uint64_t, ((((uint8_t *)&gm_l51) + 16) + 8));
                                                                    uint8_t t328 = (t327 > ((uint64_t)0ULL));
                                                                    if (!t328) {
                                                                        goto gm_lb41;
                                                                    }
                                                                    gm_b16_8 t329 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 16));
                                                                    uint8_t t330 = gm_f_only_spaces_bvta5sq(t329);
                                                                    if (!t330) {
                                                                        goto gm_lb41;
                                                                    }
                                                                    void * t331 = (void *)((uint8_t *)&F.l12);
                                                                    gm_b16_8 t332 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 16));
                                                                    gm_tftb1odc739t5 t333 = gm_f_extend_0cs85gs(t331, t332);
                                                                    gm_l96 = t333;
                                                                    uint8_t t334 = (uint8_t)!((((uint8_t *)&gm_l96))[0] != 0);
                                                                    if (!t334) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l96) + 1, gm_srceoep1gquqdsq, 363, 25);
                                                                    goto gm_lb40;
                                                                }
gm_lb41:;
                                                                void * t335 = (void *)((uint8_t *)&F.l12);
                                                                gm_tftb1odc739t5 t336 = gm_f_push_t83rg2m(t335, ((uint8_t)32ULL));
                                                                gm_l97 = t336;
                                                                uint8_t t337 = (uint8_t)!((((uint8_t *)&gm_l97))[0] != 0);
                                                                if (!t337) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l97) + 1, gm_srceoep1gquqdsq, 365, 25);
                                                            }
gm_lb40:;
                                                        }
                                                        void * t338 = (void *)((uint8_t *)&F.l12);
                                                        gm_b16_8 t339 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l51));
                                                        gm_tftb1odc739t5 t340 = gm_f_extend_0cs85gs(t338, t339);
                                                        gm_l98 = t340;
                                                        uint8_t t341 = (uint8_t)!((((uint8_t *)&gm_l98))[0] != 0);
                                                        if (!t341) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l98) + 1, gm_srceoep1gquqdsq, 368, 17);
                                                        gm_t8c5h7u09n10a t342;
                                                        GM_ST(uint8_t, (uint8_t *)&t342 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t342 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t342 + 4, 0);
                                                        GM_ST(uint64_t, (uint8_t *)&t342 + 8, 0);
                                                        GM_ST(uint8_t, ((uint8_t *)&t342 + 0), ((uint8_t)2ULL));
                                                        gm_l15 = t342;
                                                        goto gm_lb36;
                                                    }
gm_lb39:;
                                                    {
                                                        uint8_t t343 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                                                        uint8_t t344 = (uint8_t)(t343 == ((uint8_t)4ULL));
                                                        if (!t344) {
                                                            goto gm_lb42;
                                                        }
                                                        gm_b16_8 t345 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 8));
                                                        gm_l52 = t345;
                                                        gm_l53 = ((uint8_t)0);
                                                        {
                                                            {
                                                                uint8_t t346 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                                                                uint8_t t347 = (uint8_t)(t346 == ((uint8_t)1ULL));
                                                                if (!t347) {
                                                                    goto gm_lb44;
                                                                }
                                                                uint64_t t348 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                                gm_l54 = t348;
                                                                void * t349 = GM_LD(void *, ((uint8_t *)&F.l2));
                                                                uint64_t t350 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                                uint64_t t351 = gm_l54;
                                                                if ((uintptr_t)t351 >= (uintptr_t)(t350)) gm_trap_index((uintptr_t)t351, t350, gm_srceoep1gquqdsq, 373, 48);
                                                                uint8_t t352 = GM_LD(uint8_t, ((((uint8_t *)t349) + (uintptr_t)t351 * 40) + 33));
                                                                gm_l53 = t352;
                                                                goto gm_lb43;
                                                            }
gm_lb44:;
                                                        }
gm_lb43:;
                                                        {
                                                            {
                                                                uint8_t t353 = gm_l31;
                                                                uint8_t t354 = (uint8_t)!t353;
                                                                if (!t354) {
                                                                    goto gm_lb46;
                                                                }
                                                                uint8_t t355 = gm_l53;
                                                                uint8_t t356 = (uint8_t)!t355;
                                                                if (!t356) {
                                                                    goto gm_lb46;
                                                                }
                                                                void * t357 = (void *)((uint8_t *)&F.l12);
                                                                gm_tftb1odc739t5 t358 = gm_f_push_t83rg2m(t357, ((uint8_t)32ULL));
                                                                gm_l99 = t358;
                                                                uint8_t t359 = (uint8_t)!((((uint8_t *)&gm_l99))[0] != 0);
                                                                if (!t359) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l99) + 1, gm_srceoep1gquqdsq, 374, 48);
                                                                goto gm_lb45;
                                                            }
gm_lb46:;
                                                        }
gm_lb45:;
                                                        void * t360 = (void *)((uint8_t *)&F.l12);
                                                        gm_b16_8 t361 = gm_l52;
                                                        gm_b16_8 t362;
                                                        GM_ST(void *, (uint8_t *)&t362, (void *)(gm_skmt2kt2p7v0u));
                                                        GM_ST(uintptr_t, (uint8_t *)&t362 + 8, (uintptr_t)(2));
                                                        gm_b16_8 t363;
                                                        GM_ST(void *, (uint8_t *)&t363, (void *)(gm_so514oj5nbhcu));
                                                        GM_ST(uintptr_t, (uint8_t *)&t363 + 8, (uintptr_t)(1));
                                                        gm_tftb1odc739t5 t364 = gm_f_replace_lf6hcmn(t360, t361, t362, t363);
                                                        gm_l100 = t364;
                                                        uint8_t t365 = (uint8_t)!((((uint8_t *)&gm_l100))[0] != 0);
                                                        if (!t365) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l100) + 1, gm_srceoep1gquqdsq, 376, 17);
                                                        gm_t8c5h7u09n10a t366;
                                                        GM_ST(uint8_t, (uint8_t *)&t366 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t366 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t366 + 4, 0);
                                                        GM_ST(uint64_t, (uint8_t *)&t366 + 8, 0);
                                                        GM_ST(uint8_t, ((uint8_t *)&t366 + 0), ((uint8_t)2ULL));
                                                        gm_l15 = t366;
                                                        goto gm_lb36;
                                                    }
gm_lb42:;
                                                }
                                                {
                                                    {
                                                        {
                                                            uint8_t t367 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                                                            uint8_t t368 = (uint8_t)(t367 == ((uint8_t)0ULL));
                                                            if (!t368) {
                                                                goto gm_lb47;
                                                            }
                                                            uint64_t t369 = GM_LD(uint64_t, (((uint8_t *)&F.l26) + 8));
                                                            gm_l55 = t369;
                                                            void * t370 = GM_LD(void *, ((uint8_t *)&F.l2));
                                                            uint64_t t371 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                            uint64_t t372 = gm_l55;
                                                            if ((uintptr_t)t372 >= (uintptr_t)(t371)) gm_trap_index((uintptr_t)t372, t371, gm_srceoep1gquqdsq, 380, 25);
                                                            memmove(((uint8_t *)&F.l56), (((uint8_t *)t370) + (uintptr_t)t372 * 40), 40);
                                                            gm_l57 = ((uint64_t)1ULL);
                                                            gm_l58 = ((uint8_t)0);
                                                            {
                                                                {
                                                                    uint8_t t373 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                                                                    uint8_t t374 = (uint8_t)(t373 == ((uint8_t)0ULL));
                                                                    if (!t374) {
                                                                        goto gm_lb49;
                                                                    }
                                                                    goto gm_lb48;
                                                                }
gm_lb49:;
                                                                {
                                                                    uint8_t t375 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                                                                    uint8_t t376 = (uint8_t)(t375 == ((uint8_t)1ULL));
                                                                    if (!t376) {
                                                                        goto gm_lb50;
                                                                    }
                                                                    uint64_t t377 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                                    gm_l59 = t377;
                                                                    void * t378 = GM_LD(void *, ((uint8_t *)&F.l2));
                                                                    uint64_t t379 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                                    uint64_t t380 = gm_l59;
                                                                    if ((uintptr_t)t380 >= (uintptr_t)(t379)) gm_trap_index((uintptr_t)t380, t379, gm_srceoep1gquqdsq, 386, 34);
                                                                    memmove(((uint8_t *)&F.l60), (((uint8_t *)t378) + (uintptr_t)t380 * 40), 40);
                                                                    uint8_t t381 = GM_LD(uint8_t, (((uint8_t *)&F.l60) + 33));
                                                                    if (t381) {
                                                                        gm_l101 = ((uint8_t)1);
                                                                    } else {
                                                                        uint8_t t382 = GM_LD(uint8_t, (((uint8_t *)&F.l56) + 32));
                                                                        gm_l101 = t382;
                                                                    }
                                                                    uint8_t t383 = gm_l101;
                                                                    gm_l61 = t383;
                                                                    void * t384 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                                    uint64_t t385 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                                    uint64_t t386 = GM_LD(uint64_t, (((uint8_t *)&F.l60) + 8));
                                                                    uint64_t t387 = GM_LD(uint64_t, ((uint8_t *)&F.l56));
                                                                    uint64_t t388 = (uintptr_t)t386;
                                                                    uint64_t t389 = (uintptr_t)t387;
                                                                    if (t388 > t389 || t389 > (uintptr_t)t385) gm_trap_slice(t388, t389, t385, gm_srceoep1gquqdsq, 388, 32);
                                                                    uint64_t t390 = ((uintptr_t)t387) - (uintptr_t)t386;
                                                                    gm_b16_8 t391;
                                                                    GM_ST(void *, (uint8_t *)&t391, (void *)(((uint8_t *)t384 + (uintptr_t)t386 * 1)));
                                                                    GM_ST(uintptr_t, (uint8_t *)&t391 + 8, (uintptr_t)(t390));
                                                                    gm_l62 = t391;
                                                                    gm_b16_8 t392 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 16));
                                                                    gm_b16_8 t393;
                                                                    GM_ST(void *, (uint8_t *)&t393, (void *)(gm_ss67f0gmfen1m));
                                                                    GM_ST(uintptr_t, (uint8_t *)&t393 + 8, (uintptr_t)(2));
                                                                    gm_b16_8 t394;
                                                                    GM_ST(void *, (uint8_t *)&t394, (void *)(gm_sm8obe455p2d9));
                                                                    GM_ST(uintptr_t, (uint8_t *)&t394 + 8, (uintptr_t)(2));
                                                                    gm_t6j6smj7qoctu t395;
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t395 + 0), t393);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t395 + 16), t394);
                                                                    uint8_t t396 = 0;
                                                                    if (gm_eq0iqiubfm7e(((uint8_t *)&t395) + 0, ((uint8_t *)&t392))) t396 = 1;
                                                                    if (gm_eq0iqiubfm7e(((uint8_t *)&t395) + 16, ((uint8_t *)&t392))) t396 = 1;
                                                                    uint8_t t397 = (uint8_t)(t396);
                                                                    if (t397) {
                                                                        gm_l102 = ((uint64_t)1ULL);
                                                                    } else {
                                                                        gm_l102 = ((uint64_t)2ULL);
                                                                    }
                                                                    uint64_t t398 = gm_l102;
                                                                    gm_l63 = t398;
                                                                    {
                                                                        {
                                                                            uint8_t t399 = gm_l61;
                                                                            uint8_t t400 = (uint8_t)!t399;
                                                                            if (!t400) {
                                                                                goto gm_lb52;
                                                                            }
                                                                            uint64_t t401 = GM_LD(uint64_t, (((uint8_t *)&gm_l62) + 8));
                                                                            uint64_t t402 = gm_l63;
                                                                            uint8_t t403 = (t401 >= t402);
                                                                            if (!t403) {
                                                                                goto gm_lb52;
                                                                            }
                                                                            gm_b16_8 t404 = gm_l62;
                                                                            uint8_t t405 = gm_f_only_spaces_bvta5sq(t404);
                                                                            if (!t405) {
                                                                                goto gm_lb52;
                                                                            }
                                                                            gm_b16_8 t406 = gm_l0;
                                                                            uint8_t t407 = gm_f_keeps_padding_m0v49h1(t406, &(*(gm_t88jm1dgsmkjs *)((uint8_t *)&F.l60)), &(*(gm_t88jm1dgsmkjs *)((uint8_t *)&F.l56)));
                                                                            if (!t407) {
                                                                                goto gm_lb52;
                                                                            }
                                                                            void * t408 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                                            uint64_t t409 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                                            gm_b16_8 t410 = gm_l0;
                                                                            uint64_t t411 = GM_LD(uint64_t, ((uint8_t *)&F.l56));
                                                                            uint64_t t412 = gm_f_line_start_pl10jt5(t410, t411);
                                                                            uint64_t t413 = GM_LD(uint64_t, ((uint8_t *)&F.l56));
                                                                            uint64_t t414 = (uintptr_t)t412;
                                                                            uint64_t t415 = (uintptr_t)t413;
                                                                            if (t414 > t415 || t415 > (uintptr_t)t409) gm_trap_slice(t414, t415, t409, gm_srceoep1gquqdsq, 396, 53);
                                                                            uint64_t t416 = ((uintptr_t)t413) - (uintptr_t)t412;
                                                                            gm_b16_8 t417;
                                                                            GM_ST(void *, (uint8_t *)&t417, (void *)(((uint8_t *)t408 + (uintptr_t)t412 * 1)));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t417 + 8, (uintptr_t)(t416));
                                                                            uint64_t t418 = gm_f_relative_column_ctuu0pd(t417);
                                                                            gm_l64 = t418;
                                                                            gm_b16_8 t419 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                                                            gm_b16_8 t420 = GM_LD(gm_b16_8, ((uint8_t *)&t419));
                                                                            gm_b16_8 t421 = gm_f_last_line_hk6q5l4(t420);
                                                                            uint64_t t422 = gm_f_relative_column_ctuu0pd(t421);
                                                                            gm_l65 = t422;
                                                                            uint64_t t423 = gm_l64;
                                                                            uint64_t t424 = gm_l65;
                                                                            uint8_t t425 = (t423 > t424);
                                                                            if (t425) {
                                                                                uint64_t t426 = gm_l64;
                                                                                uint64_t t427 = gm_l65;
                                                                                uint64_t r428; bool o429 = __builtin_sub_overflow(t426, t427, &r428);
                                                                                if (o429) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 398, 52);
                                                                                uint64_t t430 = r428;
                                                                                gm_l103 = t430;
                                                                            } else {
                                                                                gm_l103 = ((uint64_t)1ULL);
                                                                            }
                                                                            uint64_t t431 = gm_l103;
                                                                            gm_l57 = t431;
                                                                            goto gm_lb51;
                                                                        }
gm_lb52:;
                                                                    }
gm_lb51:;
                                                                    uint64_t t432 = GM_LD(uint64_t, (((uint8_t *)&gm_l62) + 8));
                                                                    uint8_t t433 = ((t432 == ((uint64_t)0ULL)));
                                                                    if (t433) {
                                                                        void * t434 = GM_LD(void *, (((uint8_t *)&F.l60) + 16));
                                                                        uint64_t t435 = GM_LD(uint64_t, ((((uint8_t *)&F.l60) + 16) + 8));
                                                                        uint64_t t436 = GM_LD(uint64_t, ((((uint8_t *)&F.l60) + 16) + 8));
                                                                        uint64_t r437; bool o438 = __builtin_sub_overflow(t436, ((uint64_t)1ULL), &r437);
                                                                        if (o438) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 401, 70);
                                                                        uint64_t t439 = r437;
                                                                        if ((uintptr_t)t439 >= (uintptr_t)(t435)) gm_trap_index((uintptr_t)t439, t435, gm_srceoep1gquqdsq, 401, 59);
                                                                        uint8_t t440 = GM_LD(uint8_t, (((uint8_t *)t434) + (uintptr_t)t439 * 1));
                                                                        uint8_t t441 = gm_f_is_word_kb7cdmu(t440);
                                                                        gm_l105 = t441;
                                                                    } else {
                                                                        gm_l105 = ((uint8_t)0);
                                                                    }
                                                                    uint8_t t442 = gm_l105;
                                                                    if (t442) {
                                                                        void * t443 = GM_LD(void *, (((uint8_t *)&F.l56) + 16));
                                                                        uint64_t t444 = GM_LD(uint64_t, ((((uint8_t *)&F.l56) + 16) + 8));
                                                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t444)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t444, gm_srceoep1gquqdsq, 401, 103);
                                                                        uint8_t t445 = GM_LD(uint8_t, (((uint8_t *)t443) + (uintptr_t)((uint64_t)0ULL) * 1));
                                                                        uint8_t t446 = gm_f_is_word_kb7cdmu(t445);
                                                                        gm_l104 = t446;
                                                                    } else {
                                                                        gm_l104 = ((uint8_t)0);
                                                                    }
                                                                    uint8_t t447 = gm_l104;
                                                                    gm_l66 = t447;
                                                                    uint8_t t448 = gm_l66;
                                                                    uint8_t t449 = (uint8_t)!t448;
                                                                    if (t449) {
                                                                        uint8_t t450 = gm_l61;
                                                                        uint8_t t451 = (uint8_t)!t450;
                                                                        if (t451) {
                                                                            gm_l108 = ((uint8_t)1);
                                                                        } else {
                                                                            gm_b16_8 t452 = GM_LD(gm_b16_8, (((uint8_t *)&F.l60) + 16));
                                                                            gm_b16_8 t453 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 16));
                                                                            void * t454 = (void *)((uint8_t *)&F.l11);
                                                                            uint8_t t455 = gm_f_would_merge_44g1m96((void *)NULL, cx, NULL, t452, t453, t454);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_ld64_4;
                                                                            }
                                                                            gm_l108 = t455;
                                                                        }
                                                                        uint8_t t456 = gm_l108;
                                                                        if (t456) {
                                                                            gm_l107 = ((uint8_t)1);
                                                                        } else {
                                                                            gm_b16_8 t457 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                                                            gm_b16_8 t458 = GM_LD(gm_b16_8, ((uint8_t *)&t457));
                                                                            gm_b16_8 t459 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 16));
                                                                            uint8_t t460 = gm_f_merges_with_tail_ijmh3nm((void *)NULL, cx, NULL, t458, t459);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_ld64_4;
                                                                            }
                                                                            gm_l107 = t460;
                                                                        }
                                                                        uint8_t t461 = gm_l107;
                                                                        gm_l106 = t461;
                                                                    } else {
                                                                        gm_l106 = ((uint8_t)0);
                                                                    }
                                                                    uint8_t t462 = gm_l106;
                                                                    gm_l58 = t462;
                                                                    goto gm_lb48;
                                                                }
gm_lb50:;
                                                                {
                                                                    uint8_t t463 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                                                                    uint8_t t464 = (uint8_t)(t463 == ((uint8_t)2ULL));
                                                                    if (!t464) {
                                                                        goto gm_lb53;
                                                                    }
                                                                    uint8_t t465 = GM_LD(uint8_t, (((uint8_t *)&F.l56) + 32));
                                                                    uint8_t t466 = (uint8_t)!t465;
                                                                    gm_l58 = t466;
                                                                    goto gm_lb48;
                                                                }
gm_lb53:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 383, 17);
                                                            }
gm_lb48:;
                                                        }
                                                        {
                                                            uint8_t t467 = gm_l58;
                                                            if (t467) {
                                                                void * t468 = (void *)((uint8_t *)&F.l12);
                                                                uint64_t t469 = gm_l57;
                                                                gm_b0_1 t470 = gm_f_push_spaces_732bua4((void *)NULL, cx, NULL, t468, t469);
                                                                if (gm_unw.kind) {
                                                                    goto gm_ld64_4;
                                                                }
                                                            }
                                                            {
                                                                {
                                                                    uint8_t t471 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                                                                    uint8_t t472 = (uint8_t)(t471 == ((uint8_t)1ULL));
                                                                    if (!t472) {
                                                                        goto gm_lb55;
                                                                    }
                                                                    uint64_t t473 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                                    gm_l67 = t473;
                                                                    uint64_t t474 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                                                    uint8_t t475 = (t474 > ((uint64_t)0ULL));
                                                                    if (t475) {
                                                                        void * t476 = GM_LD(void *, ((uint8_t *)&F.l16));
                                                                        uint64_t t477 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                                                        uint64_t t478 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                                                        uint64_t r479; bool o480 = __builtin_sub_overflow(t478, ((uint64_t)1ULL), &r479);
                                                                        if (o480) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 409, 43);
                                                                        uint64_t t481 = r479;
                                                                        if ((uintptr_t)t481 >= (uintptr_t)(t477)) gm_trap_index((uintptr_t)t481, t477, gm_srceoep1gquqdsq, 409, 31);
                                                                        void * t482 = (void *)(((uint8_t *)t476) + (uintptr_t)t481 * 48);
                                                                        gm_l68 = t482;
                                                                        {
                                                                            {
                                                                                void * t483 = gm_l68;
                                                                                uint8_t t484 = GM_LD(uint8_t, (((uint8_t *)t483) + 9));
                                                                                if (!t484) {
                                                                                    goto gm_lb57;
                                                                                }
                                                                                void * t485 = gm_l68;
                                                                                {
                                                                                    {
                                                                                        uint8_t t486 = (uint8_t)(((((uint8_t *)t485) + 16))[16] != 0);
                                                                                        if (t486) {
                                                                                            goto gm_lb59;
                                                                                        }
                                                                                        gm_l109 = ((uint8_t)1);
                                                                                        goto gm_lb58;
                                                                                    }
gm_lb59:;
                                                                                    {
                                                                                        gm_l109 = ((uint8_t)0);
                                                                                        goto gm_lb58;
                                                                                    }
gm_lb60:;
                                                                                    gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 410, 40);
                                                                                }
gm_lb58:;
                                                                                uint8_t t487 = gm_l109;
                                                                                if (!t487) {
                                                                                    goto gm_lb57;
                                                                                }
                                                                                void * t488 = GM_LD(void *, ((uint8_t *)&F.l2));
                                                                                uint64_t t489 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                                                uint64_t t490 = gm_l67;
                                                                                if ((uintptr_t)t490 >= (uintptr_t)(t489)) gm_trap_index((uintptr_t)t490, t489, gm_srceoep1gquqdsq, 410, 61);
                                                                                uint8_t t491 = GM_LD(uint8_t, ((((uint8_t *)t488) + (uintptr_t)t490 * 40) + 34));
                                                                                if (!t491) {
                                                                                    goto gm_lb57;
                                                                                }
                                                                                uint8_t t492 = GM_LD(uint8_t, (((uint8_t *)&F.l56) + 35));
                                                                                uint8_t t493 = (uint8_t)!t492;
                                                                                if (!t493) {
                                                                                    goto gm_lb57;
                                                                                }
                                                                                void * t494 = gm_l68;
                                                                                gm_b16_8 t495 = gm_l0;
                                                                                uint64_t t496 = GM_LD(uint64_t, ((uint8_t *)&F.l56));
                                                                                uint64_t t497 = gm_f_column_r91o38o(t495, t496);
                                                                                void * t498 = (void *)((uint8_t *)&F.l12);
                                                                                uint64_t t499 = gm_f_out_column_hr4hgng(t498);
                                                                                gm_t2lqu8jsj511d t500;
                                                                                GM_ST(uint64_t, ((uint8_t *)&t500 + 0), t497);
                                                                                GM_ST(uint64_t, ((uint8_t *)&t500 + 8), t499);
                                                                                gm_tc70a9dbbmgkg t501;
                                                                                GM_ST(uint8_t, (uint8_t *)&t501 + 17, 0);
                                                                                GM_ST(uint16_t, (uint8_t *)&t501 + 18, 0);
                                                                                GM_ST(uint32_t, (uint8_t *)&t501 + 20, 0);
                                                                                GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t501), t500);
                                                                                ((uint8_t *)&t501)[16] = 1;
                                                                                GM_ST(gm_tc70a9dbbmgkg, (((uint8_t *)t494) + 16), t501);
                                                                                goto gm_lb56;
                                                                            }
gm_lb57:;
                                                                        }
gm_lb56:;
                                                                    }
                                                                    goto gm_lb54;
                                                                }
gm_lb55:;
                                                            }
gm_lb54:;
                                                            void * t502 = (void *)((uint8_t *)&F.l12);
                                                            gm_b16_8 t503 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 16));
                                                            gm_tftb1odc739t5 t504 = gm_f_extend_0cs85gs(t502, t503);
                                                            gm_l110 = t504;
                                                        }
                                                        {
                                                            uint8_t t505 = (uint8_t)!((((uint8_t *)&gm_l110))[0] != 0);
                                                            if (!t505) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l110) + 1, gm_srceoep1gquqdsq, 415, 17);
                                                            uint8_t t506 = GM_LD(uint8_t, (((uint8_t *)&F.l56) + 34));
                                                            if (t506) {
                                                                gm_b16_8 t507 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 16));
                                                                gm_b16_8 t508;
                                                                GM_ST(void *, (uint8_t *)&t508, (void *)(gm_sh40na9qbga3l));
                                                                GM_ST(uintptr_t, (uint8_t *)&t508 + 8, (uintptr_t)(1));
                                                                uint8_t t509 = (gm_eq0iqiubfm7e(((uint8_t *)&t507), ((uint8_t *)&t508)));
                                                                gm_l69 = t509;
                                                                gm_l70 = ((uint8_t)0);
                                                                uint8_t t510 = gm_l69;
                                                                if (t510) {
                                                                    {
                                                                        {
                                                                            uint8_t t511 = (uint8_t)((((uint8_t *)&gm_l21))[8] != 0);
                                                                            if (!t511) {
                                                                                goto gm_lb62;
                                                                            }
                                                                            uint64_t t512 = GM_LD(uint64_t, ((uint8_t *)&gm_l21));
                                                                            gm_l71 = t512;
                                                                            void * t513 = GM_LD(void *, ((uint8_t *)&F.l2));
                                                                            uint64_t t514 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                                                            uint64_t t515 = gm_l71;
                                                                            if ((uintptr_t)t515 >= (uintptr_t)(t514)) gm_trap_index((uintptr_t)t515, t514, gm_srceoep1gquqdsq, 420, 53);
                                                                            gm_b16_8 t516 = GM_LD(gm_b16_8, ((((uint8_t *)t513) + (uintptr_t)t515 * 40) + 16));
                                                                            gm_b16_8 t517;
                                                                            GM_ST(void *, (uint8_t *)&t517, (void *)(gm_sl9nvct67or45));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t517 + 8, (uintptr_t)(2));
                                                                            gm_b16_8 t518;
                                                                            GM_ST(void *, (uint8_t *)&t518, (void *)(gm_slpo22sjs83b1));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t518 + 8, (uintptr_t)(4));
                                                                            gm_b16_8 t519;
                                                                            GM_ST(void *, (uint8_t *)&t519, (void *)(gm_s9es2tos1bhfp));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t519 + 8, (uintptr_t)(4));
                                                                            gm_tfeab279c16rr t520;
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t520 + 0), t517);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t520 + 16), t518);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t520 + 32), t519);
                                                                            uint8_t t521 = 0;
                                                                            if (gm_eq0iqiubfm7e(((uint8_t *)&t520) + 0, ((uint8_t *)&t516))) t521 = 1;
                                                                            if (gm_eq0iqiubfm7e(((uint8_t *)&t520) + 16, ((uint8_t *)&t516))) t521 = 1;
                                                                            if (gm_eq0iqiubfm7e(((uint8_t *)&t520) + 32, ((uint8_t *)&t516))) t521 = 1;
                                                                            uint8_t t522 = (uint8_t)(t521);
                                                                            gm_l70 = t522;
                                                                            goto gm_lb61;
                                                                        }
gm_lb62:;
                                                                    }
gm_lb61:;
                                                                }
                                                                void * t523 = (void *)((uint8_t *)&F.l16);
                                                                uint64_t t524 = gm_l17;
                                                                uint8_t t525 = gm_l69;
                                                                if (t525) {
                                                                    uint8_t t526 = GM_LD(uint8_t, (((uint8_t *)&F.l56) + 36));
                                                                    uint8_t t527 = (uint8_t)!t526;
                                                                    gm_l111 = t527;
                                                                } else {
                                                                    gm_l111 = ((uint8_t)0);
                                                                }
                                                                uint8_t t528 = gm_l111;
                                                                uint8_t t529 = gm_l69;
                                                                uint8_t t530 = (uint8_t)!t529;
                                                                gm_tc70a9dbbmgkg t531;
                                                                memset(&t531, 0, sizeof t531);
                                                                uint8_t t532 = gm_l70;
                                                                gm_tsnno3vacsa4v t533;
                                                                GM_ST(uint16_t, (uint8_t *)&t533 + 10, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&t533 + 12, 0);
                                                                GM_ST(uint8_t, (uint8_t *)&t533 + 41, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&t533 + 42, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&t533 + 44, 0);
                                                                GM_ST(uint64_t, ((uint8_t *)&t533 + 0), t524);
                                                                GM_ST(uint8_t, ((uint8_t *)&t533 + 8), t528);
                                                                GM_ST(uint8_t, ((uint8_t *)&t533 + 9), t530);
                                                                GM_ST(gm_tc70a9dbbmgkg, ((uint8_t *)&t533 + 16), t531);
                                                                GM_ST(uint8_t, ((uint8_t *)&t533 + 40), t532);
                                                                gm_tftb1odc739t5 t534 = gm_f_push_8h1r7s6_3(t523, &t533);
                                                                gm_l112 = t534;
                                                                uint8_t t535 = (uint8_t)!((((uint8_t *)&gm_l112))[0] != 0);
                                                                if (!t535) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l112) + 1, gm_srceoep1gquqdsq, 422, 21);
                                                            }
                                                            uint8_t t536 = gm_f_continues_line_2cliit4(&(*(gm_t88jm1dgsmkjs *)((uint8_t *)&F.l56)));
                                                            gm_l19 = t536;
                                                            uint8_t t537 = GM_LD(uint8_t, (((uint8_t *)&F.l56) + 35));
                                                            uint8_t t538 = (uint8_t)!t537;
                                                            if (t538) {
                                                                gm_l22 = ((uint8_t)0);
                                                            }
                                                            gm_b16_8 t539 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 16));
                                                            gm_b16_8 t540;
                                                            GM_ST(void *, (uint8_t *)&t540, (void *)(gm_sh40na9qbga3l));
                                                            GM_ST(uintptr_t, (uint8_t *)&t540 + 8, (uintptr_t)(1));
                                                            uint8_t t541 = (gm_eq0iqiubfm7e(((uint8_t *)&t539), ((uint8_t *)&t540)));
                                                            if (t541) {
                                                                uint8_t t542 = GM_LD(uint8_t, (((uint8_t *)&F.l56) + 33));
                                                                uint8_t t543 = (uint8_t)!t542;
                                                                gm_l113 = t543;
                                                            } else {
                                                                gm_l113 = ((uint8_t)0);
                                                            }
                                                            uint8_t t544 = gm_l113;
                                                            gm_l50 = t544;
                                                        }
                                                        {
                                                            uint64_t t545 = gm_l55;
                                                            gm_t8c5h7u09n10a t546;
                                                            GM_ST(uint8_t, (uint8_t *)&t546 + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t546 + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t546 + 4, 0);
                                                            GM_ST(uint8_t, ((uint8_t *)&t546 + 0), ((uint8_t)1ULL));
                                                            GM_ST(uint64_t, ((uint8_t *)&t546 + 8), t545);
                                                            gm_l15 = t546;
                                                            goto gm_lb36;
                                                        }
                                                    }
gm_lb47:;
                                                }
                                                {
                                                    gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 353, 9);
                                                }
                                            }
gm_lb36:;
                                        }
                                        {
                                            uint8_t t547 = gm_l50;
                                            if (t547) {
                                                gm_l114 = ((uint8_t)1);
                                            } else {
                                                uint8_t t548 = gm_l30;
                                                if (t548) {
                                                    uint8_t t549 = gm_l20;
                                                    gm_l115 = t549;
                                                } else {
                                                    gm_l115 = ((uint8_t)0);
                                                }
                                                uint8_t t550 = gm_l115;
                                                gm_l114 = t550;
                                            }
                                            uint8_t t551 = gm_l114;
                                            gm_l20 = t551;
                                            goto gm_lb10;
                                        }
                                    }
gm_lb11:;
                                }
                                {
                                    gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 277, 5);
                                }
                            }
gm_lb10:;
                        }
gm_lb9:;
                    }
                    {
                        uint64_t t552 = gm_l24;
                        uint64_t r553; bool o554 = __builtin_add_overflow(t552, ((uint64_t)1ULL), &r553);
                        uint64_t t555 = r553;
                        gm_l24 = t555;
                    }
                }
            }
gm_lb7:;
        }
        {
            uint8_t t556 = gm_l13;
            if (t556) {
                void * t557 = (void *)((uint8_t *)&F.l12);
                gm_tftb1odc739t5 t558 = gm_f_push_t83rg2m(t557, ((uint8_t)10ULL));
                gm_l116 = t558;
                uint8_t t559 = (uint8_t)!((((uint8_t *)&gm_l116))[0] != 0);
                if (!t559) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l116) + 1, gm_srceoep1gquqdsq, 438, 20);
            }
            memmove(&F.ret, ((uint8_t *)&F.l12), 40);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld64_4;
        }
    }
    {
        void * t560 = (void *)((uint8_t *)&F.l16);
        gm_f_deinit_ectdopm_2(t560);
    }
    {
        void * t561 = (void *)((uint8_t *)&F.l11);
        gm_f_deinit_p1kmdo2(t561);
    }
    {
        void * t562 = (void *)((uint8_t *)&F.l5);
        gm_f_deinit_aii0n0l(t562);
    }
    {
        void * t563 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_26uqnl1(t563);
    }
    goto gm_ls564;
gm_ld64_4:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t565 = (void *)((uint8_t *)&F.l16);
        gm_f_deinit_ectdopm_2(t565);
        gm_unw = gm_saved;
    }
gm_ld64_3:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t566 = (void *)((uint8_t *)&F.l11);
        gm_f_deinit_p1kmdo2(t566);
        gm_unw = gm_saved;
    }
gm_ld64_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t567 = (void *)((uint8_t *)&F.l5);
        gm_f_deinit_aii0n0l(t567);
        gm_unw = gm_saved;
    }
gm_ld64_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t568 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_26uqnl1(t568);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls564:;
gm_lb1:;
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
__attribute__((aligned(4))) uint8_t gm_f_lexes_apart_9scqa7n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_lexes_apart_9scqa7n F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    gm_b16_8 gm_l6; /* first */
    gm_b16_8 gm_l7; /* second */
    uint64_t gm_l8; /* count */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l12; /* piece */
    GM_STACK_CHECK(&F, 955u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    uint8_t gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    {
        gm_tjt4ka19str13 t10;
        gm_f_new_jr5ejs0((void *)&t10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t10;
        {
            void * t11 = (void *)((uint8_t *)&F.l2);
            gm_t3mf58gv9kf3n t12 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t11));
            gm_t3mf58gv9kf3n t13 = t12;
            gm_node wn14 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t13, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
            gm_trqfvmjtt4sij t17;
            gm_f_list_uvfu1k1((void *)&t17, (void *)NULL, (&wn14), NULL);
            if (gm_unw.kind) {
                goto gm_ll16;
            }
            F.l3 = t17;
            void * t18 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t19 = gm_l0;
            gm_tftb1odc739t5 t20 = gm_f_extend_0cs85gs(t18, t19);
            gm_l13 = t20;
            uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srceoep1gquqdsq, 99, 9);
            void * t22 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t23 = gm_l1;
            gm_tftb1odc739t5 t24 = gm_f_extend_0cs85gs(t22, t23);
            gm_l14 = t24;
            uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srceoep1gquqdsq, 100, 9);
            gm_t2792tsg5eg72 t26;
            gm_f_diagnostics_n3kkhf4((void *)&t26, (void *)NULL, (&wn14), NULL);
            if (gm_unw.kind) {
                goto gm_ll16;
            }
            F.l4 = t26;
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            void * t29 = (void *)((uint8_t *)&F.l4);
            gm_f_lex_dc8mbmf((void *)&F.l5, (void *)NULL, (&wn14), NULL, ((uint32_t)0ULL), t28, t29);
            if (gm_unw.kind) {
                goto gm_ll16;
            }
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(0));
            gm_l6 = t30;
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(0));
            gm_l7 = t31;
            gm_l8 = ((uint64_t)0ULL);
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
            gm_l9 = t32;
            gm_l10 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t33 = gm_l10;
                    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    uint8_t t35 = (t33 < t34);
                    if (!t35) {
                        goto gm_lb3;
                    }
                    {
                        void * t36 = GM_LD(void *, ((uint8_t *)&gm_l9));
                        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                        uint64_t t38 = gm_l10;
                        {
                            {
                                memmove(((uint8_t *)&F.l11), (((uint8_t *)t36) + (uintptr_t)t38 * 48), 48);
                                gm_t3fcbuehh3bs2 t39;
                                GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t39 + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&t39 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t39 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)9ULL));
                                gm_t3fcbuehh3bs2 t40;
                                GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t40 + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&t40 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t40 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)10ULL));
                                gm_t23tohc57ebhi t41;
                                GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t41 + 0), t39);
                                GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t41 + 32), t40);
                                gm_t3fcbuehh3bs2 t42 = GM_LD(gm_t3fcbuehh3bs2, ((uint8_t *)&F.l11));
                                uint8_t t43 = 0;
                                if (gm_eqt7ftn1841u(((uint8_t *)&t41) + 0, ((uint8_t *)&t42))) t43 = 1;
                                if (gm_eqt7ftn1841u(((uint8_t *)&t41) + 32, ((uint8_t *)&t42))) t43 = 1;
                                uint8_t t44 = (uint8_t)(t43);
                                if (t44) {
                                    goto gm_lb5;
                                }
                                void * t45 = GM_LD(void *, ((uint8_t *)&F.l3));
                                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                uint32_t t47 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 32) + 4));
                                uint64_t t48 = (uint64_t)t47;
                                uint32_t t49 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 32) + 8));
                                uint64_t t50 = (uint64_t)t49;
                                uint64_t t51 = (uintptr_t)t48;
                                uint64_t t52 = (uintptr_t)t50;
                                if (t51 > t52 || t52 > (uintptr_t)t46) gm_trap_slice(t51, t52, t46, gm_srceoep1gquqdsq, 108, 22);
                                uint64_t t53 = ((uintptr_t)t50) - (uintptr_t)t48;
                                gm_b16_8 t54;
                                GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t45 + (uintptr_t)t48 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53));
                                gm_l12 = t54;
                                uint64_t t55 = gm_l8;
                                uint8_t t56 = ((t55 == ((uint64_t)0ULL)));
                                if (t56) {
                                    gm_b16_8 t57 = gm_l12;
                                    gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                                    gm_l6 = t58;
                                }
                                uint64_t t59 = gm_l8;
                                uint8_t t60 = ((t59 == ((uint64_t)1ULL)));
                                if (t60) {
                                    gm_b16_8 t61 = gm_l12;
                                    gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                                    gm_l7 = t62;
                                }
                                uint64_t t63 = gm_l8;
                                uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                                if (o65) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 111, 13);
                                uint64_t t66 = r64;
                                gm_l8 = t66;
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 106, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t67 = gm_l10;
                    uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                    uint64_t t70 = r68;
                    gm_l10 = t70;
                }
            }
gm_lb3:;
            uint64_t t71 = gm_l8;
            uint8_t t72 = (!(t71 == ((uint64_t)2ULL)));
            if (t72) {
                gm_l17 = ((uint8_t)1);
            } else {
                gm_b16_8 t73 = gm_l6;
                gm_b16_8 t74 = gm_l0;
                uint8_t t75 = (!gm_eq0iqiubfm7e(((uint8_t *)&t73), ((uint8_t *)&t74)));
                gm_l17 = t75;
            }
            uint8_t t76 = gm_l17;
            if (t76) {
                gm_l16 = ((uint8_t)1);
            } else {
                gm_b16_8 t77 = gm_l7;
                gm_b16_8 t78 = gm_l1;
                uint8_t t79 = (!gm_eq0iqiubfm7e(((uint8_t *)&t77), ((uint8_t *)&t78)));
                gm_l16 = t79;
            }
            uint8_t t80 = gm_l16;
            if (t80) {
                gm_l15 = ((uint8_t)1);
            } else {
                void * t81 = (void *)((uint8_t *)&F.l4);
                uint8_t t82 = gm_f_has_errors_if7ai0m(t81);
                gm_l15 = t82;
            }
            uint8_t t83 = gm_l15;
            F.ret = t83;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ll16;
        }
        goto gm_ls84;
gm_ll16:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we15; }
        goto gm_ld9_1;
gm_ls84:;
gm_lb2:;
we15:;
    }
    {
        void * t85 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_sejdpqv(t85);
    }
    goto gm_ls86;
gm_ld9_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t87 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_sejdpqv(t87);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls86:;
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
__attribute__((aligned(4))) uint8_t gm_f_merges_with_tail_ijmh3nm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_merges_with_tail_ijmh3nm F;
    gm_b16_8 gm_l0; /* out */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* tail */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 727u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    {
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t16 = ((t15 == ((uint64_t)0ULL)));
        if (t16) {
            gm_l11 = ((uint8_t)1);
        } else {
            void * t17 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t18, gm_srceoep1gquqdsq, 120, 40);
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t17) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint8_t t20 = gm_f_is_punct_char_3bc3hom(t19);
            uint8_t t21 = (uint8_t)!t20;
            gm_l11 = t21;
        }
        uint8_t t22 = gm_l11;
        if (t22) {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        gm_l2 = t23;
        {
            for (;;) {
                uint64_t t24 = gm_l2;
                uint8_t t25 = (t24 > ((uint64_t)0ULL));
                if (!t25) {
                    goto gm_lb2;
                }
                uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t27 = gm_l2;
                uint64_t r28; bool o29 = __builtin_sub_overflow(t26, t27, &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 122, 25);
                uint64_t t30 = r28;
                uint8_t t31 = (t30 < ((uint64_t)3ULL));
                if (!t31) {
                    goto gm_lb2;
                }
                void * t32 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t34 = gm_l2;
                uint64_t r35; bool o36 = __builtin_sub_overflow(t34, ((uint64_t)1ULL), &r35);
                if (o36) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 122, 67);
                uint64_t t37 = r35;
                if ((uintptr_t)t37 >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)t37, t33, gm_srceoep1gquqdsq, 122, 63);
                uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t32) + (uintptr_t)t37 * 1));
                uint8_t t39 = gm_f_is_punct_char_3bc3hom(t38);
                if (!t39) {
                    goto gm_lb2;
                }
                {
                    uint64_t t40 = gm_l2;
                    uint64_t r41; bool o42 = __builtin_sub_overflow(t40, ((uint64_t)1ULL), &r41);
                    if (o42) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 122, 81);
                    uint64_t t43 = r41;
                    gm_l2 = t43;
                }
gm_lb4:;
            }
        }
gm_lb2:;
        void * t44 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t46 = gm_l2;
        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t48 = (uintptr_t)t46;
        uint64_t t49 = (uintptr_t)t47;
        if (t48 > t49 || t49 > (uintptr_t)t45) gm_trap_slice(t48, t49, t45, gm_srceoep1gquqdsq, 123, 13);
        uint64_t t50 = ((uintptr_t)t47) - (uintptr_t)t46;
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(((uint8_t *)t44 + (uintptr_t)t46 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(t50));
        gm_l3 = t51;
        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t53 = (t52 < ((uint64_t)2ULL));
        if (t53) {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_tjt4ka19str13 t54;
        gm_f_new_jr5ejs0((void *)&t54, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t54;
        {
            void * t55 = (void *)((uint8_t *)&F.l4);
            gm_t3mf58gv9kf3n t56 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t55));
            gm_t3mf58gv9kf3n t57 = t56;
            gm_node wn58 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t57, .hctx = cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
            gm_trqfvmjtt4sij t61;
            gm_f_list_uvfu1k1((void *)&t61, (void *)NULL, (&wn58), NULL);
            if (gm_unw.kind) {
                goto gm_ll60;
            }
            F.l5 = t61;
            void * t62 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t63 = gm_l3;
            gm_tftb1odc739t5 t64 = gm_f_extend_0cs85gs(t62, t63);
            gm_l12 = t64;
            uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srceoep1gquqdsq, 129, 9);
            void * t66 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t67 = gm_l1;
            gm_tftb1odc739t5 t68 = gm_f_extend_0cs85gs(t66, t67);
            gm_l13 = t68;
            uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srceoep1gquqdsq, 130, 9);
            gm_t2792tsg5eg72 t70;
            gm_f_diagnostics_n3kkhf4((void *)&t70, (void *)NULL, (&wn58), NULL);
            if (gm_unw.kind) {
                goto gm_ll60;
            }
            F.l6 = t70;
            gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
            void * t73 = (void *)((uint8_t *)&F.l6);
            gm_f_lex_dc8mbmf((void *)&F.l7, (void *)NULL, (&wn58), NULL, ((uint32_t)0ULL), t72, t73);
            if (gm_unw.kind) {
                goto gm_ll60;
            }
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
            gm_l8 = t74;
            gm_l9 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t75 = gm_l9;
                    uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint8_t t77 = (t75 < t76);
                    if (!t77) {
                        goto gm_lb6;
                    }
                    {
                        void * t78 = GM_LD(void *, ((uint8_t *)&gm_l8));
                        uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint64_t t80 = gm_l9;
                        {
                            {
                                memmove(((uint8_t *)&F.l10), (((uint8_t *)t78) + (uintptr_t)t80 * 48), 48);
                                uint32_t t81 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 32) + 4));
                                uint64_t t82 = (uint64_t)t81;
                                uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                uint8_t t84 = ((t82 == t83));
                                if (t84) {
                                    F.ret = ((uint8_t)0);
                                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                                    goto gm_ll60;
                                }
                                {
                                    {
                                        uint32_t t85 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 32) + 4));
                                        uint64_t t86 = (uint64_t)t85;
                                        uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        uint8_t t88 = (t86 < t87);
                                        if (!t88) {
                                            goto gm_lb12;
                                        }
                                        uint32_t t89 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 32) + 8));
                                        uint64_t t90 = (uint64_t)t89;
                                        uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        uint8_t t92 = (t90 > t91);
                                        if (!t92) {
                                            goto gm_lb12;
                                        }
                                        F.ret = ((uint8_t)1);
                                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                                        goto gm_ll60;
                                    }
gm_lb12:;
                                }
gm_lb11:;
                                goto gm_lb9;
                            }
gm_lb10:;
                            gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 133, 9);
                        }
gm_lb9:;
                    }
gm_lb8:;
                    uint64_t t93 = gm_l9;
                    uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                    uint64_t t96 = r94;
                    gm_l9 = t96;
                }
            }
gm_lb6:;
            F.ret = ((uint8_t)1);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ll60;
        }
        goto gm_ls97;
gm_ll60:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we59; }
        goto gm_ld14_1;
gm_ls97:;
gm_lb5:;
we59:;
    }
    {
        void * t98 = (void *)((uint8_t *)&F.l4);
        gm_f_deinit_sejdpqv(t98);
    }
    goto gm_ls99;
gm_ld14_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t100 = (void *)((uint8_t *)&F.l4);
        gm_f_deinit_sejdpqv(t100);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls99:;
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_tok_qap88op(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_tok_qap88op F;
    gm_b16_8 gm_l0; /* items */
    uint64_t gm_l1; /* from */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* k */
    uint64_t gm_l5; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 219u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t9 = gm_l1;
    gm_l3 = t9;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l2 = t10;
    {
        for (;;) {
            uint64_t t11 = gm_l3;
            uint64_t t12 = gm_l2;
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t14 = gm_l3;
                        gm_l4 = t14;
                        {
                            {
                                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t17 = gm_l4;
                                uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t15) + (uintptr_t)t17 * 40) + 0));
                                uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
                                if (!t19) {
                                    goto gm_lb7;
                                }
                                uint64_t t20 = GM_LD(uint64_t, ((((uint8_t *)t15) + (uintptr_t)t17 * 40) + 8));
                                gm_l5 = t20;
                                uint64_t t21 = gm_l5;
                                gm_tf5srh4mp4b3i t22;
                                GM_ST(uint8_t, (uint8_t *)&t22 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t22 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t22 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t22), t21);
                                ((uint8_t *)&t22)[8] = 1;
                                F.ret = t22;
                                goto gm_fret;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 230, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t23 = gm_l3;
            uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
            uint64_t t26 = r24;
            gm_l3 = t26;
        }
    }
gm_lb1:;
    gm_tf5srh4mp4b3i t27;
    memset(&t27, 0, sizeof t27);
    F.ret = t27;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_relative_column_ctuu0pd(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_scan_gap_tkq0f9l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_scan_gap_tkq0f9l F;
    gm_b16_8 gm_l0; /* gap */
    void * gm_l1; /* items */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* ws_start */
    uint8_t gm_l4; /* b */
    uint64_t gm_l5; /* end */
    int32_t gm_l6; /* depth */
    uint64_t gm_l7; /* j */
    GM_STACK_CHECK(&F, 1049u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tqs5p4n2tq19v gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tqs5p4n2tq19v gm_l12;
    gm_tqs5p4n2tq19v gm_l13;
    gm_tqs5p4n2tq19v gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_l2 = ((uint64_t)0ULL);
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t16 = gm_l2;
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t18 = (t16 < t17);
            if (!t18) {
                goto gm_lb1;
            }
            {
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t21 = gm_l2;
                uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)t19) + (uintptr_t)t21 * 1));
                gm_l4 = t22;
                uint8_t t23 = gm_l4;
                uint8_t t24 = ((t23 == ((uint8_t)10ULL)));
                if (t24) {
                    void * t25 = gm_l1;
                    gm_tj63kbqqj2092 t26;
                    memset(&t26, 0, sizeof t26);
                    GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)2ULL));
                    gm_tftb1odc739t5 t27 = gm_f_push_1e500nc_4(t25, &t26);
                    gm_l8 = t27;
                    uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                    if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srceoep1gquqdsq, 34, 13);
                    uint64_t t29 = gm_l2;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    if (o31) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 35, 13);
                    uint64_t t32 = r30;
                    gm_l2 = t32;
                    uint64_t t33 = gm_l2;
                    gm_l3 = t33;
                } else {
                    uint8_t t34 = gm_l4;
                    uint8_t t35 = ((t34 == ((uint8_t)59ULL)));
                    if (t35) {
                        void * t36 = gm_l1;
                        gm_tj63kbqqj2092 t37;
                        memset(&t37, 0, sizeof t37);
                        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)1ULL));
                        gm_tftb1odc739t5 t38 = gm_f_push_1e500nc_4(t36, &t37);
                        gm_l9 = t38;
                        uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srceoep1gquqdsq, 38, 13);
                        uint64_t t40 = gm_l2;
                        uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                        if (o42) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 39, 13);
                        uint64_t t43 = r41;
                        gm_l2 = t43;
                        uint64_t t44 = gm_l2;
                        gm_l3 = t44;
                    } else {
                        {
                            {
                                uint8_t t45 = gm_l4;
                                uint8_t t46 = ((t45 == ((uint8_t)47ULL)));
                                if (!t46) {
                                    goto gm_lb5;
                                }
                                void * t47 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t49 = gm_l2;
                                uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                                if (o51) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 41, 40);
                                uint64_t t52 = r50;
                                uint8_t t53 = (t52 < t48);
                                if (t53) {
                                    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)t47) + (uintptr_t)t52 * 1));
                                    gm_tqs5p4n2tq19v t55;
                                    GM_ST(uint8_t, ((uint8_t *)&t55), t54);
                                    ((uint8_t *)&t55)[1] = 1;
                                    gm_l10 = t55;
                                } else {
                                    gm_tqs5p4n2tq19v t56;
                                    memset(&t56, 0, sizeof t56);
                                    gm_l10 = t56;
                                }
                                gm_tqs5p4n2tq19v t57 = gm_l10;
                                gm_tqs5p4n2tq19v t58;
                                GM_ST(uint8_t, ((uint8_t *)&t58), ((uint8_t)47ULL));
                                ((uint8_t *)&t58)[1] = 1;
                                uint8_t t59 = (gm_eqb44cb3cf1n(((uint8_t *)&t57), ((uint8_t *)&t58)));
                                if (!t59) {
                                    goto gm_lb5;
                                }
                                gm_b16_8 t60 = gm_l0;
                                uint64_t t61 = gm_l2;
                                uint64_t t62 = gm_f_line_end_jm825k5(t60, t61);
                                gm_l5 = t62;
                                void * t63 = gm_l1;
                                void * t64 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t66 = gm_l2;
                                uint64_t t67 = gm_l5;
                                uint64_t t68 = (uintptr_t)t66;
                                uint64_t t69 = (uintptr_t)t67;
                                if (t68 > t69 || t69 > (uintptr_t)t65) gm_trap_slice(t68, t69, t65, gm_srceoep1gquqdsq, 43, 54);
                                uint64_t t70 = ((uintptr_t)t67) - (uintptr_t)t66;
                                gm_b16_8 t71;
                                GM_ST(void *, (uint8_t *)&t71, (void *)(((uint8_t *)t64 + (uintptr_t)t66 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(t70));
                                gm_b16_8 t72 = gm_f_trim_end_d3d35bq(t71);
                                void * t73 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t75 = gm_l3;
                                uint64_t t76 = gm_l2;
                                uint64_t t77 = (uintptr_t)t75;
                                uint64_t t78 = (uintptr_t)t76;
                                if (t77 > t78 || t78 > (uintptr_t)t74) gm_trap_slice(t77, t78, t74, gm_srceoep1gquqdsq, 43, 72);
                                uint64_t t79 = ((uintptr_t)t76) - (uintptr_t)t75;
                                gm_b16_8 t80;
                                GM_ST(void *, (uint8_t *)&t80, (void *)(((uint8_t *)t73 + (uintptr_t)t75 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(t79));
                                gm_t6j6smj7qoctu t81;
                                GM_ST(gm_b16_8, ((uint8_t *)&t81 + 0), t72);
                                GM_ST(gm_b16_8, ((uint8_t *)&t81 + 16), t80);
                                gm_tj63kbqqj2092 t82;
                                GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)3ULL));
                                GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t82 + 8), t81);
                                gm_tftb1odc739t5 t83 = gm_f_push_1e500nc_4(t63, &t82);
                                gm_l11 = t83;
                                uint8_t t84 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                if (!t84) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srceoep1gquqdsq, 43, 13);
                                uint64_t t85 = gm_l5;
                                gm_l2 = t85;
                                uint64_t t86 = gm_l2;
                                gm_l3 = t86;
                                goto gm_lb4;
                            }
gm_lb5:;
                            {
                                {
                                    uint8_t t87 = gm_l4;
                                    uint8_t t88 = ((t87 == ((uint8_t)47ULL)));
                                    if (!t88) {
                                        goto gm_lb7;
                                    }
                                    void * t89 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                    uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                    uint64_t t91 = gm_l2;
                                    uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
                                    if (o93) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 46, 40);
                                    uint64_t t94 = r92;
                                    uint8_t t95 = (t94 < t90);
                                    if (t95) {
                                        uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)t89) + (uintptr_t)t94 * 1));
                                        gm_tqs5p4n2tq19v t97;
                                        GM_ST(uint8_t, ((uint8_t *)&t97), t96);
                                        ((uint8_t *)&t97)[1] = 1;
                                        gm_l12 = t97;
                                    } else {
                                        gm_tqs5p4n2tq19v t98;
                                        memset(&t98, 0, sizeof t98);
                                        gm_l12 = t98;
                                    }
                                    gm_tqs5p4n2tq19v t99 = gm_l12;
                                    gm_tqs5p4n2tq19v t100;
                                    GM_ST(uint8_t, ((uint8_t *)&t100), ((uint8_t)42ULL));
                                    ((uint8_t *)&t100)[1] = 1;
                                    uint8_t t101 = (gm_eqb44cb3cf1n(((uint8_t *)&t99), ((uint8_t *)&t100)));
                                    if (!t101) {
                                        goto gm_lb7;
                                    }
                                    gm_l6 = ((int32_t)1LL);
                                    uint64_t t102 = gm_l2;
                                    uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)2ULL), &r103);
                                    if (o104) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 48, 18);
                                    uint64_t t105 = r103;
                                    gm_l7 = t105;
                                    {
                                        for (;;) {
                                            int32_t t106 = gm_l6;
                                            uint8_t t107 = (t106 > ((int32_t)0LL));
                                            if (!t107) {
                                                goto gm_lb8;
                                            }
                                            uint64_t t108 = gm_l7;
                                            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                            uint8_t t110 = (t108 < t109);
                                            if (!t110) {
                                                goto gm_lb8;
                                            }
                                            {
                                                {
                                                    {
                                                        void * t111 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                        uint64_t t113 = gm_l7;
                                                        uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)t111) + (uintptr_t)t113 * 1));
                                                        uint8_t t115 = ((t114 == ((uint8_t)47ULL)));
                                                        if (!t115) {
                                                            goto gm_lb12;
                                                        }
                                                        void * t116 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                        uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                        uint64_t t118 = gm_l7;
                                                        uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                                                        if (o120) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 50, 46);
                                                        uint64_t t121 = r119;
                                                        uint8_t t122 = (t121 < t117);
                                                        if (t122) {
                                                            uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)t116) + (uintptr_t)t121 * 1));
                                                            gm_tqs5p4n2tq19v t124;
                                                            GM_ST(uint8_t, ((uint8_t *)&t124), t123);
                                                            ((uint8_t *)&t124)[1] = 1;
                                                            gm_l13 = t124;
                                                        } else {
                                                            gm_tqs5p4n2tq19v t125;
                                                            memset(&t125, 0, sizeof t125);
                                                            gm_l13 = t125;
                                                        }
                                                        gm_tqs5p4n2tq19v t126 = gm_l13;
                                                        gm_tqs5p4n2tq19v t127;
                                                        GM_ST(uint8_t, ((uint8_t *)&t127), ((uint8_t)42ULL));
                                                        ((uint8_t *)&t127)[1] = 1;
                                                        uint8_t t128 = (gm_eqb44cb3cf1n(((uint8_t *)&t126), ((uint8_t *)&t127)));
                                                        if (!t128) {
                                                            goto gm_lb12;
                                                        }
                                                        int32_t t129 = gm_l6;
                                                        int32_t r130; bool o131 = __builtin_add_overflow(t129, ((int32_t)1LL), &r130);
                                                        if (o131) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 51, 21);
                                                        int32_t t132 = r130;
                                                        gm_l6 = t132;
                                                        uint64_t t133 = gm_l7;
                                                        uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)2ULL), &r134);
                                                        if (o135) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 52, 21);
                                                        uint64_t t136 = r134;
                                                        gm_l7 = t136;
                                                        goto gm_lb11;
                                                    }
gm_lb12:;
                                                    {
                                                        {
                                                            void * t137 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                            uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                            uint64_t t139 = gm_l7;
                                                            if ((uintptr_t)t139 >= (uintptr_t)(t138)) gm_trap_index((uintptr_t)t139, t138, gm_srceoep1gquqdsq, 53, 27);
                                                            uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)t137) + (uintptr_t)t139 * 1));
                                                            uint8_t t141 = ((t140 == ((uint8_t)42ULL)));
                                                            if (!t141) {
                                                                goto gm_lb14;
                                                            }
                                                            void * t142 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                            uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                            uint64_t t144 = gm_l7;
                                                            uint64_t r145; bool o146 = __builtin_add_overflow(t144, ((uint64_t)1ULL), &r145);
                                                            if (o146) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 53, 53);
                                                            uint64_t t147 = r145;
                                                            uint8_t t148 = (t147 < t143);
                                                            if (t148) {
                                                                uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)t142) + (uintptr_t)t147 * 1));
                                                                gm_tqs5p4n2tq19v t150;
                                                                GM_ST(uint8_t, ((uint8_t *)&t150), t149);
                                                                ((uint8_t *)&t150)[1] = 1;
                                                                gm_l14 = t150;
                                                            } else {
                                                                gm_tqs5p4n2tq19v t151;
                                                                memset(&t151, 0, sizeof t151);
                                                                gm_l14 = t151;
                                                            }
                                                            gm_tqs5p4n2tq19v t152 = gm_l14;
                                                            gm_tqs5p4n2tq19v t153;
                                                            GM_ST(uint8_t, ((uint8_t *)&t153), ((uint8_t)47ULL));
                                                            ((uint8_t *)&t153)[1] = 1;
                                                            uint8_t t154 = (gm_eqb44cb3cf1n(((uint8_t *)&t152), ((uint8_t *)&t153)));
                                                            if (!t154) {
                                                                goto gm_lb14;
                                                            }
                                                            int32_t t155 = gm_l6;
                                                            int32_t r156; bool o157 = __builtin_sub_overflow(t155, ((int32_t)1LL), &r156);
                                                            if (o157) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 54, 21);
                                                            int32_t t158 = r156;
                                                            gm_l6 = t158;
                                                            uint64_t t159 = gm_l7;
                                                            uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)2ULL), &r160);
                                                            if (o161) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 55, 21);
                                                            uint64_t t162 = r160;
                                                            gm_l7 = t162;
                                                            goto gm_lb13;
                                                        }
gm_lb14:;
                                                        uint64_t t163 = gm_l7;
                                                        uint64_t r164; bool o165 = __builtin_add_overflow(t163, ((uint64_t)1ULL), &r164);
                                                        if (o165) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 57, 21);
                                                        uint64_t t166 = r164;
                                                        gm_l7 = t166;
                                                    }
gm_lb13:;
                                                }
gm_lb11:;
                                            }
gm_lb10:;
                                        }
                                    }
gm_lb8:;
                                    void * t167 = gm_l1;
                                    void * t168 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                    uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                    uint64_t t170 = gm_l2;
                                    uint64_t t171 = gm_l7;
                                    uint64_t t172 = (uintptr_t)t170;
                                    uint64_t t173 = (uintptr_t)t171;
                                    if (t172 > t173 || t173 > (uintptr_t)t169) gm_trap_slice(t172, t173, t169, gm_srceoep1gquqdsq, 60, 31);
                                    uint64_t t174 = ((uintptr_t)t171) - (uintptr_t)t170;
                                    gm_b16_8 t175;
                                    GM_ST(void *, (uint8_t *)&t175, (void *)(((uint8_t *)t168 + (uintptr_t)t170 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(t174));
                                    gm_tj63kbqqj2092 t176;
                                    GM_ST(uint8_t, (uint8_t *)&t176 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t176 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t176 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t176 + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t176 + 32, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t176 + 0), ((uint8_t)4ULL));
                                    GM_ST(gm_b16_8, ((uint8_t *)&t176 + 8), t175);
                                    gm_tftb1odc739t5 t177 = gm_f_push_1e500nc_4(t167, &t176);
                                    gm_l15 = t177;
                                    uint8_t t178 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                    if (!t178) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srceoep1gquqdsq, 60, 13);
                                    uint64_t t179 = gm_l7;
                                    gm_l2 = t179;
                                    uint64_t t180 = gm_l2;
                                    gm_l3 = t180;
                                    goto gm_lb6;
                                }
gm_lb7:;
                                uint64_t t181 = gm_l2;
                                uint64_t r182; bool o183 = __builtin_add_overflow(t181, ((uint64_t)1ULL), &r182);
                                if (o183) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 64, 13);
                                uint64_t t184 = r182;
                                gm_l2 = t184;
                            }
gm_lb6:;
                        }
gm_lb4:;
                    }
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_would_merge_44g1m96(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_would_merge_44g1m96 F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    void * gm_l2; /* cache */
    uint8_t gm_l3; /* x */
    uint8_t gm_l4; /* y */
    uint8_t gm_l5; /* known */
    uint8_t gm_l6; /* merges */
    GM_STACK_CHECK(&F, 413u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    gm_tf5srh4mp4b3i gm_l10;
    uint8_t gm_l11;
    gm_tf5srh4mp4b3i gm_l12;
    gm_tqs5p4n2tq19v gm_l13;
    gm_tftb1odc739t5 gm_l14;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
    if (t11) {
        gm_l7 = ((uint8_t)1);
    } else {
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
        gm_l7 = t13;
    }
    uint8_t t14 = gm_l7;
    if (t14) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t15 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
    if (o19) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 83, 12);
    uint64_t t20 = r18;
    if ((uintptr_t)t20 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t20, t16, gm_srceoep1gquqdsq, 83, 10);
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t15) + (uintptr_t)t20 * 1));
    gm_l3 = t21;
    void * t22 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t23, gm_srceoep1gquqdsq, 84, 10);
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t22) + (uintptr_t)((uint64_t)0ULL) * 1));
    gm_l4 = t24;
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)gm_c3dq62rrli9));
    uint8_t t26 = gm_l3;
    gm_tf5srh4mp4b3i t27 = gm_f_index_of_hdeg351(t25, t26);
    gm_l10 = t27;
    {
        {
            uint8_t t28 = (uint8_t)((((uint8_t *)&gm_l10))[8] != 0);
            if (t28) {
                goto gm_lb2;
            }
            gm_l9 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l9 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 86, 8);
    }
gm_lb1:;
    uint8_t t29 = gm_l9;
    if (t29) {
        gm_l8 = ((uint8_t)1);
    } else {
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)gm_c3dq62rrli9));
        uint8_t t31 = gm_l4;
        gm_tf5srh4mp4b3i t32 = gm_f_index_of_hdeg351(t30, t31);
        gm_l12 = t32;
        {
            {
                uint8_t t33 = (uint8_t)((((uint8_t *)&gm_l12))[8] != 0);
                if (t33) {
                    goto gm_lb5;
                }
                gm_l11 = ((uint8_t)0);
                goto gm_lb4;
            }
gm_lb5:;
            {
                gm_l11 = ((uint8_t)1);
                goto gm_lb4;
            }
gm_lb6:;
            gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 86, 40);
        }
gm_lb4:;
        uint8_t t34 = gm_l11;
        gm_l8 = t34;
    }
    uint8_t t35 = gm_l8;
    if (t35) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    {
        {
            void * t36 = gm_l2;
            gm_b16_8 t37 = gm_l0;
            gm_b16_8 t38 = gm_l1;
            gm_t6j6smj7qoctu t39;
            GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t37);
            GM_ST(gm_b16_8, ((uint8_t *)&t39 + 16), t38);
            gm_tqs5p4n2tq19v t40 = gm_f_get_t1u93iu(t36, &t39);
            gm_l13 = t40;
            uint8_t t41 = (uint8_t)((((uint8_t *)&gm_l13))[1] != 0);
            if (!t41) {
                goto gm_lb8;
            }
            uint8_t t42 = GM_LD(uint8_t, ((uint8_t *)&gm_l13));
            gm_l5 = t42;
            uint8_t t43 = gm_l5;
            F.ret = t43;
            goto gm_fret;
        }
gm_lb8:;
    }
gm_lb7:;
    gm_b16_8 t44 = gm_l0;
    gm_b16_8 t45 = gm_l1;
    uint8_t t46 = gm_f_lexes_apart_9scqa7n((void *)NULL, cx, NULL, t44, t45);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t46;
    void * t47 = gm_l2;
    gm_b16_8 t48 = gm_l0;
    gm_b16_8 t49 = gm_l1;
    gm_t6j6smj7qoctu t50;
    GM_ST(gm_b16_8, ((uint8_t *)&t50 + 0), t48);
    GM_ST(gm_b16_8, ((uint8_t *)&t50 + 16), t49);
    uint8_t t51 = gm_l6;
    gm_tftb1odc739t5 t52 = gm_f_put_fmu1emq(t47, &t50, t51);
    gm_l14 = t52;
    uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
    if (!t53) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srceoep1gquqdsq, 89, 5);
    uint8_t t54 = gm_l6;
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
