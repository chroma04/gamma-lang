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
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_ta24rd13gupia;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t f1; } gm_ttsm01t0d8srg;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; int64_t f1; } gm_t4d55u65a2eb7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta24rd13gupia) == 32 && _Alignof(gm_ta24rd13gupia) == 16 && offsetof(gm_ta24rd13gupia, f0) == 0 && offsetof(gm_ta24rd13gupia, f1) == 16, "layout of `?i128`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; uint8_t f2; uint8_t pe[7]; } gm_th5c3ge61oapm;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttsm01t0d8srg) == 1 && _Alignof(gm_ttsm01t0d8srg) == 1 && offsetof(gm_ttsm01t0d8srg, f1) == 0, "layout of `?()`");
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { uint64_t f0; gm_th2lqbo77pvsc f1; } gm_t13urt52695sh;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t4d55u65a2eb7) == 24 && _Alignof(gm_t4d55u65a2eb7) == 8 && offsetof(gm_t4d55u65a2eb7, f0) == 0 && offsetof(gm_t4d55u65a2eb7, f1) == 16, "layout of `([]u8, i64)`");
typedef struct { gm_th5c3ge61oapm f0; uint8_t f1; uint8_t pe[7]; } gm_t4ml5v7ugcjrt;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_t4c7ipjj9s3fp f0; gm_trqfvmjtt4sij f1; uint8_t f2; uint8_t p3[7]; gm_trqfvmjtt4sij f3; gm_t4c7ipjj9s3fp f4; gm_t4c7ipjj9s3fp f5; } gm_tbpdf5ej15iof;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th5c3ge61oapm) == 32 && _Alignof(gm_th5c3ge61oapm) == 8 && offsetof(gm_th5c3ge61oapm, f0) == 0 && offsetof(gm_th5c3ge61oapm, f1) == 16 && offsetof(gm_th5c3ge61oapm, f2) == 24, "layout of `EnumInfo`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_t13urt52695sh) == 32 && _Alignof(gm_t13urt52695sh) == 8 && offsetof(gm_t13urt52695sh, f0) == 0 && offsetof(gm_t13urt52695sh, f1) == 8, "layout of `(i: usize, x: ?string)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4ml5v7ugcjrt) == 40 && _Alignof(gm_t4ml5v7ugcjrt) == 8 && offsetof(gm_t4ml5v7ugcjrt, f0) == 0 && offsetof(gm_t4ml5v7ugcjrt, f1) == 32, "layout of `?EnumInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbpdf5ej15iof) == 256 && _Alignof(gm_tbpdf5ej15iof) == 8 && offsetof(gm_tbpdf5ej15iof, f0) == 0 && offsetof(gm_tbpdf5ej15iof, f1) == 56 && offsetof(gm_tbpdf5ej15iof, f2) == 96 && offsetof(gm_tbpdf5ej15iof, f3) == 104 && offsetof(gm_tbpdf5ej15iof, f4) == 144 && offsetof(gm_tbpdf5ej15iof, f5) == 200, "layout of `Views`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { gm_ttu09lij36f8c f0; uint8_t f1; uint8_t pe[7]; } gm_tu3261i9p5758;
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tu3261i9p5758) == 48 && _Alignof(gm_tu3261i9p5758) == 8 && offsetof(gm_tu3261i9p5758, f0) == 0 && offsetof(gm_tu3261i9p5758, f1) == 40, "layout of `??View`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
struct gm_fr_add_member_name_j3i0eje {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_debug_view_gp38euq {
    gm_frame h;
    gm_ttu09lij36f8c l2; /* v */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_define_enum_v780plg {
    gm_frame h;
    gm_tckdobunaulee l14; /* v */
    gm_trqfvmjtt4sij l15; /* items */
    gm_tckdobunaulee l20; /* v */
    gm_b16_8 ret;
};
struct gm_fr_define_view_sob497j {
    gm_frame h;
    gm_ttu09lij36f8c l2; /* cached */
    gm_trqfvmjtt4sij l7; /* members */
    gm_t4c7ipjj9s3fp l8; /* taken */
    gm_trqfvmjtt4sij l11; /* labels */
    gm_trqfvmjtt4sij l30; /* arms */
    gm_tpautr4br7h3l l31; /* def */
    gm_trqfvmjtt4sij l32; /* names */
    gm_t0mglksrkfa5e l35; /* f */
    gm_t0mglksrkfa5e l40; /* f */
    gm_trqfvmjtt4sij l43; /* names */
    gm_t0mglksrkfa5e l46; /* f */
    gm_trqfvmjtt4sij l48; /* arms */
    gm_t0mglksrkfa5e l52; /* f */
    gm_trqfvmjtt4sij l56; /* names */
    gm_tckdobunaulee l59; /* vd */
    gm_trqfvmjtt4sij l61; /* arms */
    gm_tckdobunaulee l65; /* vd */
    gm_trqfvmjtt4sij l67; /* body */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_3v2gjd4_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dpjcq5f {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_js3tkq1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tv2p23d {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_enum_info_61n2nkp {
    gm_frame h;
    gm_t4ml5v7ugcjrt ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_format_08mmoe2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_0bd7vot {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3qphnbe {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6a91mh8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7c4i91d {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7nu71mn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8ibn6ei {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cn4968h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hn5gv7p {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ivs3cj0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jk4dii4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kl4ha3s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lfjb9aq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_of6149p {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_phh9f1s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tc7tn98 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ubnq9mq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v301q4v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_fq78978 {
    gm_frame h;
    gm_ttsm01t0d8srg ret;
};
struct gm_fr_get_p217beu {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_hash_map_5kr0r9p {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_9mnq5mu {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_is_aggregate_buh1uic {
    gm_frame h;
    gm_tpautr4br7h3l l2; /* def */
    gm_tckdobunaulee l6; /* v */
    uint8_t ret;
};
struct gm_fr_is_signed_integer_ige9b74 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_list_58nr42v_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_at7dsst {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_packed_v79gj1r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pad_juftt9c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_pointer_view_efkbf4q {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pop_knqt0be {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_m7lva8q {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_member_eubf7s6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_push_union_m2cqbk2 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* decls */
    gm_b0_1 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4usjfl0 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h9tbucc_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_reserve_40g0de6 {
    gm_frame h;
    gm_ttu09lij36f8c l4; /* v */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_scalar_f0e206b {
    gm_frame h;
    gm_ttu09lij36f8c ret;
};
struct gm_fr_show_5qbi623 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_static_assert_15uo2jf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_target_view_ams6b9s {
    gm_frame h;
    gm_ttu09lij36f8c l2; /* cached */
    gm_b16_8 ret;
};
struct gm_fr_to_i64_4cgpvak {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_t913bb735pvt5 ret;
};
struct gm_fr_unique_view_members_3bvsjcm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_usize_ty_9mijhtc {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_view_defs_u461okc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_view_kind_pth6nol {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_view_number_29hi30b {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qkqm3in {
    gm_frame h;
    gm_ttu09lij36f8c l2; /* cached */
    gm_ttu09lij36f8c l7; /* r */
    gm_ttu09lij36f8c l11; /* r */
    gm_ttu09lij36f8c l12; /* v */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_views_bc1b3r4 {
    gm_frame h;
    gm_tbpdf5ej15iof ret;
};
struct gm_fr_write_dqbiqk0 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ier753o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_u1no4ia {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ue93873 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_vfiknt5 {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_debug_view_gp38euq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_define_enum_v780plg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_define_view_sob497j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dpjcq5f(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_js3tkq1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tv2p23d(void);
__attribute__((aligned(4))) void gm_f_enum_info_61n2nkp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_p1k923m(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_format_08mmoe2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_0bd7vot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_3qphnbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6a91mh8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_7c4i91d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_7nu71mn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8ibn6ei(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_bqgl4hq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_cn4968h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_hn5gv7p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ivs3cj0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_jk4dii4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_kl4ha3s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lfjb9aq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_of6149p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_phh9f1s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_tc7tn98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_ubnq9mq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_utdcsob(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_v301q4v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_get_fq78978(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_p217beu(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_v0dhnfj(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint64_t gm_f_hash64_apkb7bo(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_5kr0r9p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_9mnq5mu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_aggregate_buh1uic(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_ige9b74(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_at7dsst(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_member_name_mj2bvs3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_packed_v79gj1r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_pad_juftt9c(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_pointer_view_efkbf4q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tkn42u7na3t81 gm_f_pop_knqt0be(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_m7lva8q(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_member_eubf7s6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_union_m2cqbk2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_00pon0c(void * a0, uint64_t a1, gm_b0_1 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_25ngckq(void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4usjfl0(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_remove_6blu1gh(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_reserve_40g0de6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_scalar_f0e206b(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_ttu09lij36f8c gm_f_scalar_f0e206b_c(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_static_assert_15uo2jf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_target_view_ams6b9s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_to_i128_o7ud1ue(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_to_i64_4cgpvak(const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_view_members_3bvsjcm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_with_mffff7e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_view_defs_u461okc(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_view_kind_pth6nol(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_view_number_29hi30b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_view_qkqm3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_views_bc1b3r4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_av01663(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dqbiqk0(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ier753o(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oepvt6a(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s3v41br(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u1no4ia(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ue93873(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vfiknt5(void * a0, const void * a1);
extern uint8_t gm_cgsmfcq937e_3[16] __attribute__((aligned(8)));
extern const uint8_t gm_s0dhnude2n5fe[8];
extern const uint8_t gm_s137iv03ua7e7[30];
extern const uint8_t gm_s1csgs22c4bhh[13];
extern const uint8_t gm_s1dmrsprfdp53[8];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3p17h7dbk8us[8];
extern const uint8_t gm_s420tm9ua7ggg[4];
extern const uint8_t gm_s44ek1gecbajs[7];
extern const uint8_t gm_s4v09sn49l08n[7];
extern const uint8_t gm_s557bv2sfqcom[6];
extern const uint8_t gm_s5qkai093c1q0[6];
extern const uint8_t gm_s5qpqp0tch0l9[7];
extern const uint8_t gm_s6l0iqp1ptptl[17];
extern const uint8_t gm_s77aa5te2lvj3[6];
extern const uint8_t gm_s8s03uo3h8dsp[9];
extern const uint8_t gm_sakkgen9m77d6[16];
extern const uint8_t gm_sdtv0pa4ahtps[52];
extern const uint8_t gm_sead5oitv3opu[39];
extern const uint8_t gm_selmtbgalekup[10];
extern const uint8_t gm_sfbr39r30psvt[4];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_shhr1hlq1ethc[4];
extern const uint8_t gm_si68bogf7eu32[8];
extern const uint8_t gm_sjeopa747clpb[4];
extern const uint8_t gm_sk2tgsi47prt7[14];
extern const uint8_t gm_sk8hn2ar9njah[15];
extern const uint8_t gm_sko48juemga53[34];
extern const uint8_t gm_smg3p90in08sb[10];
extern const uint8_t gm_snokcvhtidt97[6];
extern const uint8_t gm_so31m6feg820p[4];
extern const uint8_t gm_soim4k70n6bc7[3];
extern const uint8_t gm_sonrpqr05q208[14];
extern const uint8_t gm_soulf4opjqcvv[10];
extern const uint8_t gm_sp75410cl55v2[13];
extern const uint8_t gm_sqmf3k0nnjf4r[4];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srckfibkpt681aa[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcs9q7ghsrcgv5[];
extern const uint8_t gm_ss3lpq4se4ir5[5];
extern const uint8_t gm_stc6hgo9kskc3[8];
extern const uint8_t gm_stntm4cqqrhf7[7];
extern const uint8_t gm_sts5j6f5pkov0[5];
extern const uint8_t gm_sumv38t63phct[14];
extern const uint8_t gm_sv3d9namfmaua[9];
extern const uint8_t gm_svmo2cukmep6f[9];
extern const gm_tmpl gm_tpl0o9c14r51hfo;
extern const gm_tmpl gm_tpl1avjsgn54rqb;
extern const gm_tmpl gm_tpl3qt6ss8l9cgf;
extern const gm_tmpl gm_tpl48h8br68c917;
extern const gm_tmpl gm_tpl4g6bpqlb10dt;
extern const gm_tmpl gm_tpl4n3cnfr6vo96;
extern const gm_tmpl gm_tpl4u6a7su34sch;
extern const gm_tmpl gm_tpl5r25bpo8hobt;
extern const gm_tmpl gm_tpl6d7dqphpmhs7;
extern const gm_tmpl gm_tpl6gp425a13qgr;
extern const gm_tmpl gm_tplbr5d1kr4l84u;
extern const gm_tmpl gm_tplcouqld11oeil;
extern const gm_tmpl gm_tpldofkl5m8hqug;
extern const gm_tmpl gm_tplf27d39e1vcer;
extern const gm_tmpl gm_tplg05c24rh8peq;
extern const gm_tmpl gm_tplg24lfmv0pffq;
extern const gm_tmpl gm_tpli8aunvo88ks7;
extern const gm_tmpl gm_tpliig5tr0gjn5a;
extern const gm_tmpl gm_tplm3dvc32djhr7;
extern const gm_tmpl gm_tplpnk24geo6qd8;
extern const gm_tmpl gm_tplvdft4iatcp17;
extern const gm_tmpl gm_tplvdkp4jmav7jr;
extern const gm_tmpl gm_tplvkf7arb107q2;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_add_member_name_j3i0eje F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 816);
    gm_b16_8 t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_put_5c6qgui(t3, t4, ((uint8_t)1));
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srckfibkpt681aa, 450, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dpjcq5f(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dpjcq5f F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_js3tkq1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_js3tkq1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tv2p23d(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tv2p23d F;
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
inline __attribute__((aligned(4))) void gm_f_format_08mmoe2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_08mmoe2 F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tf1bjg81u0glv gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s6l0iqp1ptptl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(16));
    gm_tf1bjg81u0glv t3 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
    gm_f_format_bqgl4hq((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplpnk24geo6qd8), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_0bd7vot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_0bd7vot F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfbr39r30psvt));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpli8aunvo88ks7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_3qphnbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3qphnbe F;
    gm_t4d55u65a2eb7 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t4d55u65a2eb7 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s1dmrsprfdp53));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(7));
    gm_t4d55u65a2eb7 t3 = GM_LD(gm_t4d55u65a2eb7, ((uint8_t *)a0));
    gm_f_format_utdcsob((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5r25bpo8hobt), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6a91mh8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_6a91mh8 F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tf1bjg81u0glv gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5qpqp0tch0l9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_tf1bjg81u0glv t3 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
    gm_f_format_bqgl4hq((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl6gp425a13qgr), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_7c4i91d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_7c4i91d F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqmf3k0nnjf4r));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4n3cnfr6vo96), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7nu71mn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7nu71mn F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0dhnude2n5fe));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbr5d1kr4l84u), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8ibn6ei(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_8ibn6ei F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp75410cl55v2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplm3dvc32djhr7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cn4968h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cn4968h F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sv3d9namfmaua));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl3qt6ss8l9cgf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_hn5gv7p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hn5gv7p F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s8s03uo3h8dsp));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl6d7dqphpmhs7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ivs3cj0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ivs3cj0 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sko48juemga53));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4u6a7su34sch), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_jk4dii4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jk4dii4 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stc6hgo9kskc3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4g6bpqlb10dt), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_kl4ha3s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_kl4ha3s F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soulf4opjqcvv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1avjsgn54rqb), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lfjb9aq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lfjb9aq F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss3lpq4se4ir5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplg05c24rh8peq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_of6149p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_of6149p F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s77aa5te2lvj3));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0o9c14r51hfo), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_phh9f1s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_phh9f1s F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sead5oitv3opu));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvkf7arb107q2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_tc7tn98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_tc7tn98 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so31m6feg820p));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldofkl5m8hqug), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ubnq9mq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ubnq9mq F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svmo2cukmep6f));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcouqld11oeil), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_v301q4v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_v301q4v F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s137iv03ua7e7));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(29));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvdkp4jmav7jr), t2);
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
inline __attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_get_fq78978(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_fq78978 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    uint64_t gm_l4;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_p1k923m(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l4 = t6;
    } else {
        gm_ttsm01t0d8srg t7;
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
    gm_ttsm01t0d8srg t13;
    GM_ST(gm_b0_1, ((uint8_t *)&t13), GM_ZERO(gm_b0_1));
    ((uint8_t *)&t13)[0] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_p217beu(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_p217beu F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    uint64_t gm_l4;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_722uua4(t2, t3);
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
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t10) + (uintptr_t)t12 * 32) + 24));
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
inline __attribute__((aligned(4))) void gm_f_hash_map_5kr0r9p(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_5kr0r9p F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_dpjcq5f();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_9mnq5mu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_9mnq5mu F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_tv2p23d();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_ige9b74(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_signed_integer_ige9b74 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_signed_integer_s66djm7(t4, GM_CONV(gm_csguankko4lmhd, t5));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_ghvav22 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&(*(gm_t53at80pfdus3 *)gm_out), t4, GM_CONV(gm_csguankko4lmhd, t5));
    goto gm_fdone;
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
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 64));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_3 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_3v2gjd4_3();
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
inline __attribute__((aligned(4))) void gm_f_list_at7dsst(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_at7dsst F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_js3tkq1();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_eh6vfle F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_of_e6hfj1r((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 16));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tkn42u7na3t81 gm_f_pop_knqt0be(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_knqt0be F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t88rcucg1b7uk gm_l2; /* x */
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
        gm_tkn42u7na3t81 t6;
        memset(&t6, 0, sizeof t6);
        F.ret = t6;
        goto gm_fret;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = gm_l1;
    uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
    uint64_t t13 = r11;
    if ((uintptr_t)t13 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t13, t9, gm_srcrgc9bkquf6nr, 133, 10);
    gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t8) + (uintptr_t)t13 * 4));
    gm_l2 = t14;
    void * t15 = gm_l0;
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
    uint64_t t19 = gm_l1;
    uint64_t r20; bool o21 = __builtin_sub_overflow(t19, ((uint64_t)1ULL), &r20);
    if (o21) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t22 = r20;
    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t24 = (uintptr_t)t22;
    if (t23 > t24 || t24 > (uintptr_t)t18) gm_trap_slice(t23, t24, t18, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t25 = ((uintptr_t)t22) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t17 + (uintptr_t)((uint64_t)0ULL) * 4)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t15), t26);
    gm_t88rcucg1b7uk t27 = gm_l2;
    gm_tkn42u7na3t81 t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 6, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[4] = 1;
    F.ret = t28;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k_3 F;
    void * gm_l0; /* l */
    gm_tcmkompbl09il gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_3v2gjd4_3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_h9tbucc_3(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_m7lva8q(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_m7lva8q F;
    void * gm_l0; /* l */
    gm_tbccvikg6b96e gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_js3tkq1();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_4usjfl0(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4usjfl0(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4usjfl0 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h9tbucc_3 F;
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
inline __attribute__((aligned(4))) void gm_f_scalar_f0e206b(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scalar_f0e206b F;
    gm_b16_8 gm_l0; /* cty */
    GM_STACK_CHECK_SHALLOW(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)gm_cgsmfcq937e_3));
    gm_t6j6smj7qoctu t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 16), t3);
    gm_ttu09lij36f8c t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 33, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 34, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 36, 0);
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t5), t4);
    ((uint8_t *)&t5)[32] = 1;
    memmove(gm_out, ((uint8_t *)&t5), 40);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_5qbi623 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t4, t5);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint64_t t5 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 64));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_static_assert_15uo2jf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2) {
    struct gm_fr_static_assert_15uo2jf F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* cty */
    uint64_t gm_l2; /* size */
    GM_STACK_CHECK(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((((uint8_t *)t2) + 872) + 104);
    gm_b16_8 t4 = gm_l1;
    uint64_t t5 = gm_l2;
    gm_t3877pk7lv748 t6;
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t4);
    GM_ST(uint64_t, ((uint8_t *)&t6 + 16), t5);
    gm_tftb1odc739t5 t7 = gm_f_write_u1no4ia(t3, &t6);
    gm_l3 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcs9q7ghsrcgv5, 343, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_to_i64_4cgpvak(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_i64_4cgpvak F;
    __int128 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_ta24rd13gupia gm_l2;
    __int128 gm_l3;
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
    gm_f_to_i128_o7ud1ue((void *)&gm_l2, &t2);
    uint8_t t3 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
    if (t3) {
        __int128 t4 = GM_LD(__int128, ((uint8_t *)&gm_l2));
        gm_l3 = t4;
    } else {
        gm_t913bb735pvt5 t5;
        memset(&t5, 0, sizeof t5);
        F.ret = t5;
        goto gm_fret;
    }
    __int128 t6 = gm_l3;
    gm_l1 = t6;
    __int128 t7 = gm_l1;
    gm_t913bb735pvt5 t8;
    int64_t t9 = (int64_t)t7;
    gm_t913bb735pvt5 t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
    GM_ST(int64_t, ((uint8_t *)&t10), t9);
    ((uint8_t *)&t10)[8] = 1;
    gm_t913bb735pvt5 t11;
    memset(&t11, 0, sizeof t11);
    t8 = ((__int128)(t7) >= ((__int128)(((unsigned __int128)18446744073709551615ULL << 64) | 9223372036854775808ULL)) && ((__int128)(t7) < 0 || (unsigned __int128)(t7) <= (((unsigned __int128)0ULL << 64) | 9223372036854775807ULL))) ? t10 : t11;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_view_members_3bvsjcm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_unique_view_members_3bvsjcm F;
    gm_b16_8 gm_l0; /* names */
    void * gm_l1; /* taken */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    void * t3 = gm_l1;
    gm_b16_8 t4 = gm_f_unique_with_mffff7e((void *)NULL, cx, NULL, t2, t3, ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_9mijhtc F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_view_defs_u461okc(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_defs_u461okc F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 872) + 104));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dqbiqk0(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_dqbiqk0 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sjeopa747clpb));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplvdft4iatcp17), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ier753o(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ier753o F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 103u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sonrpqr05q208));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_tftb1odc739t5 t4 = gm_f_write_av01663(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplf27d39e1vcer), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u1no4ia(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_u1no4ia F;
    void * gm_l0; /* l */
    gm_t3877pk7lv748 gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdtv0pa4ahtps));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_tftb1odc739t5 t4 = gm_f_write_oepvt6a(t2, &(*(gm_t3877pk7lv748 *)((uint8_t *)a1)), ((void *)&gm_tplg24lfmv0pffq), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ue93873(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ue93873 F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 103u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1csgs22c4bhh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_tftb1odc739t5 t4 = gm_f_write_av01663(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpliig5tr0gjn5a), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vfiknt5(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_vfiknt5 F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_selmtbgalekup));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_tftb1odc739t5 t4 = gm_f_write_s3v41br(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tpl48h8br68c917), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_debug_view_gp38euq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_debug_view_gp38euq F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l3; /* p */
    GM_STACK_CHECK(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tkn42u7na3t81 gm_l4;
    void * t5 = gm_l0;
    uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t5) + 872) + 96));
    if (t6) {
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l1;
        gm_f_view_qkqm3in((void *)&(*(gm_ttu09lij36f8c *)gm_out), (void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    void * t9 = gm_l0;
    GM_ST(uint8_t, ((((uint8_t *)t9) + 872) + 96), ((uint8_t)1));
    void * t10 = gm_l0;
    gm_t88rcucg1b7uk t11 = gm_l1;
    gm_f_view_qkqm3in((void *)&F.l2, (void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        for (;;) {
            void * t12 = gm_l0;
            void * t13 = (void *)((((uint8_t *)t12) + 872) + 56);
            gm_tkn42u7na3t81 t14 = gm_f_pop_knqt0be(t13);
            gm_l4 = t14;
            uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l4))[4] != 0);
            if (!t15) {
                goto gm_lb1;
            }
            gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l4));
            gm_l3 = t16;
            {
                void * t17 = gm_l0;
                gm_t88rcucg1b7uk t18 = gm_l3;
                gm_b0_1 t19 = gm_f_define_view_sob497j((void *)NULL, cx, NULL, t17, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t20 = gm_l0;
    GM_ST(uint8_t, ((((uint8_t *)t20) + 872) + 96), ((uint8_t)0));
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
__attribute__((aligned(4))) gm_b16_8 gm_f_define_enum_v780plg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_define_enum_v780plg F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* view */
    void * gm_l3; /* def */
    gm_t88rcucg1b7uk gm_l4; /* tag */
    gm_b16_8 gm_l5; /* variants */
    gm_th5c3ge61oapm gm_l6; /* info */
    gm_b16_8 gm_l7; /* name */
    uint64_t gm_l8; /* n */
    gm_b16_8 gm_l9; /* prefix */
    uint64_t gm_l10; /* k */
    uint8_t gm_l11; /* clash */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    int64_t gm_l16; /* lo */
    int64_t gm_l17; /* hi */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_b16_8 gm_l21; /* e */
    int64_t gm_l22; /* value */
    uint64_t gm_l23; /* natural */
    gm_tc0nbpdhofi37 gm_l24; /* <tmp> */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* s */
    uint64_t gm_l28; /* bits */
    uint8_t gm_l29; /* fits */
    uint64_t gm_l30; /* bits */
    int64_t gm_l31; /* wide */
    gm_b16_8 gm_l32; /* e */
    GM_STACK_CHECK(&F, 2601u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t4ml5v7ugcjrt gm_l33;
    gm_th5c3ge61oapm gm_l34;
    gm_tabb710d9mq2e gm_l35;
    gm_tabb710d9mq2e gm_l36;
    uint8_t gm_l37;
    gm_tabb710d9mq2e gm_l38;
    gm_tqs5p4n2tq19v gm_l39;
    gm_tabb710d9mq2e gm_l40;
    gm_tabb710d9mq2e gm_l41;
    gm_t913bb735pvt5 gm_l42;
    int64_t gm_l43;
    gm_tftb1odc739t5 gm_l44;
    gm_tabb710d9mq2e gm_l45;
    gm_tftb1odc739t5 gm_l46;
    uint8_t gm_l47;
    uint8_t gm_l48;
    int64_t gm_l49;
    gm_tabb710d9mq2e gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tabb710d9mq2e gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_tabb710d9mq2e gm_l54;
    gm_tftb1odc739t5 gm_l55;
    {
        void * t25 = gm_l0;
        gm_t88rcucg1b7uk t26 = gm_l1;
        void * t27 = gm_f_adt_of_gej99vn(t25, GM_CONV(gm_csguankko4lmhd, t26));
        gm_l3 = t27;
        {
            {
                {
                    {
                        void * t28 = gm_l3;
                        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t28) + 24) + 0));
                        uint8_t t30 = (uint8_t)(t29 == ((uint8_t)1ULL));
                        if (!t30) {
                            goto gm_lb2;
                        }
                        gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t28) + 24) + 8));
                        gm_l4 = t31;
                        gm_b16_8 t32 = GM_LD(gm_b16_8, (((((uint8_t *)t28) + 24) + 8) + 8));
                        gm_l5 = t32;
                        void * t33 = gm_l0;
                        gm_t88rcucg1b7uk t34 = gm_l1;
                        gm_f_enum_info_61n2nkp((void *)&gm_l33, (void *)NULL, cx, NULL, t33, t34);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l33))[32] != 0);
                        if (t35) {
                            memmove(((uint8_t *)&gm_l34), ((uint8_t *)&gm_l33), 32);
                        } else {
                            void * t36 = gm_l0;
                            gm_t88rcucg1b7uk t37 = gm_l4;
                            gm_b16_8 t38 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t36, t37);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t38;
                            goto gm_fret;
                        }
                        memmove(((uint8_t *)&gm_l6), ((uint8_t *)&gm_l34), 32);
                        gm_b16_8 t39 = gm_l2;
                        gm_l7 = t39;
                        gm_b16_8 t40 = gm_l2;
                        gm_b16_8 t41;
                        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s557bv2sfqcom));
                        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(5));
                        uint8_t t42 = gm_f_starts_with_6vvt01e(t40, t41);
                        uint8_t t43 = (uint8_t)!t42;
                        if (t43) {
                            void * t44 = gm_l0;
                            void * t45 = gm_l0;
                            gm_t88rcucg1b7uk t46 = gm_l1;
                            gm_b16_8 t47 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t45, t46);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t48;
                            GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
                            gm_f_format_kl4ha3s((void *)&gm_l35, (void *)NULL, cx, NULL, t48);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                            if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_srcs9q7ghsrcgv5, 377, 29);
                            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                            gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                            uint64_t t52 = gm_f_view_number_29hi30b((void *)NULL, cx, NULL, t44, t51);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l8 = t52;
                            uint64_t t53 = gm_l8;
                            gm_t4ce0jd3tdro4 t54;
                            GM_ST(uint64_t, ((uint8_t *)&t54 + 0), t53);
                            gm_f_format_8ibn6ei((void *)&gm_l36, (void *)NULL, cx, NULL, t54);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                            if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_srcs9q7ghsrcgv5, 378, 16);
                            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                            gm_l7 = t57;
                        }
                        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
                        gm_l9 = t58;
                        gm_l10 = ((uint64_t)3ULL);
                        {
                            for (;;) {
                                if (!((uint8_t)1)) {
                                    goto gm_lb3;
                                }
                                {
                                    gm_l11 = ((uint8_t)0);
                                    gm_b16_8 t59 = gm_l5;
                                    gm_l12 = t59;
                                    gm_l13 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t60 = gm_l13;
                                            uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                            uint8_t t62 = (t60 < t61);
                                            if (!t62) {
                                                goto gm_lb6;
                                            }
                                            {
                                                void * t63 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                                uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                                uint64_t t65 = gm_l13;
                                                {
                                                    {
                                                        memmove(((uint8_t *)&F.l14), (((uint8_t *)t63) + (uintptr_t)t65 * 72), 72);
                                                        void * t66 = gm_l0;
                                                        void * t67 = (void *)((((uint8_t *)t66) + 872) + 200);
                                                        gm_b16_8 t68 = gm_l9;
                                                        gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                                        gm_t6j6smj7qoctu t70;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t68);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 16), t69);
                                                        gm_f_format_of6149p((void *)&gm_l38, (void *)NULL, cx, NULL, &t70);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                                        if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 8, gm_srcs9q7ghsrcgv5, 387, 41);
                                                        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                                                        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                                                        gm_tqs5p4n2tq19v t74 = gm_f_get_p217beu(t67, t73);
                                                        gm_l39 = t74;
                                                        {
                                                            {
                                                                uint8_t t75 = (uint8_t)((((uint8_t *)&gm_l39))[1] != 0);
                                                                if (t75) {
                                                                    goto gm_lb12;
                                                                }
                                                                gm_l37 = ((uint8_t)0);
                                                                goto gm_lb11;
                                                            }
gm_lb12:;
                                                            {
                                                                gm_l37 = ((uint8_t)1);
                                                                goto gm_lb11;
                                                            }
gm_lb13:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 387, 16);
                                                        }
gm_lb11:;
                                                        uint8_t t76 = gm_l37;
                                                        if (t76) {
                                                            gm_l11 = ((uint8_t)1);
                                                        }
                                                        goto gm_lb9;
                                                    }
gm_lb10:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 386, 9);
                                                }
gm_lb9:;
                                            }
gm_lb8:;
                                            uint64_t t77 = gm_l13;
                                            uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                                            uint64_t t80 = r78;
                                            gm_l13 = t80;
                                        }
                                    }
gm_lb6:;
                                    uint8_t t81 = gm_l11;
                                    uint8_t t82 = (uint8_t)!t81;
                                    if (t82) {
                                        goto gm_lb3;
                                    }
                                    gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
                                    void * t84 = gm_l0;
                                    gm_t88rcucg1b7uk t85 = gm_l1;
                                    gm_b16_8 t86 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t84, t85);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint64_t t87 = gm_l10;
                                    gm_b16_8 t88 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t86, t87);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_t6j6smj7qoctu t89;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t89 + 0), t83);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t89 + 16), t88);
                                    gm_f_format_of6149p((void *)&gm_l40, (void *)NULL, cx, NULL, &t89);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                                    if (!t90) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 8, gm_srcs9q7ghsrcgv5, 390, 18);
                                    gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l40) + 8));
                                    gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                                    gm_l9 = t92;
                                    uint64_t t93 = gm_l10;
                                    uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                                    if (o95) gm_trap("integer overflow in `+`", gm_srcs9q7ghsrcgv5, 391, 9);
                                    uint64_t t96 = r94;
                                    gm_l10 = t96;
                                }
gm_lb5:;
                            }
                        }
gm_lb3:;
                    }
                    {
                        gm_trqfvmjtt4sij t97;
                        gm_f_list_epvducg((void *)&t97, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l15 = t97;
                        gm_l16 = ((int64_t)0LL);
                        gm_l17 = ((int64_t)0LL);
                        gm_b16_8 t98 = gm_l5;
                        gm_l18 = t98;
                        gm_l19 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t99 = gm_l19;
                                uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                uint8_t t101 = (t99 < t100);
                                if (!t101) {
                                    goto gm_lb14;
                                }
                                {
                                    void * t102 = GM_LD(void *, ((uint8_t *)&gm_l18));
                                    uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                    uint64_t t104 = gm_l19;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l20), (((uint8_t *)t102) + (uintptr_t)t104 * 72), 72);
                                            gm_b16_8 t105 = gm_l9;
                                            gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                            gm_t6j6smj7qoctu t107;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t107 + 0), t105);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t107 + 16), t106);
                                            gm_f_format_of6149p((void *)&gm_l41, (void *)NULL, cx, NULL, &t107);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                            if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 8, gm_srcs9q7ghsrcgv5, 397, 14);
                                            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 8));
                                            gm_l21 = t109;
                                            gm_t913bb735pvt5 t110 = gm_f_to_i64_4cgpvak(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l20) + 24)));
                                            gm_l42 = t110;
                                            uint8_t t111 = (uint8_t)((((uint8_t *)&gm_l42))[8] != 0);
                                            if (t111) {
                                                int64_t t112 = GM_LD(int64_t, ((uint8_t *)&gm_l42));
                                                gm_l43 = t112;
                                            } else {
                                                gm_l43 = ((int64_t)0LL);
                                            }
                                            int64_t t113 = gm_l43;
                                            gm_l22 = t113;
                                            int64_t t114 = gm_l16;
                                            int64_t t115 = gm_l22;
                                            int64_t t116 = (t114 < t115 ? t114 : t115);
                                            gm_l16 = t116;
                                            int64_t t117 = gm_l17;
                                            int64_t t118 = gm_l22;
                                            int64_t t119 = (t117 > t118 ? t117 : t118);
                                            gm_l17 = t119;
                                            void * t120 = gm_l0;
                                            void * t121 = (void *)((((uint8_t *)t120) + 872) + 200);
                                            gm_b16_8 t122 = gm_l21;
                                            gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
                                            gm_tftb1odc739t5 t124 = gm_f_put_5c6qgui(t121, t123, ((uint8_t)1));
                                            gm_l44 = t124;
                                            uint8_t t125 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                                            if (!t125) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 1, gm_srcs9q7ghsrcgv5, 401, 9);
                                            void * t126 = gm_l0;
                                            gm_b16_8 t127 = gm_l21;
                                            gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
                                            gm_b0_1 t129 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t126, t128);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            void * t130 = (void *)((uint8_t *)&F.l15);
                                            gm_b16_8 t131 = gm_l21;
                                            int64_t t132 = gm_l22;
                                            gm_t4d55u65a2eb7 t133;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t133 + 0), t131);
                                            GM_ST(int64_t, ((uint8_t *)&t133 + 16), t132);
                                            gm_f_format_3qphnbe((void *)&gm_l45, (void *)NULL, cx, NULL, &t133);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t134 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                            if (!t134) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 8, gm_srcs9q7ghsrcgv5, 403, 20);
                                            gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                                            gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&t135));
                                            gm_tftb1odc739t5 t137 = gm_f_push_3ef746b(t130, t136);
                                            gm_l46 = t137;
                                            uint8_t t138 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                            if (!t138) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_srcs9q7ghsrcgv5, 403, 9);
                                            goto gm_lb17;
                                        }
gm_lb18:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 396, 5);
                                    }
gm_lb17:;
                                }
gm_lb16:;
                                uint64_t t139 = gm_l19;
                                uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                                uint64_t t142 = r140;
                                gm_l19 = t142;
                            }
                        }
gm_lb14:;
                    }
                    {
                        gm_l23 = ((uint64_t)4ULL);
                        gm_tc0nbpdhofi37 t143;
                        GM_ST(uint64_t, ((uint8_t *)&t143 + 0), ((uint64_t)1ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t143 + 8), ((uint64_t)2ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t143 + 16), ((uint64_t)4ULL));
                        gm_l24 = t143;
                        void * t144 = (void *)((uint8_t *)&gm_l24);
                        uint64_t t145 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t146 = (uintptr_t)((uint64_t)3ULL);
                        if (t145 > t146 || t146 > (uintptr_t)((uint64_t)3ULL)) gm_trap_slice(t145, t146, ((uint64_t)3ULL), gm_srcs9q7ghsrcgv5, 408, 5);
                        uint64_t t147 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t148;
                        GM_ST(void *, (uint8_t *)&t148, (void *)(((uint8_t *)t144 + (uintptr_t)((uint64_t)0ULL) * 8)));
                        GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(t147));
                        gm_l25 = t148;
                        gm_l26 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t149 = gm_l26;
                                uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                uint8_t t151 = (t149 < t150);
                                if (!t151) {
                                    goto gm_lb19;
                                }
                                {
                                    void * t152 = GM_LD(void *, ((uint8_t *)&gm_l25));
                                    uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                    uint64_t t154 = gm_l26;
                                    {
                                        {
                                            uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)t152) + (uintptr_t)t154 * 8));
                                            gm_l27 = t155;
                                            uint64_t t156 = gm_l27;
                                            uint64_t r157; bool o158 = __builtin_mul_overflow(((uint64_t)8ULL), t156, &r157);
                                            if (o158) gm_trap("integer overflow in `*`", gm_srcs9q7ghsrcgv5, 409, 17);
                                            uint64_t t159 = r157;
                                            gm_l28 = t159;
                                            int64_t t160 = gm_l16;
                                            uint8_t t161 = (t160 < ((int64_t)0LL));
                                            if (t161) {
                                                int64_t t162 = gm_l16;
                                                uint64_t t163 = gm_l28;
                                                uint64_t r164; bool o165 = __builtin_sub_overflow(t163, ((uint64_t)1ULL), &r164);
                                                if (o165) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 410, 48);
                                                uint64_t t166 = r164;
                                                unsigned __int128 n167 = (unsigned __int128)t166;
                                                if (n167 >= 64) gm_trap_shift((uint64_t)n167, "i64", gm_srcs9q7ghsrcgv5, 410, 37);
                                                int64_t r168 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n167);
                                                bool o169 = (int64_t)(r168 >> (int)n167) != ((int64_t)1LL);
                                                if (o169) gm_trap("integer overflow in `<<`", gm_srcs9q7ghsrcgv5, 410, 37);
                                                int64_t t170 = r168;
                                                if (t170 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 410, 35);
                                                int64_t t171 = (int64_t)((uint64_t)0 - (uint64_t)t170);
                                                uint8_t t172 = (t162 >= t171);
                                                if (t172) {
                                                    int64_t t173 = gm_l17;
                                                    uint64_t t174 = gm_l28;
                                                    uint64_t r175; bool o176 = __builtin_sub_overflow(t174, ((uint64_t)1ULL), &r175);
                                                    if (o176) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 410, 80);
                                                    uint64_t t177 = r175;
                                                    unsigned __int128 n178 = (unsigned __int128)t177;
                                                    if (n178 >= 64) gm_trap_shift((uint64_t)n178, "i64", gm_srcs9q7ghsrcgv5, 410, 69);
                                                    int64_t r179 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n178);
                                                    bool o180 = (int64_t)(r179 >> (int)n178) != ((int64_t)1LL);
                                                    if (o180) gm_trap("integer overflow in `<<`", gm_srcs9q7ghsrcgv5, 410, 69);
                                                    int64_t t181 = r179;
                                                    uint8_t t182 = (t173 < t181);
                                                    gm_l48 = t182;
                                                } else {
                                                    gm_l48 = ((uint8_t)0);
                                                }
                                                uint8_t t183 = gm_l48;
                                                gm_l47 = t183;
                                            } else {
                                                int64_t t184 = gm_l17;
                                                uint64_t t185 = gm_l28;
                                                unsigned __int128 n186 = (unsigned __int128)t185;
                                                if (n186 >= 64) gm_trap_shift((uint64_t)n186, "i64", gm_srcs9q7ghsrcgv5, 410, 106);
                                                int64_t r187 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n186);
                                                bool o188 = (int64_t)(r187 >> (int)n186) != ((int64_t)1LL);
                                                if (o188) gm_trap("integer overflow in `<<`", gm_srcs9q7ghsrcgv5, 410, 106);
                                                int64_t t189 = r187;
                                                uint8_t t190 = (t184 < t189);
                                                gm_l47 = t190;
                                            }
                                            uint8_t t191 = gm_l47;
                                            gm_l29 = t191;
                                            uint8_t t192 = gm_l29;
                                            if (t192) {
                                                uint64_t t193 = gm_l27;
                                                gm_l23 = t193;
                                                goto gm_lb19;
                                            }
                                            goto gm_lb22;
                                        }
gm_lb23:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 408, 5);
                                    }
gm_lb22:;
                                }
gm_lb21:;
                                uint64_t t194 = gm_l26;
                                uint64_t r195; bool o196 = __builtin_add_overflow(t194, ((uint64_t)1ULL), &r195);
                                uint64_t t197 = r195;
                                gm_l26 = t197;
                            }
                        }
gm_lb19:;
                        uint64_t t198 = gm_l23;
                        uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                        uint8_t t200 = (t198 < t199);
                        if (t200) {
                            uint64_t t201 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                            if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcs9q7ghsrcgv5, 417, 22);
                            uint64_t t202 = (uint64_t)(t201 / ((uint64_t)2ULL));
                            uint64_t r203; bool o204 = __builtin_mul_overflow(((uint64_t)8ULL), t202, &r203);
                            if (o204) gm_trap("integer overflow in `*`", gm_srcs9q7ghsrcgv5, 417, 17);
                            uint64_t t205 = r203;
                            gm_l30 = t205;
                            uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 24));
                            if (t206) {
                                uint64_t t207 = gm_l30;
                                uint64_t r208; bool o209 = __builtin_sub_overflow(t207, ((uint64_t)1ULL), &r208);
                                if (o209) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 418, 51);
                                uint64_t t210 = r208;
                                unsigned __int128 n211 = (unsigned __int128)t210;
                                if (n211 >= 64) gm_trap_shift((uint64_t)n211, "i64", gm_srcs9q7ghsrcgv5, 418, 40);
                                int64_t r212 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n211);
                                bool o213 = (int64_t)(r212 >> (int)n211) != ((int64_t)1LL);
                                if (o213) gm_trap("integer overflow in `<<`", gm_srcs9q7ghsrcgv5, 418, 40);
                                int64_t t214 = r212;
                                if (t214 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 418, 38);
                                int64_t t215 = (int64_t)((uint64_t)0 - (uint64_t)t214);
                                int64_t r216; bool o217 = __builtin_sub_overflow(t215, ((int64_t)1LL), &r216);
                                if (o217) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 418, 38);
                                int64_t t218 = r216;
                                gm_l49 = t218;
                            } else {
                                uint64_t t219 = gm_l30;
                                unsigned __int128 n220 = (unsigned __int128)t219;
                                if (n220 >= 64) gm_trap_shift((uint64_t)n220, "i64", gm_srcs9q7ghsrcgv5, 418, 75);
                                int64_t r221 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n220);
                                bool o222 = (int64_t)(r221 >> (int)n220) != ((int64_t)1LL);
                                if (o222) gm_trap("integer overflow in `<<`", gm_srcs9q7ghsrcgv5, 418, 75);
                                int64_t t223 = r221;
                                gm_l49 = t223;
                            }
                            int64_t t224 = gm_l49;
                            gm_l31 = t224;
                            gm_b16_8 t225 = gm_l9;
                            gm_tnpifv20irnph t226;
                            GM_ST(gm_b16_8, ((uint8_t *)&t226 + 0), t225);
                            gm_f_format_ubnq9mq((void *)&gm_l50, (void *)NULL, cx, NULL, t226);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t227 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                            if (!t227) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 8, gm_srcs9q7ghsrcgv5, 419, 14);
                            gm_b16_8 t228 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 8));
                            gm_l32 = t228;
                            void * t229 = gm_l0;
                            void * t230 = (void *)((((uint8_t *)t229) + 872) + 200);
                            gm_b16_8 t231 = gm_l32;
                            gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)&t231));
                            gm_tftb1odc739t5 t233 = gm_f_put_5c6qgui(t230, t232, ((uint8_t)1));
                            gm_l51 = t233;
                            uint8_t t234 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                            if (!t234) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srcs9q7ghsrcgv5, 420, 9);
                            void * t235 = gm_l0;
                            gm_b16_8 t236 = gm_l32;
                            gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
                            gm_b0_1 t238 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t235, t237);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t239 = (void *)((uint8_t *)&F.l15);
                            gm_b16_8 t240 = gm_l32;
                            int64_t t241 = gm_l31;
                            gm_t4d55u65a2eb7 t242;
                            GM_ST(gm_b16_8, ((uint8_t *)&t242 + 0), t240);
                            GM_ST(int64_t, ((uint8_t *)&t242 + 16), t241);
                            gm_f_format_3qphnbe((void *)&gm_l52, (void *)NULL, cx, NULL, &t242);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t243 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                            if (!t243) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 8, gm_srcs9q7ghsrcgv5, 422, 20);
                            gm_b16_8 t244 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l52) + 8));
                            gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)&t244));
                            gm_tftb1odc739t5 t246 = gm_f_push_3ef746b(t239, t245);
                            gm_l53 = t246;
                            uint8_t t247 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                            if (!t247) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_srcs9q7ghsrcgv5, 422, 9);
                        }
                    }
                    {
                        void * t248 = gm_l0;
                        void * t249 = (void *)((((uint8_t *)t248) + 872) + 104);
                        gm_b16_8 t250 = gm_l7;
                        gm_b16_8 t251 = gm_f_packed_v79gj1r((void *)NULL, cx, NULL, t250);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
                        gm_b16_8 t253 = GM_LD(gm_b16_8, ((uint8_t *)&t252));
                        gm_b16_8 t254;
                        GM_ST(void *, (uint8_t *)&t254, (void *)(gm_sg5gdnrith2i6));
                        GM_ST(uintptr_t, (uint8_t *)&t254 + 8, (uintptr_t)(2));
                        gm_f_join_sfknljb((void *)&gm_l54, (void *)NULL, cx, NULL, t253, t254);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t255 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                        if (!t255) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 8, gm_srcs9q7ghsrcgv5, 424, 57);
                        gm_b16_8 t256 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l54) + 8));
                        gm_t6j6smj7qoctu t257;
                        GM_ST(gm_b16_8, ((uint8_t *)&t257 + 0), t251);
                        GM_ST(gm_b16_8, ((uint8_t *)&t257 + 16), t256);
                        gm_tftb1odc739t5 t258 = gm_f_write_ier753o(t249, &t257);
                        gm_l55 = t258;
                        uint8_t t259 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                        if (!t259) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 1, gm_srcs9q7ghsrcgv5, 424, 5);
                        void * t260 = gm_l0;
                        gm_b16_8 t261 = gm_l7;
                        uint64_t t262 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                        gm_b0_1 t263 = gm_f_static_assert_15uo2jf((void *)NULL, cx, NULL, t260, t261, t262);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t264 = gm_l7;
                        F.ret = t264;
                        goto gm_fret;
                    }
                }
gm_lb2:;
            }
            {
                gm_trap("reached `@unreachable()`", gm_srcs9q7ghsrcgv5, 373, 44);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_define_view_sob497j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_define_view_sob497j F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t6j6smj7qoctu gm_l3; /* v */
    gm_b16_8 gm_l4; /* cty */
    uint64_t gm_l5; /* size */
    gm_b16_8 gm_l6; /* offs */
    gm_b16_8 gm_l9; /* elems */
    gm_b16_8 gm_l10; /* names */
    gm_b16_8 gm_l12; /* chosen */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* k */
    gm_th2lqbo77pvsc gm_l16; /* n */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* k */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* k */
    gm_t88rcucg1b7uk gm_l23; /* e */
    gm_t88rcucg1b7uk gm_l24; /* elem */
    gm_b16_8 gm_l25; /* target */
    gm_b16_8 gm_l26; /* len */
    gm_t88rcucg1b7uk gm_l27; /* inner */
    gm_t88rcucg1b7uk gm_l28; /* err */
    gm_t88rcucg1b7uk gm_l29; /* ok */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    gm_b16_8 gm_l36; /* chosen */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* k */
    gm_b16_8 gm_l41; /* name */
    gm_b16_8 gm_l42; /* fields */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_b16_8 gm_l47; /* chosen */
    gm_b16_8 gm_l49; /* <tmp> */
    uint64_t gm_l50; /* <tmp> */
    uint64_t gm_l51; /* k */
    gm_t88rcucg1b7uk gm_l53; /* tag */
    gm_b16_8 gm_l54; /* variants */
    gm_b16_8 gm_l55; /* tag_cty */
    gm_b16_8 gm_l57; /* <tmp> */
    uint64_t gm_l58; /* <tmp> */
    gm_b16_8 gm_l60; /* chosen */
    gm_b16_8 gm_l62; /* <tmp> */
    uint64_t gm_l63; /* <tmp> */
    uint64_t gm_l64; /* k */
    gm_t88rcucg1b7uk gm_l66; /* p */
    uint64_t gm_l68; /* at */
    gm_b16_8 gm_l69; /* <tmp> */
    uint64_t gm_l70; /* <tmp> */
    gm_tbccvikg6b96e gm_l71; /* m */
    GM_STACK_CHECK(&F, 5669u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tu3261i9p5758 gm_l72;
    gm_ttu09lij36f8c gm_l73;
    gm_t6j6smj7qoctu gm_l74;
    gm_t53at80pfdus3 gm_l75;
    gm_t13urt52695sh gm_l76;
    gm_b16_8 gm_l77;
    gm_tabb710d9mq2e gm_l78;
    gm_tftb1odc739t5 gm_l79;
    gm_tabb710d9mq2e gm_l80;
    gm_tftb1odc739t5 gm_l81;
    gm_tlcm4lvu3g1a8 gm_l82;
    gm_tabb710d9mq2e gm_l83;
    gm_tftb1odc739t5 gm_l84;
    gm_tabb710d9mq2e gm_l85;
    gm_tftb1odc739t5 gm_l86;
    gm_tftb1odc739t5 gm_l87;
    gm_tftb1odc739t5 gm_l88;
    gm_tftb1odc739t5 gm_l89;
    gm_tftb1odc739t5 gm_l90;
    gm_tftb1odc739t5 gm_l91;
    gm_t8ta482bl6be0 gm_l92;
    gm_b16_8 gm_l93;
    gm_tabb710d9mq2e gm_l94;
    gm_tftb1odc739t5 gm_l95;
    gm_t8ta482bl6be0 gm_l96;
    gm_b16_8 gm_l97;
    gm_tabb710d9mq2e gm_l98;
    gm_tftb1odc739t5 gm_l99;
    gm_tabb710d9mq2e gm_l100;
    gm_tftb1odc739t5 gm_l101;
    gm_tftb1odc739t5 gm_l102;
    gm_tftb1odc739t5 gm_l103;
    gm_t3l8idb2m6c1r gm_l104;
    gm_t88rcucg1b7uk gm_l105;
    gm_b16_8 gm_l106;
    gm_tabb710d9mq2e gm_l107;
    gm_tftb1odc739t5 gm_l108;
    gm_tftb1odc739t5 gm_l109;
    gm_tftb1odc739t5 gm_l110;
    {
        void * t63 = gm_l0;
        void * t64 = (void *)(((uint8_t *)t63) + 872);
        uint32_t t65 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
        gm_f_get_v0dhnfj((void *)&gm_l72, t64, t65);
        uint8_t t66 = (uint8_t)((((uint8_t *)&gm_l72))[40] != 0);
        if (t66) {
            memmove(((uint8_t *)&gm_l73), ((uint8_t *)&gm_l72), 40);
        } else {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l73), 40);
        uint8_t t67 = (uint8_t)((((uint8_t *)&F.l2))[32] != 0);
        if (t67) {
            memmove(((uint8_t *)&gm_l74), ((uint8_t *)&F.l2), 32);
        } else {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        memmove(((uint8_t *)&gm_l3), ((uint8_t *)&gm_l74), 32);
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
        gm_l4 = t68;
        void * t69 = gm_l0;
        gm_t88rcucg1b7uk t70 = gm_l1;
        uint64_t t71 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t69, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t71;
        void * t72 = gm_l0;
        gm_t88rcucg1b7uk t73 = gm_l1;
        gm_b16_8 t74 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t72, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t74;
        gm_trqfvmjtt4sij t75;
        gm_f_list_at7dsst((void *)&t75, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t75;
        gm_t4c7ipjj9s3fp t76;
        gm_f_hash_map_cuh4fs9((void *)&t76, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l8 = t76;
        void * t77 = gm_l0;
        gm_t88rcucg1b7uk t78 = gm_l1;
        gm_f_kind_ghvav22((void *)&gm_l75, t77, GM_CONV(gm_csguankko4lmhd, t78));
        {
            {
                {
                    uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l75) + 0));
                    uint8_t t80 = (uint8_t)(t79 == ((uint8_t)12ULL));
                    if (!t80) {
                        goto gm_lb2;
                    }
                    gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l75) + 8));
                    gm_l9 = t81;
                    gm_b16_8 t82 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l75) + 8) + 16));
                    gm_l10 = t82;
                    gm_trqfvmjtt4sij t83;
                    gm_f_list_epvducg((void *)&t83, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l11 = t83;
                    uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint8_t t85 = (t84 > ((uint64_t)0ULL));
                    if (t85) {
                        gm_b16_8 t86 = gm_l10;
                        void * t87 = (void *)((uint8_t *)&F.l8);
                        gm_b16_8 t88 = gm_f_unique_view_members_3bvsjcm((void *)NULL, cx, NULL, t86, t87);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l12 = t88;
                        gm_b16_8 t89 = gm_l12;
                        gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                        gm_l13 = t90;
                        gm_l14 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t91 = gm_l14;
                                uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                uint8_t t93 = (t91 < t92);
                                if (!t93) {
                                    goto gm_lb3;
                                }
                                {
                                    uint64_t t94 = gm_l14;
                                    void * t95 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                    uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                    uint64_t t97 = gm_l14;
                                    gm_th2lqbo77pvsc t98 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t95) + (uintptr_t)t97 * 24));
                                    gm_t13urt52695sh t99;
                                    GM_ST(uint64_t, ((uint8_t *)&t99 + 0), t94);
                                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t99 + 8), t98);
                                    gm_l76 = t99;
                                    {
                                        {
                                            uint64_t t100 = GM_LD(uint64_t, ((uint8_t *)&gm_l76));
                                            gm_l15 = t100;
                                            memmove(((uint8_t *)&gm_l16), (((uint8_t *)&gm_l76) + 8), 24);
                                            void * t101 = (void *)((uint8_t *)&F.l11);
                                            uint8_t t102 = (uint8_t)((((uint8_t *)&gm_l16))[16] != 0);
                                            if (t102) {
                                                gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
                                                gm_l77 = t103;
                                            } else {
                                                uint64_t t104 = gm_l15;
                                                gm_t4ce0jd3tdro4 t105;
                                                GM_ST(uint64_t, ((uint8_t *)&t105 + 0), t104);
                                                gm_f_format_7c4i91d((void *)&gm_l78, (void *)NULL, cx, NULL, t105);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t106 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                                                if (!t106) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 8, gm_srcs9q7ghsrcgv5, 239, 68);
                                                gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l78) + 8));
                                                gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
                                                gm_l77 = t108;
                                            }
                                            gm_b16_8 t109 = gm_l77;
                                            gm_tftb1odc739t5 t110 = gm_f_push_3ef746b(t101, t109);
                                            gm_l79 = t110;
                                            uint8_t t111 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                                            if (!t111) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l79) + 1, gm_srcs9q7ghsrcgv5, 239, 49);
                                            goto gm_lb6;
                                        }
gm_lb7:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 239, 17);
                                    }
gm_lb6:;
                                }
gm_lb5:;
                                uint64_t t112 = gm_l14;
                                uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                                uint64_t t115 = r113;
                                gm_l14 = t115;
                            }
                        }
gm_lb3:;
                    } else {
                        gm_l18 = ((uint64_t)0ULL);
                        uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                        gm_l17 = t116;
                        {
                            for (;;) {
                                uint64_t t117 = gm_l18;
                                uint64_t t118 = gm_l17;
                                uint8_t t119 = (t117 < t118);
                                if (!t119) {
                                    goto gm_lb8;
                                }
                                {
                                    {
                                        {
                                            uint64_t t120 = gm_l18;
                                            gm_l19 = t120;
                                            void * t121 = (void *)((uint8_t *)&F.l11);
                                            uint64_t t122 = gm_l19;
                                            gm_t4ce0jd3tdro4 t123;
                                            GM_ST(uint64_t, ((uint8_t *)&t123 + 0), t122);
                                            gm_f_format_7c4i91d((void *)&gm_l80, (void *)NULL, cx, NULL, t123);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                                            if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 8, gm_srcs9q7ghsrcgv5, 241, 53);
                                            gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l80) + 8));
                                            gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                                            gm_tftb1odc739t5 t127 = gm_f_push_3ef746b(t121, t126);
                                            gm_l81 = t127;
                                            uint8_t t128 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                                            if (!t128) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l81) + 1, gm_srcs9q7ghsrcgv5, 241, 41);
                                            goto gm_lb11;
                                        }
gm_lb12:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 241, 17);
                                    }
gm_lb11:;
                                }
gm_lb10:;
                                uint64_t t129 = gm_l18;
                                uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                                uint64_t t132 = r130;
                                gm_l18 = t132;
                            }
                        }
gm_lb8:;
                    }
                    gm_b16_8 t133 = gm_l9;
                    gm_l20 = t133;
                    gm_l21 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t134 = gm_l21;
                            uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                            uint8_t t136 = (t134 < t135);
                            if (!t136) {
                                goto gm_lb13;
                            }
                            {
                                uint64_t t137 = gm_l21;
                                void * t138 = GM_LD(void *, ((uint8_t *)&gm_l20));
                                uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                uint64_t t140 = gm_l21;
                                gm_t88rcucg1b7uk t141 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t138) + (uintptr_t)t140 * 4));
                                gm_tlcm4lvu3g1a8 t142;
                                GM_ST(uint32_t, (uint8_t *)&t142 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t142 + 0), t137);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t142 + 8), t141);
                                gm_l82 = t142;
                                {
                                    {
                                        uint64_t t143 = GM_LD(uint64_t, ((uint8_t *)&gm_l82));
                                        gm_l22 = t143;
                                        gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l82) + 8));
                                        gm_l23 = t144;
                                        void * t145 = gm_l0;
                                        void * t146 = (void *)((uint8_t *)&F.l7);
                                        void * t147 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                        uint64_t t148 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                        uint64_t t149 = gm_l22;
                                        if ((uintptr_t)t149 >= (uintptr_t)(t148)) gm_trap_index((uintptr_t)t149, t148, gm_srcs9q7ghsrcgv5, 243, 69);
                                        uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)t147) + (uintptr_t)t149 * 8));
                                        gm_t88rcucg1b7uk t151 = gm_l23;
                                        void * t152 = GM_LD(void *, ((uint8_t *)&F.l11));
                                        uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
                                        uint64_t t154 = gm_l22;
                                        if ((uintptr_t)t154 >= (uintptr_t)(t153)) gm_trap_index((uintptr_t)t154, t153, gm_srcs9q7ghsrcgv5, 243, 81);
                                        gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)t152) + (uintptr_t)t154 * 16));
                                        gm_b0_1 t156 = gm_f_push_member_eubf7s6((void *)NULL, cx, NULL, t145, t146, t150, t151, t155);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb16;
                                    }
gm_lb17:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 243, 13);
                                }
gm_lb16:;
                            }
gm_lb15:;
                            uint64_t t157 = gm_l21;
                            uint64_t r158; bool o159 = __builtin_add_overflow(t157, ((uint64_t)1ULL), &r158);
                            uint64_t t160 = r158;
                            gm_l21 = t160;
                        }
                    }
gm_lb13:;
                    goto gm_lb1;
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&gm_l75) + 0));
                    uint8_t t162 = (uint8_t)(t161 == ((uint8_t)10ULL));
                    if (!t162) {
                        goto gm_lb18;
                    }
                    gm_t88rcucg1b7uk t163 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l75) + 8) + 4));
                    gm_l24 = t163;
                    void * t164 = gm_l0;
                    gm_t88rcucg1b7uk t165 = gm_l24;
                    gm_b16_8 t166 = gm_f_target_view_ams6b9s((void *)NULL, cx, NULL, t164, t165);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l25 = t166;
                    void * t167 = gm_l0;
                    void * t168 = gm_l0;
                    gm_t88rcucg1b7uk t169 = gm_f_usize_ty_9mijhtc((void *)NULL, cx, NULL, t168);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t170 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t167, t169);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l26 = t170;
                    void * t171 = (void *)((uint8_t *)&F.l7);
                    void * t172 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t173)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t173, gm_srcs9q7ghsrcgv5, 248, 45);
                    uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)t172) + (uintptr_t)((uint64_t)0ULL) * 8));
                    void * t175 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t176)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t176, gm_srcs9q7ghsrcgv5, 248, 60);
                    uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)t175) + (uintptr_t)((uint64_t)1ULL) * 8));
                    gm_b16_8 t178 = gm_l25;
                    gm_tnpifv20irnph t179;
                    GM_ST(gm_b16_8, ((uint8_t *)&t179 + 0), t178);
                    gm_f_format_cn4968h((void *)&gm_l83, (void *)NULL, cx, NULL, t179);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t180 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
                    if (!t180) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l83) + 8, gm_srcs9q7ghsrcgv5, 248, 75);
                    gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l83) + 8));
                    gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
                    gm_tbccvikg6b96e t183;
                    GM_ST(uint64_t, ((uint8_t *)&t183 + 0), t174);
                    GM_ST(uint64_t, ((uint8_t *)&t183 + 8), t177);
                    GM_ST(gm_b16_8, ((uint8_t *)&t183 + 16), t182);
                    gm_tftb1odc739t5 t184 = gm_f_push_m7lva8q(t171, &t183);
                    gm_l84 = t184;
                    uint8_t t185 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
                    if (!t185) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l84) + 1, gm_srcs9q7ghsrcgv5, 248, 13);
                    void * t186 = (void *)((uint8_t *)&F.l7);
                    void * t187 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t188)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t188, gm_srcs9q7ghsrcgv5, 249, 45);
                    uint64_t t189 = GM_LD(uint64_t, (((uint8_t *)t187) + (uintptr_t)((uint64_t)1ULL) * 8));
                    uint64_t t190 = gm_l5;
                    void * t191 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t192 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t192)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t192, gm_srcs9q7ghsrcgv5, 249, 67);
                    uint64_t t193 = GM_LD(uint64_t, (((uint8_t *)t191) + (uintptr_t)((uint64_t)1ULL) * 8));
                    uint64_t r194; bool o195 = __builtin_sub_overflow(t190, t193, &r194);
                    if (o195) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 249, 60);
                    uint64_t t196 = r194;
                    gm_b16_8 t197 = gm_l26;
                    gm_tnpifv20irnph t198;
                    GM_ST(gm_b16_8, ((uint8_t *)&t198 + 0), t197);
                    gm_f_format_jk4dii4((void *)&gm_l85, (void *)NULL, cx, NULL, t198);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t199 = (uint8_t)!((((uint8_t *)&gm_l85))[0] != 0);
                    if (!t199) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l85) + 8, gm_srcs9q7ghsrcgv5, 249, 82);
                    gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l85) + 8));
                    gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
                    gm_tbccvikg6b96e t202;
                    GM_ST(uint64_t, ((uint8_t *)&t202 + 0), t189);
                    GM_ST(uint64_t, ((uint8_t *)&t202 + 8), t196);
                    GM_ST(gm_b16_8, ((uint8_t *)&t202 + 16), t201);
                    gm_tftb1odc739t5 t203 = gm_f_push_m7lva8q(t186, &t202);
                    gm_l86 = t203;
                    uint8_t t204 = (uint8_t)!((((uint8_t *)&gm_l86))[0] != 0);
                    if (!t204) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l86) + 1, gm_srcs9q7ghsrcgv5, 249, 13);
                    goto gm_lb1;
                }
gm_lb18:;
                {
                    uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&gm_l75) + 0));
                    uint8_t t206 = (uint8_t)(t205 == ((uint8_t)13ULL));
                    if (!t206) {
                        goto gm_lb19;
                    }
                    gm_t88rcucg1b7uk t207 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l75) + 8));
                    gm_l27 = t207;
                    void * t208 = gm_l0;
                    void * t209 = (void *)((uint8_t *)&F.l7);
                    void * t210 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t211 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t211)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t211, gm_srcs9q7ghsrcgv5, 252, 38);
                    uint64_t t212 = GM_LD(uint64_t, (((uint8_t *)t210) + (uintptr_t)((uint64_t)0ULL) * 8));
                    gm_t88rcucg1b7uk t213 = gm_l27;
                    gm_b16_8 t214;
                    GM_ST(void *, (uint8_t *)&t214, (void *)(gm_s5qkai093c1q0));
                    GM_ST(uintptr_t, (uint8_t *)&t214 + 8, (uintptr_t)(5));
                    gm_b0_1 t215 = gm_f_push_member_eubf7s6((void *)NULL, cx, NULL, t208, t209, t212, t213, t214);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t216 = (void *)((uint8_t *)&F.l7);
                    void * t217 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t218)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t218, gm_srcs9q7ghsrcgv5, 253, 45);
                    uint64_t t219 = GM_LD(uint64_t, (((uint8_t *)t217) + (uintptr_t)((uint64_t)1ULL) * 8));
                    gm_b16_8 t220;
                    GM_ST(void *, (uint8_t *)&t220, (void *)(gm_sk8hn2ar9njah));
                    GM_ST(uintptr_t, (uint8_t *)&t220 + 8, (uintptr_t)(14));
                    gm_tbccvikg6b96e t221;
                    GM_ST(uint64_t, ((uint8_t *)&t221 + 0), t219);
                    GM_ST(uint64_t, ((uint8_t *)&t221 + 8), ((uint64_t)1ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t221 + 16), t220);
                    gm_tftb1odc739t5 t222 = gm_f_push_m7lva8q(t216, &t221);
                    gm_l87 = t222;
                    uint8_t t223 = (uint8_t)!((((uint8_t *)&gm_l87))[0] != 0);
                    if (!t223) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l87) + 1, gm_srcs9q7ghsrcgv5, 253, 13);
                    goto gm_lb1;
                }
gm_lb19:;
            }
            {
                {
                    uint8_t t224 = GM_LD(uint8_t, (((uint8_t *)&gm_l75) + 0));
                    uint8_t t225 = (uint8_t)(t224 == ((uint8_t)14ULL));
                    if (!t225) {
                        goto gm_lb20;
                    }
                    gm_t88rcucg1b7uk t226 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l75) + 8));
                    gm_l28 = t226;
                    gm_t88rcucg1b7uk t227 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l75) + 8) + 4));
                    gm_l29 = t227;
                    void * t228 = (void *)((uint8_t *)&F.l7);
                    void * t229 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t230 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t230)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t230, gm_srcs9q7ghsrcgv5, 256, 45);
                    uint64_t t231 = GM_LD(uint64_t, (((uint8_t *)t229) + (uintptr_t)((uint64_t)0ULL) * 8));
                    gm_b16_8 t232;
                    GM_ST(void *, (uint8_t *)&t232, (void *)(gm_sk2tgsi47prt7));
                    GM_ST(uintptr_t, (uint8_t *)&t232 + 8, (uintptr_t)(13));
                    gm_tbccvikg6b96e t233;
                    GM_ST(uint64_t, ((uint8_t *)&t233 + 0), t231);
                    GM_ST(uint64_t, ((uint8_t *)&t233 + 8), ((uint64_t)1ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t233 + 16), t232);
                    gm_tftb1odc739t5 t234 = gm_f_push_m7lva8q(t228, &t233);
                    gm_l88 = t234;
                    uint8_t t235 = (uint8_t)!((((uint8_t *)&gm_l88))[0] != 0);
                    if (!t235) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l88) + 1, gm_srcs9q7ghsrcgv5, 256, 13);
                    gm_trqfvmjtt4sij t236;
                    gm_f_list_58nr42v_3((void *)&t236, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l30 = t236;
                    void * t237 = (void *)((uint8_t *)&F.l30);
                    gm_t88rcucg1b7uk t238 = gm_l29;
                    gm_b16_8 t239;
                    GM_ST(void *, (uint8_t *)&t239, (void *)(gm_soim4k70n6bc7));
                    GM_ST(uintptr_t, (uint8_t *)&t239 + 8, (uintptr_t)(2));
                    gm_tcmkompbl09il t240;
                    GM_ST(uint32_t, (uint8_t *)&t240 + 4, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t240 + 0), t238);
                    GM_ST(gm_b16_8, ((uint8_t *)&t240 + 8), t239);
                    gm_tftb1odc739t5 t241 = gm_f_push_bpovr2k_3(t237, &t240);
                    gm_l89 = t241;
                    uint8_t t242 = (uint8_t)!((((uint8_t *)&gm_l89))[0] != 0);
                    if (!t242) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l89) + 1, gm_srcs9q7ghsrcgv5, 258, 13);
                    void * t243 = (void *)((uint8_t *)&F.l30);
                    gm_t88rcucg1b7uk t244 = gm_l28;
                    gm_b16_8 t245;
                    GM_ST(void *, (uint8_t *)&t245, (void *)(gm_shhr1hlq1ethc));
                    GM_ST(uintptr_t, (uint8_t *)&t245 + 8, (uintptr_t)(3));
                    gm_tcmkompbl09il t246;
                    GM_ST(uint32_t, (uint8_t *)&t246 + 4, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t246 + 0), t244);
                    GM_ST(gm_b16_8, ((uint8_t *)&t246 + 8), t245);
                    gm_tftb1odc739t5 t247 = gm_f_push_bpovr2k_3(t243, &t246);
                    gm_l90 = t247;
                    uint8_t t248 = (uint8_t)!((((uint8_t *)&gm_l90))[0] != 0);
                    if (!t248) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l90) + 1, gm_srcs9q7ghsrcgv5, 259, 13);
                    void * t249 = gm_l0;
                    void * t250 = (void *)((uint8_t *)&F.l7);
                    void * t251 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t252 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t252)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t252, gm_srcs9q7ghsrcgv5, 260, 37);
                    uint64_t t253 = GM_LD(uint64_t, (((uint8_t *)t251) + (uintptr_t)((uint64_t)1ULL) * 8));
                    gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&F.l30));
                    gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
                    gm_b0_1 t256 = gm_f_push_union_m2cqbk2((void *)NULL, cx, NULL, t249, t250, t253, t255);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb20:;
                {
                    {
                        uint8_t t257 = GM_LD(uint8_t, (((uint8_t *)&gm_l75) + 0));
                        uint8_t t258 = (uint8_t)(t257 == ((uint8_t)17ULL));
                        if (!t258) {
                            goto gm_lb21;
                        }
                        void * t259 = gm_l0;
                        gm_t88rcucg1b7uk t260 = gm_l1;
                        void * t261 = gm_f_adt_of_gej99vn(t259, GM_CONV(gm_csguankko4lmhd, t260));
                        {
                            {
                                {
                                    uint8_t t262 = GM_LD(uint8_t, ((((uint8_t *)t261) + 24) + 0));
                                    uint8_t t263 = (uint8_t)(t262 == ((uint8_t)0ULL));
                                    if (!t263) {
                                        goto gm_lb23;
                                    }
                                    memmove(((uint8_t *)&F.l31), ((((uint8_t *)t261) + 24) + 8), 48);
                                    gm_trqfvmjtt4sij t264;
                                    gm_f_list_epvducg((void *)&t264, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l32 = t264;
                                    gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
                                    gm_l33 = t265;
                                    gm_l34 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t266 = gm_l34;
                                            uint64_t t267 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                            uint8_t t268 = (t266 < t267);
                                            if (!t268) {
                                                goto gm_lb24;
                                            }
                                            {
                                                void * t269 = GM_LD(void *, ((uint8_t *)&gm_l33));
                                                uint64_t t270 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                                uint64_t t271 = gm_l34;
                                                {
                                                    {
                                                        memmove(((uint8_t *)&F.l35), (((uint8_t *)t269) + (uintptr_t)t271 * 40), 40);
                                                        void * t272 = (void *)((uint8_t *)&F.l32);
                                                        gm_b16_8 t273 = GM_LD(gm_b16_8, ((uint8_t *)&F.l35));
                                                        gm_tftb1odc739t5 t274 = gm_f_push_3ef746b(t272, t273);
                                                        gm_l91 = t274;
                                                        uint8_t t275 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                                                        if (!t275) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l91) + 1, gm_srcs9q7ghsrcgv5, 266, 43);
                                                        goto gm_lb27;
                                                    }
gm_lb28:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 266, 21);
                                                }
gm_lb27:;
                                            }
gm_lb26:;
                                            uint64_t t276 = gm_l34;
                                            uint64_t r277; bool o278 = __builtin_add_overflow(t276, ((uint64_t)1ULL), &r277);
                                            uint64_t t279 = r277;
                                            gm_l34 = t279;
                                        }
                                    }
gm_lb24:;
                                    gm_b16_8 t280 = GM_LD(gm_b16_8, ((uint8_t *)&F.l32));
                                    gm_b16_8 t281 = GM_LD(gm_b16_8, ((uint8_t *)&t280));
                                    void * t282 = (void *)((uint8_t *)&F.l8);
                                    gm_b16_8 t283 = gm_f_unique_view_members_3bvsjcm((void *)NULL, cx, NULL, t281, t282);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l36 = t283;
                                    gm_b16_8 t284 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
                                    gm_b16_8 t285 = GM_LD(gm_b16_8, ((uint8_t *)&t284));
                                    gm_l37 = t285;
                                    gm_l38 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t286 = gm_l38;
                                            uint64_t t287 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                                            uint8_t t288 = (t286 < t287);
                                            if (!t288) {
                                                goto gm_lb29;
                                            }
                                            {
                                                uint64_t t289 = gm_l38;
                                                void * t290 = GM_LD(void *, ((uint8_t *)&gm_l37));
                                                uint64_t t291 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                                                uint64_t t292 = gm_l38;
                                                gm_t0mglksrkfa5e t293 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t290) + (uintptr_t)t292 * 40));
                                                gm_t8ta482bl6be0 t294;
                                                GM_ST(uint64_t, ((uint8_t *)&t294 + 0), t289);
                                                GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&t294 + 8), t293);
                                                gm_l92 = t294;
                                                {
                                                    {
                                                        uint64_t t295 = GM_LD(uint64_t, ((uint8_t *)&gm_l92));
                                                        gm_l39 = t295;
                                                        memmove(((uint8_t *)&F.l40), (((uint8_t *)&gm_l92) + 8), 40);
                                                        void * t296 = GM_LD(void *, ((uint8_t *)&gm_l36));
                                                        uint64_t t297 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                                                        uint64_t t298 = gm_l39;
                                                        if ((uintptr_t)t298 >= (uintptr_t)(t297)) gm_trap_index((uintptr_t)t298, t297, gm_srcs9q7ghsrcgv5, 269, 33);
                                                        uint8_t t299 = (uint8_t)(((((uint8_t *)t296) + (uintptr_t)t298 * 24))[16] != 0);
                                                        if (t299) {
                                                            gm_b16_8 t300 = GM_LD(gm_b16_8, (((uint8_t *)t296) + (uintptr_t)t298 * 24));
                                                            gm_l93 = t300;
                                                        } else {
                                                            uint64_t t301 = gm_l39;
                                                            gm_t4ce0jd3tdro4 t302;
                                                            GM_ST(uint64_t, ((uint8_t *)&t302 + 0), t301);
                                                            gm_f_format_tc7tn98((void *)&gm_l94, (void *)NULL, cx, NULL, t302);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            uint8_t t303 = (uint8_t)!((((uint8_t *)&gm_l94))[0] != 0);
                                                            if (!t303) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l94) + 8, gm_srcs9q7ghsrcgv5, 269, 48);
                                                            gm_b16_8 t304 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l94) + 8));
                                                            gm_b16_8 t305 = GM_LD(gm_b16_8, ((uint8_t *)&t304));
                                                            gm_l93 = t305;
                                                        }
                                                        gm_b16_8 t306 = gm_l93;
                                                        gm_l41 = t306;
                                                        void * t307 = gm_l0;
                                                        void * t308 = (void *)((uint8_t *)&F.l7);
                                                        void * t309 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                                        uint64_t t310 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                                        uint64_t t311 = gm_l39;
                                                        if ((uintptr_t)t311 >= (uintptr_t)(t310)) gm_trap_index((uintptr_t)t311, t310, gm_srcs9q7ghsrcgv5, 270, 50);
                                                        uint64_t t312 = GM_LD(uint64_t, (((uint8_t *)t309) + (uintptr_t)t311 * 8));
                                                        gm_t88rcucg1b7uk t313 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l40) + 16));
                                                        gm_b16_8 t314 = gm_l41;
                                                        gm_b0_1 t315 = gm_f_push_member_eubf7s6((void *)NULL, cx, NULL, t307, t308, t312, t313, t314);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        goto gm_lb32;
                                                    }
gm_lb33:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 268, 21);
                                                }
gm_lb32:;
                                            }
gm_lb31:;
                                            uint64_t t316 = gm_l38;
                                            uint64_t r317; bool o318 = __builtin_add_overflow(t316, ((uint64_t)1ULL), &r317);
                                            uint64_t t319 = r317;
                                            gm_l38 = t319;
                                        }
                                    }
gm_lb29:;
                                    goto gm_lb22;
                                }
gm_lb23:;
                            }
                            {
                                {
                                    uint8_t t320 = GM_LD(uint8_t, ((((uint8_t *)t261) + 24) + 0));
                                    uint8_t t321 = (uint8_t)(t320 == ((uint8_t)2ULL));
                                    if (!t321) {
                                        goto gm_lb34;
                                    }
                                    gm_b16_8 t322 = GM_LD(gm_b16_8, ((((uint8_t *)t261) + 24) + 8));
                                    gm_l42 = t322;
                                    gm_trqfvmjtt4sij t323;
                                    gm_f_list_epvducg((void *)&t323, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l43 = t323;
                                    gm_b16_8 t324 = gm_l42;
                                    gm_l44 = t324;
                                    gm_l45 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t325 = gm_l45;
                                            uint64_t t326 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                                            uint8_t t327 = (t325 < t326);
                                            if (!t327) {
                                                goto gm_lb35;
                                            }
                                            {
                                                void * t328 = GM_LD(void *, ((uint8_t *)&gm_l44));
                                                uint64_t t329 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                                                uint64_t t330 = gm_l45;
                                                {
                                                    {
                                                        memmove(((uint8_t *)&F.l46), (((uint8_t *)t328) + (uintptr_t)t330 * 40), 40);
                                                        void * t331 = (void *)((uint8_t *)&F.l43);
                                                        gm_b16_8 t332 = GM_LD(gm_b16_8, ((uint8_t *)&F.l46));
                                                        gm_tftb1odc739t5 t333 = gm_f_push_3ef746b(t331, t332);
                                                        gm_l95 = t333;
                                                        uint8_t t334 = (uint8_t)!((((uint8_t *)&gm_l95))[0] != 0);
                                                        if (!t334) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l95) + 1, gm_srcs9q7ghsrcgv5, 275, 39);
                                                        goto gm_lb38;
                                                    }
gm_lb39:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 275, 21);
                                                }
gm_lb38:;
                                            }
gm_lb37:;
                                            uint64_t t335 = gm_l45;
                                            uint64_t r336; bool o337 = __builtin_add_overflow(t335, ((uint64_t)1ULL), &r336);
                                            uint64_t t338 = r336;
                                            gm_l45 = t338;
                                        }
                                    }
gm_lb35:;
                                    gm_b16_8 t339 = GM_LD(gm_b16_8, ((uint8_t *)&F.l43));
                                    gm_b16_8 t340 = GM_LD(gm_b16_8, ((uint8_t *)&t339));
                                    void * t341 = (void *)((uint8_t *)&F.l8);
                                    gm_b16_8 t342 = gm_f_unique_view_members_3bvsjcm((void *)NULL, cx, NULL, t340, t341);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l47 = t342;
                                    gm_trqfvmjtt4sij t343;
                                    gm_f_list_58nr42v_3((void *)&t343, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l48 = t343;
                                    gm_b16_8 t344 = gm_l42;
                                    gm_b16_8 t345 = GM_LD(gm_b16_8, ((uint8_t *)&t344));
                                    gm_l49 = t345;
                                    gm_l50 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t346 = gm_l50;
                                            uint64_t t347 = GM_LD(uint64_t, (((uint8_t *)&gm_l49) + 8));
                                            uint8_t t348 = (t346 < t347);
                                            if (!t348) {
                                                goto gm_lb40;
                                            }
                                            {
                                                uint64_t t349 = gm_l50;
                                                void * t350 = GM_LD(void *, ((uint8_t *)&gm_l49));
                                                uint64_t t351 = GM_LD(uint64_t, (((uint8_t *)&gm_l49) + 8));
                                                uint64_t t352 = gm_l50;
                                                gm_t0mglksrkfa5e t353 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t350) + (uintptr_t)t352 * 40));
                                                gm_t8ta482bl6be0 t354;
                                                GM_ST(uint64_t, ((uint8_t *)&t354 + 0), t349);
                                                GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&t354 + 8), t353);
                                                gm_l96 = t354;
                                                {
                                                    {
                                                        uint64_t t355 = GM_LD(uint64_t, ((uint8_t *)&gm_l96));
                                                        gm_l51 = t355;
                                                        memmove(((uint8_t *)&F.l52), (((uint8_t *)&gm_l96) + 8), 40);
                                                        void * t356 = (void *)((uint8_t *)&F.l48);
                                                        gm_t88rcucg1b7uk t357 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l52) + 16));
                                                        void * t358 = GM_LD(void *, ((uint8_t *)&gm_l47));
                                                        uint64_t t359 = GM_LD(uint64_t, (((uint8_t *)&gm_l47) + 8));
                                                        uint64_t t360 = gm_l51;
                                                        if ((uintptr_t)t360 >= (uintptr_t)(t359)) gm_trap_index((uintptr_t)t360, t359, gm_srcs9q7ghsrcgv5, 279, 55);
                                                        uint8_t t361 = (uint8_t)(((((uint8_t *)t358) + (uintptr_t)t360 * 24))[16] != 0);
                                                        if (t361) {
                                                            gm_b16_8 t362 = GM_LD(gm_b16_8, (((uint8_t *)t358) + (uintptr_t)t360 * 24));
                                                            gm_l97 = t362;
                                                        } else {
                                                            uint64_t t363 = gm_l51;
                                                            gm_t4ce0jd3tdro4 t364;
                                                            GM_ST(uint64_t, ((uint8_t *)&t364 + 0), t363);
                                                            gm_f_format_tc7tn98((void *)&gm_l98, (void *)NULL, cx, NULL, t364);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            uint8_t t365 = (uint8_t)!((((uint8_t *)&gm_l98))[0] != 0);
                                                            if (!t365) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l98) + 8, gm_srcs9q7ghsrcgv5, 279, 70);
                                                            gm_b16_8 t366 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l98) + 8));
                                                            gm_b16_8 t367 = GM_LD(gm_b16_8, ((uint8_t *)&t366));
                                                            gm_l97 = t367;
                                                        }
                                                        gm_b16_8 t368 = gm_l97;
                                                        gm_tcmkompbl09il t369;
                                                        GM_ST(uint32_t, (uint8_t *)&t369 + 4, 0);
                                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t369 + 0), t357);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t369 + 8), t368);
                                                        gm_tftb1odc739t5 t370 = gm_f_push_bpovr2k_3(t356, &t369);
                                                        gm_l99 = t370;
                                                        uint8_t t371 = (uint8_t)!((((uint8_t *)&gm_l99))[0] != 0);
                                                        if (!t371) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l99) + 1, gm_srcs9q7ghsrcgv5, 279, 25);
                                                        goto gm_lb43;
                                                    }
gm_lb44:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 278, 21);
                                                }
gm_lb43:;
                                            }
gm_lb42:;
                                            uint64_t t372 = gm_l50;
                                            uint64_t r373; bool o374 = __builtin_add_overflow(t372, ((uint64_t)1ULL), &r373);
                                            uint64_t t375 = r373;
                                            gm_l50 = t375;
                                        }
                                    }
gm_lb40:;
                                    void * t376 = gm_l0;
                                    void * t377 = (void *)((uint8_t *)&F.l7);
                                    gm_b16_8 t378 = GM_LD(gm_b16_8, ((uint8_t *)&F.l48));
                                    gm_b16_8 t379 = GM_LD(gm_b16_8, ((uint8_t *)&t378));
                                    gm_b0_1 t380 = gm_f_push_union_m2cqbk2((void *)NULL, cx, NULL, t376, t377, ((uint64_t)0ULL), t379);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb22;
                                }
gm_lb34:;
                            }
                            {
                                {
                                    uint8_t t381 = GM_LD(uint8_t, ((((uint8_t *)t261) + 24) + 0));
                                    uint8_t t382 = (uint8_t)(t381 == ((uint8_t)1ULL));
                                    if (!t382) {
                                        goto gm_lb45;
                                    }
                                    gm_t88rcucg1b7uk t383 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t261) + 24) + 8));
                                    gm_l53 = t383;
                                    gm_b16_8 t384 = GM_LD(gm_b16_8, (((((uint8_t *)t261) + 24) + 8) + 8));
                                    gm_l54 = t384;
                                    void * t385 = gm_l0;
                                    gm_t88rcucg1b7uk t386 = gm_l1;
                                    gm_b16_8 t387 = gm_l4;
                                    gm_b16_8 t388 = gm_f_define_enum_v780plg((void *)NULL, cx, NULL, t385, t386, t387);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l55 = t388;
                                    void * t389 = gm_l0;
                                    gm_t88rcucg1b7uk t390 = gm_l1;
                                    uint8_t t391 = gm_f_is_aggregate_buh1uic((void *)NULL, cx, NULL, t389, t390);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t392 = (uint8_t)!t391;
                                    if (t392) {
                                        F.ret = GM_ZERO(gm_b0_1);
                                        goto gm_fret;
                                    }
                                    void * t393 = (void *)((uint8_t *)&F.l7);
                                    void * t394 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t395 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t395)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t395, gm_srcs9q7ghsrcgv5, 286, 53);
                                    uint64_t t396 = GM_LD(uint64_t, (((uint8_t *)t394) + (uintptr_t)((uint64_t)0ULL) * 8));
                                    void * t397 = gm_l0;
                                    gm_t88rcucg1b7uk t398 = gm_l53;
                                    uint64_t t399 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t397, t398);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_b16_8 t400 = gm_l55;
                                    gm_tnpifv20irnph t401;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t401 + 0), t400);
                                    gm_f_format_7nu71mn((void *)&gm_l100, (void *)NULL, cx, NULL, t401);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t402 = (uint8_t)!((((uint8_t *)&gm_l100))[0] != 0);
                                    if (!t402) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l100) + 8, gm_srcs9q7ghsrcgv5, 286, 88);
                                    gm_b16_8 t403 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l100) + 8));
                                    gm_b16_8 t404 = GM_LD(gm_b16_8, ((uint8_t *)&t403));
                                    gm_tbccvikg6b96e t405;
                                    GM_ST(uint64_t, ((uint8_t *)&t405 + 0), t396);
                                    GM_ST(uint64_t, ((uint8_t *)&t405 + 8), t399);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t405 + 16), t404);
                                    gm_tftb1odc739t5 t406 = gm_f_push_m7lva8q(t393, &t405);
                                    gm_l101 = t406;
                                    uint8_t t407 = (uint8_t)!((((uint8_t *)&gm_l101))[0] != 0);
                                    if (!t407) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l101) + 1, gm_srcs9q7ghsrcgv5, 286, 21);
                                    void * t408 = (void *)((uint8_t *)&F.l8);
                                    gm_b16_8 t409;
                                    GM_ST(void *, (uint8_t *)&t409, (void *)(gm_s420tm9ua7ggg));
                                    GM_ST(uintptr_t, (uint8_t *)&t409 + 8, (uintptr_t)(3));
                                    gm_tftb1odc739t5 t410 = gm_f_put_5c6qgui(t408, t409, ((uint8_t)1));
                                    gm_l102 = t410;
                                    uint8_t t411 = (uint8_t)!((((uint8_t *)&gm_l102))[0] != 0);
                                    if (!t411) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l102) + 1, gm_srcs9q7ghsrcgv5, 287, 21);
                                    gm_trqfvmjtt4sij t412;
                                    gm_f_list_epvducg((void *)&t412, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l56 = t412;
                                    gm_b16_8 t413 = gm_l54;
                                    gm_l57 = t413;
                                    gm_l58 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t414 = gm_l58;
                                            uint64_t t415 = GM_LD(uint64_t, (((uint8_t *)&gm_l57) + 8));
                                            uint8_t t416 = (t414 < t415);
                                            if (!t416) {
                                                goto gm_lb46;
                                            }
                                            {
                                                void * t417 = GM_LD(void *, ((uint8_t *)&gm_l57));
                                                uint64_t t418 = GM_LD(uint64_t, (((uint8_t *)&gm_l57) + 8));
                                                uint64_t t419 = gm_l58;
                                                {
                                                    {
                                                        memmove(((uint8_t *)&F.l59), (((uint8_t *)t417) + (uintptr_t)t419 * 72), 72);
                                                        void * t420 = (void *)((uint8_t *)&F.l56);
                                                        gm_b16_8 t421 = GM_LD(gm_b16_8, ((uint8_t *)&F.l59));
                                                        gm_tftb1odc739t5 t422 = gm_f_push_3ef746b(t420, t421);
                                                        gm_l103 = t422;
                                                        uint8_t t423 = (uint8_t)!((((uint8_t *)&gm_l103))[0] != 0);
                                                        if (!t423) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l103) + 1, gm_srcs9q7ghsrcgv5, 289, 42);
                                                        goto gm_lb49;
                                                    }
gm_lb50:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 289, 21);
                                                }
gm_lb49:;
                                            }
gm_lb48:;
                                            uint64_t t424 = gm_l58;
                                            uint64_t r425; bool o426 = __builtin_add_overflow(t424, ((uint64_t)1ULL), &r425);
                                            uint64_t t427 = r425;
                                            gm_l58 = t427;
                                        }
                                    }
gm_lb46:;
                                    gm_b16_8 t428 = GM_LD(gm_b16_8, ((uint8_t *)&F.l56));
                                    gm_b16_8 t429 = GM_LD(gm_b16_8, ((uint8_t *)&t428));
                                    void * t430 = (void *)((uint8_t *)&F.l8);
                                    gm_b16_8 t431 = gm_f_unique_view_members_3bvsjcm((void *)NULL, cx, NULL, t429, t430);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l60 = t431;
                                    gm_trqfvmjtt4sij t432;
                                    gm_f_list_58nr42v_3((void *)&t432, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l61 = t432;
                                    gm_b16_8 t433 = gm_l54;
                                    gm_b16_8 t434 = GM_LD(gm_b16_8, ((uint8_t *)&t433));
                                    gm_l62 = t434;
                                    gm_l63 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t435 = gm_l63;
                                            uint64_t t436 = GM_LD(uint64_t, (((uint8_t *)&gm_l62) + 8));
                                            uint8_t t437 = (t435 < t436);
                                            if (!t437) {
                                                goto gm_lb51;
                                            }
                                            {
                                                uint64_t t438 = gm_l63;
                                                void * t439 = GM_LD(void *, ((uint8_t *)&gm_l62));
                                                uint64_t t440 = GM_LD(uint64_t, (((uint8_t *)&gm_l62) + 8));
                                                uint64_t t441 = gm_l63;
                                                gm_tckdobunaulee t442 = GM_LD(gm_tckdobunaulee, (((uint8_t *)t439) + (uintptr_t)t441 * 72));
                                                gm_t3l8idb2m6c1r t443;
                                                GM_ST(uint64_t, ((uint8_t *)&t443 + 0), t438);
                                                GM_ST(gm_tckdobunaulee, ((uint8_t *)&t443 + 8), t442);
                                                gm_l104 = t443;
                                                {
                                                    {
                                                        uint64_t t444 = GM_LD(uint64_t, ((uint8_t *)&gm_l104));
                                                        gm_l64 = t444;
                                                        memmove(((uint8_t *)&F.l65), (((uint8_t *)&gm_l104) + 8), 72);
                                                        uint8_t t445 = (uint8_t)(((((uint8_t *)&F.l65) + 16))[4] != 0);
                                                        if (t445) {
                                                            gm_t88rcucg1b7uk t446 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l65) + 16));
                                                            gm_l105 = t446;
                                                        } else {
                                                            goto gm_lb53;
                                                        }
                                                        gm_t88rcucg1b7uk t447 = gm_l105;
                                                        gm_l66 = t447;
                                                        void * t448 = (void *)((uint8_t *)&F.l61);
                                                        gm_t88rcucg1b7uk t449 = gm_l66;
                                                        void * t450 = GM_LD(void *, ((uint8_t *)&gm_l60));
                                                        uint64_t t451 = GM_LD(uint64_t, (((uint8_t *)&gm_l60) + 8));
                                                        uint64_t t452 = gm_l64;
                                                        if ((uintptr_t)t452 >= (uintptr_t)(t451)) gm_trap_index((uintptr_t)t452, t451, gm_srcs9q7ghsrcgv5, 294, 52);
                                                        uint8_t t453 = (uint8_t)(((((uint8_t *)t450) + (uintptr_t)t452 * 24))[16] != 0);
                                                        if (t453) {
                                                            gm_b16_8 t454 = GM_LD(gm_b16_8, (((uint8_t *)t450) + (uintptr_t)t452 * 24));
                                                            gm_l106 = t454;
                                                        } else {
                                                            uint64_t t455 = gm_l64;
                                                            gm_t4ce0jd3tdro4 t456;
                                                            GM_ST(uint64_t, ((uint8_t *)&t456 + 0), t455);
                                                            gm_f_format_0bd7vot((void *)&gm_l107, (void *)NULL, cx, NULL, t456);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            uint8_t t457 = (uint8_t)!((((uint8_t *)&gm_l107))[0] != 0);
                                                            if (!t457) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l107) + 8, gm_srcs9q7ghsrcgv5, 294, 67);
                                                            gm_b16_8 t458 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l107) + 8));
                                                            gm_b16_8 t459 = GM_LD(gm_b16_8, ((uint8_t *)&t458));
                                                            gm_l106 = t459;
                                                        }
                                                        gm_b16_8 t460 = gm_l106;
                                                        gm_tcmkompbl09il t461;
                                                        GM_ST(uint32_t, (uint8_t *)&t461 + 4, 0);
                                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t461 + 0), t449);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t461 + 8), t460);
                                                        gm_tftb1odc739t5 t462 = gm_f_push_bpovr2k_3(t448, &t461);
                                                        gm_l108 = t462;
                                                        uint8_t t463 = (uint8_t)!((((uint8_t *)&gm_l108))[0] != 0);
                                                        if (!t463) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l108) + 1, gm_srcs9q7ghsrcgv5, 294, 25);
                                                        goto gm_lb54;
                                                    }
gm_lb55:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 292, 21);
                                                }
gm_lb54:;
                                            }
gm_lb53:;
                                            uint64_t t464 = gm_l63;
                                            uint64_t r465; bool o466 = __builtin_add_overflow(t464, ((uint64_t)1ULL), &r465);
                                            uint64_t t467 = r465;
                                            gm_l63 = t467;
                                        }
                                    }
gm_lb51:;
                                    void * t468 = gm_l0;
                                    void * t469 = (void *)((uint8_t *)&F.l7);
                                    void * t470 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t471 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t471)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t471, gm_srcs9q7ghsrcgv5, 296, 45);
                                    uint64_t t472 = GM_LD(uint64_t, (((uint8_t *)t470) + (uintptr_t)((uint64_t)1ULL) * 8));
                                    gm_b16_8 t473 = GM_LD(gm_b16_8, ((uint8_t *)&F.l61));
                                    gm_b16_8 t474 = GM_LD(gm_b16_8, ((uint8_t *)&t473));
                                    gm_b0_1 t475 = gm_f_push_union_m2cqbk2((void *)NULL, cx, NULL, t468, t469, t472, t474);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb22;
                                }
gm_lb45:;
                            }
                            {
                                gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 263, 13);
                            }
                        }
gm_lb22:;
                    }
                    {
                        goto gm_lb1;
                    }
                }
gm_lb21:;
            }
            {
                {
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb56:;
                gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 234, 5);
            }
        }
gm_lb1:;
    }
    {
        gm_trqfvmjtt4sij t476;
        gm_f_list_uvfu1k1((void *)&t476, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l67 = t476;
        gm_l68 = ((uint64_t)0ULL);
        gm_b16_8 t477 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
        gm_l69 = t477;
        gm_l70 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t478 = gm_l70;
                uint64_t t479 = GM_LD(uint64_t, (((uint8_t *)&gm_l69) + 8));
                uint8_t t480 = (t478 < t479);
                if (!t480) {
                    goto gm_lb57;
                }
                {
                    void * t481 = GM_LD(void *, ((uint8_t *)&gm_l69));
                    uint64_t t482 = GM_LD(uint64_t, (((uint8_t *)&gm_l69) + 8));
                    uint64_t t483 = gm_l70;
                    {
                        {
                            memmove(((uint8_t *)&gm_l71), (((uint8_t *)t481) + (uintptr_t)t483 * 32), 32);
                            void * t484 = (void *)((uint8_t *)&F.l67);
                            uint64_t t485 = GM_LD(uint64_t, ((uint8_t *)&gm_l71));
                            uint64_t t486 = gm_l68;
                            uint64_t r487; bool o488 = __builtin_sub_overflow(t485, t486, &r487);
                            if (o488) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 305, 20);
                            uint64_t t489 = r487;
                            gm_b0_1 t490 = gm_f_pad_juftt9c((void *)NULL, cx, NULL, t484, t489);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t491 = (void *)((uint8_t *)&F.l67);
                            gm_b16_8 t492 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l71) + 16));
                            gm_tnpifv20irnph t493;
                            GM_ST(gm_b16_8, ((uint8_t *)&t493 + 0), t492);
                            gm_tftb1odc739t5 t494 = gm_f_write_dqbiqk0(t491, t493);
                            gm_l109 = t494;
                            uint8_t t495 = (uint8_t)!((((uint8_t *)&gm_l109))[0] != 0);
                            if (!t495) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l109) + 1, gm_srcs9q7ghsrcgv5, 306, 9);
                            uint64_t t496 = GM_LD(uint64_t, ((uint8_t *)&gm_l71));
                            uint64_t t497 = GM_LD(uint64_t, (((uint8_t *)&gm_l71) + 8));
                            uint64_t r498; bool o499 = __builtin_add_overflow(t496, t497, &r498);
                            if (o499) gm_trap("integer overflow in `+`", gm_srcs9q7ghsrcgv5, 307, 14);
                            uint64_t t500 = r498;
                            gm_l68 = t500;
                            goto gm_lb60;
                        }
gm_lb61:;
                        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 304, 5);
                    }
gm_lb60:;
                }
gm_lb59:;
                uint64_t t501 = gm_l70;
                uint64_t r502; bool o503 = __builtin_add_overflow(t501, ((uint64_t)1ULL), &r502);
                uint64_t t504 = r502;
                gm_l70 = t504;
            }
        }
gm_lb57:;
        void * t505 = (void *)((uint8_t *)&F.l67);
        uint64_t t506 = gm_l5;
        uint64_t t507 = gm_l68;
        uint64_t r508; bool o509 = __builtin_sub_overflow(t506, t507, &r508);
        if (o509) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 309, 16);
        uint64_t t510 = r508;
        gm_b0_1 t511 = gm_f_pad_juftt9c((void *)NULL, cx, NULL, t505, t510);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t512 = gm_l0;
        void * t513 = (void *)((((uint8_t *)t512) + 872) + 104);
        gm_b16_8 t514 = gm_l4;
        gm_b16_8 t515 = gm_f_packed_v79gj1r((void *)NULL, cx, NULL, t514);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t516 = GM_LD(gm_b16_8, ((uint8_t *)&F.l67));
        gm_t6j6smj7qoctu t517;
        GM_ST(gm_b16_8, ((uint8_t *)&t517 + 0), t515);
        GM_ST(gm_b16_8, ((uint8_t *)&t517 + 16), t516);
        gm_tftb1odc739t5 t518 = gm_f_write_ue93873(t513, &t517);
        gm_l110 = t518;
        uint8_t t519 = (uint8_t)!((((uint8_t *)&gm_l110))[0] != 0);
        if (!t519) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l110) + 1, gm_srcs9q7ghsrcgv5, 310, 5);
        void * t520 = gm_l0;
        gm_b16_8 t521 = gm_l4;
        uint64_t t522 = gm_l5;
        gm_b0_1 t523 = gm_f_static_assert_15uo2jf((void *)NULL, cx, NULL, t520, t521, t522);
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
__attribute__((aligned(4))) void gm_f_enum_info_61n2nkp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_enum_info_61n2nkp F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    void * gm_l2; /* def */
    gm_t88rcucg1b7uk gm_l3; /* tag */
    uint64_t gm_l4; /* size */
    uint64_t gm_l5; /* n */
    uint8_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* base */
    GM_STACK_CHECK(&F, 774u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    gm_th2lqbo77pvsc gm_l18;
    {
        {
            void * t12 = gm_l0;
            gm_t88rcucg1b7uk t13 = gm_l1;
            gm_f_kind_ghvav22((void *)&gm_l10, t12, GM_CONV(gm_csguankko4lmhd, t13));
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)17ULL));
            if (!t15) {
                goto gm_lb2;
            }
            void * t16 = gm_l0;
            gm_t88rcucg1b7uk t17 = gm_l1;
            void * t18 = gm_f_adt_of_gej99vn(t16, GM_CONV(gm_csguankko4lmhd, t17));
            gm_l2 = t18;
            {
                {
                    void * t19 = gm_l2;
                    uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t19) + 24) + 0));
                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)1ULL));
                    if (!t21) {
                        goto gm_lb4;
                    }
                    gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t19) + 24) + 8));
                    gm_l3 = t22;
                    void * t23 = gm_l0;
                    gm_t88rcucg1b7uk t24 = gm_l3;
                    uint64_t t25 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t23, t24);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l4 = t25;
                    uint64_t t26 = gm_l4;
                    uint8_t t27 = (t26 > ((uint64_t)4ULL));
                    if (t27) {
                        gm_t4ml5v7ugcjrt t28;
                        memset(&t28, 0, sizeof t28);
                        memmove(gm_out, ((uint8_t *)&t28), 40);
                        goto gm_fdone;
                    }
                    gm_l5 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t29 = gm_l5;
                            void * t30 = gm_l2;
                            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 8));
                            uint8_t t32 = (t29 < t31);
                            if (!t32) {
                                goto gm_lb5;
                            }
                            void * t33 = gm_l2;
                            void * t34 = GM_LD(void *, ((uint8_t *)t33));
                            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)t33) + 8));
                            uint64_t t36 = gm_l5;
                            if ((uintptr_t)t36 >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)t36, t35, gm_srcs9q7ghsrcgv5, 362, 33);
                            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t34) + (uintptr_t)t36 * 1));
                            gm_l6 = t37;
                            uint8_t t38 = gm_l6;
                            uint8_t t39 = (((uint8_t)97ULL) <= t38);
                            if (t39) {
                                uint8_t t40 = gm_l6;
                                uint8_t t41 = (t40 <= ((uint8_t)122ULL));
                                gm_l14 = t41;
                            } else {
                                gm_l14 = ((uint8_t)0);
                            }
                            uint8_t t42 = gm_l14;
                            if (t42) {
                                gm_l13 = ((uint8_t)1);
                            } else {
                                void * t43 = gm_l2;
                                void * t44 = GM_LD(void *, ((uint8_t *)t43));
                                uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)t43) + 8));
                                uint64_t t46 = gm_l5;
                                if ((uintptr_t)t46 >= (uintptr_t)(t45)) gm_trap_index((uintptr_t)t46, t45, gm_srcs9q7ghsrcgv5, 362, 61);
                                uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)t44) + (uintptr_t)t46 * 1));
                                gm_l7 = t47;
                                uint8_t t48 = gm_l7;
                                uint8_t t49 = (((uint8_t)65ULL) <= t48);
                                if (t49) {
                                    uint8_t t50 = gm_l7;
                                    uint8_t t51 = (t50 <= ((uint8_t)90ULL));
                                    gm_l15 = t51;
                                } else {
                                    gm_l15 = ((uint8_t)0);
                                }
                                uint8_t t52 = gm_l15;
                                gm_l13 = t52;
                            }
                            uint8_t t53 = gm_l13;
                            if (t53) {
                                gm_l12 = ((uint8_t)1);
                            } else {
                                void * t54 = gm_l2;
                                void * t55 = GM_LD(void *, ((uint8_t *)t54));
                                uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t54) + 8));
                                uint64_t t57 = gm_l5;
                                if ((uintptr_t)t57 >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)t57, t56, gm_srcs9q7ghsrcgv5, 362, 89);
                                uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)t55) + (uintptr_t)t57 * 1));
                                gm_l8 = t58;
                                uint8_t t59 = gm_l8;
                                uint8_t t60 = (((uint8_t)48ULL) <= t59);
                                if (t60) {
                                    uint8_t t61 = gm_l8;
                                    uint8_t t62 = (t61 <= ((uint8_t)57ULL));
                                    gm_l16 = t62;
                                } else {
                                    gm_l16 = ((uint8_t)0);
                                }
                                uint8_t t63 = gm_l16;
                                gm_l12 = t63;
                            }
                            uint8_t t64 = gm_l12;
                            if (t64) {
                                gm_l11 = ((uint8_t)1);
                            } else {
                                void * t65 = gm_l2;
                                void * t66 = GM_LD(void *, ((uint8_t *)t65));
                                uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)t65) + 8));
                                uint64_t t68 = gm_l5;
                                if ((uintptr_t)t68 >= (uintptr_t)(t67)) gm_trap_index((uintptr_t)t68, t67, gm_srcs9q7ghsrcgv5, 362, 117);
                                uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)t66) + (uintptr_t)t68 * 1));
                                uint8_t t70 = ((t69 == ((uint8_t)95ULL)));
                                gm_l11 = t70;
                            }
                            uint8_t t71 = gm_l11;
                            if (!t71) {
                                goto gm_lb5;
                            }
                            {
                                uint64_t t72 = gm_l5;
                                uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                                if (o74) gm_trap("integer overflow in `+`", gm_srcs9q7ghsrcgv5, 362, 139);
                                uint64_t t75 = r73;
                                gm_l5 = t75;
                            }
gm_lb7:;
                        }
                    }
gm_lb5:;
                    void * t76 = gm_l2;
                    void * t77 = GM_LD(void *, ((uint8_t *)t76));
                    uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)t76) + 8));
                    uint64_t t79 = gm_l5;
                    uint64_t t80 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t81 = (uintptr_t)t79;
                    if (t80 > t81 || t81 > (uintptr_t)t78) gm_trap_slice(t80, t81, t78, gm_srcs9q7ghsrcgv5, 363, 13);
                    uint64_t t82 = ((uintptr_t)t79) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t83;
                    GM_ST(void *, (uint8_t *)&t83, (void *)(((uint8_t *)t77 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(t82));
                    gm_l9 = t83;
                    gm_b16_8 t84 = gm_l9;
                    gm_f_member_name_mj2bvs3((void *)&gm_l18, (void *)NULL, cx, NULL, t84);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        {
                            uint8_t t85 = (uint8_t)((((uint8_t *)&gm_l18))[16] != 0);
                            if (t85) {
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
                        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 364, 8);
                    }
gm_lb8:;
                    uint8_t t86 = gm_l17;
                    if (t86) {
                        gm_t4ml5v7ugcjrt t87;
                        memset(&t87, 0, sizeof t87);
                        memmove(gm_out, ((uint8_t *)&t87), 40);
                        goto gm_fdone;
                    }
                    gm_b16_8 t88 = gm_l9;
                    uint64_t t89 = gm_l4;
                    void * t90 = gm_l0;
                    gm_t88rcucg1b7uk t91 = gm_l3;
                    uint8_t t92 = gm_f_is_signed_integer_ige9b74(t90, GM_CONV(gm_csguankko4lmhd, t91));
                    gm_th5c3ge61oapm t93;
                    GM_ST(uint8_t, (uint8_t *)&t93 + 25, 0);
                    GM_ST(uint16_t, (uint8_t *)&t93 + 26, 0);
                    GM_ST(uint32_t, (uint8_t *)&t93 + 28, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t93 + 0), t88);
                    GM_ST(uint64_t, ((uint8_t *)&t93 + 16), t89);
                    GM_ST(uint8_t, ((uint8_t *)&t93 + 24), t92);
                    gm_t4ml5v7ugcjrt t94;
                    GM_ST(uint8_t, (uint8_t *)&t94 + 33, 0);
                    GM_ST(uint16_t, (uint8_t *)&t94 + 34, 0);
                    GM_ST(uint32_t, (uint8_t *)&t94 + 36, 0);
                    GM_ST(gm_th5c3ge61oapm, ((uint8_t *)&t94), t93);
                    ((uint8_t *)&t94)[32] = 1;
                    memmove(gm_out, ((uint8_t *)&t94), 40);
                    goto gm_fdone;
                }
gm_lb4:;
                gm_t4ml5v7ugcjrt t95;
                memset(&t95, 0, sizeof t95);
                memmove(gm_out, ((uint8_t *)&t95), 40);
                goto gm_fdone;
            }
gm_lb3:;
        }
gm_lb2:;
        gm_t4ml5v7ugcjrt t96;
        memset(&t96, 0, sizeof t96);
        memmove(gm_out, ((uint8_t *)&t96), 40);
        goto gm_fdone;
    }
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
__attribute__((aligned(4))) uint8_t gm_f_is_aggregate_buh1uic(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_aggregate_buh1uic F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l3; /* variants */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* p */
    GM_STACK_CHECK(&F, 342u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l8;
    uint8_t gm_l9;
    void * t25 = gm_l0;
    gm_t88rcucg1b7uk t26 = gm_l1;
    gm_f_kind_ghvav22((void *)&gm_l8, t25, GM_CONV(gm_csguankko4lmhd, t26));
    {
        {
            {
                {
                    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)12ULL));
                    if (!t28) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                    uint8_t t30 = (uint8_t)(t29 == ((uint8_t)10ULL));
                    if (!t30) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)14ULL));
                    if (!t32) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                goto gm_lb2;
            }
gm_lb3:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)13ULL));
            if (!t34) {
                goto gm_lb7;
            }
            void * t35 = gm_l0;
            gm_t88rcucg1b7uk t36 = gm_l1;
            gm_t5v8n6qrn4rh4 t37;
            gm_f_repr_53quqki((void *)&t37, (void *)NULL, cx, NULL, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t37) + 0));
            uint8_t t39 = (uint8_t)(!(t38 == ((uint8_t)3ULL)));
            gm_t5v8n6qrn4rh4 t40;
            GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t40 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t40 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)3ULL));
            F.ret = t39;
            goto gm_fret;
        }
gm_lb7:;
        {
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)17ULL));
            if (!t42) {
                goto gm_lb8;
            }
            void * t43 = gm_l0;
            gm_t88rcucg1b7uk t44 = gm_l1;
            void * t45 = gm_f_adt_of_gej99vn(t43, GM_CONV(gm_csguankko4lmhd, t44));
            {
                {
                    uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t45) + 24) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)0ULL));
                    if (!t47) {
                        goto gm_lb10;
                    }
                    memmove(((uint8_t *)&F.l2), ((((uint8_t *)t45) + 24) + 8), 48);
                    {
                        {
                            uint8_t t48 = (uint8_t)(((((uint8_t *)&F.l2) + 40))[4] != 0);
                            if (t48) {
                                goto gm_lb12;
                            }
                            gm_l9 = ((uint8_t)1);
                            goto gm_lb11;
                        }
gm_lb12:;
                        {
                            gm_l9 = ((uint8_t)0);
                            goto gm_lb11;
                        }
gm_lb13:;
                        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 143, 40);
                    }
gm_lb11:;
                    uint8_t t49 = gm_l9;
                    F.ret = t49;
                    goto gm_fret;
                }
gm_lb10:;
                {
                    uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t45) + 24) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)2ULL));
                    if (!t51) {
                        goto gm_lb14;
                    }
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
gm_lb14:;
                {
                    uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t45) + 24) + 0));
                    uint8_t t53 = (uint8_t)(t52 == ((uint8_t)1ULL));
                    if (!t53) {
                        goto gm_lb15;
                    }
                    gm_b16_8 t54 = GM_LD(gm_b16_8, (((((uint8_t *)t45) + 24) + 8) + 8));
                    gm_l3 = t54;
                    gm_b16_8 t55 = gm_l3;
                    gm_l4 = t55;
                    gm_l5 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t56 = gm_l5;
                            uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            uint8_t t58 = (t56 < t57);
                            if (!t58) {
                                goto gm_lb16;
                            }
                            {
                                void * t59 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                uint64_t t61 = gm_l5;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t59) + (uintptr_t)t61 * 72), 72);
                                        {
                                            {
                                                uint8_t t62 = (uint8_t)(((((uint8_t *)&F.l6) + 16))[4] != 0);
                                                if (!t62) {
                                                    goto gm_lb22;
                                                }
                                                gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 16));
                                                gm_l7 = t63;
                                                void * t64 = gm_l0;
                                                gm_t88rcucg1b7uk t65 = gm_l7;
                                                uint64_t t66 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t64, t65);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t67 = (t66 > ((uint64_t)0ULL));
                                                if (t67) {
                                                    F.ret = ((uint8_t)1);
                                                    goto gm_fret;
                                                }
                                                goto gm_lb21;
                                            }
gm_lb22:;
                                        }
gm_lb21:;
                                        goto gm_lb19;
                                    }
gm_lb20:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 146, 21);
                                }
gm_lb19:;
                            }
gm_lb18:;
                            uint64_t t68 = gm_l5;
                            uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                            uint64_t t71 = r69;
                            gm_l5 = t71;
                        }
                    }
gm_lb16:;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
gm_lb15:;
                gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 142, 13);
            }
gm_lb9:;
        }
gm_lb8:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb23:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 137, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_packed_v79gj1r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_packed_v79gj1r F;
    gm_b16_8 gm_l0; /* cty */
    gm_t6j6smj7qoctu gm_l1; /* sp */
    GM_STACK_CHECK(&F, 363u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_ttu09lij36f8c gm_l2;
    gm_tabb710d9mq2e gm_l3;
    gm_tabb710d9mq2e gm_l4;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_s3p0rbs9hc3lg));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(1));
            gm_f_split_once_mqcu1ce((void *)&gm_l2, t4, t5);
            uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l2))[32] != 0);
            if (!t6) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l2), 32);
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 16));
            gm_t6j6smj7qoctu t9;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t7);
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 16), t8);
            gm_f_format_v301q4v((void *)&gm_l3, (void *)NULL, cx, NULL, &t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcs9q7ghsrcgv5, 431, 48);
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            F.ret = t12;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t13 = gm_l0;
    gm_tnpifv20irnph t14;
    GM_ST(gm_b16_8, ((uint8_t *)&t14 + 0), t13);
    gm_f_format_ivs3cj0((void *)&gm_l4, (void *)NULL, cx, NULL, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcs9q7ghsrcgv5, 432, 12);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_pad_juftt9c(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_pad_juftt9c F;
    void * gm_l0; /* body */
    uint64_t gm_l1; /* bytes */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_l3 = ((uint64_t)0ULL);
    uint64_t t12 = gm_l1;
    if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcs9q7ghsrcgv5, 437, 17);
    uint64_t t13 = (uint64_t)(t12 / ((uint64_t)8ULL));
    gm_l2 = t13;
    {
        for (;;) {
            uint64_t t14 = gm_l3;
            uint64_t t15 = gm_l2;
            uint8_t t16 = (t14 < t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                {
                    {
                        void * t17 = gm_l0;
                        gm_b16_8 t18;
                        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sakkgen9m77d6));
                        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(15));
                        gm_tftb1odc739t5 t19 = gm_f_extend_0cs85gs(t17, t18);
                        gm_l6 = t19;
                        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcs9q7ghsrcgv5, 437, 29);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 437, 5);
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
    gm_l5 = ((uint64_t)0ULL);
    uint64_t t25 = gm_l1;
    if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcs9q7ghsrcgv5, 438, 17);
    uint64_t t26 = (uint64_t)(t25 % ((uint64_t)8ULL));
    gm_l4 = t26;
    {
        for (;;) {
            uint64_t t27 = gm_l5;
            uint64_t t28 = gm_l4;
            uint8_t t29 = (t27 < t28);
            if (!t29) {
                goto gm_lb6;
            }
            {
                {
                    {
                        void * t30 = gm_l0;
                        gm_b16_8 t31;
                        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sumv38t63phct));
                        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(13));
                        gm_tftb1odc739t5 t32 = gm_f_extend_0cs85gs(t30, t31);
                        gm_l7 = t32;
                        uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcs9q7ghsrcgv5, 438, 29);
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 438, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t34 = gm_l5;
            uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
            uint64_t t37 = r35;
            gm_l5 = t37;
        }
    }
gm_lb6:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_pointer_view_efkbf4q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_pointer_view_efkbf4q F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    gm_t88rcucg1b7uk gm_l5; /* target */
    GM_STACK_CHECK(&F, 307u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t88rcucg1b7uk gm_l6;
    gm_t53at80pfdus3 gm_l7;
    gm_tabb710d9mq2e gm_l8;
    void * t7 = gm_l0;
    gm_t88rcucg1b7uk t8 = gm_l1;
    gm_f_kind_ghvav22((void *)&gm_l7, t7, GM_CONV(gm_csguankko4lmhd, t8));
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)8ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l7) + 8) + 4));
            gm_l2 = t11;
            gm_t88rcucg1b7uk t12 = gm_l2;
            gm_l6 = t12;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)9ULL));
            if (!t14) {
                goto gm_lb3;
            }
            gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l7) + 8) + 4));
            gm_l3 = t15;
            gm_t88rcucg1b7uk t16 = gm_l3;
            gm_l6 = t16;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)13ULL));
            if (!t18) {
                goto gm_lb4;
            }
            gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
            gm_l4 = t19;
            void * t20 = gm_l0;
            gm_t88rcucg1b7uk t21 = gm_l4;
            gm_b16_8 t22 = gm_f_pointer_view_efkbf4q((void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t22;
            goto gm_fret;
        }
gm_lb4:;
        {
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s44ek1gecbajs));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(6));
            F.ret = t23;
            goto gm_fret;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 194, 18);
    }
gm_lb1:;
    gm_t88rcucg1b7uk t24 = gm_l6;
    gm_l5 = t24;
    void * t25 = gm_l0;
    gm_t88rcucg1b7uk t26 = gm_l5;
    gm_b16_8 t27 = gm_f_target_view_ams6b9s((void *)NULL, cx, NULL, t25, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t28;
    GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t27);
    gm_f_format_lfjb9aq((void *)&gm_l8, (void *)NULL, cx, NULL, t28);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcs9q7ghsrcgv5, 200, 12);
    gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
    gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
    F.ret = t31;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_member_eubf7s6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_push_member_eubf7s6 F;
    void * gm_l0; /* ty */
    void * gm_l1; /* members */
    uint64_t gm_l2; /* offset */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* name */
    gm_t6j6smj7qoctu gm_l5; /* v */
    GM_STACK_CHECK(&F, 411u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_ttu09lij36f8c gm_l6;
    gm_t6j6smj7qoctu gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tftb1odc739t5 gm_l9;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l3;
    gm_f_view_qkqm3in((void *)&gm_l6, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l6))[32] != 0);
    if (t4) {
        memmove(((uint8_t *)&gm_l7), ((uint8_t *)&gm_l6), 32);
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l7), 32);
    void * t5 = gm_l0;
    gm_b16_8 t6 = gm_l4;
    gm_b0_1 t7 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t8 = gm_l1;
    uint64_t t9 = gm_l2;
    void * t10 = gm_l0;
    gm_t88rcucg1b7uk t11 = gm_l3;
    uint64_t t12 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
    gm_b16_8 t14 = gm_l4;
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
    gm_tfeab279c16rr t16;
    GM_ST(gm_b16_8, ((uint8_t *)&t16 + 0), t13);
    GM_ST(gm_b16_8, ((uint8_t *)&t16 + 16), t14);
    GM_ST(gm_b16_8, ((uint8_t *)&t16 + 32), t15);
    gm_f_format_hn5gv7p((void *)&gm_l8, (void *)NULL, cx, NULL, &t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcs9q7ghsrcgv5, 324, 69);
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
    gm_tbccvikg6b96e t20;
    GM_ST(uint64_t, ((uint8_t *)&t20 + 0), t9);
    GM_ST(uint64_t, ((uint8_t *)&t20 + 8), t12);
    GM_ST(gm_b16_8, ((uint8_t *)&t20 + 16), t19);
    gm_tftb1odc739t5 t21 = gm_f_push_m7lva8q(t8, &t20);
    gm_l9 = t21;
    uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcs9q7ghsrcgv5, 324, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_union_m2cqbk2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_b16_8 a3) {
    struct gm_fr_push_union_m2cqbk2 F;
    void * gm_l0; /* ty */
    void * gm_l1; /* members */
    uint64_t gm_l2; /* offset */
    gm_b16_8 gm_l3; /* arms */
    uint64_t gm_l5; /* size */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tcmkompbl09il gm_l8; /* arm */
    gm_t6j6smj7qoctu gm_l9; /* v */
    GM_STACK_CHECK(&F, 654u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_ttu09lij36f8c gm_l10;
    gm_t6j6smj7qoctu gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_trqfvmjtt4sij t7;
    gm_f_list_uvfu1k1((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t7;
    gm_l5 = ((uint64_t)0ULL);
    gm_b16_8 t8 = gm_l3;
    gm_l6 = t8;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l7;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t14 = gm_l7;
                {
                    {
                        memmove(((uint8_t *)&gm_l8), (((uint8_t *)t12) + (uintptr_t)t14 * 24), 24);
                        void * t15 = gm_l0;
                        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l8));
                        gm_f_view_qkqm3in((void *)&gm_l10, (void *)NULL, cx, NULL, t15, t16);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t17 = (uint8_t)((((uint8_t *)&gm_l10))[32] != 0);
                        if (t17) {
                            memmove(((uint8_t *)&gm_l11), ((uint8_t *)&gm_l10), 32);
                        } else {
                            goto gm_lb3;
                        }
                        memmove(((uint8_t *)&gm_l9), ((uint8_t *)&gm_l11), 32);
                        void * t18 = gm_l0;
                        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                        gm_b0_1 t20 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t18, t19);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t21 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 16));
                        gm_tfeab279c16rr t25;
                        GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t22);
                        GM_ST(gm_b16_8, ((uint8_t *)&t25 + 16), t23);
                        GM_ST(gm_b16_8, ((uint8_t *)&t25 + 32), t24);
                        gm_tftb1odc739t5 t26 = gm_f_write_vfiknt5(t21, &t25);
                        gm_l12 = t26;
                        uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcs9q7ghsrcgv5, 334, 9);
                        uint64_t t28 = gm_l5;
                        void * t29 = gm_l0;
                        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l8));
                        uint64_t t31 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t29, t30);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t32 = (t28 > t31 ? t28 : t31);
                        gm_l5 = t32;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 331, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t33 = gm_l7;
            uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
            uint64_t t36 = r34;
            gm_l7 = t36;
        }
    }
gm_lb1:;
    uint64_t t37 = gm_l5;
    uint8_t t38 = (t37 > ((uint64_t)0ULL));
    if (t38) {
        void * t39 = gm_l1;
        uint64_t t40 = gm_l2;
        uint64_t t41 = gm_l5;
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_tnpifv20irnph t43;
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t42);
        gm_f_format_phh9f1s((void *)&gm_l13, (void *)NULL, cx, NULL, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
        if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcs9q7ghsrcgv5, 338, 67);
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
        gm_tbccvikg6b96e t47;
        GM_ST(uint64_t, ((uint8_t *)&t47 + 0), t40);
        GM_ST(uint64_t, ((uint8_t *)&t47 + 8), t41);
        GM_ST(gm_b16_8, ((uint8_t *)&t47 + 16), t46);
        gm_tftb1odc739t5 t48 = gm_f_push_m7lva8q(t39, &t47);
        gm_l14 = t48;
        uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
        if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srcs9q7ghsrcgv5, 338, 9);
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
inline __attribute__((aligned(4))) void gm_f_reserve_40g0de6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_reserve_40g0de6 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* cty */
    GM_STACK_CHECK(&F, 356u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_tabb710d9mq2e gm_l7;
    gm_tftb1odc739t5 gm_l8;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t6 = gm_f_view_number_29hi30b((void *)NULL, cx, NULL, t2, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t6;
    void * t7 = gm_l0;
    gm_t88rcucg1b7uk t8 = gm_l1;
    uint8_t t9 = gm_f_is_aggregate_buh1uic((void *)NULL, cx, NULL, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t9) {
        uint64_t t10 = gm_l2;
        void * t11 = gm_l0;
        gm_t88rcucg1b7uk t12 = gm_l1;
        gm_b16_8 t13 = gm_f_view_kind_pth6nol((void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tf1bjg81u0glv t14;
        GM_ST(uint64_t, ((uint8_t *)&t14 + 0), t10);
        GM_ST(gm_b16_8, ((uint8_t *)&t14 + 8), t13);
        gm_f_format_08mmoe2((void *)&gm_l6, (void *)NULL, cx, NULL, &t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcs9q7ghsrcgv5, 186, 43);
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        gm_l5 = t17;
    } else {
        uint64_t t18 = gm_l2;
        gm_t4ce0jd3tdro4 t19;
        GM_ST(uint64_t, ((uint8_t *)&t19 + 0), t18);
        gm_f_format_8ibn6ei((void *)&gm_l7, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcs9q7ghsrcgv5, 186, 104);
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        gm_l5 = t22;
    }
    gm_b16_8 t23 = gm_l5;
    gm_l3 = t23;
    gm_b16_8 t24 = gm_l3;
    gm_f_scalar_f0e206b((void *)&F.l4, t24);
    void * t25 = gm_l0;
    void * t26 = (void *)(((uint8_t *)t25) + 872);
    uint32_t t27 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    gm_tftb1odc739t5 t28 = gm_f_put_25ngckq(t26, t27, &(*(gm_ttu09lij36f8c *)((uint8_t *)&F.l4)));
    gm_l8 = t28;
    uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcs9q7ghsrcgv5, 188, 5);
    memmove(gm_out, ((uint8_t *)&F.l4), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_scalar_f0e206b(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_ttu09lij36f8c gm_f_scalar_f0e206b_c(gm_b16_8 a0) { gm_ttu09lij36f8c gm_r; gm_f_scalar_f0e206b(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) gm_b0_1 gm_f_static_assert_15uo2jf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_target_view_ams6b9s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_target_view_ams6b9s F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t6j6smj7qoctu gm_l3; /* v */
    gm_t6j6smj7qoctu gm_l4; /* v */
    gm_t6j6smj7qoctu gm_l5; /* v */
    GM_STACK_CHECK(&F, 557u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tu3261i9p5758 gm_l6;
    gm_ttu09lij36f8c gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_ttu09lij36f8c gm_l9;
    {
        {
            void * t12 = gm_l0;
            void * t13 = (void *)(((uint8_t *)t12) + 872);
            uint32_t t14 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
            gm_f_get_v0dhnfj((void *)&gm_l6, t13, t14);
            uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l6))[40] != 0);
            if (!t15) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l6), 40);
            {
                {
                    uint8_t t16 = (uint8_t)((((uint8_t *)&F.l2))[32] != 0);
                    if (!t16) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&gm_l3), ((uint8_t *)&F.l2), 32);
                    {
                        {
                            uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)&gm_l3) + 16) + 8));
                            uint8_t t18 = ((t17 == ((uint64_t)0ULL)));
                            if (!t18) {
                                goto gm_lb6;
                            }
                            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                            gm_b16_8 t20;
                            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s557bv2sfqcom));
                            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(5));
                            uint8_t t21 = gm_f_starts_with_6vvt01e(t19, t20);
                            uint8_t t22 = (uint8_t)!t21;
                            if (!t22) {
                                goto gm_lb6;
                            }
                            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                            F.ret = t23;
                            goto gm_fret;
                        }
gm_lb6:;
                    }
gm_lb5:;
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(4));
            F.ret = t24;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t25 = gm_l0;
    gm_t88rcucg1b7uk t26 = gm_l1;
    uint64_t t27 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t25, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t28 = ((t27 == ((uint64_t)0ULL)));
    if (t28) {
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sts5j6f5pkov0));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(4));
        F.ret = t29;
        goto gm_fret;
    }
    void * t30 = gm_l0;
    gm_t88rcucg1b7uk t31 = gm_l1;
    uint8_t t32 = gm_f_is_aggregate_buh1uic((void *)NULL, cx, NULL, t30, t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t32) {
        void * t33 = gm_l0;
        gm_t88rcucg1b7uk t34 = gm_l1;
        gm_f_reserve_40g0de6((void *)&gm_l7, (void *)NULL, cx, NULL, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l7))[32] != 0);
        if (!t35) gm_trap("unwrap of null", gm_srcs9q7ghsrcgv5, 214, 14);
        memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l7), 32);
        void * t36 = gm_l0;
        void * t37 = (void *)((((uint8_t *)t36) + 872) + 56);
        gm_t88rcucg1b7uk t38 = gm_l1;
        gm_tftb1odc739t5 t39 = gm_f_push_k3l9nic(t37, GM_CONV(gm_csguankko4lmhd, t38));
        gm_l8 = t39;
        uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcs9q7ghsrcgv5, 215, 9);
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
        F.ret = t41;
        goto gm_fret;
    }
    {
        {
            void * t42 = gm_l0;
            gm_t88rcucg1b7uk t43 = gm_l1;
            gm_f_view_qkqm3in((void *)&gm_l9, (void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t44 = (uint8_t)((((uint8_t *)&gm_l9))[32] != 0);
            if (!t44) {
                goto gm_lb8;
            }
            memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l9), 32);
            {
                {
                    uint64_t t45 = GM_LD(uint64_t, ((((uint8_t *)&gm_l5) + 16) + 8));
                    uint8_t t46 = ((t45 == ((uint64_t)0ULL)));
                    if (!t46) {
                        goto gm_lb10;
                    }
                    gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                    gm_b16_8 t48;
                    GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s557bv2sfqcom));
                    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(5));
                    uint8_t t49 = gm_f_starts_with_6vvt01e(t47, t48);
                    uint8_t t50 = (uint8_t)!t49;
                    if (!t50) {
                        goto gm_lb10;
                    }
                    gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                    F.ret = t51;
                    goto gm_fret;
                }
gm_lb10:;
            }
gm_lb9:;
            goto gm_lb7;
        }
gm_lb8:;
    }
gm_lb7:;
    gm_b16_8 t52;
    GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sts5j6f5pkov0));
    GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(4));
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_view_defs_u461okc(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_view_kind_pth6nol(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_view_kind_pth6nol F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 402u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l3;
    uint8_t gm_l4;
    gm_t53at80pfdus3 gm_l5;
    gm_b16_8 gm_l6;
    uint8_t gm_l7;
    gm_t4ml5v7ugcjrt gm_l8;
    void * t14 = gm_l0;
    gm_t88rcucg1b7uk t15 = gm_l1;
    gm_f_kind_ghvav22((void *)&gm_l3, t14, GM_CONV(gm_csguankko4lmhd, t15));
    {
        {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)10ULL));
            if (!t17) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l3) + 8) + 4));
            gm_l2 = t18;
            void * t19 = gm_l0;
            gm_t88rcucg1b7uk t20 = gm_l2;
            gm_f_kind_ghvav22((void *)&gm_l5, t19, GM_CONV(gm_csguankko4lmhd, t20));
            {
                {
                    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t22 = (uint8_t)(t21 == ((uint8_t)0ULL));
                    if (!t22) {
                        goto gm_lb4;
                    }
                    uint16_t t23 = GM_LD(uint16_t, ((((uint8_t *)&gm_l5) + 8) + 2));
                    uint8_t t24 = ((t23 == ((uint16_t)8ULL)));
                    if (!t24) {
                        goto gm_lb4;
                    }
                    gm_l4 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l4 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 164, 16);
            }
gm_lb3:;
            uint8_t t25 = gm_l4;
            if (t25) {
                gm_b16_8 t26;
                GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s3p17h7dbk8us));
                GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(7));
                F.ret = t26;
                goto gm_fret;
            }
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_stntm4cqqrhf7));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(6));
            F.ret = t27;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)13ULL));
            if (!t29) {
                goto gm_lb6;
            }
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_smg3p90in08sb));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(9));
            F.ret = t30;
            goto gm_fret;
        }
gm_lb6:;
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)14ULL));
            if (!t32) {
                goto gm_lb7;
            }
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(gm_si68bogf7eu32));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(7));
            F.ret = t33;
            goto gm_fret;
        }
gm_lb7:;
        {
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)17ULL));
            if (!t35) {
                goto gm_lb8;
            }
            void * t36 = gm_l0;
            gm_t88rcucg1b7uk t37 = gm_l1;
            gm_f_enum_info_61n2nkp((void *)&gm_l8, (void *)NULL, cx, NULL, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    uint8_t t38 = (uint8_t)((((uint8_t *)&gm_l8))[32] != 0);
                    if (t38) {
                        goto gm_lb10;
                    }
                    gm_l7 = ((uint8_t)0);
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    gm_l7 = ((uint8_t)1);
                    goto gm_lb9;
                }
gm_lb11:;
                gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 169, 33);
            }
gm_lb9:;
            uint8_t t39 = gm_l7;
            if (t39) {
                gm_b16_8 t40;
                GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s4v09sn49l08n));
                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(6));
                gm_l6 = t40;
            } else {
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(0));
                gm_l6 = t41;
            }
            gm_b16_8 t42 = gm_l6;
            F.ret = t42;
            goto gm_fret;
        }
gm_lb8:;
        {
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(0));
            F.ret = t43;
            goto gm_fret;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 162, 5);
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
inline __attribute__((aligned(4))) uint64_t gm_f_view_number_29hi30b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_view_number_29hi30b F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* key */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 170u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l3;
    gm_ttsm01t0d8srg gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_b16_8 t8 = gm_l1;
    uint64_t t9 = gm_f_hash64_apkb7bo(t8);
    if (((uint64_t)1000000000000ULL) == 0) gm_trap("division by zero", gm_srcs9q7ghsrcgv5, 177, 10);
    uint64_t t10 = (uint64_t)(t9 % ((uint64_t)1000000000000ULL));
    gm_l2 = t10;
    {
        for (;;) {
            void * t11 = gm_l0;
            void * t12 = (void *)((((uint8_t *)t11) + 872) + 144);
            uint64_t t13 = gm_l2;
            gm_ttsm01t0d8srg t14 = gm_f_get_fq78978(t12, t13);
            gm_l4 = t14;
            {
                {
                    uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l4))[0] != 0);
                    if (t15) {
                        goto gm_lb5;
                    }
                    gm_l3 = ((uint8_t)0);
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    gm_l3 = ((uint8_t)1);
                    goto gm_lb4;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 178, 11);
            }
gm_lb4:;
            uint8_t t16 = gm_l3;
            if (!t16) {
                goto gm_lb1;
            }
            {
                uint64_t t17 = gm_l2;
                uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
                if (o19) gm_trap("integer overflow in `+`", gm_srcs9q7ghsrcgv5, 178, 54);
                uint64_t t20 = r18;
                if (((uint64_t)1000000000000ULL) == 0) gm_trap("division by zero", gm_srcs9q7ghsrcgv5, 178, 53);
                uint64_t t21 = (uint64_t)(t20 % ((uint64_t)1000000000000ULL));
                gm_l2 = t21;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t22 = gm_l0;
    void * t23 = (void *)((((uint8_t *)t22) + 872) + 144);
    uint64_t t24 = gm_l2;
    gm_tftb1odc739t5 t25 = gm_f_put_00pon0c(t23, t24, GM_ZERO(gm_b0_1));
    gm_l5 = t25;
    uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcs9q7ghsrcgv5, 179, 5);
    uint64_t t27 = gm_l2;
    F.ret = t27;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_view_qkqm3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_view_qkqm3in F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* k */
    gm_t88rcucg1b7uk gm_l6; /* p */
    uint64_t gm_l8; /* len */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    gm_t6j6smj7qoctu gm_l10; /* e */
    GM_STACK_CHECK(&F, 1290u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tu3261i9p5758 gm_l13;
    gm_tlcm4lvu3g1a8 gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_ttu09lij36f8c gm_l16;
    gm_t5v8n6qrn4rh4 gm_l17;
    gm_ttu09lij36f8c gm_l18;
    gm_ttu09lij36f8c gm_l19;
    gm_t53at80pfdus3 gm_l20;
    gm_ttu09lij36f8c gm_l21;
    gm_ttu09lij36f8c gm_l22;
    gm_tabb710d9mq2e gm_l23;
    uint8_t gm_l24;
    gm_t4ml5v7ugcjrt gm_l25;
    gm_tftb1odc739t5 gm_l26;
    {
        {
            void * t23 = gm_l0;
            void * t24 = (void *)(((uint8_t *)t23) + 872);
            uint32_t t25 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
            gm_f_get_v0dhnfj((void *)&gm_l13, t24, t25);
            uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l13))[40] != 0);
            if (!t26) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l13), 40);
            void * t27 = gm_l0;
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((((uint8_t *)t27) + 872) + 56));
            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
            gm_l3 = t29;
            gm_l4 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t30 = gm_l4;
                    uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t32 = (t30 < t31);
                    if (!t32) {
                        goto gm_lb3;
                    }
                    {
                        uint64_t t33 = gm_l4;
                        void * t34 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t36 = gm_l4;
                        gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t34) + (uintptr_t)t36 * 4));
                        gm_tlcm4lvu3g1a8 t38;
                        GM_ST(uint32_t, (uint8_t *)&t38 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t38 + 0), t33);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t38 + 8), t37);
                        gm_l14 = t38;
                        {
                            {
                                uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                                gm_l5 = t39;
                                gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l14) + 8));
                                gm_l6 = t40;
                                uint32_t t41 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
                                uint32_t t42 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
                                uint8_t t43 = ((t41 == t42));
                                if (t43) {
                                    void * t44 = gm_l0;
                                    void * t45 = (void *)((((uint8_t *)t44) + 872) + 56);
                                    uint64_t t46 = gm_l5;
                                    gm_t88rcucg1b7uk t47 = GM_CONV(gm_t88rcucg1b7uk, gm_f_remove_6blu1gh(t45, t46));
                                    void * t48 = gm_l0;
                                    gm_t88rcucg1b7uk t49 = gm_l1;
                                    gm_b0_1 t50 = gm_f_define_view_sob497j((void *)NULL, cx, NULL, t48, t49);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb3;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 89, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t51 = gm_l4;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    uint64_t t54 = r52;
                    gm_l4 = t54;
                }
            }
gm_lb3:;
            memmove(gm_out, ((uint8_t *)&F.l2), 40);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t55 = gm_l0;
    gm_t88rcucg1b7uk t56 = gm_l1;
    uint64_t t57 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t55, t56);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t58 = ((t57 == ((uint64_t)0ULL)));
    if (t58) {
        void * t59 = gm_l0;
        void * t60 = (void *)(((uint8_t *)t59) + 872);
        uint32_t t61 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
        gm_ttu09lij36f8c t62;
        memset(&t62, 0, sizeof t62);
        gm_tftb1odc739t5 t63 = gm_f_put_25ngckq(t60, t61, &t62);
        gm_l15 = t63;
        uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
        if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcs9q7ghsrcgv5, 99, 9);
        gm_ttu09lij36f8c t65;
        memset(&t65, 0, sizeof t65);
        memmove(gm_out, ((uint8_t *)&t65), 40);
        goto gm_fdone;
    }
    void * t66 = gm_l0;
    gm_t88rcucg1b7uk t67 = gm_l1;
    gm_f_repr_53quqki((void *)&gm_l17, (void *)NULL, cx, NULL, t66, t67);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l17) + 0));
            uint8_t t69 = (uint8_t)(t68 == ((uint8_t)2ULL));
            if (!t69) {
                goto gm_lb9;
            }
            gm_b16_8 t70;
            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_snokcvhtidt97));
            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(5));
            gm_f_scalar_f0e206b((void *)&gm_l16, t70);
            goto gm_lb8;
        }
gm_lb9:;
        {
            uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l17) + 0));
            uint8_t t72 = (uint8_t)(t71 == ((uint8_t)3ULL));
            if (!t72) {
                goto gm_lb10;
            }
            void * t73 = gm_l0;
            gm_t88rcucg1b7uk t74 = gm_l1;
            gm_b16_8 t75 = gm_f_pointer_view_efkbf4q((void *)NULL, cx, NULL, t73, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_scalar_f0e206b((void *)&gm_l16, t75);
            goto gm_lb8;
        }
gm_lb10:;
        {
            uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l17) + 0));
            uint8_t t77 = (uint8_t)(t76 == ((uint8_t)6ULL));
            if (!t77) {
                goto gm_lb11;
            }
            void * t78 = gm_l0;
            gm_t88rcucg1b7uk t79 = gm_l1;
            uint8_t t80 = gm_f_is_aggregate_buh1uic((void *)NULL, cx, NULL, t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t80) {
                void * t81 = gm_l0;
                gm_t88rcucg1b7uk t82 = gm_l1;
                gm_f_reserve_40g0de6((void *)&F.l7, (void *)NULL, cx, NULL, t81, t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t83 = gm_l0;
                gm_t88rcucg1b7uk t84 = gm_l1;
                gm_b0_1 t85 = gm_f_define_view_sob497j((void *)NULL, cx, NULL, t83, t84);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                memmove(((uint8_t *)&gm_l18), ((uint8_t *)&F.l7), 40);
            } else {
                void * t86 = gm_l0;
                gm_t88rcucg1b7uk t87 = gm_l1;
                gm_f_kind_ghvav22((void *)&gm_l20, t86, GM_CONV(gm_csguankko4lmhd, t87));
                {
                    {
                        uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
                        uint8_t t89 = (uint8_t)(t88 == ((uint8_t)11ULL));
                        if (!t89) {
                            goto gm_lb13;
                        }
                        uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                        gm_l8 = t90;
                        gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l20) + 8) + 8));
                        gm_l9 = t91;
                        {
                            {
                                void * t92 = gm_l0;
                                gm_t88rcucg1b7uk t93 = gm_l9;
                                gm_f_view_qkqm3in((void *)&gm_l22, (void *)NULL, cx, NULL, t92, t93);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t94 = (uint8_t)((((uint8_t *)&gm_l22))[32] != 0);
                                if (!t94) {
                                    goto gm_lb15;
                                }
                                memmove(((uint8_t *)&gm_l10), ((uint8_t *)&gm_l22), 32);
                                gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
                                uint64_t t96 = gm_l8;
                                gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 16));
                                gm_tf1bjg81u0glv t98;
                                GM_ST(uint64_t, ((uint8_t *)&t98 + 0), t96);
                                GM_ST(gm_b16_8, ((uint8_t *)&t98 + 8), t97);
                                gm_f_format_6a91mh8((void *)&gm_l23, (void *)NULL, cx, NULL, &t98);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                                if (!t99) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_srcs9q7ghsrcgv5, 114, 52);
                                gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                                gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
                                gm_t6j6smj7qoctu t102;
                                GM_ST(gm_b16_8, ((uint8_t *)&t102 + 0), t95);
                                GM_ST(gm_b16_8, ((uint8_t *)&t102 + 16), t101);
                                gm_ttu09lij36f8c t103;
                                GM_ST(uint8_t, (uint8_t *)&t103 + 33, 0);
                                GM_ST(uint16_t, (uint8_t *)&t103 + 34, 0);
                                GM_ST(uint32_t, (uint8_t *)&t103 + 36, 0);
                                GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t103), t102);
                                ((uint8_t *)&t103)[32] = 1;
                                gm_l21 = t103;
                                goto gm_lb14;
                            }
gm_lb15:;
                            void * t104 = gm_l0;
                            gm_t88rcucg1b7uk t105 = gm_l1;
                            gm_b16_8 t106 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t104, t105);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_f_scalar_f0e206b((void *)&gm_l21, t106);
                        }
gm_lb14:;
                        memmove(((uint8_t *)&gm_l19), ((uint8_t *)&gm_l21), 40);
                        goto gm_lb12;
                    }
gm_lb13:;
                    {
                        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
                        uint8_t t108 = (uint8_t)(t107 == ((uint8_t)17ULL));
                        if (!t108) {
                            goto gm_lb16;
                        }
                        void * t109 = gm_l0;
                        gm_t88rcucg1b7uk t110 = gm_l1;
                        gm_f_enum_info_61n2nkp((void *)&gm_l25, (void *)NULL, cx, NULL, t109, t110);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        {
                            {
                                uint8_t t111 = (uint8_t)((((uint8_t *)&gm_l25))[32] != 0);
                                if (t111) {
                                    goto gm_lb18;
                                }
                                gm_l24 = ((uint8_t)0);
                                goto gm_lb17;
                            }
gm_lb18:;
                            {
                                gm_l24 = ((uint8_t)1);
                                goto gm_lb17;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 119, 35);
                        }
gm_lb17:;
                        uint8_t t112 = gm_l24;
                        if (!t112) {
                            goto gm_lb16;
                        }
                        void * t113 = gm_l0;
                        gm_t88rcucg1b7uk t114 = gm_l1;
                        gm_f_reserve_40g0de6((void *)&F.l11, (void *)NULL, cx, NULL, t113, t114);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t115 = gm_l0;
                        gm_t88rcucg1b7uk t116 = gm_l1;
                        gm_b0_1 t117 = gm_f_define_view_sob497j((void *)NULL, cx, NULL, t115, t116);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        memmove(((uint8_t *)&gm_l19), ((uint8_t *)&F.l11), 40);
                        goto gm_lb12;
                    }
gm_lb16:;
                    {
                        void * t118 = gm_l0;
                        gm_t88rcucg1b7uk t119 = gm_l1;
                        gm_b16_8 t120 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t118, t119);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_f_scalar_f0e206b((void *)&gm_l19, t120);
                        goto gm_lb12;
                    }
gm_lb20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 111, 17);
                }
gm_lb12:;
                memmove(((uint8_t *)&gm_l18), ((uint8_t *)&gm_l19), 40);
            }
            memmove(((uint8_t *)&gm_l16), ((uint8_t *)&gm_l18), 40);
            goto gm_lb8;
        }
gm_lb11:;
        {
            void * t121 = gm_l0;
            gm_t88rcucg1b7uk t122 = gm_l1;
            gm_b16_8 t123 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t121, t122);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_scalar_f0e206b((void *)&gm_l16, t123);
            goto gm_lb8;
        }
gm_lb21:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 102, 16);
    }
gm_lb8:;
    memmove(((uint8_t *)&F.l12), ((uint8_t *)&gm_l16), 40);
    void * t124 = gm_l0;
    void * t125 = (void *)(((uint8_t *)t124) + 872);
    uint32_t t126 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    gm_tftb1odc739t5 t127 = gm_f_put_25ngckq(t125, t126, &(*(gm_ttu09lij36f8c *)((uint8_t *)&F.l12)));
    gm_l26 = t127;
    uint8_t t128 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
    if (!t128) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcs9q7ghsrcgv5, 131, 5);
    memmove(gm_out, ((uint8_t *)&F.l12), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_views_bc1b3r4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_views_bc1b3r4 F;
    GM_STACK_CHECK(&F, 504u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t4c7ipjj9s3fp t2;
    gm_f_hash_map_9mnq5mu((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t3;
    gm_f_list_nof7vfj((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t4;
    gm_f_list_uvfu1k1((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    gm_f_hash_map_5kr0r9p((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t6;
    gm_f_hash_map_cuh4fs9((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbpdf5ej15iof t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 97, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 98, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 100, 0);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t7 + 0), t2);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t7 + 56), t3);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 96), ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t7 + 104), t4);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t7 + 144), t5);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t7 + 200), t6);
    memmove(gm_out, ((uint8_t *)&t7), 256);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
