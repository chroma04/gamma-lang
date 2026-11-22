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
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint8_t f2; uint8_t pe[3]; } gm_tilqfqdpj446h;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t f6; uint8_t pe[4]; } gm_t0o0svomecjg6;
typedef struct { gm_t9805qtonbsag f0; void * f1; } gm_t12ib7gg43lh0;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(base: ^Expr, index: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_b16_8 f1; } gm_thervm8j23qet;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tilqfqdpj446h) == 12 && _Alignof(gm_tilqfqdpj446h) == 4 && offsetof(gm_tilqfqdpj446h, f0) == 0 && offsetof(gm_tilqfqdpj446h, f1) == 4 && offsetof(gm_tilqfqdpj446h, f2) == 8, "layout of `(dst: u32, f: u32, second: bool)`");
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[12]; unsigned __int128 f1; } gm_tl5jbgvjjt15g;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { uint64_t f0; gm_t9805qtonbsag f1; } gm_tp0pp2cmr6tvd;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; gm_t9805qtonbsag f3; } gm_tp4rm55bg84ue;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; } gm_tpfol4ocdvoij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t p4[4]; gm_b16_8 f4; uint32_t f5; uint8_t p6[4]; gm_b16_8 f6; uint32_t f7; uint8_t pe[4]; } gm_tqh25f25b69k5;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_trt0mvq732stt;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0o0svomecjg6) == 32 && _Alignof(gm_t0o0svomecjg6) == 8 && offsetof(gm_t0o0svomecjg6, f0) == 0 && offsetof(gm_t0o0svomecjg6, f1) == 8 && offsetof(gm_t0o0svomecjg6, f2) == 16 && offsetof(gm_t0o0svomecjg6, f3) == 24 && offsetof(gm_t0o0svomecjg6, f4) == 25 && offsetof(gm_t0o0svomecjg6, f5) == 26 && offsetof(gm_t0o0svomecjg6, f6) == 27, "layout of `Local`");
_Static_assert(sizeof(gm_t12ib7gg43lh0) == 16 && _Alignof(gm_t12ib7gg43lh0) == 8 && offsetof(gm_t12ib7gg43lh0, f0) == 0 && offsetof(gm_t12ib7gg43lh0, f1) == 8, "layout of `(dst: ?u32, expr: ^Expr)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
typedef struct { uint64_t f0; gm_tsoie1ppgdvcg f1; } gm_t5im2v50m3hk6;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_tfbnciphf3261 v1; gm_tl5jbgvjjt15g v2; gm_t88rcucg1b7uk v3; gm_t88rcucg1b7uk v4; } f1; } gm_t7b7esi3n9r90;
typedef struct { gm_tp4rm55bg84ue f0; uint8_t f1; uint8_t pe[3]; } gm_t7isn7qnmqh8i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_te9hrk0u35rev v0; gm_te9hrk0u35rev v1; uint32_t v2; } f1; } gm_t7pnmbsgd9mnq;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
typedef struct { gm_tdgfduqjheqdm f0; uint8_t f1; uint8_t pe[7]; } gm_tdn4erebgflsv;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, bits: u64)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
typedef struct { gm_t9805qtonbsag f0; gm_tvdg94bk7aa22 f1; uint8_t p2[4]; gm_b16_8 f2; } gm_th622ku78unet;
_Static_assert(sizeof(gm_thervm8j23qet) == 24 && _Alignof(gm_thervm8j23qet) == 8 && offsetof(gm_thervm8j23qet, f0) == 0 && offsetof(gm_thervm8j23qet, f1) == 8, "layout of `Slot(string, ())`");
typedef struct { void * f0; gm_trqfvmjtt4sij f1; } gm_tig4i22alq15r;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl5jbgvjjt15g) == 32 && _Alignof(gm_tl5jbgvjjt15g) == 16 && offsetof(gm_tl5jbgvjjt15g, f0) == 0 && offsetof(gm_tl5jbgvjjt15g, f1) == 16, "layout of `(ty: Ty, bits: u128)`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; uint32_t f5; uint8_t p6[4]; gm_trqfvmjtt4sij f6; uint32_t f7; uint8_t p8[4]; gm_trqfvmjtt4sij f8; uint32_t f9; uint8_t pe[4]; } gm_tm81239bir3h6;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tp0pp2cmr6tvd) == 16 && _Alignof(gm_tp0pp2cmr6tvd) == 8 && offsetof(gm_tp0pp2cmr6tvd, f0) == 0 && offsetof(gm_tp0pp2cmr6tvd, f1) == 8, "layout of `(i: usize, x: ?u32)`");
_Static_assert(sizeof(gm_tp4rm55bg84ue) == 20 && _Alignof(gm_tp4rm55bg84ue) == 4 && offsetof(gm_tp4rm55bg84ue, f0) == 0 && offsetof(gm_tp4rm55bg84ue, f1) == 4 && offsetof(gm_tp4rm55bg84ue, f2) == 8 && offsetof(gm_tp4rm55bg84ue, f3) == 12, "layout of `LoopLabels`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tpfol4ocdvoij) == 32 && _Alignof(gm_tpfol4ocdvoij) == 8 && offsetof(gm_tpfol4ocdvoij, f0) == 0 && offsetof(gm_tpfol4ocdvoij, f1) == 16 && offsetof(gm_tpfol4ocdvoij, f2) == 28, "layout of `(name: string, span: Span, ty: Ty)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_tqh25f25b69k5) == 96 && _Alignof(gm_tqh25f25b69k5) == 8 && offsetof(gm_tqh25f25b69k5, f0) == 0 && offsetof(gm_tqh25f25b69k5, f1) == 8 && offsetof(gm_tqh25f25b69k5, f2) == 24 && offsetof(gm_tqh25f25b69k5, f3) == 40 && offsetof(gm_tqh25f25b69k5, f4) == 48 && offsetof(gm_tqh25f25b69k5, f5) == 64 && offsetof(gm_tqh25f25b69k5, f6) == 72 && offsetof(gm_tqh25f25b69k5, f7) == 88, "layout of `Func`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_trt0mvq732stt) == 32 && _Alignof(gm_trt0mvq732stt) == 8 && offsetof(gm_trt0mvq732stt, f0) == 0 && offsetof(gm_trt0mvq732stt, f1) == 8 && offsetof(gm_trt0mvq732stt, f2) == 24, "layout of `(dst: u32, bytes: string, c: bool)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tcc1rq4n1avb2 v1; uint64_t v2; gm_tpfol4ocdvoij v3; uint32_t v4; } f1; } gm_tu78tssgij60g;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
typedef struct { uint32_t f0; gm_ts6oqppjh0ba3 f1; uint8_t p2[10]; gm_t7b7esi3n9r90 f2; gm_t7b7esi3n9r90 f3; } gm_t1ctgp8j1v2as;
typedef struct { gm_t7b7esi3n9r90 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t1s3s9ds8udn4;
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_t7b7esi3n9r90 f0; uint64_t f1; uint8_t pe[8]; } gm_t592joge1j4uv;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
typedef struct { uint32_t f0; gm_tmn7n0n9raou5 f1; uint8_t p2[4]; gm_t7b7esi3n9r90 f2; } gm_t5hdm76ui2uro;
_Static_assert(sizeof(gm_t5im2v50m3hk6) == 48 && _Alignof(gm_t5im2v50m3hk6) == 8 && offsetof(gm_t5im2v50m3hk6, f0) == 0 && offsetof(gm_t5im2v50m3hk6, f1) == 8, "layout of `(i: usize, x: LocalDecl)`");
_Static_assert(sizeof(gm_t7b7esi3n9r90) == 48 && _Alignof(gm_t7b7esi3n9r90) == 16 && offsetof(gm_t7b7esi3n9r90, f0) == 0 && offsetof(gm_t7b7esi3n9r90, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_t7isn7qnmqh8i) == 24 && _Alignof(gm_t7isn7qnmqh8i) == 4 && offsetof(gm_t7isn7qnmqh8i, f0) == 0 && offsetof(gm_t7isn7qnmqh8i, f1) == 20, "layout of `?LoopLabels`");
_Static_assert(sizeof(gm_t7pnmbsgd9mnq) == 16 && _Alignof(gm_t7pnmbsgd9mnq) == 4 && offsetof(gm_t7pnmbsgd9mnq, f0) == 0 && offsetof(gm_t7pnmbsgd9mnq, f1) == 4, "layout of `Exit`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; uint32_t f2; uint8_t p3[12]; gm_t7b7esi3n9r90 f3; } gm_t8rrrcunvsibs;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t419mo45g7j0a v0; gm_t7b7esi3n9r90 v1; } f1; } gm_t9i04n6j2d1gi;
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { uint32_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[11]; gm_t7b7esi3n9r90 f2; } gm_tbto4k5jg9fve;
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; uint32_t f2; uint8_t pe[12]; } gm_tcpisgbff1dqv;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdn4erebgflsv) == 72 && _Alignof(gm_tdn4erebgflsv) == 8 && offsetof(gm_tdn4erebgflsv, f0) == 0 && offsetof(gm_tdn4erebgflsv, f1) == 64, "layout of `?FnDef`");
typedef struct { gm_t7b7esi3n9r90 f0; uint8_t f1; uint8_t p2[3]; uint32_t f2; uint8_t pe[8]; } gm_tejc6vv5sqno9;
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; } gm_tf4lk4crue8vt;
typedef struct { gm_t7b7esi3n9r90 f0; uint8_t f1; uint8_t pe[15]; } gm_tf8kfhcnval78;
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; gm_t7b7esi3n9r90 f2; gm_t7b7esi3n9r90 f3; uint8_t f4; uint8_t p5[7]; uint64_t f5; } gm_tgroihbjsmdvm;
_Static_assert(sizeof(gm_th622ku78unet) == 40 && _Alignof(gm_th622ku78unet) == 8 && offsetof(gm_th622ku78unet, f0) == 0 && offsetof(gm_th622ku78unet, f1) == 8 && offsetof(gm_th622ku78unet, f2) == 24, "layout of `(dst: ?u32, op: Builtin, args: []Value)`");
_Static_assert(sizeof(gm_tig4i22alq15r) == 48 && _Alignof(gm_tig4i22alq15r) == 8 && offsetof(gm_tig4i22alq15r, f0) == 0 && offsetof(gm_tig4i22alq15r, f1) == 8, "layout of `Ctx`");
typedef struct { gm_tu78tssgij60g f0; gm_b16_8 f1; } gm_tlg23kb3oj23s;
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_t7b7esi3n9r90 v1; gm_t419mo45g7j0a v2; } f1; } gm_tllthq21h2f0k;
_Static_assert(sizeof(gm_tm81239bir3h6) == 240 && _Alignof(gm_tm81239bir3h6) == 8 && offsetof(gm_tm81239bir3h6, f0) == 0 && offsetof(gm_tm81239bir3h6, f1) == 8 && offsetof(gm_tm81239bir3h6, f2) == 16 && offsetof(gm_tm81239bir3h6, f3) == 56 && offsetof(gm_tm81239bir3h6, f4) == 96 && offsetof(gm_tm81239bir3h6, f5) == 136 && offsetof(gm_tm81239bir3h6, f6) == 144 && offsetof(gm_tm81239bir3h6, f7) == 184 && offsetof(gm_tm81239bir3h6, f8) == 192 && offsetof(gm_tm81239bir3h6, f9) == 232, "layout of `Lw`");
typedef struct { gm_b16_8 f0; gm_t7b7esi3n9r90 f1; } gm_tmvmieab1acso;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
typedef struct { uint32_t f0; uint8_t f1; uint8_t p2[11]; gm_t7b7esi3n9r90 f2; gm_t7b7esi3n9r90 f3; } gm_tr6ll1dg9imrl;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { gm_t7b7esi3n9r90 f0; gm_t7b7esi3n9r90 f1; } gm_tthvfhsvm2g0s;
_Static_assert(sizeof(gm_tu78tssgij60g) == 40 && _Alignof(gm_tu78tssgij60g) == 8 && offsetof(gm_tu78tssgij60g, f0) == 0 && offsetof(gm_tu78tssgij60g, f1) == 8, "layout of `Base`");
typedef struct { gm_t9805qtonbsag f0; uint8_t p1[8]; gm_tllthq21h2f0k f1; gm_b16_8 f2; } gm_t116nudii5c54;
_Static_assert(sizeof(gm_t1ctgp8j1v2as) == 112 && _Alignof(gm_t1ctgp8j1v2as) == 16 && offsetof(gm_t1ctgp8j1v2as, f0) == 0 && offsetof(gm_t1ctgp8j1v2as, f1) == 4 && offsetof(gm_t1ctgp8j1v2as, f2) == 16 && offsetof(gm_t1ctgp8j1v2as, f3) == 64, "layout of `(dst: u32, op: BinOp, a: Value, b: Value)`");
_Static_assert(sizeof(gm_t1s3s9ds8udn4) == 80 && _Alignof(gm_t1s3s9ds8udn4) == 16 && offsetof(gm_t1s3s9ds8udn4, f0) == 0 && offsetof(gm_t1s3s9ds8udn4, f1) == 48 && offsetof(gm_t1s3s9ds8udn4, f2) == 64, "layout of `(cond: Value, then: []Inst, else_: []Inst)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tlg23kb3oj23s f1; uint8_t f2; uint8_t pe[7]; } gm_t3349g713hmcr;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tlg23kb3oj23s f1; uint32_t f2; uint8_t pe[4]; } gm_t4j77n1ssj077;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t592joge1j4uv) == 64 && _Alignof(gm_t592joge1j4uv) == 16 && offsetof(gm_t592joge1j4uv, f0) == 0 && offsetof(gm_t592joge1j4uv, f1) == 48, "layout of `(index: Value, size: u64)`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
_Static_assert(sizeof(gm_t5hdm76ui2uro) == 64 && _Alignof(gm_t5hdm76ui2uro) == 16 && offsetof(gm_t5hdm76ui2uro, f0) == 0 && offsetof(gm_t5hdm76ui2uro, f1) == 4 && offsetof(gm_t5hdm76ui2uro, f2) == 16, "layout of `(dst: u32, kind: Coercion, v: Value)`");
typedef struct { gm_tlg23kb3oj23s f0; uint8_t p1[8]; gm_t7b7esi3n9r90 f1; } gm_t6fp3q71vjrmk;
typedef struct { gm_t7b7esi3n9r90 f0; gm_tlg23kb3oj23s f1; uint8_t f2; uint8_t pe[7]; } gm_t6j9ipvj0d6k5;
typedef struct { gm_t7pnmbsgd9mnq f0; gm_tf8kfhcnval78 f1; } gm_t7dcjsiqi95h3;
_Static_assert(sizeof(gm_t8rrrcunvsibs) == 128 && _Alignof(gm_t8rrrcunvsibs) == 16 && offsetof(gm_t8rrrcunvsibs, f0) == 0 && offsetof(gm_t8rrrcunvsibs, f1) == 16 && offsetof(gm_t8rrrcunvsibs, f2) == 64 && offsetof(gm_t8rrrcunvsibs, f3) == 80, "layout of `(dst: u32, whole: Value, index: u32, v: Value)`");
_Static_assert(sizeof(gm_t9i04n6j2d1gi) == 64 && _Alignof(gm_t9i04n6j2d1gi) == 16 && offsetof(gm_t9i04n6j2d1gi, f0) == 0 && offsetof(gm_t9i04n6j2d1gi, f1) == 16, "layout of `Sink`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_tbto4k5jg9fve) == 64 && _Alignof(gm_tbto4k5jg9fve) == 16 && offsetof(gm_tbto4k5jg9fve, f0) == 0 && offsetof(gm_tbto4k5jg9fve, f1) == 4 && offsetof(gm_tbto4k5jg9fve, f2) == 16, "layout of `(dst: u32, op: UnOp, a: Value)`");
_Static_assert(sizeof(gm_tcpisgbff1dqv) == 80 && _Alignof(gm_tcpisgbff1dqv) == 16 && offsetof(gm_tcpisgbff1dqv, f0) == 0 && offsetof(gm_tcpisgbff1dqv, f1) == 16 && offsetof(gm_tcpisgbff1dqv, f2) == 64, "layout of `(dst: u32, v: Value, index: u32)`");
_Static_assert(sizeof(gm_tejc6vv5sqno9) == 64 && _Alignof(gm_tejc6vv5sqno9) == 16 && offsetof(gm_tejc6vv5sqno9, f0) == 0 && offsetof(gm_tejc6vv5sqno9, f1) == 48 && offsetof(gm_tejc6vv5sqno9, f2) == 52, "layout of `(cond: Value, negated: bool, label: u32)`");
_Static_assert(sizeof(gm_tf4lk4crue8vt) == 64 && _Alignof(gm_tf4lk4crue8vt) == 16 && offsetof(gm_tf4lk4crue8vt, f0) == 0 && offsetof(gm_tf4lk4crue8vt, f1) == 16, "layout of `(dst: u32, v: Value)`");
_Static_assert(sizeof(gm_tf8kfhcnval78) == 64 && _Alignof(gm_tf8kfhcnval78) == 16 && offsetof(gm_tf8kfhcnval78, f0) == 0 && offsetof(gm_tf8kfhcnval78, f1) == 48, "layout of `?Value`");
typedef struct { gm_tlg23kb3oj23s f0; uint8_t f1; uint8_t pe[7]; } gm_tfonjjajjj3jq;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_tb31k5l794t7p v0; gm_t592joge1j4uv v1; gm_t592joge1j4uv v2; gm_tlcm4lvu3g1a8 v3; } f1; } gm_tfqmtuprvunod;
typedef struct { uint32_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[11]; gm_tf8kfhcnval78 f2; } gm_tg85uffmq8b38;
_Static_assert(sizeof(gm_tgroihbjsmdvm) == 176 && _Alignof(gm_tgroihbjsmdvm) == 16 && offsetof(gm_tgroihbjsmdvm, f0) == 0 && offsetof(gm_tgroihbjsmdvm, f1) == 16 && offsetof(gm_tgroihbjsmdvm, f2) == 64 && offsetof(gm_tgroihbjsmdvm, f3) == 112 && offsetof(gm_tgroihbjsmdvm, f4) == 160 && offsetof(gm_tgroihbjsmdvm, f5) == 168, "layout of `(dst: u32, ptr: Value, lo: Value, hi: Value, inclusive: bool, size: u64)`");
typedef struct { gm_t7b7esi3n9r90 f0; gm_t7b7esi3n9r90 f1; gm_tf8kfhcnval78 f2; uint8_t f3; uint8_t pe[15]; } gm_thvv7nb91atip;
typedef struct { gm_t9i04n6j2d1gi f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_tf8kfhcnval78 f4; } gm_tihh2qes0hpd4;
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_tf4lk4crue8vt v1; gm_tmvmieab1acso v2; } f1; } gm_tkgci6o4gktbf;
_Static_assert(sizeof(gm_tlg23kb3oj23s) == 56 && _Alignof(gm_tlg23kb3oj23s) == 8 && offsetof(gm_tlg23kb3oj23s, f0) == 0 && offsetof(gm_tlg23kb3oj23s, f1) == 40, "layout of `Place`");
_Static_assert(sizeof(gm_tllthq21h2f0k) == 64 && _Alignof(gm_tllthq21h2f0k) == 16 && offsetof(gm_tllthq21h2f0k, f0) == 0 && offsetof(gm_tllthq21h2f0k, f1) == 16, "layout of `Callee`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tlg23kb3oj23s f1; } gm_tlpcrb3vu6o2d;
_Static_assert(sizeof(gm_tmvmieab1acso) == 64 && _Alignof(gm_tmvmieab1acso) == 16 && offsetof(gm_tmvmieab1acso, f0) == 0 && offsetof(gm_tmvmieab1acso, f1) == 16, "layout of `(decls: []u32, v: Value)`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tr6ll1dg9imrl) == 112 && _Alignof(gm_tr6ll1dg9imrl) == 16 && offsetof(gm_tr6ll1dg9imrl, f0) == 0 && offsetof(gm_tr6ll1dg9imrl, f1) == 4 && offsetof(gm_tr6ll1dg9imrl, f2) == 16 && offsetof(gm_tr6ll1dg9imrl, f3) == 64, "layout of `(dst: u32, negated: bool, a: Value, b: Value)`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_tthvfhsvm2g0s) == 96 && _Alignof(gm_tthvfhsvm2g0s) == 16 && offsetof(gm_tthvfhsvm2g0s, f0) == 0 && offsetof(gm_tthvfhsvm2g0s, f1) == 48, "layout of `(index: Value, len: Value)`");
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; uint8_t p3[8]; gm_tf8kfhcnval78 f3; gm_b16_8 f4; } gm_tvdggnhat0k99;
typedef struct { uint32_t f0; uint32_t f1; uint8_t p2[8]; gm_tf8kfhcnval78 f2; } gm_tvv8lnu6a3ppj;
_Static_assert(sizeof(gm_t116nudii5c54) == 96 && _Alignof(gm_t116nudii5c54) == 16 && offsetof(gm_t116nudii5c54, f0) == 0 && offsetof(gm_t116nudii5c54, f1) == 16 && offsetof(gm_t116nudii5c54, f2) == 80, "layout of `(dst: ?u32, callee: Callee, args: []Value)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
typedef struct { uint64_t f0; gm_tfonjjajjj3jq f1; } gm_t30kh7ksmrtsb;
_Static_assert(sizeof(gm_t3349g713hmcr) == 72 && _Alignof(gm_t3349g713hmcr) == 8 && offsetof(gm_t3349g713hmcr, f0) == 0 && offsetof(gm_t3349g713hmcr, f1) == 8 && offsetof(gm_t3349g713hmcr, f2) == 64, "layout of `(dst: u32, place: Place, result: bool)`");
_Static_assert(sizeof(gm_t4j77n1ssj077) == 72 && _Alignof(gm_t4j77n1ssj077) == 8 && offsetof(gm_t4j77n1ssj077, f0) == 0 && offsetof(gm_t4j77n1ssj077, f1) == 8 && offsetof(gm_t4j77n1ssj077, f2) == 64, "layout of `(dst: u32, place: Place, variant: u32)`");
_Static_assert(sizeof(gm_t6fp3q71vjrmk) == 112 && _Alignof(gm_t6fp3q71vjrmk) == 16 && offsetof(gm_t6fp3q71vjrmk, f0) == 0 && offsetof(gm_t6fp3q71vjrmk, f1) == 64, "layout of `(place: Place, v: Value)`");
_Static_assert(sizeof(gm_t6j9ipvj0d6k5) == 112 && _Alignof(gm_t6j9ipvj0d6k5) == 16 && offsetof(gm_t6j9ipvj0d6k5, f0) == 0 && offsetof(gm_t6j9ipvj0d6k5, f1) == 48 && offsetof(gm_t6j9ipvj0d6k5, f2) == 104, "layout of `(present: Value, place: Place, result: bool)`");
_Static_assert(sizeof(gm_t7dcjsiqi95h3) == 80 && _Alignof(gm_t7dcjsiqi95h3) == 16 && offsetof(gm_t7dcjsiqi95h3, f0) == 0 && offsetof(gm_t7dcjsiqi95h3, f1) == 16, "layout of `(kind: Exit, v: ?Value)`");
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tb38v5s9j1r32 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc9qv69frb08e;
_Static_assert(sizeof(gm_tfonjjajjj3jq) == 64 && _Alignof(gm_tfonjjajjj3jq) == 8 && offsetof(gm_tfonjjajjj3jq, f0) == 0 && offsetof(gm_tfonjjajjj3jq, f1) == 56, "layout of `?Place`");
_Static_assert(sizeof(gm_tfqmtuprvunod) == 80 && _Alignof(gm_tfqmtuprvunod) == 16 && offsetof(gm_tfqmtuprvunod, f0) == 0 && offsetof(gm_tfqmtuprvunod, f1) == 16, "layout of `Proj`");
_Static_assert(sizeof(gm_tg85uffmq8b38) == 80 && _Alignof(gm_tg85uffmq8b38) == 16 && offsetof(gm_tg85uffmq8b38, f0) == 0 && offsetof(gm_tg85uffmq8b38, f1) == 4 && offsetof(gm_tg85uffmq8b38, f2) == 16, "layout of `(dst: u32, kind: Wrap, v: ?Value)`");
_Static_assert(sizeof(gm_thvv7nb91atip) == 176 && _Alignof(gm_thvv7nb91atip) == 16 && offsetof(gm_thvv7nb91atip, f0) == 0 && offsetof(gm_thvv7nb91atip, f1) == 48 && offsetof(gm_thvv7nb91atip, f2) == 96 && offsetof(gm_thvv7nb91atip, f3) == 160, "layout of `(lo: Value, hi: Value, len: ?Value, inclusive: bool)`");
_Static_assert(sizeof(gm_tihh2qes0hpd4) == 176 && _Alignof(gm_tihh2qes0hpd4) == 16 && offsetof(gm_tihh2qes0hpd4, f0) == 0 && offsetof(gm_tihh2qes0hpd4, f1) == 64 && offsetof(gm_tihh2qes0hpd4, f2) == 80 && offsetof(gm_tihh2qes0hpd4, f3) == 96 && offsetof(gm_tihh2qes0hpd4, f4) == 112, "layout of `(sink: Sink, pieces: []const string, specs: []const FmtSpec, args: []Value, table: ?Value)`");
_Static_assert(sizeof(gm_tkgci6o4gktbf) == 80 && _Alignof(gm_tkgci6o4gktbf) == 16 && offsetof(gm_tkgci6o4gktbf, f0) == 0 && offsetof(gm_tkgci6o4gktbf, f1) == 16, "layout of `Handler`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_tthvfhsvm2g0s v0; gm_thvv7nb91atip v1; gm_t6j9ipvj0d6k5 v2; } f1; } gm_tkj0hg83g38nr;
_Static_assert(sizeof(gm_tlpcrb3vu6o2d) == 64 && _Alignof(gm_tlpcrb3vu6o2d) == 8 && offsetof(gm_tlpcrb3vu6o2d, f0) == 0 && offsetof(gm_tlpcrb3vu6o2d, f1) == 8, "layout of `(dst: u32, place: Place)`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tvdggnhat0k99) == 112 && _Alignof(gm_tvdggnhat0k99) == 16 && offsetof(gm_tvdggnhat0k99, f0) == 0 && offsetof(gm_tvdggnhat0k99, f1) == 4 && offsetof(gm_tvdggnhat0k99, f2) == 8 && offsetof(gm_tvdggnhat0k99, f3) == 32 && offsetof(gm_tvdggnhat0k99, f4) == 96, "layout of `(label: u32, tir_label: u32, install: []Inst, on: ?Value, body: []Inst)`");
_Static_assert(sizeof(gm_tvv8lnu6a3ppj) == 80 && _Alignof(gm_tvv8lnu6a3ppj) == 16 && offsetof(gm_tvv8lnu6a3ppj, f0) == 0 && offsetof(gm_tvv8lnu6a3ppj, f1) == 4 && offsetof(gm_tvv8lnu6a3ppj, f2) == 16, "layout of `(dst: u32, variant: u32, payload: ?Value)`");
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t30kh7ksmrtsb) == 72 && _Alignof(gm_t30kh7ksmrtsb) == 8 && offsetof(gm_t30kh7ksmrtsb, f0) == 0 && offsetof(gm_t30kh7ksmrtsb, f1) == 8, "layout of `(i: usize, x: ?Place)`");
_Static_assert(sizeof(gm_tc9qv69frb08e) == 136 && _Alignof(gm_tc9qv69frb08e) == 8 && offsetof(gm_tc9qv69frb08e, f0) == 0 && offsetof(gm_tc9qv69frb08e, f1) == 8 && offsetof(gm_tc9qv69frb08e, f2) == 120, "layout of `Pat`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_tf4lk4crue8vt v0; gm_tlpcrb3vu6o2d v1; gm_tlpcrb3vu6o2d v2; gm_t1ctgp8j1v2as v3; gm_tbto4k5jg9fve v4; gm_tr6ll1dg9imrl v5; gm_t5hdm76ui2uro v6; gm_th622ku78unet v7; gm_t116nudii5c54 v8; gm_tiilmfd16v3fc v9; gm_trt0mvq732stt v10; gm_tf4lk4crue8vt v11; gm_tgroihbjsmdvm v12; gm_tihh2qes0hpd4 v13; gm_tvv8lnu6a3ppj v14; gm_tg85uffmq8b38 v15; gm_tilqfqdpj446h v16; gm_tcc1rq4n1avb2 v17; gm_tcc1rq4n1avb2 v18; gm_ttggq7ng4kp3p v19; gm_tr6ll1dg9imrl v20; gm_tcpisgbff1dqv v21; gm_t8rrrcunvsibs v22; gm_t3349g713hmcr v23; gm_t4j77n1ssj077 v24; gm_t6fp3q71vjrmk v25; gm_tkj0hg83g38nr v26; gm_tiilmfd16v3fc v27; gm_tiilmfd16v3fc v28; gm_t1s3s9ds8udn4 v29; uint32_t v30; gm_tejc6vv5sqno9 v31; gm_tf8kfhcnval78 v32; gm_thervm8j23qet v33; gm_t7b7esi3n9r90 v34; gm_tf8kfhcnval78 v35; gm_b16_8 v36; gm_tvdggnhat0k99 v37; gm_tkgci6o4gktbf v38; gm_t7dcjsiqi95h3 v39; gm_tthvfhsvm2g0s v40; gm_t7b7esi3n9r90 v41; gm_t12ib7gg43lh0 v42; } f1; } gm_th7badf4b6ee3;
_Static_assert(sizeof(gm_tkj0hg83g38nr) == 192 && _Alignof(gm_tkj0hg83g38nr) == 16 && offsetof(gm_tkj0hg83g38nr, f0) == 0 && offsetof(gm_tkj0hg83g38nr, f1) == 16, "layout of `Check`");
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_th7badf4b6ee3) == 208 && _Alignof(gm_th7badf4b6ee3) == 16 && offsetof(gm_th7badf4b6ee3, f0) == 0 && offsetof(gm_th7badf4b6ee3, f1) == 16, "layout of `Op`");
typedef struct { gm_th7badf4b6ee3 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tqp4hhm6timm2;
_Static_assert(sizeof(gm_tqp4hhm6timm2) == 224 && _Alignof(gm_tqp4hhm6timm2) == 16 && offsetof(gm_tqp4hhm6timm2, f0) == 0 && offsetof(gm_tqp4hhm6timm2, f1) == 208, "layout of `Inst`");
struct gm_fr_assert__nol4kp3 {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    gm_b16_8 l2; /* args */
    gm_b0_1 ret;
};
struct gm_fr_bits_field_cdqu5on {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_block_iujbbjb {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* b */
    gm_t9805qtonbsag l9; /* res */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_check_bounds_6f9s0q1 {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* index */
    gm_t7b7esi3n9r90 l2; /* len */
    gm_b0_1 ret;
};
struct gm_fr_conds_sqc7llk {
    gm_frame h;
    gm_t7b7esi3n9r90 l7; /* v */
    gm_t7b7esi3n9r90 l10; /* v */
    gm_tlg23kb3oj23s l12; /* p */
    gm_b0_1 ret;
};
struct gm_fr_context_l4fgtfp {
    gm_frame h;
    gm_tig4i22alq15r l1; /* cx */
    gm_tig4i22alq15r ret;
};
struct gm_fr_def_of_m512sbu {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_diverges_p3okmol {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_else_expr_lrum9ir {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    gm_t3dcoc7077t1q l2; /* x */
    void * l3; /* s */
    gm_t88rcucg1b7uk l5; /* inner */
    gm_tlg23kb3oj23s l6; /* p */
    gm_t9805qtonbsag l7; /* res */
    uint64_t l9; /* off */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_emit_q94msgt {
    gm_frame h;
    gm_th7badf4b6ee3 l1; /* op */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_3v2gjd4_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_4g9am13 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6h1imen {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d1l4euh {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dheabhi {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fooqdot {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fv6i5s8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hh33a3d {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_i0ticdn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uvn29bg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_escape_avsr9t6 {
    gm_frame h;
    void * l0; /* lw */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_escape_pattern_6lpj7tc {
    gm_frame h;
    gm_tlg23kb3oj23s l2; /* p */
    gm_t7b7esi3n9r90 l5; /* v */
    gm_trqfvmjtt4sij l8; /* arms */
    gm_t7b7esi3n9r90 l11; /* matched */
    gm_b0_1 ret;
};
struct gm_fr_escape_stmt_g02tr5c {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* stmts */
    gm_b0_1 ret;
};
struct gm_fr_expr_tp86bm9 {
    gm_frame h;
    gm_tbci5ospk5nnn l3; /* i */
    gm_t7b7esi3n9r90 l9; /* v */
    gm_tlg23kb3oj23s l15; /* p */
    gm_t7b7esi3n9r90 l21; /* v */
    gm_t7b7esi3n9r90 l27; /* a */
    gm_t7b7esi3n9r90 l28; /* b */
    gm_t7b7esi3n9r90 l32; /* a */
    gm_t7b7esi3n9r90 l33; /* b */
    gm_t7b7esi3n9r90 l35; /* a */
    gm_t710lsrq1m8gk l36; /* w */
    gm_t9i04n6j2d1gi l41; /* sink */
    gm_tf8kfhcnval78 l42; /* table */
    gm_t7b7esi3n9r90 l47; /* w */
    gm_t7b7esi3n9r90 l51; /* whole */
    gm_tlg23kb3oj23s l53; /* p */
    gm_tlg23kb3oj23s l55; /* p */
    gm_tlg23kb3oj23s l57; /* p */
    gm_tlg23kb3oj23s l59; /* p */
    gm_t7b7esi3n9r90 l69; /* a */
    gm_t7b7esi3n9r90 l74; /* a */
    gm_t7b7esi3n9r90 l75; /* b */
    gm_t7b7esi3n9r90 l80; /* a */
    gm_t7b7esi3n9r90 l81; /* b */
    gm_t7b7esi3n9r90 l88; /* v */
    gm_tsth9qu1dv0g3 l94; /* callee */
    gm_tllthq21h2f0k l100; /* c */
    gm_tf8kfhcnval78 l108; /* payload */
    gm_t7b7esi3n9r90 l112; /* v */
    gm_t710lsrq1m8gk l116; /* x */
    gm_tf8kfhcnval78 l118; /* v */
    gm_t7b7esi3n9r90 l120; /* val */
    gm_t7b7esi3n9r90 l124; /* val */
    gm_t7b7esi3n9r90 l130; /* v */
    gm_tf8kfhcnval78 l131; /* val */
    gm_t7b7esi3n9r90 l133; /* v */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_exprs_r7m130e {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_extend_vbdqifj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_float_value_o4b87lc {
    gm_frame h;
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_fn_1fu50sd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_24nvriq {
    gm_frame h;
    gm_t7b7esi3n9r90 l0; /* c */
    gm_b0_1 ret;
};
struct gm_fr_fn_36v8itm {
    gm_frame h;
    gm_t7b7esi3n9r90 l0; /* w */
    gm_b0_1 ret;
};
struct gm_fr_fn_47srmke {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_48tkiti {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_49psp5f {
    gm_frame h;
    gm_tf8kfhcnval78 l0; /* msg */
    gm_b0_1 ret;
};
struct gm_fr_fn_506drml {
    gm_frame h;
    gm_tbbqefuhv2r8h l2; /* arm */
    uint32_t l3; /* fail */
    gm_b0_1 ret;
};
struct gm_fr_fn_67nerak {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7h6ducr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cccid3f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cfv665f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cquk74j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_d405sd4 {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* v */
    gm_t7b7esi3n9r90 l2; /* v */
    gm_b0_1 ret;
};
struct gm_fr_fn_dduu5o8 {
    gm_frame h;
    void * l2; /* q */
    uint32_t l3; /* next */
    gm_b0_1 ret;
};
struct gm_fr_fn_drdvs2i {
    gm_frame h;
    gm_t7b7esi3n9r90 l0; /* b */
    gm_b0_1 ret;
};
struct gm_fr_fn_k0ofpte {
    gm_frame h;
    gm_t7b7esi3n9r90 l0; /* w */
    gm_t7b7esi3n9r90 l3; /* v */
    gm_b0_1 ret;
};
struct gm_fr_fn_k3pfo3n {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* v */
    gm_b0_1 ret;
};
struct gm_fr_fn_lldup98 {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* v */
    gm_b0_1 ret;
};
struct gm_fr_fn_m7prcu1 {
    gm_frame h;
    gm_tlg23kb3oj23s l1; /* at */
    gm_t7b7esi3n9r90 l2; /* v */
    gm_b0_1 ret;
};
struct gm_fr_fn_mh72upo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mo8l3p0 {
    gm_frame h;
    gm_t7b7esi3n9r90 l5; /* v */
    gm_t7b7esi3n9r90 l10; /* v */
    gm_b0_1 ret;
};
struct gm_fr_fn_roenhir {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_s5r3poa {
    gm_frame h;
    gm_t7b7esi3n9r90 l2; /* v */
    gm_b0_1 ret;
};
struct gm_fr_fn_t2qurbh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_u0v0t7q {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_v8tau4j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vkfbupa {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_get_d0bmskj {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    gm_tvdg94bk7aa22 l2; /* op */
    void * l4; /* s */
    gm_t88rcucg1b7uk l6; /* inner */
    gm_t88rcucg1b7uk l7; /* elem */
    uint64_t l9; /* esz */
    gm_tlg23kb3oj23s l11; /* seq */
    gm_t9805qtonbsag l12; /* ptr */
    gm_t7b7esi3n9r90 l13; /* len */
    gm_tlg23kb3oj23s l17; /* p */
    gm_t7b7esi3n9r90 l19; /* i */
    uint32_t l21; /* res */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_if_expr_bsdi44h {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    gm_trl2khmuphncd l2; /* x */
    gm_t9805qtonbsag l3; /* res */
    gm_t7b7esi3n9r90 l5; /* cv */
    uint32_t l8; /* end */
    uint32_t l9; /* els */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_in_temp_t7gs0ds {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* v */
    uint32_t ret;
};
struct gm_fr_index_place_3gfu5dn {
    gm_frame h;
    gm_tlg23kb3oj23s l9; /* p */
    gm_t7b7esi3n9r90 l10; /* i */
    gm_tlg23kb3oj23s l13; /* p */
    gm_t7b7esi3n9r90 l14; /* ptr */
    gm_t7b7esi3n9r90 l15; /* len */
    gm_t7b7esi3n9r90 l16; /* i */
    gm_t7b7esi3n9r90 l18; /* ptr */
    gm_t7b7esi3n9r90 l19; /* i */
    gm_t7b7esi3n9r90 l22; /* ptr */
    gm_t7b7esi3n9r90 l23; /* i */
    gm_tfonjjajjj3jq ret;
};
struct gm_fr_int_bits_6bio9a8 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_int_value_ttu5547 {
    gm_frame h;
    gm_tbci5ospk5nnn l2; /* i */
    gm_tf8kfhcnval78 ret;
};
struct gm_fr_is_bits_pbijkuf {
    gm_frame h;
    gm_tpautr4br7h3l l3; /* def */
    uint8_t ret;
};
struct gm_fr_is_place_expr_6llket3 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_label_3373lil {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_leave_ehrof6p {
    gm_frame h;
    gm_t7b7esi3n9r90 l0; /* v */
    gm_b0_1 ret;
};
struct gm_fr_list_0202sdh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0ngbo25 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_58nr42v_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_a3ut5mo {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_gaq8h4q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_hft9rao {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_mgqfh8v {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rb5mlsd {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s1kpkpr {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_urufktd {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_load_go4fgkp {
    gm_frame h;
    gm_tlg23kb3oj23s l1; /* p */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_local_place_qnf3mb2 {
    gm_frame h;
    gm_tlg23kb3oj23s ret;
};
struct gm_fr_local_target_vvq8lnf {
    gm_frame h;
    gm_t7isn7qnmqh8i ret;
};
struct gm_fr_logic_9o6bp90 {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    uint8_t l2; /* is_and */
    void * l4; /* r */
    uint32_t l5; /* res */
    gm_t7b7esi3n9r90 l6; /* a */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_loop_expr_k6v92f2 {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    gm_t710lsrq1m8gk l2; /* x */
    uint32_t l4; /* brk */
    uint32_t l5; /* top */
    uint32_t l6; /* next_ */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_loop_labels_uvqimfl {
    gm_frame h;
    gm_t7isn7qnmqh8i ret;
};
struct gm_fr_lower_rh7s5j6 {
    gm_frame h;
    gm_tdgfduqjheqdm l3; /* def */
    gm_tm81239bir3h6 l4; /* lw */
    gm_tsoie1ppgdvcg l8; /* l */
    gm_t7b7esi3n9r90 l12; /* v */
    gm_tqh25f25b69k5 l13; /* func */
    gm_tqh25f25b69k5 ret;
};
struct gm_fr_mark_addressed_t8qojin {
    gm_frame h;
    gm_tfqmtuprvunod l5; /* x */
    gm_b0_1 ret;
};
struct gm_fr_mark_captured_lpp77bm {
    gm_frame h;
    uint32_t l2; /* depth */
    gm_b16_8 l4; /* locals */
    gm_b0_1 ret;
};
struct gm_fr_mark_root_b973vvi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_match_expr_rfas5u2 {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    gm_b16_8 l3; /* arms */
    gm_t9805qtonbsag l4; /* res */
    gm_tlg23kb3oj23s l5; /* p */
    uint32_t l6; /* end */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_t2b6tm8c0vgo1 l1; /* kind */
    void * ret;
};
struct gm_fr_mk_pat_hr2l6c4 {
    gm_frame h;
    gm_tb38v5s9j1r32 l1; /* kind */
    void * ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_one_n667r26 {
    gm_frame h;
    gm_tfqmtuprvunod l0; /* x */
    gm_trqfvmjtt4sij l1; /* l */
    gm_b16_8 ret;
};
struct gm_fr_operand_place_cdle4ls {
    gm_frame h;
    gm_t7b7esi3n9r90 l2; /* v */
    gm_tfonjjajjj3jq ret;
};
struct gm_fr_pattern_od1h9r0 {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* pat */
    gm_tlg23kb3oj23s l2; /* p */
    uint32_t l3; /* fail */
    gm_t7b7esi3n9r90 l7; /* v */
    gm_t7b7esi3n9r90 l9; /* v */
    gm_t7b7esi3n9r90 l10; /* k */
    gm_tbci5ospk5nnn l12; /* lo */
    gm_tbci5ospk5nnn l13; /* hi */
    gm_t7b7esi3n9r90 l15; /* v */
    gm_t7b7esi3n9r90 l16; /* a */
    gm_t7b7esi3n9r90 l17; /* b */
    gm_t7b7esi3n9r90 l31; /* whole */
    gm_b16_8 l42; /* alts */
    uint32_t l43; /* ok */
    gm_b0_1 ret;
};
struct gm_fr_payload_off_o6q9s7n {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_payload_ty_gn5u4tt {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_place_oohr45n {
    gm_frame h;
    gm_t7b7esi3n9r90 l10; /* p */
    gm_tlg23kb3oj23s l13; /* p */
    gm_tfonjjajjj3jq ret;
};
struct gm_fr_project_6hpd48e {
    gm_frame h;
    gm_tlg23kb3oj23s l0; /* p */
    gm_tfqmtuprvunod l1; /* x */
    gm_trqfvmjtt4sij l2; /* proj */
    gm_tlg23kb3oj23s ret;
};
struct gm_fr_ptr_size_lig81gu {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_push_0pgacsj {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_2v345ri {
    gm_frame h;
    gm_tfonjjajjj3jq l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_e2qt8as {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kki5o6e {
    gm_frame h;
    gm_tfqmtuprvunod l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pf02nv6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ql89qc9 {
    gm_frame h;
    gm_tqp4hhm6timm2 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_r4haklv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u46i39f {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u8i76or {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_result_8csahoh {
    gm_frame h;
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_region_q7ivn7p {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* saved */
    gm_b16_8 ret;
};
struct gm_fr_regrow_0s9hfc3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5719f1b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5hl8984 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5l0p1mi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ddqa9t6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h9tbucc_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_q7kpg21 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_s8usk1j {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_tsagih4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_uue84k4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_result_local_pp1s755 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_result_of_9m0s9vd {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_slice_dup5j25 {
    gm_frame h;
    gm_t7b7esi3n9r90 l7; /* ptr */
    gm_tf8kfhcnval78 l8; /* len */
    gm_tlg23kb3oj23s l10; /* p */
    gm_tlg23kb3oj23s l13; /* p */
    gm_t7b7esi3n9r90 l16; /* lo */
    gm_t7b7esi3n9r90 l18; /* hi */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_stmt_k90qtjr {
    gm_frame h;
    void * l0; /* lw */
    gm_t7b7esi3n9r90 l5; /* v */
    gm_tlg23kb3oj23s l8; /* p */
    gm_t7b7esi3n9r90 l16; /* v */
    gm_trqfvmjtt4sij l19; /* chain */
    gm_tlg23kb3oj23s l22; /* bp */
    gm_t7b7esi3n9r90 l23; /* v */
    gm_trqfvmjtt4sij l24; /* wholes */
    gm_t7b7esi3n9r90 l28; /* nv */
    gm_tlg23kb3oj23s l35; /* p */
    gm_t7b7esi3n9r90 l36; /* v */
    gm_trqfvmjtt4sij l39; /* ps */
    gm_t7b7esi3n9r90 l44; /* v */
    gm_tfonjjajjj3jq l51; /* x */
    gm_tlg23kb3oj23s l52; /* q */
    gm_t7b7esi3n9r90 l53; /* f */
    void * l56; /* body */
    gm_b0_1 ret;
};
struct gm_fr_stmts_ctl06qt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_store_result_fiq5f1j {
    gm_frame h;
    gm_t7b7esi3n9r90 l3; /* v */
    gm_b0_1 ret;
};
struct gm_fr_synthetic_t3b2kd0 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_temp_lneuo24 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_try_expr_2gjs5f7 {
    gm_frame h;
    void * l0; /* lw */
    void * l1; /* e */
    gm_t083kd7b1kkag l2; /* x */
    void * l3; /* s */
    gm_t88rcucg1b7uk l5; /* ret */
    uint8_t l6; /* is_result */
    gm_tlg23kb3oj23s l7; /* p */
    uint64_t l9; /* off */
    gm_closure l10; /* leave */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ggopu3h {
    gm_frame h;
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_visit_ajtfk2n {
    gm_frame h;
    gm_closure l1; /* f */
    gm_b0_1 ret;
};
struct gm_fr_with_expr_g16bcnu {
    gm_frame h;
    void * l0; /* lw */
    gm_t710lsrq1m8gk l2; /* w */
    void * l3; /* s */
    gm_tf8kfhcnval78 l4; /* on */
    gm_t9805qtonbsag l6; /* res */
    gm_t7b7esi3n9r90 ret;
};
struct gm_fr_word_1sfojus {
    gm_frame h;
    gm_tf8kfhcnval78 ret;
};
struct gm_fr_zero_sln4lkh {
    gm_frame h;
    gm_t7b7esi3n9r90 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_ancestor_o28bfm6(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_assert__nol4kp3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_bits_field_cdqu5on(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_block_iujbbjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_bounds_6f9s0q1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_conds_sqc7llk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_context_l4fgtfp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_diverges_p3okmol(void * a0, void * a1);
__attribute__((aligned(4))) uint32_t gm_f_elide_checks_s9bgk65(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_else_expr_lrum9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_q94msgt(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4g9am13(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6h1imen(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d1l4euh(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fooqdot(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hh33a3d(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_i0ticdn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uvn29bg(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) void gm_f_escape_avsr9t6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_pattern_6lpj7tc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint32_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_stmt_g02tr5c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_expr_tp86bm9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_exprs_r7m130e(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vbdqifj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint16_t gm_f_f16_bits_2us9irg(double a0);
__attribute__((aligned(4))) void gm_f_float_value_o4b87lc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, double a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1fu50sd(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_24nvriq(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_36v8itm(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_47srmke(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_48tkiti(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_49psp5f(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_506drml(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_67nerak(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7h6ducr(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_cccid3f(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_cfv665f(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_cquk74j(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_d405sd4(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_dduu5o8(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_drdvs2i(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_k0ofpte(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_k3pfo3n(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_lldup98(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_m7prcu1(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_mh72upo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_mo8l3p0(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_roenhir(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_s5r3poa(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_t2qurbh(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_u0v0t7q(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_v8tau4j(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_vkfbupa(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void gm_f_get_d0bmskj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_get_mk00hfd(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_if_expr_bsdi44h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
__attribute__((aligned(4))) uint32_t gm_f_in_temp_t7gs0ds(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_41b084m(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_index_place_3gfu5dn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, uint8_t a4);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_int_bits_6bio9a8(const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_int_bits_6bio9a8_c(gm_tbci5ospk5nnn a0);
__attribute__((aligned(4))) void gm_f_int_value_ttu5547(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_bits_pbijkuf(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_place_expr_6llket3(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint32_t gm_f_label_3373lil(void * a0);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_leave_ehrof6p(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0ngbo25(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_a3ut5mo(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_gaq8h4q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_hft9rao(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_mgqfh8v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rb5mlsd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_urufktd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_go4fgkp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_local_place_qnf3mb2(void *gm_out, uint32_t a0);
__attribute__((aligned(4))) gm_tlg23kb3oj23s gm_f_local_place_qnf3mb2_c(uint32_t a0);
inline __attribute__((aligned(4))) void gm_f_local_target_vvq8lnf(void *gm_out, void * a0, gm_te9hrk0u35rev a1);
__attribute__((aligned(4))) gm_t7isn7qnmqh8i gm_f_local_target_vvq8lnf_c(void * a0, gm_te9hrk0u35rev a1);
__attribute__((aligned(4))) void gm_f_logic_9o6bp90(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_loop_expr_k6v92f2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_loop_labels_uvqimfl(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t7isn7qnmqh8i gm_f_loop_labels_uvqimfl_c(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_lower_rh7s5j6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_mark_addressed_t8qojin(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_mark_captured_lpp77bm(void * a0, uint32_t a1, uint32_t a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_mark_root_b973vvi(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_match_expr_rfas5u2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_n667r26(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_operand_place_cdle4ls(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_pattern_od1h9r0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint32_t a3);
__attribute__((aligned(4))) uint64_t gm_f_payload_off_o6q9s7n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_payload_ty_gn5u4tt(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_place_oohr45n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_project_6hpd48e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_size_lig81gu(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2v345ri(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_e2qt8as(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kki5o6e(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ql89qc9(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r4haklv(void * a0, gm_t9805qtonbsag a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u46i39f(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u8i76or(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_read_result_8csahoh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9805qtonbsag a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_region_q7ivn7p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0s9hfc3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5hl8984(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5l0p1mi(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q7kpg21(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s8usk1j(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_tsagih4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_uue84k4(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_result_local_pp1s755(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_result_of_9m0s9vd(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_root_local_40frg7f(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_slice_dup5j25(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_stmt_k90qtjr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_stmts_ctl06qt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_store_result_fiq5f1j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9805qtonbsag a1, void * a2);
__attribute__((aligned(4))) uint32_t gm_f_synthetic_t3b2kd0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_lneuo24(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_try_expr_2gjs5f7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_unit_ggopu3h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_visit_ajtfk2n(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_with_expr_g16bcnu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_word_1sfojus(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2);
inline __attribute__((aligned(4))) void gm_f_zero_sln4lkh(void *gm_out, gm_csguankko4lmhd a0);
__attribute__((aligned(4))) gm_t7b7esi3n9r90 gm_f_zero_sln4lkh_c(gm_csguankko4lmhd a0);
extern uint8_t gm_cp0utnbgndu[16] __attribute__((aligned(8)));
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern uint8_t gm_knetc4kj0qo[40] __attribute__((aligned(8)));
extern uint8_t gm_ksksp29ghso[40] __attribute__((aligned(8)));
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srccujoh2vgr9aq[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_assert__nol4kp3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_assert__nol4kp3 F;
    gm_b16_8 gm_l3; /* body */
    GM_STACK_CHECK(&F, 316u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    F.l2 = a2;
    void * t2 = F.l0;
    gm_closure t3 = ((gm_closure){(void *)gm_f_fn_24nvriq, (void *)&F.h, cx});
    gm_b16_8 t4 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t4;
    void * t5 = F.l0;
    gm_b16_8 t6 = gm_l3;
    void * t7 = F.l1;
    gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t7) + 72));
    gm_th7badf4b6ee3 t9;
    memset(&t9, 0, sizeof t9);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)36ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t9 + 16), t6);
    gm_b0_1 t10 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t5, &t9, t8);
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
inline __attribute__((aligned(4))) uint8_t gm_f_bits_field_cdqu5on(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bits_field_cdqu5on F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    GM_STACK_CHECK_SHALLOW(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t4 = gm_l1;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)19ULL));
            if (!t6) {
                goto gm_lb2;
            }
            void * t7 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
            gm_l2 = t7;
            void * t8 = gm_l0;
            void * t9 = gm_l2;
            gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t9));
            uint8_t t11 = gm_f_is_bits_pbijkuf(t8, GM_CONV(gm_csguankko4lmhd, t10));
            F.ret = t11;
            goto gm_fret;
        }
gm_lb2:;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_pn2p277 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)4ULL));
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
inline __attribute__((aligned(4))) uint8_t gm_f_diverges_p3okmol(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_diverges_p3okmol F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    void * t5 = gm_l1;
    gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t5));
    gm_t53at80pfdus3 t7;
    gm_f_kind_jcnt4hm((void *)&t7, t4, GM_CONV(gm_csguankko4lmhd, t6));
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
    uint8_t t9 = (uint8_t)((t8 == ((uint8_t)5ULL)));
    gm_t53at80pfdus3 t10;
    memset(&t10, 0, sizeof t10);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)5ULL));
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_q94msgt(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_emit_q94msgt F;
    void * gm_l0; /* lw */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 485u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 96);
    gm_tl874t2atnlh3 t4 = gm_l2;
    gm_th7badf4b6ee3 t5 = GM_LD(gm_th7badf4b6ee3, ((uint8_t *)&F.l1));
    gm_tqp4hhm6timm2 t6;
    GM_ST(uint32_t, (uint8_t *)&t6 + 220, 0);
    GM_ST(gm_th7badf4b6ee3, ((uint8_t *)&t6 + 0), t5);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 208), t4);
    gm_tftb1odc739t5 t7 = gm_f_push_ql89qc9(t3, &t6);
    gm_l3 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srccujoh2vgr9aq, 63, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4g9am13(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_4g9am13 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)16ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6h1imen(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6h1imen F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d1l4euh(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d1l4euh F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 20)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dheabhi F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)16ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fooqdot(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fooqdot F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hh33a3d(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hh33a3d F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_i0ticdn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_i0ticdn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uvn29bg(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_uvn29bg F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l1;
    void * t2 = GM_INT_TO_PTR(((uint64_t)16ULL));
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l1)) != 0);
    if (!t3) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    gm_l0 = t4;
    void * t5 = gm_l0;
    uint64_t t6 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 224)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vbdqifj(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_vbdqifj F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_5l0p1mi(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 80)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 80);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 80)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1fu50sd(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_1fu50sd F;
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l0));
    gm_t9805qtonbsag t3 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l3));
    void * t4 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l2) + 16));
    gm_b0_1 t5 = gm_f_store_result_fiq5f1j((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_47srmke(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_47srmke F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_closure t3 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_visit_ajtfk2n *)((&F.h)->up))->l1));
    gm_f_visit_ajtfk2n(t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_48tkiti(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_48tkiti F;
    void * gm_l0; /* el */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        {
            uint8_t t4 = (uint8_t)(GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l2) + 24)) != 0);
            if (!t4) {
                goto gm_lb2;
            }
            void * t5 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l2) + 24));
            gm_l0 = t5;
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l0));
            gm_t9805qtonbsag t7 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l3));
            void * t8 = gm_l0;
            gm_b0_1 t9 = gm_f_store_result_fiq5f1j((void *)NULL, cx, NULL, t6, t7, t8);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7h6ducr(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_7h6ducr F;
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l0));
    gm_t9805qtonbsag t3 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l6));
    void * t4 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l2) + 32));
    gm_b0_1 t5 = gm_f_store_result_fiq5f1j((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_roenhir(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_roenhir F;
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)((((&F.h)->up)->up)->up))->l0));
    void * t3 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)((((&F.h)->up)->up)->up))->l2) + 24));
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)((((&F.h)->up)->up)->up))->l0));
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = (void *)(((uint8_t *)t5) + 8);
    gm_t88rcucg1b7uk t7 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t7b7esi3n9r90 t8;
    gm_f_block_iujbbjb((void *)&t8, (void *)NULL, cx, NULL, t2, t3, t7);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_u0v0t7q(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_u0v0t7q F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_stmt_k90qtjr *)((&F.h)->up))->l0));
    void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_stmt_k90qtjr *)((&F.h)->up))->l56));
    gm_t7b7esi3n9r90 t4;
    gm_f_expr_tp86bm9((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_int_bits_6bio9a8(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_bits_6bio9a8 F;
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    unsigned __int128 t2 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
    uint64_t t3 = (uint64_t)t2;
    gm_tf5srh4mp4b3i t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t4 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t4), t3);
    ((uint8_t *)&t4)[8] = 1;
    F.ret = t4;
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
inline __attribute__((aligned(4))) uint32_t gm_f_label_3373lil(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_label_3373lil F;
    void * gm_l0; /* lw */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 136));
    uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srccujoh2vgr9aq, 72, 5);
    uint32_t t7 = r5;
    GM_ST(uint32_t, (((uint8_t *)t2) + 136), t7);
    void * t8 = gm_l0;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)t8) + 136));
    uint32_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint32_t)1ULL), &r10);
    if (o11) gm_trap("integer overflow in `-`", gm_srccujoh2vgr9aq, 73, 12);
    uint32_t t12 = r10;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_0202sdh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0202sdh F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_dheabhi();
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
inline __attribute__((aligned(4))) void gm_f_list_0ngbo25(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0ngbo25 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_uvn29bg();
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_3v2gjd4_2();
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
inline __attribute__((aligned(4))) void gm_f_list_a3ut5mo(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_a3ut5mo F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fooqdot();
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
inline __attribute__((aligned(4))) void gm_f_list_gaq8h4q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_gaq8h4q F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_d1l4euh();
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
inline __attribute__((aligned(4))) void gm_f_list_hft9rao(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_hft9rao F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_4g9am13();
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
inline __attribute__((aligned(4))) void gm_f_list_mgqfh8v(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_mgqfh8v F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_6h1imen();
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
inline __attribute__((aligned(4))) void gm_f_list_rb5mlsd(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rb5mlsd F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_hh33a3d();
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
inline __attribute__((aligned(4))) void gm_f_list_urufktd(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_urufktd F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_i0ticdn();
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
inline __attribute__((aligned(4))) void gm_f_local_place_qnf3mb2(void *gm_out, uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_local_place_qnf3mb2 F;
    uint32_t gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)gm_cp0utnbgndu));
    gm_tu78tssgij60g t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t4 + 8), t2);
    gm_tlg23kb3oj23s t5;
    GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t5 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 40), t3);
    memmove(gm_out, ((uint8_t *)&t5), 56);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_local_target_vvq8lnf(void *gm_out, void * a0, gm_te9hrk0u35rev a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_local_target_vvq8lnf F;
    void * gm_l0; /* lw */
    gm_te9hrk0u35rev gm_l1; /* t */
    uint32_t gm_l2; /* tl */
    GM_STACK_CHECK_SHALLOW(&F, 71u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t5 = (uint8_t)(t4 == ((uint8_t)0ULL));
            if (!t5) {
                goto gm_lb2;
            }
            uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            uint8_t t7 = ((t6 == ((uint32_t)0ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&gm_l1) + 4) + 4));
            gm_l2 = t8;
            void * t9 = gm_l0;
            uint32_t t10 = gm_l2;
            gm_f_loop_labels_uvqimfl((void *)&(*(gm_t7isn7qnmqh8i *)gm_out), t9, t10);
            goto gm_fdone;
        }
gm_lb2:;
        gm_t7isn7qnmqh8i t11;
        memset(&t11, 0, sizeof t11);
        memmove(gm_out, ((uint8_t *)&t11), 24);
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
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_pat_hr2l6c4 F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_tq7s98ihonkju gm_l3;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l2;
    gm_tb38v5s9j1r32 t4 = GM_LD(gm_tb38v5s9j1r32, ((uint8_t *)&F.l1));
    gm_tc9qv69frb08e t5;
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 132, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 0), t2);
    GM_ST(gm_tb38v5s9j1r32, ((uint8_t *)&t5 + 8), t4);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t5 + 120), t3);
    gm_tq7s98ihonkju t6 = gm_f_new_1b2khgi((void *)NULL, cx, NULL, &t5, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ksksp29ghso)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcod98slk3vnra, 325, 83);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_n667r26(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_one_n667r26 F;
    GM_STACK_CHECK(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tftb1odc739t5 gm_l2;
    gm_trqfvmjtt4sij t2;
    gm_f_list_hft9rao((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = (void *)((uint8_t *)&F.l1);
    gm_tftb1odc739t5 t4 = gm_f_push_kki5o6e(t3, &(*(gm_tfqmtuprvunod *)((uint8_t *)&F.l0)));
    gm_l2 = t4;
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srccujoh2vgr9aq, 645, 5);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_size_lig81gu(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_ptr_size_lig81gu F;
    void * gm_l0; /* lw */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tbccvikg6b96e gm_l1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = (void *)(((uint8_t *)t5) + 8);
    gm_t88rcucg1b7uk t7 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_layout_a5ovnrm((void *)&gm_l1, (void *)NULL, cx, NULL, t3, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0pgacsj F;
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
        gm_b16_8 t9 = gm_f_empty_slice_dheabhi();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ddqa9t6(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 48), ((uint8_t *)&F.l1), 48);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2v345ri(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_2v345ri F;
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
        gm_b16_8 t9 = gm_f_empty_slice_6h1imen();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_s8usk1j(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 64), ((uint8_t *)&F.l1), 64);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 64)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k_2 F;
    void * gm_l0; /* l */
    gm_tbbqefuhv2r8h gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_3v2gjd4_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_h9tbucc_2(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_e2qt8as(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_e2qt8as F;
    void * gm_l0; /* l */
    gm_tp4rm55bg84ue gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_d1l4euh();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_uue84k4(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 20), ((uint8_t *)a1), 20);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 20)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kki5o6e(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kki5o6e F;
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
        gm_b16_8 t9 = gm_f_empty_slice_4g9am13();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_5l0p1mi(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ql89qc9(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ql89qc9 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_uvn29bg();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_5hl8984(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 224), ((uint8_t *)a1), 224);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 224)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r4haklv(void * a0, gm_t9805qtonbsag a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_r4haklv F;
    void * gm_l0; /* l */
    gm_t9805qtonbsag gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_hh33a3d();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_0s9hfc3(t8, ((uint64_t)1ULL), t10);
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
    gm_t9805qtonbsag t18 = gm_l1;
    GM_ST(gm_t9805qtonbsag, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u46i39f(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u46i39f F;
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
        gm_b16_8 t9 = gm_f_empty_slice_i0ticdn();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_q7kpg21(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u8i76or(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u8i76or F;
    void * gm_l0; /* l */
    gm_t0o0svomecjg6 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_fooqdot();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_tsagih4(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) void gm_f_read_result_8csahoh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9805qtonbsag a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_read_result_8csahoh F;
    void * gm_l0; /* lw */
    gm_t9805qtonbsag gm_l1; /* res */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* r */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint32_t gm_l5;
    uint8_t t2 = (uint8_t)((((uint8_t *)&gm_l1))[4] != 0);
    if (t2) {
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
        gm_l5 = t3;
    } else {
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t4));
        goto gm_fdone;
    }
    uint32_t t5 = gm_l5;
    gm_l4 = t5;
    void * t6 = gm_l0;
    uint32_t t7 = gm_l4;
    gm_tlg23kb3oj23s t8;
    gm_f_local_place_qnf3mb2((void *)&t8, t7);
    gm_t88rcucg1b7uk t9 = gm_l2;
    gm_tl874t2atnlh3 t10 = gm_l3;
    gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t6, &t8, t9, t10);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_region_q7ivn7p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_closure a1) {
    struct gm_fr_region_q7ivn7p F;
    void * gm_l0; /* lw */
    gm_closure gm_l1; /* body */
    gm_b16_8 gm_l3; /* out */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    memmove(((uint8_t *)&F.l2), (((uint8_t *)t2) + 96), 40);
    void * t3 = gm_l0;
    gm_trqfvmjtt4sij t4;
    gm_f_list_0ngbo25((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t3) + 96), t4);
    gm_closure t5 = gm_l1;
    gm_closure t6 = t5;
    gm_node *ccx7 = t6.cx;
    void * t8 = t6.code;
    gm_b0_1 t9;
    if (GM_PTR_TAGGED(t8)) {
        ((void (*)(void))GM_PTR_UNTAG(t8))();
    } else {
        t9 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *))t8)(t6.env, ccx7, NULL);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t10 = gm_l0;
    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 96));
    gm_l3 = t11;
    void * t12 = gm_l0;
    memmove((((uint8_t *)t12) + 96), ((uint8_t *)&F.l2), 40);
    gm_b16_8 t13 = gm_l3;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0s9hfc3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0s9hfc3 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5hl8984(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5hl8984 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)224ULL), ((uint64_t)16ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5l0p1mi(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5l0p1mi F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)80ULL), ((uint64_t)16ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ddqa9t6 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)16ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h9tbucc_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q7kpg21(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_q7kpg21 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s8usk1j(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_s8usk1j F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)64ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_tsagih4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_tsagih4 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_uue84k4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_uue84k4 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)20ULL), ((uint64_t)4ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_result_of_9m0s9vd(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_result_of_9m0s9vd F;
    void * gm_l0; /* lw */
    uint32_t gm_l1; /* tl */
    GM_STACK_CHECK_SHALLOW(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t2 = gm_l1;
    uint64_t t3 = (uint64_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 192) + 8));
    uint8_t t6 = (t3 >= t5);
    if (t6) {
        gm_t9805qtonbsag t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 192));
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t8) + 192) + 8));
    uint32_t t11 = gm_l1;
    uint64_t t12 = (uint64_t)t11;
    if ((uintptr_t)t12 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t12, t10, gm_srccujoh2vgr9aq, 929, 12);
    gm_t9805qtonbsag t13 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t9) + (uintptr_t)t12 * 8));
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_temp_lneuo24(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_temp_lneuo24 F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 56);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_push_k3l9nic(t3, GM_CONV(gm_csguankko4lmhd, t4));
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srccujoh2vgr9aq, 67, 5);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 56) + 8));
    uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
    if (o10) gm_trap("integer overflow in `-`", gm_srccujoh2vgr9aq, 68, 13);
    uint64_t t11 = r9;
    uint32_t t12 = (uint32_t)t11;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_t6j6smj7qoctu t7;
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 16), t6);
    gm_t53at80pfdus3 t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t8 + 8), t7);
    gm_t88rcucg1b7uk t9 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t8);
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
inline __attribute__((aligned(4))) void gm_f_unit_ggopu3h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_ggopu3h F;
    void * gm_l0; /* lw */
    GM_STACK_CHECK(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t5));
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
inline __attribute__((aligned(4))) void gm_f_visit_ajtfk2n(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visit_ajtfk2n F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    gm_closure t2 = F.l1;
    void * t3 = gm_l0;
    gm_closure t4 = t2;
    gm_node *ccx5 = t4.cx;
    void * t6 = t4.code;
    gm_b0_1 t7;
    if (GM_PTR_TAGGED(t6)) {
        ((void (*)(void *))GM_PTR_UNTAG(t6))(t3);
    } else {
        t7 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t6)(t4.env, ccx5, NULL, t3);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t8 = gm_l0;
    gm_closure t9 = ((gm_closure){(void *)gm_f_fn_47srmke, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t8, t9);
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
inline __attribute__((aligned(4))) void gm_f_zero_sln4lkh(void *gm_out, gm_csguankko4lmhd a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_zero_sln4lkh F;
    gm_t88rcucg1b7uk gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_t88rcucg1b7uk, a0);
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_t7b7esi3n9r90 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t3 + 16), t2);
    memmove(gm_out, ((uint8_t *)&t3), 48);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_assert__nol4kp3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) uint8_t gm_f_bits_field_cdqu5on(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_block_iujbbjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_block_iujbbjb F;
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint8_t gm_l3; /* has_defers */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_trcmmd12cefb1 gm_l6; /* st */
    void * gm_l7; /* tail */
    uint8_t gm_l8; /* sized */
    uint32_t gm_l10; /* l */
    gm_b16_8 gm_l11; /* body */
    uint32_t gm_l12; /* r */
    GM_STACK_CHECK(&F, 957u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l13;
    void * gm_l14;
    uint8_t gm_l15;
    gm_tbccvikg6b96e gm_l16;
    gm_t9805qtonbsag gm_l17;
    uint32_t gm_l18;
    gm_l3 = ((uint8_t)0);
    void * t10 = F.l1;
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)t10));
    gm_l4 = t11;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t13 = gm_l5;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                void * t16 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t18 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&gm_l6), (((uint8_t *)t16) + (uintptr_t)t18 * 32), 32);
                        {
                            {
                                uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                                uint8_t t20 = (uint8_t)(t19 == ((uint8_t)5ULL));
                                if (!t20) {
                                    goto gm_lb7;
                                }
                                gm_l13 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l13 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 696, 12);
                        }
gm_lb6:;
                        uint8_t t21 = gm_l13;
                        if (t21) {
                            gm_l3 = ((uint8_t)1);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 695, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t22 = gm_l5;
            uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
            uint64_t t25 = r23;
            gm_l5 = t25;
        }
    }
gm_lb1:;
    uint8_t t26 = gm_l3;
    uint8_t t27 = (uint8_t)!t26;
    if (t27) {
        void * t28 = F.l0;
        void * t29 = F.l1;
        gm_b0_1 t30 = gm_f_stmts_ctl06qt((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t31 = F.l1;
        uint8_t t32 = (uint8_t)(GM_LD(void *, (((uint8_t *)t31) + 16)) != 0);
        if (t32) {
            void * t33 = GM_LD(void *, (((uint8_t *)t31) + 16));
            gm_l14 = t33;
        } else {
            void * t34 = F.l0;
            gm_f_unit_ggopu3h((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        void * t35 = gm_l14;
        gm_l7 = t35;
        void * t36 = F.l0;
        void * t37 = gm_l7;
        gm_f_expr_tp86bm9((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    void * t38 = F.l0;
    void * t39 = GM_LD(void *, ((uint8_t *)t38));
    gm_t88rcucg1b7uk t40 = gm_l2;
    gm_f_layout_a5ovnrm((void *)&gm_l16, (void *)NULL, cx, NULL, t39, t40);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&gm_l16));
    uint8_t t42 = (t41 > ((uint64_t)0ULL));
    if (t42) {
        void * t43 = F.l0;
        void * t44 = GM_LD(void *, ((uint8_t *)t43));
        void * t45 = (void *)(((uint8_t *)t44) + 8);
        gm_t88rcucg1b7uk t46 = gm_l2;
        gm_t53at80pfdus3 t47;
        gm_f_kind_jcnt4hm((void *)&t47, t45, GM_CONV(gm_csguankko4lmhd, t46));
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t47) + 0));
        uint8_t t49 = (uint8_t)(!(t48 == ((uint8_t)5ULL)));
        gm_t53at80pfdus3 t50;
        memset(&t50, 0, sizeof t50);
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)5ULL));
        gm_l15 = t49;
    } else {
        gm_l15 = ((uint8_t)0);
    }
    uint8_t t51 = gm_l15;
    gm_l8 = t51;
    uint8_t t52 = gm_l8;
    if (t52) {
        void * t53 = F.l0;
        gm_t88rcucg1b7uk t54 = gm_l2;
        uint32_t t55 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t53, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t56;
        GM_ST(uint8_t, (uint8_t *)&t56 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t56 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t56), t55);
        ((uint8_t *)&t56)[4] = 1;
        gm_l17 = t56;
    } else {
        gm_t9805qtonbsag t57;
        memset(&t57, 0, sizeof t57);
        gm_l17 = t57;
    }
    gm_t9805qtonbsag t58 = gm_l17;
    F.l9 = t58;
    void * t59 = F.l0;
    uint32_t t60 = gm_f_label_3373lil(t59);
    gm_l10 = t60;
    void * t61 = F.l0;
    gm_closure t62 = ((gm_closure){(void *)gm_f_fn_k3pfo3n, (void *)&F.h, cx});
    gm_b16_8 t63 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t61, t62);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t63;
    void * t64 = F.l0;
    uint32_t t65 = gm_l10;
    gm_b16_8 t66 = gm_l11;
    gm_tiilmfd16v3fc t67;
    GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t67 + 0), t65);
    GM_ST(gm_b16_8, ((uint8_t *)&t67 + 8), t66);
    void * t68 = F.l1;
    gm_tl874t2atnlh3 t69 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t68) + 24));
    gm_th7badf4b6ee3 t70;
    memset(&t70, 0, sizeof t70);
    GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)27ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t70 + 16), t67);
    gm_b0_1 t71 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t64, &t70, t69);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t72 = (uint8_t)((((uint8_t *)&F.l9))[4] != 0);
    if (t72) {
        uint32_t t73 = GM_LD(uint32_t, ((uint8_t *)&F.l9));
        gm_l18 = t73;
    } else {
        gm_t88rcucg1b7uk t74 = gm_l2;
        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t74));
        goto gm_fdone;
    }
    uint32_t t75 = gm_l18;
    gm_l12 = t75;
    void * t76 = F.l0;
    uint32_t t77 = gm_l12;
    gm_tlg23kb3oj23s t78;
    gm_f_local_place_qnf3mb2((void *)&t78, t77);
    gm_t88rcucg1b7uk t79 = gm_l2;
    void * t80 = F.l1;
    gm_tl874t2atnlh3 t81 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t80) + 24));
    gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t76, &t78, t79, t81);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_bounds_6f9s0q1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_check_bounds_6f9s0q1 F;
    void * gm_l0; /* lw */
    gm_tl874t2atnlh3 gm_l3; /* span */
    GM_STACK_CHECK(&F, 656u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 232));
    uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srccujoh2vgr9aq, 639, 5);
    uint32_t t7 = r5;
    GM_ST(uint32_t, (((uint8_t *)t2) + 232), t7);
    void * t8 = gm_l0;
    gm_t7b7esi3n9r90 t9 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)a1));
    gm_t7b7esi3n9r90 t10 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l2));
    gm_tthvfhsvm2g0s t11;
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t11 + 0), t9);
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t11 + 48), t10);
    gm_tkj0hg83g38nr t12;
    memset(&t12, 0, sizeof t12);
    GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tthvfhsvm2g0s, ((uint8_t *)&t12 + 16), t11);
    gm_tl874t2atnlh3 t13 = gm_l3;
    gm_th7badf4b6ee3 t14;
    GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t14 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)26ULL));
    GM_ST(gm_tkj0hg83g38nr, ((uint8_t *)&t14 + 16), t12);
    gm_b0_1 t15 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t8, &t14, t13);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_conds_sqc7llk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint32_t a2) {
    struct gm_fr_conds_sqc7llk F;
    void * gm_l0; /* lw */
    gm_b16_8 gm_l1; /* cond */
    uint32_t gm_l2; /* fail */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tlr9dp305htm7 gm_l5; /* c */
    void * gm_l6; /* b */
    void * gm_l8; /* pat */
    void * gm_l9; /* value */
    uint32_t gm_l11; /* l */
    GM_STACK_CHECK(&F, 1306u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tfonjjajjj3jq gm_l13;
    gm_tlg23kb3oj23s gm_l14;
    gm_b16_8 t10 = gm_l1;
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
                                void * t21 = gm_l6;
                                gm_f_expr_tp86bm9((void *)&F.l7, (void *)NULL, cx, NULL, t20, t21);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t22 = gm_l0;
                                uint32_t t23 = gm_l2;
                                gm_t7b7esi3n9r90 t24 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l7));
                                gm_tejc6vv5sqno9 t25;
                                GM_ST(uint8_t, (uint8_t *)&t25 + 49, 0);
                                GM_ST(uint16_t, (uint8_t *)&t25 + 50, 0);
                                GM_ST(uint64_t, (uint8_t *)&t25 + 56, 0);
                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t25 + 0), t24);
                                GM_ST(uint8_t, ((uint8_t *)&t25 + 48), ((uint8_t)1));
                                GM_ST(uint32_t, ((uint8_t *)&t25 + 52), t23);
                                void * t26 = gm_l6;
                                gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t26) + 72));
                                gm_th7badf4b6ee3 t28;
                                memset(&t28, 0, sizeof t28);
                                GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)31ULL));
                                GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t28 + 16), t25);
                                gm_b0_1 t29 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t22, &t28, t27);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t31 = (uint8_t)(t30 == ((uint8_t)1ULL));
                                if (!t31) {
                                    goto gm_lb8;
                                }
                                void * t32 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                                gm_l8 = t32;
                                void * t33 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                                gm_l9 = t33;
                                void * t34 = gm_l0;
                                void * t35 = gm_l9;
                                gm_f_operand_place_cdle4ls((void *)&gm_l13, (void *)NULL, cx, NULL, t34, t35);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l13))[56] != 0);
                                if (t36) {
                                    memmove(((uint8_t *)&gm_l14), ((uint8_t *)&gm_l13), 56);
                                } else {
                                    void * t37 = gm_l0;
                                    void * t38 = gm_l9;
                                    gm_f_escape_avsr9t6((void *)&F.l10, (void *)NULL, cx, NULL, t37, t38);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t39 = gm_l0;
                                    void * t40 = gm_l9;
                                    gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t40));
                                    uint32_t t42 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t39, t41);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l11 = t42;
                                    void * t43 = gm_l0;
                                    uint32_t t44 = gm_l11;
                                    gm_tlg23kb3oj23s t45;
                                    gm_f_local_place_qnf3mb2((void *)&t45, t44);
                                    gm_t7b7esi3n9r90 t46 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l10));
                                    gm_t6fp3q71vjrmk t47;
                                    GM_ST(uint64_t, (uint8_t *)&t47 + 56, 0);
                                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t47 + 0), t45);
                                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t47 + 64), t46);
                                    void * t48 = gm_l9;
                                    gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t48) + 72));
                                    gm_th7badf4b6ee3 t50;
                                    memset(&t50, 0, sizeof t50);
                                    GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)25ULL));
                                    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t50 + 16), t47);
                                    gm_b0_1 t51 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t43, &t50, t49);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t52 = gm_l0;
                                    void * t53 = gm_l8;
                                    uint32_t t54 = gm_l11;
                                    gm_tlg23kb3oj23s t55;
                                    gm_f_local_place_qnf3mb2((void *)&t55, t54);
                                    uint32_t t56 = gm_l2;
                                    gm_b0_1 t57 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t52, t53, &t55, t56);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb3;
                                }
                                memmove(((uint8_t *)&F.l12), ((uint8_t *)&gm_l14), 56);
                                void * t58 = gm_l0;
                                void * t59 = gm_l8;
                                uint32_t t60 = gm_l2;
                                gm_b0_1 t61 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t58, t59, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l12)), t60);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 894, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 893, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t62 = gm_l4;
            uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
            uint64_t t65 = r63;
            gm_l4 = t65;
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
inline __attribute__((aligned(4))) void gm_f_context_l4fgtfp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_context_l4fgtfp F;
    void * gm_l0; /* s */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* g */
    uint32_t gm_l7; /* p */
    GM_STACK_CHECK(&F, 397u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    void * t14 = gm_l0;
    gm_trqfvmjtt4sij t15;
    gm_f_list_urufktd((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tig4i22alq15r t16;
    GM_ST(void *, ((uint8_t *)&t16 + 0), t14);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t16 + 8), t15);
    F.l1 = t16;
    gm_l3 = ((uint64_t)0ULL);
    void * t17 = gm_l0;
    uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t17) + 384) + 8));
    gm_l2 = t18;
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
                        void * t22 = (void *)(((uint8_t *)&F.l1) + 8);
                        gm_trqfvmjtt4sij t23;
                        gm_f_list_go2qg0b((void *)&t23, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t24 = gm_f_push_u46i39f(t22, &t23);
                        gm_l8 = t24;
                        uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                        if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srccujoh2vgr9aq, 15, 36);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 15, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t26 = gm_l3;
            uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
            uint64_t t29 = r27;
            gm_l3 = t29;
        }
    }
gm_lb1:;
    gm_l5 = ((uint64_t)0ULL);
    void * t30 = gm_l0;
    uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)t30) + 384) + 8));
    gm_l4 = t31;
    {
        for (;;) {
            uint64_t t32 = gm_l5;
            uint64_t t33 = gm_l4;
            uint8_t t34 = (t32 < t33);
            if (!t34) {
                goto gm_lb6;
            }
            {
                {
                    {
                        uint64_t t35 = gm_l5;
                        gm_l6 = t35;
                        {
                            {
                                void * t36 = gm_l0;
                                void * t37 = GM_LD(void *, (((uint8_t *)t36) + 384));
                                uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)t36) + 384) + 8));
                                uint64_t t39 = gm_l6;
                                if ((uintptr_t)t39 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t39, t38, gm_srccujoh2vgr9aq, 17, 17);
                                uint8_t t40 = (uint8_t)((((((uint8_t *)t37) + (uintptr_t)t39 * 64) + 48))[4] != 0);
                                if (!t40) {
                                    goto gm_lb12;
                                }
                                uint32_t t41 = GM_LD(uint32_t, ((((uint8_t *)t37) + (uintptr_t)t39 * 64) + 48));
                                gm_l7 = t41;
                                void * t42 = GM_LD(void *, (((uint8_t *)&F.l1) + 8));
                                uint64_t t43 = GM_LD(uint64_t, ((((uint8_t *)&F.l1) + 8) + 8));
                                uint32_t t44 = gm_l7;
                                uint64_t t45 = (uint64_t)t44;
                                if ((uintptr_t)t45 >= (uintptr_t)(t43)) gm_trap_index((uintptr_t)t45, t43, gm_srccujoh2vgr9aq, 17, 42);
                                void * t46 = (void *)(((uint8_t *)t42) + (uintptr_t)t45 * 40);
                                uint64_t t47 = gm_l6;
                                uint32_t t48 = (uint32_t)t47;
                                gm_tftb1odc739t5 t49 = gm_f_push_im9d4ro(t46, t48);
                                gm_l9 = t49;
                                uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                                if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srccujoh2vgr9aq, 17, 42);
                                goto gm_lb11;
                            }
gm_lb12:;
                        }
gm_lb11:;
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 16, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t51 = gm_l5;
            uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
            uint64_t t54 = r52;
            gm_l5 = t54;
        }
    }
gm_lb6:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_diverges_p3okmol(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_else_expr_lrum9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_else_expr_lrum9ir F;
    uint8_t gm_l4; /* is_result */
    uint32_t gm_l8; /* present */
    gm_b16_8 gm_l10; /* then */
    gm_b16_8 gm_l11; /* otherwise */
    GM_STACK_CHECK(&F, 1360u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    uint8_t gm_l12;
    gm_t53at80pfdus3 gm_l13;
    gm_tkn42u7na3t81 gm_l14;
    gm_t88rcucg1b7uk gm_l15;
    gm_tfonjjajjj3jq gm_l16;
    gm_tlg23kb3oj23s gm_l17;
    void * t5 = F.l0;
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    F.l3 = t6;
    void * t7 = F.l3;
    void * t8 = (void *)(((uint8_t *)t7) + 8);
    void * t9 = GM_LD(void *, ((uint8_t *)&F.l2));
    gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t9));
    gm_f_kind_jcnt4hm((void *)&gm_l13, t8, GM_CONV(gm_csguankko4lmhd, t10));
    {
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)14ULL));
            if (!t12) {
                goto gm_lb2;
            }
            gm_l12 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l12 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1132, 18);
    }
gm_lb1:;
    uint8_t t13 = gm_l12;
    gm_l4 = t13;
    void * t14 = F.l0;
    void * t15 = GM_LD(void *, ((uint8_t *)&F.l2));
    gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t15));
    gm_tkn42u7na3t81 t17 = gm_f_payload_ty_gn5u4tt(t14, GM_CONV(gm_csguankko4lmhd, t16));
    gm_l14 = t17;
    uint8_t t18 = (uint8_t)((((uint8_t *)&gm_l14))[4] != 0);
    if (t18) {
        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l14));
        gm_l15 = t19;
    } else {
        void * t20 = F.l0;
        void * t21 = F.l1;
        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    gm_t88rcucg1b7uk t22 = gm_l15;
    F.l5 = t22;
    void * t23 = F.l0;
    void * t24 = GM_LD(void *, ((uint8_t *)&F.l2));
    gm_f_operand_place_cdle4ls((void *)&gm_l16, (void *)NULL, cx, NULL, t23, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t25 = (uint8_t)((((uint8_t *)&gm_l16))[56] != 0);
    if (t25) {
        memmove(((uint8_t *)&gm_l17), ((uint8_t *)&gm_l16), 56);
    } else {
        void * t26 = F.l0;
        void * t27 = F.l1;
        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l6), ((uint8_t *)&gm_l17), 56);
    void * t28 = F.l0;
    void * t29 = F.l1;
    gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t29));
    gm_t9805qtonbsag t31 = gm_f_result_local_pp1s755((void *)NULL, cx, NULL, t28, t30);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l7 = t31;
    void * t32 = F.l0;
    void * t33 = F.l3;
    void * t34 = (void *)(((uint8_t *)t33) + 8);
    gm_t88rcucg1b7uk t35 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t36 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t32, t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t36;
    void * t37 = F.l0;
    uint32_t t38 = gm_l8;
    uint8_t t39 = gm_l4;
    gm_tlg23kb3oj23s t40 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l6));
    gm_t3349g713hmcr t41;
    GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t41 + 65, 0);
    GM_ST(uint16_t, (uint8_t *)&t41 + 66, 0);
    GM_ST(uint32_t, (uint8_t *)&t41 + 68, 0);
    GM_ST(uint32_t, ((uint8_t *)&t41 + 0), t38);
    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t41 + 8), t40);
    GM_ST(uint8_t, ((uint8_t *)&t41 + 64), t39);
    void * t42 = F.l1;
    gm_tl874t2atnlh3 t43 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t42) + 72));
    gm_th7badf4b6ee3 t44;
    memset(&t44, 0, sizeof t44);
    GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)23ULL));
    GM_ST(gm_t3349g713hmcr, ((uint8_t *)&t44 + 16), t41);
    gm_b0_1 t45 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t37, &t44, t43);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t46 = F.l0;
    void * t47 = GM_LD(void *, ((uint8_t *)&F.l2));
    gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t47));
    uint64_t t49 = gm_f_payload_off_o6q9s7n((void *)NULL, cx, NULL, t46, t48);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l9 = t49;
    void * t50 = F.l0;
    gm_closure t51 = ((gm_closure){(void *)gm_f_fn_d405sd4, (void *)&F.h, cx});
    gm_b16_8 t52 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t50, t51);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t52;
    void * t53 = F.l0;
    gm_closure t54 = ((gm_closure){(void *)gm_f_fn_s5r3poa, (void *)&F.h, cx});
    gm_b16_8 t55 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t53, t54);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t55;
    void * t56 = F.l0;
    uint32_t t57 = gm_l8;
    gm_b16_8 t58 = gm_l10;
    gm_b16_8 t59 = gm_l11;
    gm_t7b7esi3n9r90 t60;
    memset(&t60, 0, sizeof t60);
    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t60 + 16), t57);
    gm_t1s3s9ds8udn4 t61;
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t61 + 0), t60);
    GM_ST(gm_b16_8, ((uint8_t *)&t61 + 48), t58);
    GM_ST(gm_b16_8, ((uint8_t *)&t61 + 64), t59);
    void * t62 = F.l1;
    gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t62) + 72));
    gm_th7badf4b6ee3 t64;
    memset(&t64, 0, sizeof t64);
    GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)29ULL));
    GM_ST(gm_t1s3s9ds8udn4, ((uint8_t *)&t64 + 16), t61);
    gm_b0_1 t65 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t56, &t64, t63);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t66 = F.l0;
    gm_t9805qtonbsag t67 = F.l7;
    void * t68 = F.l1;
    gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t68));
    void * t70 = F.l1;
    gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t70) + 72));
    gm_f_read_result_8csahoh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t66, t67, t69, t71);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_emit_q94msgt(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void gm_f_escape_avsr9t6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_escape_avsr9t6 F;
    void * gm_l1; /* e */
    uint32_t gm_l2; /* d */
    GM_STACK_CHECK(&F, 784u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l3;
    gm_tbccvikg6b96e gm_l4;
    void * t2 = F.l0;
    void * t3 = F.l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 184));
    uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srccujoh2vgr9aq, 127, 5);
    uint32_t t7 = r5;
    GM_ST(uint32_t, (((uint8_t *)t2) + 184), t7);
    void * t8 = gm_l1;
    gm_closure t9 = ((gm_closure){(void *)gm_f_fn_vkfbupa, (void *)&F.h, cx});
    gm_f_visit_ajtfk2n(t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t10 = F.l0;
    void * t11 = GM_LD(void *, ((uint8_t *)t10));
    void * t12 = gm_l1;
    gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t12));
    gm_f_layout_a5ovnrm((void *)&gm_l4, (void *)NULL, cx, NULL, t11, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
    uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
    if (t15) {
        gm_l3 = ((uint8_t)1);
    } else {
        void * t16 = F.l0;
        void * t17 = gm_l1;
        uint8_t t18 = gm_f_diverges_p3okmol(t16, t17);
        gm_l3 = t18;
    }
    uint8_t t19 = gm_l3;
    if (t19) {
        void * t20 = F.l0;
        gm_t9805qtonbsag t21;
        memset(&t21, 0, sizeof t21);
        void * t22 = gm_l1;
        gm_t12ib7gg43lh0 t23;
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t23 + 0), t21);
        GM_ST(void *, ((uint8_t *)&t23 + 8), t22);
        void * t24 = gm_l1;
        gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t24) + 72));
        gm_th7badf4b6ee3 t26;
        memset(&t26, 0, sizeof t26);
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)44ULL));
        GM_ST(gm_t12ib7gg43lh0, ((uint8_t *)&t26 + 16), t23);
        gm_b0_1 t27 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t20, &t26, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t28 = gm_l1;
        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t28));
        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t29));
        goto gm_fdone;
    }
    void * t30 = F.l0;
    void * t31 = gm_l1;
    gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t31));
    uint32_t t33 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t30, t32);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t33;
    void * t34 = F.l0;
    uint32_t t35 = gm_l2;
    gm_t9805qtonbsag t36;
    GM_ST(uint8_t, (uint8_t *)&t36 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t36 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t36), t35);
    ((uint8_t *)&t36)[4] = 1;
    void * t37 = gm_l1;
    gm_t12ib7gg43lh0 t38;
    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t38 + 0), t36);
    GM_ST(void *, ((uint8_t *)&t38 + 8), t37);
    void * t39 = gm_l1;
    gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t39) + 72));
    gm_th7badf4b6ee3 t41;
    memset(&t41, 0, sizeof t41);
    GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)44ULL));
    GM_ST(gm_t12ib7gg43lh0, ((uint8_t *)&t41 + 16), t38);
    gm_b0_1 t42 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t34, &t41, t40);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t43 = gm_l2;
    gm_t7b7esi3n9r90 t44;
    memset(&t44, 0, sizeof t44);
    GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t44 + 16), t43);
    memmove(gm_out, ((uint8_t *)&t44), 48);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_pattern_6lpj7tc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint32_t a3) {
    struct gm_fr_escape_pattern_6lpj7tc F;
    void * gm_l0; /* lw */
    void * gm_l1; /* pat */
    uint32_t gm_l3; /* fail */
    gm_t88rcucg1b7uk gm_l4; /* bool_ty */
    uint32_t gm_l6; /* l */
    void * gm_l7; /* scrut */
    void * gm_l9; /* wild */
    void * gm_l10; /* m */
    GM_STACK_CHECK(&F, 1750u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t5;
    void * t6 = gm_l0;
    void * t7 = gm_l1;
    gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t7));
    void * t9 = gm_l1;
    gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t9) + 120));
    gm_tlg23kb3oj23s t11 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
    gm_f_load_go4fgkp((void *)&F.l5, (void *)NULL, cx, NULL, t6, &t11, t8, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t12 = gm_l0;
    void * t13 = gm_l1;
    gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t13));
    uint32_t t15 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t12, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t15;
    void * t16 = gm_l0;
    uint32_t t17 = gm_l6;
    gm_tlg23kb3oj23s t18;
    gm_f_local_place_qnf3mb2((void *)&t18, t17);
    gm_t7b7esi3n9r90 t19 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l5));
    gm_t6fp3q71vjrmk t20;
    GM_ST(uint64_t, (uint8_t *)&t20 + 56, 0);
    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t20 + 0), t18);
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t20 + 64), t19);
    void * t21 = gm_l1;
    gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 120));
    gm_th7badf4b6ee3 t23;
    memset(&t23, 0, sizeof t23);
    GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)25ULL));
    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t23 + 16), t20);
    gm_b0_1 t24 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t16, &t23, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t25 = gm_l1;
    gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t25));
    uint32_t t27 = gm_l6;
    void * t28 = gm_l1;
    gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t28) + 120));
    gm_t2b6tm8c0vgo1 t30;
    memset(&t30, 0, sizeof t30);
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t30 + 8), t27);
    void * t31 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t26, &t30, t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t31;
    gm_trqfvmjtt4sij t32;
    gm_f_list_58nr42v_2((void *)&t32, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l8 = t32;
    void * t33 = (void *)((uint8_t *)&F.l8);
    void * t34 = gm_l1;
    gm_t88rcucg1b7uk t35 = gm_l4;
    void * t36 = gm_l1;
    gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 120));
    gm_t2b6tm8c0vgo1 t38;
    memset(&t38, 0, sizeof t38);
    GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)4ULL));
    GM_ST(uint8_t, ((uint8_t *)&t38 + 8), ((uint8_t)1));
    void * t39 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t35, &t38, t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbbqefuhv2r8h t40;
    GM_ST(void *, ((uint8_t *)&t40 + 0), t34);
    GM_ST(void *, ((uint8_t *)&t40 + 8), ((void *)0));
    GM_ST(void *, ((uint8_t *)&t40 + 16), t39);
    gm_tftb1odc739t5 t41 = gm_f_push_bpovr2k_2(t33, &t40);
    gm_l12 = t41;
    uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srccujoh2vgr9aq, 1109, 5);
    void * t43 = gm_l1;
    gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t43));
    void * t45 = gm_l1;
    gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t45) + 120));
    gm_tb38v5s9j1r32 t47;
    memset(&t47, 0, sizeof t47);
    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
    void * t48 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t44, &t47, t46);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t48;
    void * t49 = (void *)((uint8_t *)&F.l8);
    void * t50 = gm_l9;
    gm_t88rcucg1b7uk t51 = gm_l4;
    void * t52 = gm_l1;
    gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t52) + 120));
    gm_t2b6tm8c0vgo1 t54;
    memset(&t54, 0, sizeof t54);
    GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)4ULL));
    GM_ST(uint8_t, ((uint8_t *)&t54 + 8), ((uint8_t)0));
    void * t55 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t51, &t54, t53);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbbqefuhv2r8h t56;
    GM_ST(void *, ((uint8_t *)&t56 + 0), t50);
    GM_ST(void *, ((uint8_t *)&t56 + 8), ((void *)0));
    GM_ST(void *, ((uint8_t *)&t56 + 16), t55);
    gm_tftb1odc739t5 t57 = gm_f_push_bpovr2k_2(t49, &t56);
    gm_l13 = t57;
    uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srccujoh2vgr9aq, 1111, 5);
    gm_t88rcucg1b7uk t59 = gm_l4;
    void * t60 = gm_l7;
    gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
    gm_tkaapcii86udk t62;
    GM_ST(void *, ((uint8_t *)&t62 + 0), t60);
    GM_ST(gm_b16_8, ((uint8_t *)&t62 + 8), t61);
    void * t63 = gm_l1;
    gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 120));
    gm_t2b6tm8c0vgo1 t65;
    memset(&t65, 0, sizeof t65);
    GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)44ULL));
    GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t65 + 8), t62);
    void * t66 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t59, &t65, t64);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t66;
    void * t67 = gm_l0;
    void * t68 = gm_l10;
    gm_f_escape_avsr9t6((void *)&F.l11, (void *)NULL, cx, NULL, t67, t68);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t69 = gm_l0;
    uint32_t t70 = gm_l3;
    gm_t7b7esi3n9r90 t71 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l11));
    gm_tejc6vv5sqno9 t72;
    GM_ST(uint8_t, (uint8_t *)&t72 + 49, 0);
    GM_ST(uint16_t, (uint8_t *)&t72 + 50, 0);
    GM_ST(uint64_t, (uint8_t *)&t72 + 56, 0);
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t72 + 0), t71);
    GM_ST(uint8_t, ((uint8_t *)&t72 + 48), ((uint8_t)1));
    GM_ST(uint32_t, ((uint8_t *)&t72 + 52), t70);
    void * t73 = gm_l1;
    gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t73) + 120));
    gm_th7badf4b6ee3 t75;
    memset(&t75, 0, sizeof t75);
    GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)31ULL));
    GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t75 + 16), t72);
    gm_b0_1 t76 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t69, &t75, t74);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_escape_stmt_g02tr5c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_escape_stmt_g02tr5c F;
    void * gm_l0; /* lw */
    void * gm_l1; /* st */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l4; /* b */
    void * gm_l5; /* e */
    GM_STACK_CHECK(&F, 398u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l6;
    gm_tq7s98ihonkju gm_l7;
    gm_trqfvmjtt4sij t2;
    gm_f_list_ir0h3tc_2((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t2;
    void * t3 = (void *)((uint8_t *)&F.l3);
    void * t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_push_d6c76ud_2(t3, &(*(gm_trcmmd12cefb1 *)((uint8_t *)t4)));
    gm_l6 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srccujoh2vgr9aq, 834, 5);
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_tl874t2atnlh3 t8 = gm_l2;
    gm_tgabaikr08mjg t9;
    GM_ST(uint32_t, (uint8_t *)&t9 + 36, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t7);
    GM_ST(void *, ((uint8_t *)&t9 + 16), ((void *)0));
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t9 + 24), t8);
    gm_tq7s98ihonkju t10 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t9, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knetc4kj0qo)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t10;
    uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srccujoh2vgr9aq, 835, 10);
    void * t12 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
    gm_l4 = t12;
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, ((uint8_t *)t13));
    void * t15 = (void *)(((uint8_t *)t14) + 8);
    gm_t88rcucg1b7uk t16 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t17 = gm_l4;
    gm_tl874t2atnlh3 t18 = gm_l2;
    gm_t2b6tm8c0vgo1 t19;
    memset(&t19, 0, sizeof t19);
    GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)46ULL));
    GM_ST(void *, ((uint8_t *)&t19 + 8), t17);
    void * t20 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t16, &t19, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t20;
    void * t21 = gm_l0;
    void * t22 = gm_l5;
    gm_t7b7esi3n9r90 t23;
    gm_f_escape_avsr9t6((void *)&t23, (void *)NULL, cx, NULL, t21, t22);
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
__attribute__((aligned(4))) void gm_f_expr_tp86bm9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_expr_tp86bm9 F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    void * gm_l2; /* s */
    uint64_t gm_l4; /* v */
    gm_b16_8 gm_l5; /* bytes */
    gm_b16_8 gm_l6; /* bytes */
    uint32_t gm_l7; /* d */
    void * gm_l8; /* x */
    uint32_t gm_l10; /* d */
    gm_tao63pk3r18v5 gm_l11; /* sl */
    gm_t3dcoc7077t1q gm_l12; /* x */
    void * gm_l13; /* x */
    uint8_t gm_l14; /* is_result */
    uint32_t gm_l16; /* present */
    gm_t083kd7b1kkag gm_l17; /* x */
    void * gm_l18; /* t */
    uint32_t gm_l19; /* d */
    void * gm_l20; /* x */
    uint32_t gm_l22; /* l */
    uint32_t gm_l23; /* d */
    void * gm_l24; /* elem */
    void * gm_l25; /* container */
    uint8_t gm_l26; /* negated */
    uint32_t gm_l29; /* d */
    void * gm_l30; /* k */
    void * gm_l31; /* v */
    void * gm_l34; /* k */
    void * gm_l37; /* w */
    gm_tiilmfd16v3fc gm_l38; /* eff */
    uint32_t gm_l39; /* op */
    void * gm_l40; /* f */
    void * gm_l43; /* t */
    gm_b16_8 gm_l44; /* args */
    double gm_l45; /* x */
    uint8_t gm_l46; /* b */
    uint32_t gm_l48; /* d */
    void * gm_l49; /* base */
    uint32_t gm_l50; /* index */
    uint32_t gm_l52; /* d */
    void * gm_l54; /* x */
    void * gm_l56; /* x */
    void * gm_l58; /* x */
    uint32_t gm_l60; /* d */
    uint32_t gm_l61; /* f */
    uint32_t gm_l62; /* d */
    uint32_t gm_l63; /* f */
    uint32_t gm_l64; /* d */
    uint32_t gm_l65; /* h */
    uint32_t gm_l66; /* d */
    gm_t9qkh0rsooc8d gm_l67; /* op */
    void * gm_l68; /* x */
    uint32_t gm_l70; /* d */
    gm_ts6oqppjh0ba3 gm_l71; /* op */
    void * gm_l72; /* l */
    void * gm_l73; /* r */
    uint32_t gm_l76; /* d */
    void * gm_l77; /* l */
    void * gm_l78; /* r */
    uint8_t gm_l79; /* negated */
    uint32_t gm_l82; /* d */
    uint8_t gm_l83; /* is_and */
    void * gm_l84; /* l */
    void * gm_l85; /* r */
    gm_tmn7n0n9raou5 gm_l86; /* kind */
    void * gm_l87; /* x */
    uint32_t gm_l89; /* d */
    gm_tvdg94bk7aa22 gm_l90; /* op */
    gm_b16_8 gm_l91; /* args */
    gm_b16_8 gm_l92; /* vals */
    uint32_t gm_l93; /* d */
    gm_b16_8 gm_l95; /* args */
    uint32_t gm_l96; /* f */
    gm_tiilmfd16v3fc gm_l97; /* eff */
    uint32_t gm_l98; /* op */
    void * gm_l99; /* x */
    gm_b16_8 gm_l101; /* vals */
    uint32_t gm_l102; /* d */
    gm_b16_8 gm_l103; /* xs */
    gm_b16_8 gm_l104; /* xs */
    gm_b16_8 gm_l105; /* vals */
    uint32_t gm_l106; /* d */
    gm_ttggq7ng4kp3p gm_l107; /* v */
    void * gm_l109; /* p */
    uint32_t gm_l110; /* d */
    void * gm_l111; /* x */
    uint32_t gm_l113; /* d */
    void * gm_l114; /* b */
    gm_trl2khmuphncd gm_l115; /* x */
    gm_teo9d4mncjfdu gm_l117; /* x */
    void * gm_l119; /* y */
    gm_tp4rm55bg84ue gm_l121; /* labels */
    uint32_t gm_l122; /* tl */
    void * gm_l123; /* v */
    uint32_t gm_l125; /* r */
    gm_te9hrk0u35rev gm_l126; /* t */
    gm_tp4rm55bg84ue gm_l127; /* labels */
    uint32_t gm_l128; /* levels */
    void * gm_l129; /* x */
    void * gm_l132; /* x */
    void * gm_l134; /* scrutinee */
    gm_b16_8 gm_l135; /* arms */
    GM_STACK_CHECK(&F, 22732u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l136;
    gm_tbccvikg6b96e gm_l137;
    gm_tf5srh4mp4b3i gm_l138;
    uint64_t gm_l139;
    gm_tf8kfhcnval78 gm_l140;
    gm_t7b7esi3n9r90 gm_l141;
    uint8_t gm_l142;
    gm_tfonjjajjj3jq gm_l143;
    gm_tlg23kb3oj23s gm_l144;
    gm_tbccvikg6b96e gm_l145;
    gm_t9i04n6j2d1gi gm_l146;
    uint64_t gm_l147;
    gm_tf8kfhcnval78 gm_l148;
    gm_tfonjjajjj3jq gm_l149;
    gm_tlg23kb3oj23s gm_l150;
    gm_tbccvikg6b96e gm_l151;
    gm_tfonjjajjj3jq gm_l152;
    gm_tlg23kb3oj23s gm_l153;
    gm_tfonjjajjj3jq gm_l154;
    gm_tlg23kb3oj23s gm_l155;
    gm_tfonjjajjj3jq gm_l156;
    gm_tlg23kb3oj23s gm_l157;
    uint8_t gm_l158;
    gm_t53at80pfdus3 gm_l159;
    uint8_t gm_l160;
    gm_tbccvikg6b96e gm_l161;
    gm_tllthq21h2f0k gm_l162;
    uint8_t gm_l163;
    gm_tbccvikg6b96e gm_l164;
    gm_t7isn7qnmqh8i gm_l165;
    gm_tp4rm55bg84ue gm_l166;
    gm_tbccvikg6b96e gm_l167;
    gm_t9805qtonbsag gm_l168;
    gm_t7isn7qnmqh8i gm_l169;
    gm_tp4rm55bg84ue gm_l170;
    gm_tf8kfhcnval78 gm_l171;
    gm_tbccvikg6b96e gm_l172;
    {
        void * t105 = gm_l0;
        void * t106 = GM_LD(void *, ((uint8_t *)t105));
        gm_l2 = t106;
        void * t107 = gm_l1;
        {
            {
                {
                    uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t109 = (uint8_t)(t108 == ((uint8_t)0ULL));
                    if (!t109) {
                        goto gm_lb2;
                    }
                    memmove(((uint8_t *)&F.l3), ((((uint8_t *)t107) + 8) + 8), 48);
                    void * t110 = gm_l2;
                    void * t111 = (void *)(((uint8_t *)t110) + 8);
                    void * t112 = gm_l1;
                    gm_t88rcucg1b7uk t113 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t112));
                    gm_f_kind_jcnt4hm((void *)&gm_l136, t111, GM_CONV(gm_csguankko4lmhd, t113));
                    {
                        {
                            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l136) + 0));
                            uint8_t t115 = (uint8_t)(t114 == ((uint8_t)3ULL));
                            if (!t115) {
                                goto gm_lb4;
                            }
                            void * t116 = gm_l0;
                            void * t117 = gm_l1;
                            gm_tngtuiuskn938 t118;
                            gm_f_from_int_ofjuoqh((void *)&t118, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            double t119 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &t118);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_f_float_value_o4b87lc((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t116, t117, t119);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
gm_lb4:;
                        {
                            goto gm_lb3;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 173, 13);
                    }
gm_lb3:;
                    void * t120 = gm_l2;
                    void * t121 = (void *)(((uint8_t *)t120) + 8);
                    void * t122 = gm_l1;
                    gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t122));
                    uint8_t t124 = gm_f_is_integer_8d4f9ek(t121, GM_CONV(gm_csguankko4lmhd, t123));
                    uint8_t t125 = (uint8_t)!t124;
                    if (t125) {
                        void * t126 = gm_l0;
                        void * t127 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t126, t127);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    void * t128 = gm_l2;
                    void * t129 = gm_l1;
                    gm_t88rcucg1b7uk t130 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t129));
                    gm_f_layout_a5ovnrm((void *)&gm_l137, (void *)NULL, cx, NULL, t128, t130);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t131 = GM_LD(uint64_t, ((uint8_t *)&gm_l137));
                    uint8_t t132 = (t131 > ((uint64_t)8ULL));
                    if (t132) {
                        void * t133 = gm_l1;
                        gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t133));
                        unsigned __int128 t135 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
                        gm_tl5jbgvjjt15g t136;
                        GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t136 + 8, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t136 + 0), t134);
                        GM_ST(unsigned __int128, ((uint8_t *)&t136 + 16), t135);
                        gm_t7b7esi3n9r90 t137;
                        GM_ST(uint8_t, (uint8_t *)&t137 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t137 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t137 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t137 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t137 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_tl5jbgvjjt15g, ((uint8_t *)&t137 + 16), t136);
                        memmove(gm_out, ((uint8_t *)&t137), 48);
                        goto gm_fdone;
                    }
                    gm_tf5srh4mp4b3i t138 = gm_f_int_bits_6bio9a8(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
                    gm_l138 = t138;
                    uint8_t t139 = (uint8_t)((((uint8_t *)&gm_l138))[8] != 0);
                    if (t139) {
                        uint64_t t140 = GM_LD(uint64_t, ((uint8_t *)&gm_l138));
                        gm_l139 = t140;
                    } else {
                        void * t141 = gm_l0;
                        void * t142 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t141, t142);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    uint64_t t143 = gm_l139;
                    gm_l4 = t143;
                    void * t144 = gm_l0;
                    void * t145 = gm_l1;
                    gm_t88rcucg1b7uk t146 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t145));
                    uint64_t t147 = gm_l4;
                    gm_f_word_1sfojus((void *)&gm_l140, (void *)NULL, cx, NULL, t144, t146, t147);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t148 = (uint8_t)((((uint8_t *)&gm_l140))[48] != 0);
                    if (t148) {
                        memmove(((uint8_t *)&gm_l141), ((uint8_t *)&gm_l140), 48);
                    } else {
                        void * t149 = gm_l0;
                        void * t150 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&gm_l141, (void *)NULL, cx, NULL, t149, t150);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    memmove(gm_out, ((uint8_t *)&gm_l141), 48);
                    goto gm_fdone;
                }
gm_lb2:;
            }
            {
                {
                    {
                        {
                            uint8_t t151 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t152 = (uint8_t)(t151 == ((uint8_t)5ULL));
                            if (!t152) {
                                goto gm_lb8;
                            }
                            gm_b16_8 t153 = GM_LD(gm_b16_8, ((((uint8_t *)t107) + 8) + 8));
                            gm_l5 = t153;
                            goto gm_lb7;
                        }
gm_lb8:;
                        {
                            uint8_t t154 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t155 = (uint8_t)(t154 == ((uint8_t)6ULL));
                            if (!t155) {
                                goto gm_lb9;
                            }
                            gm_b16_8 t156 = GM_LD(gm_b16_8, ((((uint8_t *)t107) + 8) + 8));
                            gm_l5 = t156;
                            goto gm_lb7;
                        }
gm_lb9:;
                        goto gm_lb6;
                    }
gm_lb7:;
                    void * t157 = gm_l0;
                    void * t158 = gm_l1;
                    gm_t88rcucg1b7uk t159 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t158));
                    uint32_t t160 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t157, t159);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l7 = t160;
                    void * t161 = gm_l0;
                    uint32_t t162 = gm_l7;
                    gm_b16_8 t163 = gm_l5;
                    void * t164 = gm_l1;
                    {
                        {
                            uint8_t t165 = GM_LD(uint8_t, ((((uint8_t *)t164) + 8) + 0));
                            uint8_t t166 = (uint8_t)(t165 == ((uint8_t)6ULL));
                            if (!t166) {
                                goto gm_lb11;
                            }
                            gm_l142 = ((uint8_t)1);
                            goto gm_lb10;
                        }
gm_lb11:;
                        {
                            gm_l142 = ((uint8_t)0);
                            goto gm_lb10;
                        }
gm_lb12:;
                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 184, 36);
                    }
gm_lb10:;
                    uint8_t t167 = gm_l142;
                    gm_trt0mvq732stt t168;
                    GM_ST(uint32_t, (uint8_t *)&t168 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&t168 + 25, 0);
                    GM_ST(uint16_t, (uint8_t *)&t168 + 26, 0);
                    GM_ST(uint32_t, (uint8_t *)&t168 + 28, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t168 + 0), t162);
                    GM_ST(gm_b16_8, ((uint8_t *)&t168 + 8), t163);
                    GM_ST(uint8_t, ((uint8_t *)&t168 + 24), t167);
                    void * t169 = gm_l1;
                    gm_tl874t2atnlh3 t170 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t169) + 72));
                    gm_th7badf4b6ee3 t171;
                    memset(&t171, 0, sizeof t171);
                    GM_ST(uint8_t, ((uint8_t *)&t171 + 0), ((uint8_t)10ULL));
                    GM_ST(gm_trt0mvq732stt, ((uint8_t *)&t171 + 16), t168);
                    gm_b0_1 t172 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t161, &t171, t170);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t173 = gm_l7;
                    gm_t7b7esi3n9r90 t174;
                    memset(&t174, 0, sizeof t174);
                    GM_ST(uint8_t, ((uint8_t *)&t174 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t174 + 16), t173);
                    memmove(gm_out, ((uint8_t *)&t174), 48);
                    goto gm_fdone;
                }
gm_lb6:;
                {
                    uint8_t t175 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t176 = (uint8_t)(t175 == ((uint8_t)8ULL));
                    if (!t176) {
                        goto gm_lb13;
                    }
                    void * t177 = gm_l1;
                    gm_t88rcucg1b7uk t178 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t177));
                    gm_t7b7esi3n9r90 t179;
                    memset(&t179, 0, sizeof t179);
                    GM_ST(uint8_t, ((uint8_t *)&t179 + 0), ((uint8_t)4ULL));
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t179 + 16), t178);
                    memmove(gm_out, ((uint8_t *)&t179), 48);
                    goto gm_fdone;
                }
gm_lb13:;
                {
                    uint8_t t180 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t181 = (uint8_t)(t180 == ((uint8_t)37ULL));
                    if (!t181) {
                        goto gm_lb14;
                    }
                    void * t182 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l8 = t182;
                    void * t183 = gm_l0;
                    void * t184 = gm_l8;
                    gm_f_expr_tp86bm9((void *)&F.l9, (void *)NULL, cx, NULL, t183, t184);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t185 = gm_l0;
                    void * t186 = gm_l1;
                    gm_t88rcucg1b7uk t187 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t186));
                    uint32_t t188 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t185, t187);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t188;
                    void * t189 = gm_l0;
                    uint32_t t190 = gm_l10;
                    gm_t7b7esi3n9r90 t191 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l9));
                    gm_tf4lk4crue8vt t192;
                    GM_ST(uint32_t, (uint8_t *)&t192 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t192 + 8, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t192 + 0), t190);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t192 + 16), t191);
                    void * t193 = gm_l1;
                    gm_tl874t2atnlh3 t194 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t193) + 72));
                    gm_th7badf4b6ee3 t195;
                    memset(&t195, 0, sizeof t195);
                    GM_ST(uint8_t, ((uint8_t *)&t195 + 0), ((uint8_t)11ULL));
                    GM_ST(gm_tf4lk4crue8vt, ((uint8_t *)&t195 + 16), t192);
                    gm_b0_1 t196 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t189, &t195, t194);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t197 = gm_l10;
                    gm_t7b7esi3n9r90 t198;
                    memset(&t198, 0, sizeof t198);
                    GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t198 + 16), t197);
                    memmove(gm_out, ((uint8_t *)&t198), 48);
                    goto gm_fdone;
                }
gm_lb14:;
            }
            {
                {
                    uint8_t t199 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t200 = (uint8_t)(t199 == ((uint8_t)21ULL));
                    if (!t200) {
                        goto gm_lb15;
                    }
                    memmove(((uint8_t *)&gm_l11), ((((uint8_t *)t107) + 8) + 8), 32);
                    void * t201 = gm_l0;
                    void * t202 = gm_l1;
                    gm_tao63pk3r18v5 t203 = GM_LD(gm_tao63pk3r18v5, ((uint8_t *)&gm_l11));
                    gm_f_slice_dup5j25((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t201, t202, &t203);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb15:;
                {
                    uint8_t t204 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t205 = (uint8_t)(t204 == ((uint8_t)41ULL));
                    if (!t205) {
                        goto gm_lb16;
                    }
                    memmove(((uint8_t *)&gm_l12), ((((uint8_t *)t107) + 8) + 8), 24);
                    void * t206 = gm_l0;
                    void * t207 = gm_l1;
                    gm_t3dcoc7077t1q t208 = GM_LD(gm_t3dcoc7077t1q, ((uint8_t *)&gm_l12));
                    gm_f_else_expr_lrum9ir((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t206, t207, &t208);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb16:;
                {
                    uint8_t t209 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t210 = (uint8_t)(t209 == ((uint8_t)40ULL));
                    if (!t210) {
                        goto gm_lb17;
                    }
                    void * t211 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l13 = t211;
                    uint8_t t212 = GM_LD(uint8_t, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l14 = t212;
                    void * t213 = gm_l0;
                    void * t214 = gm_l13;
                    gm_f_operand_place_cdle4ls((void *)&gm_l143, (void *)NULL, cx, NULL, t213, t214);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t215 = (uint8_t)((((uint8_t *)&gm_l143))[56] != 0);
                    if (t215) {
                        memmove(((uint8_t *)&gm_l144), ((uint8_t *)&gm_l143), 56);
                    } else {
                        void * t216 = gm_l0;
                        void * t217 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t216, t217);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&F.l15), ((uint8_t *)&gm_l144), 56);
                    void * t218 = gm_l0;
                    void * t219 = gm_l2;
                    void * t220 = (void *)(((uint8_t *)t219) + 8);
                    gm_t88rcucg1b7uk t221 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t220);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t222 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t218, t221);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t222;
                    void * t223 = gm_l0;
                    uint32_t t224 = gm_l16;
                    uint8_t t225 = gm_l14;
                    gm_tlg23kb3oj23s t226 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l15));
                    gm_t3349g713hmcr t227;
                    GM_ST(uint32_t, (uint8_t *)&t227 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&t227 + 65, 0);
                    GM_ST(uint16_t, (uint8_t *)&t227 + 66, 0);
                    GM_ST(uint32_t, (uint8_t *)&t227 + 68, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t227 + 0), t224);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t227 + 8), t226);
                    GM_ST(uint8_t, ((uint8_t *)&t227 + 64), t225);
                    void * t228 = gm_l1;
                    gm_tl874t2atnlh3 t229 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t228) + 72));
                    gm_th7badf4b6ee3 t230;
                    memset(&t230, 0, sizeof t230);
                    GM_ST(uint8_t, ((uint8_t *)&t230 + 0), ((uint8_t)23ULL));
                    GM_ST(gm_t3349g713hmcr, ((uint8_t *)&t230 + 16), t227);
                    gm_b0_1 t231 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t223, &t230, t229);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t232 = gm_l0;
                    uint32_t t233 = gm_l16;
                    uint8_t t234 = gm_l14;
                    gm_t7b7esi3n9r90 t235;
                    memset(&t235, 0, sizeof t235);
                    GM_ST(uint8_t, ((uint8_t *)&t235 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t235 + 16), t233);
                    gm_tlg23kb3oj23s t236 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l15));
                    gm_t6j9ipvj0d6k5 t237;
                    GM_ST(uint8_t, (uint8_t *)&t237 + 105, 0);
                    GM_ST(uint16_t, (uint8_t *)&t237 + 106, 0);
                    GM_ST(uint32_t, (uint8_t *)&t237 + 108, 0);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t237 + 0), t235);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t237 + 48), t236);
                    GM_ST(uint8_t, ((uint8_t *)&t237 + 104), t234);
                    gm_tkj0hg83g38nr t238;
                    memset(&t238, 0, sizeof t238);
                    GM_ST(uint8_t, ((uint8_t *)&t238 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_t6j9ipvj0d6k5, ((uint8_t *)&t238 + 16), t237);
                    void * t239 = gm_l1;
                    gm_tl874t2atnlh3 t240 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t239) + 72));
                    gm_th7badf4b6ee3 t241;
                    GM_ST(uint8_t, (uint8_t *)&t241 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t241 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t241 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t241 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t241 + 0), ((uint8_t)26ULL));
                    GM_ST(gm_tkj0hg83g38nr, ((uint8_t *)&t241 + 16), t238);
                    gm_b0_1 t242 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t232, &t241, t240);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t243 = gm_l2;
                    void * t244 = gm_l1;
                    gm_t88rcucg1b7uk t245 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t244));
                    gm_f_layout_a5ovnrm((void *)&gm_l145, (void *)NULL, cx, NULL, t243, t245);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t246 = GM_LD(uint64_t, ((uint8_t *)&gm_l145));
                    uint8_t t247 = ((t246 == ((uint64_t)0ULL)));
                    if (t247) {
                        void * t248 = gm_l1;
                        gm_t88rcucg1b7uk t249 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t248));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t249));
                        goto gm_fdone;
                    }
                    void * t250 = gm_l0;
                    void * t251 = gm_l0;
                    void * t252 = gm_l13;
                    gm_t88rcucg1b7uk t253 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t252));
                    gm_tlg23kb3oj23s t254 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l15));
                    uint64_t t255 = gm_f_payload_off_o6q9s7n((void *)NULL, cx, NULL, t251, t253);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t256 = gm_l1;
                    gm_t88rcucg1b7uk t257 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t256));
                    gm_tlcm4lvu3g1a8 t258;
                    GM_ST(uint32_t, (uint8_t *)&t258 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t258 + 0), t255);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t258 + 8), t257);
                    gm_tfqmtuprvunod t259;
                    memset(&t259, 0, sizeof t259);
                    GM_ST(uint8_t, ((uint8_t *)&t259 + 0), ((uint8_t)4ULL));
                    GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t259 + 16), t258);
                    gm_tlg23kb3oj23s t260;
                    gm_f_project_6hpd48e((void *)&t260, (void *)NULL, cx, NULL, &t254, &t259);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t261 = gm_l1;
                    gm_t88rcucg1b7uk t262 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t261));
                    void * t263 = gm_l1;
                    gm_tl874t2atnlh3 t264 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t263) + 72));
                    gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t250, &t260, t262, t264);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb17:;
            }
            {
                {
                    uint8_t t265 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t266 = (uint8_t)(t265 == ((uint8_t)42ULL));
                    if (!t266) {
                        goto gm_lb18;
                    }
                    memmove(((uint8_t *)&gm_l17), ((((uint8_t *)t107) + 8) + 8), 24);
                    void * t267 = gm_l0;
                    void * t268 = gm_l1;
                    gm_t083kd7b1kkag t269 = GM_LD(gm_t083kd7b1kkag, ((uint8_t *)&gm_l17));
                    gm_f_try_expr_2gjs5f7((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t267, t268, &t269);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb18:;
                {
                    uint8_t t270 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t271 = (uint8_t)(t270 == ((uint8_t)57ULL));
                    if (!t271) {
                        goto gm_lb19;
                    }
                    void * t272 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l18 = t272;
                    void * t273 = gm_l0;
                    void * t274 = gm_l1;
                    gm_t88rcucg1b7uk t275 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t274));
                    uint32_t t276 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t273, t275);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l19 = t276;
                    void * t277 = gm_l0;
                    uint32_t t278 = gm_l19;
                    void * t279 = gm_l18;
                    gm_ttggq7ng4kp3p t280;
                    GM_ST(uint32_t, (uint8_t *)&t280 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t280 + 0), t278);
                    GM_ST(void *, ((uint8_t *)&t280 + 8), t279);
                    void * t281 = gm_l1;
                    gm_tl874t2atnlh3 t282 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t281) + 72));
                    gm_th7badf4b6ee3 t283;
                    memset(&t283, 0, sizeof t283);
                    GM_ST(uint8_t, ((uint8_t *)&t283 + 0), ((uint8_t)19ULL));
                    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t283 + 16), t280);
                    gm_b0_1 t284 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t277, &t283, t282);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t285 = gm_l19;
                    gm_t7b7esi3n9r90 t286;
                    memset(&t286, 0, sizeof t286);
                    GM_ST(uint8_t, ((uint8_t *)&t286 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t286 + 16), t285);
                    memmove(gm_out, ((uint8_t *)&t286), 48);
                    goto gm_fdone;
                }
gm_lb19:;
                {
                    uint8_t t287 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t288 = (uint8_t)(t287 == ((uint8_t)25ULL));
                    if (!t288) {
                        goto gm_lb20;
                    }
                    void * t289 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l20 = t289;
                    void * t290 = gm_l0;
                    void * t291 = gm_l20;
                    gm_f_expr_tp86bm9((void *)&F.l21, (void *)NULL, cx, NULL, t290, t291);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t292 = gm_l0;
                    void * t293 = gm_l20;
                    gm_t88rcucg1b7uk t294 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t293));
                    uint32_t t295 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t292, t294);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l22 = t295;
                    void * t296 = gm_l0;
                    void * t297 = GM_LD(void *, (((uint8_t *)t296) + 16));
                    uint64_t t298 = GM_LD(uint64_t, ((((uint8_t *)t296) + 16) + 8));
                    uint32_t t299 = gm_l22;
                    uint64_t t300 = (uint64_t)t299;
                    if ((uintptr_t)t300 >= (uintptr_t)(t298)) gm_trap_index((uintptr_t)t300, t298, gm_srccujoh2vgr9aq, 213, 13);
                    GM_ST(uint8_t, ((((uint8_t *)t297) + (uintptr_t)t300 * 32) + 25), ((uint8_t)1));
                    void * t301 = gm_l0;
                    uint32_t t302 = gm_l22;
                    gm_tlg23kb3oj23s t303;
                    gm_f_local_place_qnf3mb2((void *)&t303, t302);
                    gm_t7b7esi3n9r90 t304 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l21));
                    gm_t6fp3q71vjrmk t305;
                    GM_ST(uint64_t, (uint8_t *)&t305 + 56, 0);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t305 + 0), t303);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t305 + 64), t304);
                    void * t306 = gm_l20;
                    gm_tl874t2atnlh3 t307 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t306) + 72));
                    gm_th7badf4b6ee3 t308;
                    memset(&t308, 0, sizeof t308);
                    GM_ST(uint8_t, ((uint8_t *)&t308 + 0), ((uint8_t)25ULL));
                    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t308 + 16), t305);
                    gm_b0_1 t309 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t301, &t308, t307);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t310 = gm_l0;
                    void * t311 = gm_l1;
                    gm_t88rcucg1b7uk t312 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t311));
                    uint32_t t313 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t310, t312);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l23 = t313;
                    void * t314 = gm_l0;
                    uint32_t t315 = gm_l23;
                    uint32_t t316 = gm_l22;
                    gm_tlg23kb3oj23s t317;
                    gm_f_local_place_qnf3mb2((void *)&t317, t316);
                    gm_tlpcrb3vu6o2d t318;
                    GM_ST(uint32_t, (uint8_t *)&t318 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t318 + 0), t315);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t318 + 8), t317);
                    void * t319 = gm_l1;
                    gm_tl874t2atnlh3 t320 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t319) + 72));
                    gm_th7badf4b6ee3 t321;
                    memset(&t321, 0, sizeof t321);
                    GM_ST(uint8_t, ((uint8_t *)&t321 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_tlpcrb3vu6o2d, ((uint8_t *)&t321 + 16), t318);
                    gm_b0_1 t322 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t314, &t321, t320);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t323 = gm_l23;
                    gm_t7b7esi3n9r90 t324;
                    memset(&t324, 0, sizeof t324);
                    GM_ST(uint8_t, ((uint8_t *)&t324 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t324 + 16), t323);
                    memmove(gm_out, ((uint8_t *)&t324), 48);
                    goto gm_fdone;
                }
gm_lb20:;
            }
            {
                {
                    uint8_t t325 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t326 = (uint8_t)(t325 == ((uint8_t)45ULL));
                    if (!t326) {
                        goto gm_lb21;
                    }
                    void * t327 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l24 = t327;
                    void * t328 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l25 = t328;
                    uint8_t t329 = GM_LD(uint8_t, (((((uint8_t *)t107) + 8) + 8) + 16));
                    gm_l26 = t329;
                    void * t330 = gm_l0;
                    void * t331 = gm_l24;
                    gm_f_expr_tp86bm9((void *)&F.l27, (void *)NULL, cx, NULL, t330, t331);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t332 = gm_l0;
                    void * t333 = gm_l25;
                    gm_f_expr_tp86bm9((void *)&F.l28, (void *)NULL, cx, NULL, t332, t333);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t334 = gm_l0;
                    void * t335 = gm_l1;
                    gm_t88rcucg1b7uk t336 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t335));
                    uint32_t t337 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t334, t336);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l29 = t337;
                    void * t338 = gm_l0;
                    uint32_t t339 = gm_l29;
                    uint8_t t340 = gm_l26;
                    gm_t7b7esi3n9r90 t341 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l27));
                    gm_t7b7esi3n9r90 t342 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l28));
                    gm_tr6ll1dg9imrl t343;
                    GM_ST(uint8_t, (uint8_t *)&t343 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t343 + 6, 0);
                    GM_ST(uint64_t, (uint8_t *)&t343 + 8, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t343 + 0), t339);
                    GM_ST(uint8_t, ((uint8_t *)&t343 + 4), t340);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t343 + 16), t341);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t343 + 64), t342);
                    void * t344 = gm_l1;
                    gm_tl874t2atnlh3 t345 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t344) + 72));
                    gm_th7badf4b6ee3 t346;
                    memset(&t346, 0, sizeof t346);
                    GM_ST(uint8_t, ((uint8_t *)&t346 + 0), ((uint8_t)20ULL));
                    GM_ST(gm_tr6ll1dg9imrl, ((uint8_t *)&t346 + 16), t343);
                    gm_b0_1 t347 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t338, &t346, t345);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t348 = gm_l29;
                    gm_t7b7esi3n9r90 t349;
                    memset(&t349, 0, sizeof t349);
                    GM_ST(uint8_t, ((uint8_t *)&t349 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t349 + 16), t348);
                    memmove(gm_out, ((uint8_t *)&t349), 48);
                    goto gm_fdone;
                }
gm_lb21:;
                {
                    uint8_t t350 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t351 = (uint8_t)(t350 == ((uint8_t)53ULL));
                    if (!t351) {
                        goto gm_lb22;
                    }
                    void * t352 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l30 = t352;
                    void * t353 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l31 = t353;
                    void * t354 = gm_l0;
                    void * t355 = gm_l30;
                    gm_f_expr_tp86bm9((void *)&F.l32, (void *)NULL, cx, NULL, t354, t355);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t356 = gm_l0;
                    void * t357 = gm_l31;
                    gm_f_expr_tp86bm9((void *)&F.l33, (void *)NULL, cx, NULL, t356, t357);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t358 = gm_l0;
                    gm_t7b7esi3n9r90 t359 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l32));
                    gm_t7b7esi3n9r90 t360 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l33));
                    gm_tthvfhsvm2g0s t361;
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t361 + 0), t359);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t361 + 48), t360);
                    void * t362 = gm_l1;
                    gm_tl874t2atnlh3 t363 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t362) + 72));
                    gm_th7badf4b6ee3 t364;
                    memset(&t364, 0, sizeof t364);
                    GM_ST(uint8_t, ((uint8_t *)&t364 + 0), ((uint8_t)40ULL));
                    GM_ST(gm_tthvfhsvm2g0s, ((uint8_t *)&t364 + 16), t361);
                    gm_b0_1 t365 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t358, &t364, t363);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t366 = gm_l1;
                    gm_t88rcucg1b7uk t367 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t366));
                    gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t367));
                    goto gm_fdone;
                }
gm_lb22:;
                {
                    uint8_t t368 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t369 = (uint8_t)(t368 == ((uint8_t)54ULL));
                    if (!t369) {
                        goto gm_lb23;
                    }
                    void * t370 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l34 = t370;
                    void * t371 = gm_l0;
                    void * t372 = gm_l34;
                    gm_f_expr_tp86bm9((void *)&F.l35, (void *)NULL, cx, NULL, t371, t372);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t373 = gm_l0;
                    gm_t7b7esi3n9r90 t374 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l35));
                    void * t375 = gm_l1;
                    gm_tl874t2atnlh3 t376 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t375) + 72));
                    gm_th7badf4b6ee3 t377;
                    memset(&t377, 0, sizeof t377);
                    GM_ST(uint8_t, ((uint8_t *)&t377 + 0), ((uint8_t)41ULL));
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t377 + 16), t374);
                    gm_b0_1 t378 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t373, &t377, t376);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t379 = gm_l1;
                    gm_t88rcucg1b7uk t380 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t379));
                    gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t380));
                    goto gm_fdone;
                }
gm_lb23:;
            }
            {
                {
                    uint8_t t381 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t382 = (uint8_t)(t381 == ((uint8_t)59ULL));
                    if (!t382) {
                        goto gm_lb24;
                    }
                    memmove(((uint8_t *)&F.l36), ((((uint8_t *)t107) + 8) + 8), 40);
                    void * t383 = gm_l0;
                    void * t384 = gm_l1;
                    gm_t710lsrq1m8gk t385 = GM_LD(gm_t710lsrq1m8gk, ((uint8_t *)&F.l36));
                    gm_f_with_expr_g16bcnu((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t383, t384, &t385);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb24:;
                {
                    uint8_t t386 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t387 = (uint8_t)(t386 == ((uint8_t)55ULL));
                    if (!t387) {
                        goto gm_lb25;
                    }
                    void * t388 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l37 = t388;
                    void * t389 = gm_l37;
                    {
                        {
                            uint8_t t390 = GM_LD(uint8_t, (((uint8_t *)t389) + 0));
                            uint8_t t391 = (uint8_t)(t390 == ((uint8_t)0ULL));
                            if (!t391) {
                                goto gm_lb27;
                            }
                            memmove(((uint8_t *)&gm_l38), (((uint8_t *)t389) + 8), 24);
                            uint32_t t392 = GM_LD(uint32_t, ((((uint8_t *)t389) + 8) + 24));
                            gm_l39 = t392;
                            uint32_t t393 = gm_l39;
                            gm_tiilmfd16v3fc t394 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l38));
                            gm_t419mo45g7j0a t395;
                            GM_ST(uint32_t, (uint8_t *)&t395 + 28, 0);
                            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t395 + 0), t394);
                            GM_ST(uint32_t, ((uint8_t *)&t395 + 24), t393);
                            gm_t9i04n6j2d1gi t396;
                            GM_ST(uint8_t, (uint8_t *)&t396 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t396 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t396 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t396 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t396 + 48, 0);
                            GM_ST(uint64_t, (uint8_t *)&t396 + 56, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t396 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t396 + 16), t395);
                            gm_l146 = t396;
                            goto gm_lb26;
                        }
gm_lb27:;
                        {
                            uint8_t t397 = GM_LD(uint8_t, (((uint8_t *)t389) + 0));
                            uint8_t t398 = (uint8_t)(t397 == ((uint8_t)1ULL));
                            if (!t398) {
                                goto gm_lb28;
                            }
                            void * t399 = GM_LD(void *, (((uint8_t *)t389) + 8));
                            gm_l40 = t399;
                            void * t400 = gm_l0;
                            void * t401 = gm_l40;
                            gm_t7b7esi3n9r90 t402;
                            gm_f_expr_tp86bm9((void *)&t402, (void *)NULL, cx, NULL, t400, t401);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t9i04n6j2d1gi t403;
                            GM_ST(uint8_t, (uint8_t *)&t403 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t403 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t403 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t403 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t403 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t403 + 16), t402);
                            gm_l146 = t403;
                            goto gm_lb26;
                        }
gm_lb28:;
                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 239, 26);
                    }
gm_lb26:;
                    memmove(((uint8_t *)&F.l41), ((uint8_t *)&gm_l146), 64);
                    gm_tf8kfhcnval78 t404;
                    memset(&t404, 0, sizeof t404);
                    F.l42 = t404;
                    {
                        {
                            void * t405 = gm_l37;
                            uint8_t t406 = (uint8_t)(GM_LD(void *, (((uint8_t *)t405) + 88)) != 0);
                            if (!t406) {
                                goto gm_lb30;
                            }
                            void * t407 = GM_LD(void *, (((uint8_t *)t405) + 88));
                            gm_l43 = t407;
                            void * t408 = gm_l0;
                            void * t409 = gm_l43;
                            gm_t7b7esi3n9r90 t410;
                            gm_f_expr_tp86bm9((void *)&t410, (void *)NULL, cx, NULL, t408, t409);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tf8kfhcnval78 t411;
                            GM_ST(uint8_t, (uint8_t *)&t411 + 49, 0);
                            GM_ST(uint16_t, (uint8_t *)&t411 + 50, 0);
                            GM_ST(uint32_t, (uint8_t *)&t411 + 52, 0);
                            GM_ST(uint64_t, (uint8_t *)&t411 + 56, 0);
                            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t411), t410);
                            ((uint8_t *)&t411)[48] = 1;
                            F.l42 = t411;
                            goto gm_lb29;
                        }
gm_lb30:;
                    }
gm_lb29:;
                    void * t412 = gm_l0;
                    void * t413 = gm_l37;
                    gm_b16_8 t414 = GM_LD(gm_b16_8, (((uint8_t *)t413) + 72));
                    gm_b16_8 t415 = gm_f_exprs_r7m130e((void *)NULL, cx, NULL, t412, t414);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l44 = t415;
                    void * t416 = gm_l0;
                    void * t417 = gm_l37;
                    gm_b16_8 t418 = GM_LD(gm_b16_8, (((uint8_t *)t417) + 40));
                    void * t419 = gm_l37;
                    gm_b16_8 t420 = GM_LD(gm_b16_8, (((uint8_t *)t419) + 56));
                    gm_b16_8 t421 = gm_l44;
                    gm_t9i04n6j2d1gi t422 = GM_LD(gm_t9i04n6j2d1gi, ((uint8_t *)&F.l41));
                    gm_tf8kfhcnval78 t423 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&F.l42));
                    gm_tihh2qes0hpd4 t424;
                    GM_ST(gm_t9i04n6j2d1gi, ((uint8_t *)&t424 + 0), t422);
                    GM_ST(gm_b16_8, ((uint8_t *)&t424 + 64), t418);
                    GM_ST(gm_b16_8, ((uint8_t *)&t424 + 80), t420);
                    GM_ST(gm_b16_8, ((uint8_t *)&t424 + 96), t421);
                    GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t424 + 112), t423);
                    void * t425 = gm_l1;
                    gm_tl874t2atnlh3 t426 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t425) + 72));
                    gm_th7badf4b6ee3 t427;
                    GM_ST(uint8_t, (uint8_t *)&t427 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t427 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t427 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t427 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t427 + 192, 0);
                    GM_ST(uint64_t, (uint8_t *)&t427 + 200, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t427 + 0), ((uint8_t)13ULL));
                    GM_ST(gm_tihh2qes0hpd4, ((uint8_t *)&t427 + 16), t424);
                    gm_b0_1 t428 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t416, &t427, t426);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t429 = gm_l1;
                    gm_t88rcucg1b7uk t430 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t429));
                    gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t430));
                    goto gm_fdone;
                }
gm_lb25:;
            }
            {
                {
                    uint8_t t431 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t432 = (uint8_t)(t431 == ((uint8_t)1ULL));
                    if (!t432) {
                        goto gm_lb31;
                    }
                    double t433 = GM_LD(double, ((((uint8_t *)t107) + 8) + 8));
                    gm_l45 = t433;
                    void * t434 = gm_l0;
                    void * t435 = gm_l1;
                    double t436 = gm_l45;
                    gm_f_float_value_o4b87lc((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t434, t435, t436);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb31:;
                {
                    uint8_t t437 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t438 = (uint8_t)(t437 == ((uint8_t)4ULL));
                    if (!t438) {
                        goto gm_lb32;
                    }
                    uint8_t t439 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 8));
                    gm_l46 = t439;
                    void * t440 = gm_l1;
                    gm_t88rcucg1b7uk t441 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t440));
                    uint8_t t442 = gm_l46;
                    if (t442) {
                        gm_l147 = ((uint64_t)1ULL);
                    } else {
                        gm_l147 = ((uint64_t)0ULL);
                    }
                    uint64_t t443 = gm_l147;
                    gm_tfbnciphf3261 t444;
                    GM_ST(uint32_t, (uint8_t *)&t444 + 4, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t444 + 0), t441);
                    GM_ST(uint64_t, ((uint8_t *)&t444 + 8), t443);
                    gm_t7b7esi3n9r90 t445;
                    GM_ST(uint8_t, (uint8_t *)&t445 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t445 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t445 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t445 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t445 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t445 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t445 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t445 + 16), t444);
                    memmove(gm_out, ((uint8_t *)&t445), 48);
                    goto gm_fdone;
                }
gm_lb32:;
                {
                    uint8_t t446 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t447 = (uint8_t)(t446 == ((uint8_t)7ULL));
                    if (!t447) {
                        goto gm_lb33;
                    }
                    {
                        {
                            void * t448 = gm_l0;
                            void * t449 = gm_l1;
                            gm_t88rcucg1b7uk t450 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t449));
                            gm_f_word_1sfojus((void *)&gm_l148, (void *)NULL, cx, NULL, t448, t450, ((uint64_t)0ULL));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t451 = (uint8_t)((((uint8_t *)&gm_l148))[48] != 0);
                            if (!t451) {
                                goto gm_lb35;
                            }
                            memmove(((uint8_t *)&F.l47), ((uint8_t *)&gm_l148), 48);
                            memmove(gm_out, ((uint8_t *)&F.l47), 48);
                            goto gm_fdone;
                        }
gm_lb35:;
                    }
gm_lb34:;
                    void * t452 = gm_l0;
                    void * t453 = gm_l1;
                    gm_t88rcucg1b7uk t454 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t453));
                    uint32_t t455 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t452, t454);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l48 = t455;
                    void * t456 = gm_l0;
                    uint32_t t457 = gm_l48;
                    gm_tf8kfhcnval78 t458;
                    memset(&t458, 0, sizeof t458);
                    gm_t9qkh0rsooc8d t459;
                    GM_ST(uint8_t, ((uint8_t *)&t459 + 0), ((uint8_t)0ULL));
                    gm_tg85uffmq8b38 t460;
                    GM_ST(uint8_t, (uint8_t *)&t460 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t460 + 6, 0);
                    GM_ST(uint64_t, (uint8_t *)&t460 + 8, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t460 + 0), t457);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t460 + 4), t459);
                    GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t460 + 16), t458);
                    void * t461 = gm_l1;
                    gm_tl874t2atnlh3 t462 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t461) + 72));
                    gm_th7badf4b6ee3 t463;
                    memset(&t463, 0, sizeof t463);
                    GM_ST(uint8_t, ((uint8_t *)&t463 + 0), ((uint8_t)15ULL));
                    GM_ST(gm_tg85uffmq8b38, ((uint8_t *)&t463 + 16), t460);
                    gm_b0_1 t464 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t456, &t463, t462);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t465 = gm_l48;
                    gm_t7b7esi3n9r90 t466;
                    memset(&t466, 0, sizeof t466);
                    GM_ST(uint8_t, ((uint8_t *)&t466 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t466 + 16), t465);
                    memmove(gm_out, ((uint8_t *)&t466), 48);
                    goto gm_fdone;
                }
gm_lb33:;
                {
                    {
                        {
                            uint8_t t467 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t468 = (uint8_t)(t467 == ((uint8_t)15ULL));
                            if (!t468) {
                                goto gm_lb38;
                            }
                            goto gm_lb37;
                        }
gm_lb38:;
                        {
                            uint8_t t469 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t470 = (uint8_t)(t469 == ((uint8_t)16ULL));
                            if (!t470) {
                                goto gm_lb39;
                            }
                            goto gm_lb37;
                        }
gm_lb39:;
                        {
                            uint8_t t471 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t472 = (uint8_t)(t471 == ((uint8_t)17ULL));
                            if (!t472) {
                                goto gm_lb40;
                            }
                            goto gm_lb37;
                        }
gm_lb40:;
                        {
                            uint8_t t473 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t474 = (uint8_t)(t473 == ((uint8_t)14ULL));
                            if (!t474) {
                                goto gm_lb41;
                            }
                            goto gm_lb37;
                        }
gm_lb41:;
                        {
                            uint8_t t475 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t476 = (uint8_t)(t475 == ((uint8_t)18ULL));
                            if (!t476) {
                                goto gm_lb42;
                            }
                            goto gm_lb37;
                        }
gm_lb42:;
                        {
                            uint8_t t477 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t478 = (uint8_t)(t477 == ((uint8_t)19ULL));
                            if (!t478) {
                                goto gm_lb43;
                            }
                            goto gm_lb37;
                        }
gm_lb43:;
                        {
                            uint8_t t479 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t480 = (uint8_t)(t479 == ((uint8_t)20ULL));
                            if (!t480) {
                                goto gm_lb44;
                            }
                            goto gm_lb37;
                        }
gm_lb44:;
                        goto gm_lb36;
                    }
gm_lb37:;
                    {
                        {
                            void * t481 = gm_l1;
                            uint8_t t482 = GM_LD(uint8_t, ((((uint8_t *)t481) + 8) + 0));
                            uint8_t t483 = (uint8_t)(t482 == ((uint8_t)19ULL));
                            if (!t483) {
                                goto gm_lb46;
                            }
                            void * t484 = GM_LD(void *, ((((uint8_t *)t481) + 8) + 8));
                            gm_l49 = t484;
                            uint32_t t485 = GM_LD(uint32_t, (((((uint8_t *)t481) + 8) + 8) + 8));
                            gm_l50 = t485;
                            void * t486 = gm_l0;
                            void * t487 = gm_l49;
                            gm_t88rcucg1b7uk t488 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t487));
                            uint8_t t489 = gm_f_is_bits_pbijkuf(t486, GM_CONV(gm_csguankko4lmhd, t488));
                            if (!t489) {
                                goto gm_lb46;
                            }
                            void * t490 = gm_l0;
                            void * t491 = gm_l49;
                            gm_f_expr_tp86bm9((void *)&F.l51, (void *)NULL, cx, NULL, t490, t491);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t492 = gm_l0;
                            void * t493 = gm_l1;
                            gm_t88rcucg1b7uk t494 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t493));
                            uint32_t t495 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t492, t494);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l52 = t495;
                            void * t496 = gm_l0;
                            uint32_t t497 = gm_l52;
                            uint32_t t498 = gm_l50;
                            gm_t7b7esi3n9r90 t499 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l51));
                            gm_tcpisgbff1dqv t500;
                            GM_ST(uint32_t, (uint8_t *)&t500 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t500 + 8, 0);
                            GM_ST(uint32_t, (uint8_t *)&t500 + 68, 0);
                            GM_ST(uint64_t, (uint8_t *)&t500 + 72, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t500 + 0), t497);
                            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t500 + 16), t499);
                            GM_ST(uint32_t, ((uint8_t *)&t500 + 64), t498);
                            void * t501 = gm_l1;
                            gm_tl874t2atnlh3 t502 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t501) + 72));
                            gm_th7badf4b6ee3 t503;
                            memset(&t503, 0, sizeof t503);
                            GM_ST(uint8_t, ((uint8_t *)&t503 + 0), ((uint8_t)21ULL));
                            GM_ST(gm_tcpisgbff1dqv, ((uint8_t *)&t503 + 16), t500);
                            gm_b0_1 t504 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t496, &t503, t502);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint32_t t505 = gm_l52;
                            gm_t7b7esi3n9r90 t506;
                            memset(&t506, 0, sizeof t506);
                            GM_ST(uint8_t, ((uint8_t *)&t506 + 0), ((uint8_t)0ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t506 + 16), t505);
                            memmove(gm_out, ((uint8_t *)&t506), 48);
                            goto gm_fdone;
                        }
gm_lb46:;
                    }
gm_lb45:;
                    void * t507 = gm_l0;
                    void * t508 = gm_l1;
                    gm_f_place_oohr45n((void *)&gm_l149, (void *)NULL, cx, NULL, t507, t508);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t509 = (uint8_t)((((uint8_t *)&gm_l149))[56] != 0);
                    if (t509) {
                        memmove(((uint8_t *)&gm_l150), ((uint8_t *)&gm_l149), 56);
                    } else {
                        void * t510 = gm_l0;
                        void * t511 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t510, t511);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&F.l53), ((uint8_t *)&gm_l150), 56);
                    void * t512 = gm_l2;
                    void * t513 = gm_l1;
                    gm_t88rcucg1b7uk t514 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t513));
                    gm_f_layout_a5ovnrm((void *)&gm_l151, (void *)NULL, cx, NULL, t512, t514);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t515 = GM_LD(uint64_t, ((uint8_t *)&gm_l151));
                    uint8_t t516 = ((t515 == ((uint64_t)0ULL)));
                    if (t516) {
                        void * t517 = gm_l1;
                        gm_t88rcucg1b7uk t518 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t517));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t518));
                        goto gm_fdone;
                    }
                    void * t519 = gm_l0;
                    void * t520 = gm_l1;
                    gm_t88rcucg1b7uk t521 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t520));
                    void * t522 = gm_l1;
                    gm_tl874t2atnlh3 t523 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t522) + 72));
                    gm_tlg23kb3oj23s t524 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l53));
                    gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t519, &t524, t521, t523);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb36:;
            }
            {
                {
                    uint8_t t525 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t526 = (uint8_t)(t525 == ((uint8_t)22ULL));
                    if (!t526) {
                        goto gm_lb47;
                    }
                    void * t527 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l54 = t527;
                    void * t528 = gm_l0;
                    void * t529 = gm_l54;
                    gm_f_operand_place_cdle4ls((void *)&gm_l152, (void *)NULL, cx, NULL, t528, t529);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t530 = (uint8_t)((((uint8_t *)&gm_l152))[56] != 0);
                    if (t530) {
                        memmove(((uint8_t *)&gm_l153), ((uint8_t *)&gm_l152), 56);
                    } else {
                        void * t531 = gm_l0;
                        void * t532 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t531, t532);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&F.l55), ((uint8_t *)&gm_l153), 56);
                    void * t533 = gm_l0;
                    void * t534 = gm_l0;
                    gm_tlg23kb3oj23s t535 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l55));
                    uint64_t t536 = gm_f_ptr_size_lig81gu((void *)NULL, cx, NULL, t534);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tb31k5l794t7p t537;
                    GM_ST(uint32_t, (uint8_t *)&t537 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t537 + 0), ((uint32_t)1ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t537 + 8), t536);
                    gm_tfqmtuprvunod t538;
                    memset(&t538, 0, sizeof t538);
                    GM_ST(uint8_t, ((uint8_t *)&t538 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t538 + 16), t537);
                    gm_tlg23kb3oj23s t539;
                    gm_f_project_6hpd48e((void *)&t539, (void *)NULL, cx, NULL, &t535, &t538);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t540 = gm_l1;
                    gm_t88rcucg1b7uk t541 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t540));
                    void * t542 = gm_l1;
                    gm_tl874t2atnlh3 t543 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t542) + 72));
                    gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t533, &t539, t541, t543);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb47:;
                {
                    uint8_t t544 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t545 = (uint8_t)(t544 == ((uint8_t)23ULL));
                    if (!t545) {
                        goto gm_lb48;
                    }
                    void * t546 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l56 = t546;
                    void * t547 = gm_l0;
                    void * t548 = gm_l56;
                    gm_f_operand_place_cdle4ls((void *)&gm_l154, (void *)NULL, cx, NULL, t547, t548);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t549 = (uint8_t)((((uint8_t *)&gm_l154))[56] != 0);
                    if (t549) {
                        memmove(((uint8_t *)&gm_l155), ((uint8_t *)&gm_l154), 56);
                    } else {
                        void * t550 = gm_l0;
                        void * t551 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t550, t551);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&F.l57), ((uint8_t *)&gm_l155), 56);
                    void * t552 = gm_l0;
                    gm_tb31k5l794t7p t553;
                    GM_ST(uint32_t, (uint8_t *)&t553 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t553 + 0), ((uint32_t)0ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t553 + 8), ((uint64_t)0ULL));
                    gm_tfqmtuprvunod t554;
                    memset(&t554, 0, sizeof t554);
                    GM_ST(uint8_t, ((uint8_t *)&t554 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t554 + 16), t553);
                    gm_tlg23kb3oj23s t555;
                    gm_f_project_6hpd48e((void *)&t555, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l57)), &t554);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t556 = gm_l1;
                    gm_t88rcucg1b7uk t557 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t556));
                    void * t558 = gm_l1;
                    gm_tl874t2atnlh3 t559 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t558) + 72));
                    gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t552, &t555, t557, t559);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb48:;
                {
                    uint8_t t560 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t561 = (uint8_t)(t560 == ((uint8_t)24ULL));
                    if (!t561) {
                        goto gm_lb49;
                    }
                    void * t562 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l58 = t562;
                    void * t563 = gm_l0;
                    void * t564 = gm_l58;
                    gm_f_place_oohr45n((void *)&gm_l156, (void *)NULL, cx, NULL, t563, t564);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t565 = (uint8_t)((((uint8_t *)&gm_l156))[56] != 0);
                    if (t565) {
                        memmove(((uint8_t *)&gm_l157), ((uint8_t *)&gm_l156), 56);
                    } else {
                        void * t566 = gm_l0;
                        void * t567 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t566, t567);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&F.l59), ((uint8_t *)&gm_l157), 56);
                    void * t568 = gm_l0;
                    void * t569 = (void *)((uint8_t *)&F.l59);
                    gm_f_mark_addressed_t8qojin(t568, t569);
                    void * t570 = gm_l0;
                    void * t571 = gm_l1;
                    gm_t88rcucg1b7uk t572 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t571));
                    uint32_t t573 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t570, t572);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l60 = t573;
                    void * t574 = gm_l0;
                    uint32_t t575 = gm_l60;
                    gm_tlg23kb3oj23s t576 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l59));
                    gm_tlpcrb3vu6o2d t577;
                    GM_ST(uint32_t, (uint8_t *)&t577 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t577 + 0), t575);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t577 + 8), t576);
                    void * t578 = gm_l1;
                    gm_tl874t2atnlh3 t579 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t578) + 72));
                    gm_th7badf4b6ee3 t580;
                    memset(&t580, 0, sizeof t580);
                    GM_ST(uint8_t, ((uint8_t *)&t580 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_tlpcrb3vu6o2d, ((uint8_t *)&t580 + 16), t577);
                    gm_b0_1 t581 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t574, &t580, t579);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t582 = gm_l60;
                    gm_t7b7esi3n9r90 t583;
                    memset(&t583, 0, sizeof t583);
                    GM_ST(uint8_t, ((uint8_t *)&t583 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t583 + 16), t582);
                    memmove(gm_out, ((uint8_t *)&t583), 48);
                    goto gm_fdone;
                }
gm_lb49:;
            }
            {
                {
                    uint8_t t584 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t585 = (uint8_t)(t584 == ((uint8_t)26ULL));
                    if (!t585) {
                        goto gm_lb50;
                    }
                    uint32_t t586 = GM_LD(uint32_t, ((((uint8_t *)t107) + 8) + 8));
                    gm_l61 = t586;
                    void * t587 = gm_l0;
                    void * t588 = gm_l1;
                    gm_t88rcucg1b7uk t589 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t588));
                    uint32_t t590 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t587, t589);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l62 = t590;
                    void * t591 = gm_l0;
                    uint32_t t592 = gm_l62;
                    uint32_t t593 = gm_l61;
                    void * t594 = gm_l2;
                    void * t595 = (void *)(((uint8_t *)t594) + 8);
                    void * t596 = gm_l1;
                    gm_t88rcucg1b7uk t597 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t596));
                    gm_f_kind_jcnt4hm((void *)&gm_l159, t595, GM_CONV(gm_csguankko4lmhd, t597));
                    {
                        {
                            uint8_t t598 = GM_LD(uint8_t, (((uint8_t *)&gm_l159) + 0));
                            uint8_t t599 = (uint8_t)(t598 == ((uint8_t)15ULL));
                            if (!t599) {
                                goto gm_lb52;
                            }
                            gm_l158 = ((uint8_t)1);
                            goto gm_lb51;
                        }
gm_lb52:;
                        {
                            gm_l158 = ((uint8_t)0);
                            goto gm_lb51;
                        }
gm_lb53:;
                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 285, 34);
                    }
gm_lb51:;
                    uint8_t t600 = gm_l158;
                    gm_tilqfqdpj446h t601;
                    GM_ST(uint8_t, (uint8_t *)&t601 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t601 + 10, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t601 + 0), t592);
                    GM_ST(uint32_t, ((uint8_t *)&t601 + 4), t593);
                    GM_ST(uint8_t, ((uint8_t *)&t601 + 8), t600);
                    void * t602 = gm_l1;
                    gm_tl874t2atnlh3 t603 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t602) + 72));
                    gm_th7badf4b6ee3 t604;
                    memset(&t604, 0, sizeof t604);
                    GM_ST(uint8_t, ((uint8_t *)&t604 + 0), ((uint8_t)16ULL));
                    GM_ST(gm_tilqfqdpj446h, ((uint8_t *)&t604 + 16), t601);
                    gm_b0_1 t605 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t591, &t604, t603);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t606 = gm_l62;
                    gm_t7b7esi3n9r90 t607;
                    memset(&t607, 0, sizeof t607);
                    GM_ST(uint8_t, ((uint8_t *)&t607 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t607 + 16), t606);
                    memmove(gm_out, ((uint8_t *)&t607), 48);
                    goto gm_fdone;
                }
gm_lb50:;
                {
                    uint8_t t608 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t609 = (uint8_t)(t608 == ((uint8_t)27ULL));
                    if (!t609) {
                        goto gm_lb54;
                    }
                    uint32_t t610 = GM_LD(uint32_t, ((((uint8_t *)t107) + 8) + 8));
                    gm_l63 = t610;
                    void * t611 = gm_l0;
                    void * t612 = gm_l1;
                    gm_t88rcucg1b7uk t613 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t612));
                    uint32_t t614 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t611, t613);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l64 = t614;
                    void * t615 = gm_l0;
                    uint32_t t616 = gm_l64;
                    uint32_t t617 = gm_l63;
                    gm_tcc1rq4n1avb2 t618;
                    GM_ST(uint32_t, ((uint8_t *)&t618 + 0), t616);
                    GM_ST(uint32_t, ((uint8_t *)&t618 + 4), t617);
                    void * t619 = gm_l1;
                    gm_tl874t2atnlh3 t620 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t619) + 72));
                    gm_th7badf4b6ee3 t621;
                    memset(&t621, 0, sizeof t621);
                    GM_ST(uint8_t, ((uint8_t *)&t621 + 0), ((uint8_t)17ULL));
                    GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t621 + 16), t618);
                    gm_b0_1 t622 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t615, &t621, t620);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t623 = gm_l64;
                    gm_t7b7esi3n9r90 t624;
                    memset(&t624, 0, sizeof t624);
                    GM_ST(uint8_t, ((uint8_t *)&t624 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t624 + 16), t623);
                    memmove(gm_out, ((uint8_t *)&t624), 48);
                    goto gm_fdone;
                }
gm_lb54:;
                {
                    uint8_t t625 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t626 = (uint8_t)(t625 == ((uint8_t)28ULL));
                    if (!t626) {
                        goto gm_lb55;
                    }
                    uint32_t t627 = GM_LD(uint32_t, ((((uint8_t *)t107) + 8) + 8));
                    gm_l65 = t627;
                    void * t628 = gm_l0;
                    void * t629 = gm_l1;
                    gm_t88rcucg1b7uk t630 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t629));
                    uint32_t t631 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t628, t630);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l66 = t631;
                    void * t632 = gm_l0;
                    uint32_t t633 = gm_l66;
                    uint32_t t634 = gm_l65;
                    gm_tcc1rq4n1avb2 t635;
                    GM_ST(uint32_t, ((uint8_t *)&t635 + 0), t633);
                    GM_ST(uint32_t, ((uint8_t *)&t635 + 4), t634);
                    void * t636 = gm_l1;
                    gm_tl874t2atnlh3 t637 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t636) + 72));
                    gm_th7badf4b6ee3 t638;
                    memset(&t638, 0, sizeof t638);
                    GM_ST(uint8_t, ((uint8_t *)&t638 + 0), ((uint8_t)18ULL));
                    GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t638 + 16), t635);
                    gm_b0_1 t639 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t632, &t638, t637);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t640 = gm_l66;
                    gm_t7b7esi3n9r90 t641;
                    memset(&t641, 0, sizeof t641);
                    GM_ST(uint8_t, ((uint8_t *)&t641 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t641 + 16), t640);
                    memmove(gm_out, ((uint8_t *)&t641), 48);
                    goto gm_fdone;
                }
gm_lb55:;
            }
            {
                {
                    uint8_t t642 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t643 = (uint8_t)(t642 == ((uint8_t)29ULL));
                    if (!t643) {
                        goto gm_lb56;
                    }
                    gm_t9qkh0rsooc8d t644 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t107) + 8) + 8));
                    gm_l67 = t644;
                    void * t645 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l68 = t645;
                    void * t646 = gm_l0;
                    void * t647 = gm_l68;
                    gm_f_expr_tp86bm9((void *)&F.l69, (void *)NULL, cx, NULL, t646, t647);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t648 = gm_l0;
                    void * t649 = gm_l1;
                    gm_t88rcucg1b7uk t650 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t649));
                    uint32_t t651 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t648, t650);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l70 = t651;
                    void * t652 = gm_l0;
                    uint32_t t653 = gm_l70;
                    gm_t9qkh0rsooc8d t654 = gm_l67;
                    gm_t7b7esi3n9r90 t655 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l69));
                    gm_tbto4k5jg9fve t656;
                    GM_ST(uint8_t, (uint8_t *)&t656 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t656 + 6, 0);
                    GM_ST(uint64_t, (uint8_t *)&t656 + 8, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t656 + 0), t653);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t656 + 4), t654);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t656 + 16), t655);
                    void * t657 = gm_l1;
                    gm_tl874t2atnlh3 t658 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t657) + 72));
                    gm_th7badf4b6ee3 t659;
                    memset(&t659, 0, sizeof t659);
                    GM_ST(uint8_t, ((uint8_t *)&t659 + 0), ((uint8_t)4ULL));
                    GM_ST(gm_tbto4k5jg9fve, ((uint8_t *)&t659 + 16), t656);
                    gm_b0_1 t660 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t652, &t659, t658);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t661 = gm_l70;
                    gm_t7b7esi3n9r90 t662;
                    memset(&t662, 0, sizeof t662);
                    GM_ST(uint8_t, ((uint8_t *)&t662 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t662 + 16), t661);
                    memmove(gm_out, ((uint8_t *)&t662), 48);
                    goto gm_fdone;
                }
gm_lb56:;
                {
                    uint8_t t663 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t664 = (uint8_t)(t663 == ((uint8_t)30ULL));
                    if (!t664) {
                        goto gm_lb57;
                    }
                    gm_ts6oqppjh0ba3 t665 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t107) + 8) + 8));
                    gm_l71 = t665;
                    void * t666 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l72 = t666;
                    void * t667 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 16));
                    gm_l73 = t667;
                    void * t668 = gm_l0;
                    void * t669 = gm_l72;
                    gm_f_expr_tp86bm9((void *)&F.l74, (void *)NULL, cx, NULL, t668, t669);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t670 = gm_l0;
                    void * t671 = gm_l73;
                    gm_f_expr_tp86bm9((void *)&F.l75, (void *)NULL, cx, NULL, t670, t671);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t672 = gm_l0;
                    void * t673 = gm_l1;
                    gm_t88rcucg1b7uk t674 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t673));
                    uint32_t t675 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t672, t674);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l76 = t675;
                    void * t676 = gm_l0;
                    uint32_t t677 = gm_l76;
                    gm_ts6oqppjh0ba3 t678 = gm_l71;
                    gm_t7b7esi3n9r90 t679 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l74));
                    gm_t7b7esi3n9r90 t680 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l75));
                    gm_t1ctgp8j1v2as t681;
                    GM_ST(uint16_t, (uint8_t *)&t681 + 6, 0);
                    GM_ST(uint64_t, (uint8_t *)&t681 + 8, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t681 + 0), t677);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t681 + 4), t678);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t681 + 16), t679);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t681 + 64), t680);
                    void * t682 = gm_l1;
                    gm_tl874t2atnlh3 t683 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t682) + 72));
                    gm_th7badf4b6ee3 t684;
                    memset(&t684, 0, sizeof t684);
                    GM_ST(uint8_t, ((uint8_t *)&t684 + 0), ((uint8_t)3ULL));
                    GM_ST(gm_t1ctgp8j1v2as, ((uint8_t *)&t684 + 16), t681);
                    gm_b0_1 t685 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t676, &t684, t683);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t686 = gm_l76;
                    gm_t7b7esi3n9r90 t687;
                    memset(&t687, 0, sizeof t687);
                    GM_ST(uint8_t, ((uint8_t *)&t687 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t687 + 16), t686);
                    memmove(gm_out, ((uint8_t *)&t687), 48);
                    goto gm_fdone;
                }
gm_lb57:;
                {
                    uint8_t t688 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t689 = (uint8_t)(t688 == ((uint8_t)43ULL));
                    if (!t689) {
                        goto gm_lb58;
                    }
                    void * t690 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l77 = t690;
                    void * t691 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l78 = t691;
                    uint8_t t692 = GM_LD(uint8_t, (((((uint8_t *)t107) + 8) + 8) + 16));
                    gm_l79 = t692;
                    void * t693 = gm_l0;
                    void * t694 = gm_l77;
                    gm_f_expr_tp86bm9((void *)&F.l80, (void *)NULL, cx, NULL, t693, t694);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t695 = gm_l0;
                    void * t696 = gm_l78;
                    gm_f_expr_tp86bm9((void *)&F.l81, (void *)NULL, cx, NULL, t695, t696);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t697 = gm_l0;
                    void * t698 = gm_l1;
                    gm_t88rcucg1b7uk t699 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t698));
                    uint32_t t700 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t697, t699);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l82 = t700;
                    void * t701 = gm_l0;
                    uint32_t t702 = gm_l82;
                    uint8_t t703 = gm_l79;
                    gm_t7b7esi3n9r90 t704 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l80));
                    gm_t7b7esi3n9r90 t705 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l81));
                    gm_tr6ll1dg9imrl t706;
                    GM_ST(uint8_t, (uint8_t *)&t706 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t706 + 6, 0);
                    GM_ST(uint64_t, (uint8_t *)&t706 + 8, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t706 + 0), t702);
                    GM_ST(uint8_t, ((uint8_t *)&t706 + 4), t703);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t706 + 16), t704);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t706 + 64), t705);
                    void * t707 = gm_l1;
                    gm_tl874t2atnlh3 t708 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t707) + 72));
                    gm_th7badf4b6ee3 t709;
                    memset(&t709, 0, sizeof t709);
                    GM_ST(uint8_t, ((uint8_t *)&t709 + 0), ((uint8_t)5ULL));
                    GM_ST(gm_tr6ll1dg9imrl, ((uint8_t *)&t709 + 16), t706);
                    gm_b0_1 t710 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t701, &t709, t708);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t711 = gm_l82;
                    gm_t7b7esi3n9r90 t712;
                    memset(&t712, 0, sizeof t712);
                    GM_ST(uint8_t, ((uint8_t *)&t712 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t712 + 16), t711);
                    memmove(gm_out, ((uint8_t *)&t712), 48);
                    goto gm_fdone;
                }
gm_lb58:;
            }
            {
                {
                    uint8_t t713 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t714 = (uint8_t)(t713 == ((uint8_t)31ULL));
                    if (!t714) {
                        goto gm_lb59;
                    }
                    uint8_t t715 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 8));
                    gm_l83 = t715;
                    void * t716 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l84 = t716;
                    void * t717 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 16));
                    gm_l85 = t717;
                    void * t718 = gm_l0;
                    void * t719 = gm_l1;
                    uint8_t t720 = gm_l83;
                    void * t721 = gm_l84;
                    void * t722 = gm_l85;
                    gm_f_logic_9o6bp90((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t718, t719, t720, t721, t722);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb59:;
                {
                    uint8_t t723 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t724 = (uint8_t)(t723 == ((uint8_t)32ULL));
                    if (!t724) {
                        goto gm_lb60;
                    }
                    gm_tmn7n0n9raou5 t725 = GM_LD(gm_tmn7n0n9raou5, ((((uint8_t *)t107) + 8) + 8));
                    gm_l86 = t725;
                    void * t726 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l87 = t726;
                    gm_tmn7n0n9raou5 t727 = gm_l86;
                    uint8_t t728 = GM_LD(uint8_t, (((uint8_t *)&t727) + 0));
                    uint8_t t729 = (uint8_t)((t728 == ((uint8_t)12ULL)));
                    gm_tmn7n0n9raou5 t730;
                    GM_ST(uint8_t, (uint8_t *)&t730 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t730 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t730 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t730 + 0), ((uint8_t)12ULL));
                    if (t729) {
                        void * t731 = gm_l0;
                        void * t732 = gm_l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t731, t732);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    void * t733 = gm_l0;
                    void * t734 = gm_l87;
                    gm_f_expr_tp86bm9((void *)&F.l88, (void *)NULL, cx, NULL, t733, t734);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tmn7n0n9raou5 t735 = gm_l86;
                    uint8_t t736 = GM_LD(uint8_t, (((uint8_t *)&t735) + 0));
                    uint8_t t737 = (uint8_t)((t736 == ((uint8_t)5ULL)));
                    gm_tmn7n0n9raou5 t738;
                    GM_ST(uint8_t, (uint8_t *)&t738 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t738 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t738 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t738 + 0), ((uint8_t)5ULL));
                    if (t737) {
                        void * t739 = gm_l1;
                        gm_t88rcucg1b7uk t740 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t739));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t740));
                        goto gm_fdone;
                    }
                    void * t741 = gm_l0;
                    void * t742 = gm_l1;
                    gm_t88rcucg1b7uk t743 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t742));
                    uint32_t t744 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t741, t743);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l89 = t744;
                    void * t745 = gm_l0;
                    uint32_t t746 = gm_l89;
                    gm_tmn7n0n9raou5 t747 = gm_l86;
                    gm_t7b7esi3n9r90 t748 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l88));
                    gm_t5hdm76ui2uro t749;
                    GM_ST(uint32_t, (uint8_t *)&t749 + 12, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t749 + 0), t746);
                    GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t749 + 4), t747);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t749 + 16), t748);
                    void * t750 = gm_l1;
                    gm_tl874t2atnlh3 t751 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t750) + 72));
                    gm_th7badf4b6ee3 t752;
                    memset(&t752, 0, sizeof t752);
                    GM_ST(uint8_t, ((uint8_t *)&t752 + 0), ((uint8_t)6ULL));
                    GM_ST(gm_t5hdm76ui2uro, ((uint8_t *)&t752 + 16), t749);
                    gm_b0_1 t753 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t745, &t752, t751);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t754 = gm_l89;
                    gm_t7b7esi3n9r90 t755;
                    memset(&t755, 0, sizeof t755);
                    GM_ST(uint8_t, ((uint8_t *)&t755 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t755 + 16), t754);
                    memmove(gm_out, ((uint8_t *)&t755), 48);
                    goto gm_fdone;
                }
gm_lb60:;
                {
                    uint8_t t756 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t757 = (uint8_t)(t756 == ((uint8_t)33ULL));
                    if (!t757) {
                        goto gm_lb61;
                    }
                    gm_tvdg94bk7aa22 t758 = GM_LD(gm_tvdg94bk7aa22, ((((uint8_t *)t107) + 8) + 8));
                    gm_l90 = t758;
                    gm_b16_8 t759 = GM_LD(gm_b16_8, (((((uint8_t *)t107) + 8) + 8) + 16));
                    gm_l91 = t759;
                    {
                        {
                            uint8_t t760 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                            uint8_t t761 = (uint8_t)(t760 == ((uint8_t)61ULL));
                            if (!t761) {
                                goto gm_lb63;
                            }
                            void * t762 = gm_l0;
                            void * t763 = gm_l1;
                            gm_b16_8 t764 = gm_l91;
                            gm_b0_1 t765 = gm_f_assert__nol4kp3((void *)NULL, cx, NULL, t762, t763, t764);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t766 = gm_l1;
                            gm_t88rcucg1b7uk t767 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t766));
                            gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t767));
                            goto gm_fdone;
                        }
gm_lb63:;
                        {
                            uint8_t t768 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                            uint8_t t769 = (uint8_t)(t768 == ((uint8_t)80ULL));
                            if (!t769) {
                                goto gm_lb64;
                            }
                            void * t770 = gm_l1;
                            gm_t88rcucg1b7uk t771 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t770));
                            gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t771));
                            goto gm_fdone;
                        }
gm_lb64:;
                        {
                            {
                                {
                                    uint8_t t772 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                                    uint8_t t773 = (uint8_t)(t772 == ((uint8_t)36ULL));
                                    if (!t773) {
                                        goto gm_lb67;
                                    }
                                    goto gm_lb66;
                                }
gm_lb67:;
                                {
                                    uint8_t t774 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                                    uint8_t t775 = (uint8_t)(t774 == ((uint8_t)37ULL));
                                    if (!t775) {
                                        goto gm_lb68;
                                    }
                                    goto gm_lb66;
                                }
gm_lb68:;
                                {
                                    uint8_t t776 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                                    uint8_t t777 = (uint8_t)(t776 == ((uint8_t)38ULL));
                                    if (!t777) {
                                        goto gm_lb69;
                                    }
                                    goto gm_lb66;
                                }
gm_lb69:;
                                {
                                    uint8_t t778 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                                    uint8_t t779 = (uint8_t)(t778 == ((uint8_t)39ULL));
                                    if (!t779) {
                                        goto gm_lb70;
                                    }
                                    goto gm_lb66;
                                }
gm_lb70:;
                                goto gm_lb65;
                            }
gm_lb66:;
                            void * t780 = gm_l0;
                            void * t781 = gm_l1;
                            gm_tvdg94bk7aa22 t782 = gm_l90;
                            gm_b16_8 t783 = gm_l91;
                            gm_f_get_d0bmskj((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t780, t781, t782, t783);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
gm_lb65:;
                        {
                            goto gm_lb62;
                        }
gm_lb71:;
                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 328, 13);
                    }
gm_lb62:;
                    void * t784 = gm_l0;
                    gm_b16_8 t785 = gm_l91;
                    gm_b16_8 t786 = gm_f_exprs_r7m130e((void *)NULL, cx, NULL, t784, t785);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l92 = t786;
                    void * t787 = gm_l2;
                    void * t788 = gm_l1;
                    gm_t88rcucg1b7uk t789 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t788));
                    gm_f_layout_a5ovnrm((void *)&gm_l161, (void *)NULL, cx, NULL, t787, t789);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t790 = GM_LD(uint64_t, ((uint8_t *)&gm_l161));
                    uint8_t t791 = ((t790 == ((uint64_t)0ULL)));
                    if (t791) {
                        gm_l160 = ((uint8_t)1);
                    } else {
                        void * t792 = gm_l0;
                        void * t793 = gm_l1;
                        uint8_t t794 = gm_f_diverges_p3okmol(t792, t793);
                        gm_l160 = t794;
                    }
                    uint8_t t795 = gm_l160;
                    if (t795) {
                        void * t796 = gm_l0;
                        gm_t9805qtonbsag t797;
                        memset(&t797, 0, sizeof t797);
                        gm_tvdg94bk7aa22 t798 = gm_l90;
                        gm_b16_8 t799 = gm_l92;
                        gm_th622ku78unet t800;
                        GM_ST(uint32_t, (uint8_t *)&t800 + 20, 0);
                        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t800 + 0), t797);
                        GM_ST(gm_tvdg94bk7aa22, ((uint8_t *)&t800 + 8), t798);
                        GM_ST(gm_b16_8, ((uint8_t *)&t800 + 24), t799);
                        void * t801 = gm_l1;
                        gm_tl874t2atnlh3 t802 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t801) + 72));
                        gm_th7badf4b6ee3 t803;
                        memset(&t803, 0, sizeof t803);
                        GM_ST(uint8_t, ((uint8_t *)&t803 + 0), ((uint8_t)7ULL));
                        GM_ST(gm_th622ku78unet, ((uint8_t *)&t803 + 16), t800);
                        gm_b0_1 t804 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t796, &t803, t802);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t805 = gm_l1;
                        gm_t88rcucg1b7uk t806 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t805));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t806));
                        goto gm_fdone;
                    }
                    void * t807 = gm_l0;
                    void * t808 = gm_l1;
                    gm_t88rcucg1b7uk t809 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t808));
                    uint32_t t810 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t807, t809);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l93 = t810;
                    void * t811 = gm_l0;
                    uint32_t t812 = gm_l93;
                    gm_t9805qtonbsag t813;
                    GM_ST(uint8_t, (uint8_t *)&t813 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t813 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t813), t812);
                    ((uint8_t *)&t813)[4] = 1;
                    gm_tvdg94bk7aa22 t814 = gm_l90;
                    gm_b16_8 t815 = gm_l92;
                    gm_th622ku78unet t816;
                    GM_ST(uint32_t, (uint8_t *)&t816 + 20, 0);
                    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t816 + 0), t813);
                    GM_ST(gm_tvdg94bk7aa22, ((uint8_t *)&t816 + 8), t814);
                    GM_ST(gm_b16_8, ((uint8_t *)&t816 + 24), t815);
                    void * t817 = gm_l1;
                    gm_tl874t2atnlh3 t818 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t817) + 72));
                    gm_th7badf4b6ee3 t819;
                    memset(&t819, 0, sizeof t819);
                    GM_ST(uint8_t, ((uint8_t *)&t819 + 0), ((uint8_t)7ULL));
                    GM_ST(gm_th622ku78unet, ((uint8_t *)&t819 + 16), t816);
                    gm_b0_1 t820 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t811, &t819, t818);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t821 = gm_l93;
                    gm_t7b7esi3n9r90 t822;
                    memset(&t822, 0, sizeof t822);
                    GM_ST(uint8_t, ((uint8_t *)&t822 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t822 + 16), t821);
                    memmove(gm_out, ((uint8_t *)&t822), 48);
                    goto gm_fdone;
                }
gm_lb61:;
            }
            {
                {
                    uint8_t t823 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t824 = (uint8_t)(t823 == ((uint8_t)34ULL));
                    if (!t824) {
                        goto gm_lb72;
                    }
                    memmove(((uint8_t *)&F.l94), ((((uint8_t *)t107) + 8) + 8), 40);
                    gm_b16_8 t825 = GM_LD(gm_b16_8, (((((uint8_t *)t107) + 8) + 8) + 40));
                    gm_l95 = t825;
                    {
                        {
                            uint8_t t826 = GM_LD(uint8_t, (((uint8_t *)&F.l94) + 0));
                            uint8_t t827 = (uint8_t)(t826 == ((uint8_t)0ULL));
                            if (!t827) {
                                goto gm_lb74;
                            }
                            uint32_t t828 = GM_LD(uint32_t, (((uint8_t *)&F.l94) + 8));
                            gm_l96 = t828;
                            uint32_t t829 = gm_l96;
                            gm_tllthq21h2f0k t830;
                            memset(&t830, 0, sizeof t830);
                            GM_ST(uint8_t, ((uint8_t *)&t830 + 0), ((uint8_t)0ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t830 + 16), t829);
                            gm_l162 = t830;
                            goto gm_lb73;
                        }
gm_lb74:;
                        {
                            uint8_t t831 = GM_LD(uint8_t, (((uint8_t *)&F.l94) + 0));
                            uint8_t t832 = (uint8_t)(t831 == ((uint8_t)1ULL));
                            if (!t832) {
                                goto gm_lb75;
                            }
                            memmove(((uint8_t *)&gm_l97), (((uint8_t *)&F.l94) + 8), 24);
                            uint32_t t833 = GM_LD(uint32_t, ((((uint8_t *)&F.l94) + 8) + 24));
                            gm_l98 = t833;
                            uint32_t t834 = gm_l98;
                            gm_tiilmfd16v3fc t835 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l97));
                            gm_t419mo45g7j0a t836;
                            GM_ST(uint32_t, (uint8_t *)&t836 + 28, 0);
                            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t836 + 0), t835);
                            GM_ST(uint32_t, ((uint8_t *)&t836 + 24), t834);
                            gm_tllthq21h2f0k t837;
                            GM_ST(uint8_t, (uint8_t *)&t837 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t837 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t837 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t837 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t837 + 48, 0);
                            GM_ST(uint64_t, (uint8_t *)&t837 + 56, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t837 + 0), ((uint8_t)2ULL));
                            GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t837 + 16), t836);
                            gm_l162 = t837;
                            goto gm_lb73;
                        }
gm_lb75:;
                        {
                            uint8_t t838 = GM_LD(uint8_t, (((uint8_t *)&F.l94) + 0));
                            uint8_t t839 = (uint8_t)(t838 == ((uint8_t)2ULL));
                            if (!t839) {
                                goto gm_lb76;
                            }
                            void * t840 = GM_LD(void *, (((uint8_t *)&F.l94) + 8));
                            gm_l99 = t840;
                            void * t841 = gm_l0;
                            void * t842 = gm_l99;
                            gm_t7b7esi3n9r90 t843;
                            gm_f_expr_tp86bm9((void *)&t843, (void *)NULL, cx, NULL, t841, t842);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tllthq21h2f0k t844;
                            GM_ST(uint8_t, (uint8_t *)&t844 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t844 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t844 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t844 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t844 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t844 + 16), t843);
                            gm_l162 = t844;
                            goto gm_lb73;
                        }
gm_lb76:;
                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 347, 25);
                    }
gm_lb73:;
                    memmove(((uint8_t *)&F.l100), ((uint8_t *)&gm_l162), 64);
                    void * t845 = gm_l0;
                    gm_b16_8 t846 = gm_l95;
                    gm_b16_8 t847 = gm_f_exprs_r7m130e((void *)NULL, cx, NULL, t845, t846);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l101 = t847;
                    void * t848 = gm_l2;
                    void * t849 = gm_l1;
                    gm_t88rcucg1b7uk t850 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t849));
                    gm_f_layout_a5ovnrm((void *)&gm_l164, (void *)NULL, cx, NULL, t848, t850);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t851 = GM_LD(uint64_t, ((uint8_t *)&gm_l164));
                    uint8_t t852 = ((t851 == ((uint64_t)0ULL)));
                    if (t852) {
                        gm_l163 = ((uint8_t)1);
                    } else {
                        void * t853 = gm_l0;
                        void * t854 = gm_l1;
                        uint8_t t855 = gm_f_diverges_p3okmol(t853, t854);
                        gm_l163 = t855;
                    }
                    uint8_t t856 = gm_l163;
                    if (t856) {
                        void * t857 = gm_l0;
                        gm_t9805qtonbsag t858;
                        memset(&t858, 0, sizeof t858);
                        gm_b16_8 t859 = gm_l101;
                        gm_tllthq21h2f0k t860 = GM_LD(gm_tllthq21h2f0k, ((uint8_t *)&F.l100));
                        gm_t116nudii5c54 t861;
                        GM_ST(uint64_t, (uint8_t *)&t861 + 8, 0);
                        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t861 + 0), t858);
                        GM_ST(gm_tllthq21h2f0k, ((uint8_t *)&t861 + 16), t860);
                        GM_ST(gm_b16_8, ((uint8_t *)&t861 + 80), t859);
                        void * t862 = gm_l1;
                        gm_tl874t2atnlh3 t863 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t862) + 72));
                        gm_th7badf4b6ee3 t864;
                        memset(&t864, 0, sizeof t864);
                        GM_ST(uint8_t, ((uint8_t *)&t864 + 0), ((uint8_t)8ULL));
                        GM_ST(gm_t116nudii5c54, ((uint8_t *)&t864 + 16), t861);
                        gm_b0_1 t865 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t857, &t864, t863);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t866 = gm_l0;
                        void * t867 = gm_l1;
                        uint8_t t868 = gm_f_diverges_p3okmol(t866, t867);
                        if (t868) {
                            void * t869 = gm_l0;
                            void * t870 = gm_l1;
                            gm_tl874t2atnlh3 t871 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t870) + 72));
                            gm_th7badf4b6ee3 t872;
                            memset(&t872, 0, sizeof t872);
                            GM_ST(uint8_t, ((uint8_t *)&t872 + 0), ((uint8_t)43ULL));
                            gm_b0_1 t873 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t869, &t872, t871);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        void * t874 = gm_l1;
                        gm_t88rcucg1b7uk t875 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t874));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t875));
                        goto gm_fdone;
                    }
                    void * t876 = gm_l0;
                    void * t877 = gm_l1;
                    gm_t88rcucg1b7uk t878 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t877));
                    uint32_t t879 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t876, t878);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l102 = t879;
                    void * t880 = gm_l0;
                    uint32_t t881 = gm_l102;
                    gm_t9805qtonbsag t882;
                    GM_ST(uint8_t, (uint8_t *)&t882 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t882 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t882), t881);
                    ((uint8_t *)&t882)[4] = 1;
                    gm_b16_8 t883 = gm_l101;
                    gm_tllthq21h2f0k t884 = GM_LD(gm_tllthq21h2f0k, ((uint8_t *)&F.l100));
                    gm_t116nudii5c54 t885;
                    GM_ST(uint64_t, (uint8_t *)&t885 + 8, 0);
                    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t885 + 0), t882);
                    GM_ST(gm_tllthq21h2f0k, ((uint8_t *)&t885 + 16), t884);
                    GM_ST(gm_b16_8, ((uint8_t *)&t885 + 80), t883);
                    void * t886 = gm_l1;
                    gm_tl874t2atnlh3 t887 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t886) + 72));
                    gm_th7badf4b6ee3 t888;
                    memset(&t888, 0, sizeof t888);
                    GM_ST(uint8_t, ((uint8_t *)&t888 + 0), ((uint8_t)8ULL));
                    GM_ST(gm_t116nudii5c54, ((uint8_t *)&t888 + 16), t885);
                    gm_b0_1 t889 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t880, &t888, t887);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t890 = gm_l102;
                    gm_t7b7esi3n9r90 t891;
                    memset(&t891, 0, sizeof t891);
                    GM_ST(uint8_t, ((uint8_t *)&t891 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t891 + 16), t890);
                    memmove(gm_out, ((uint8_t *)&t891), 48);
                    goto gm_fdone;
                }
gm_lb72:;
            }
            {
                {
                    {
                        {
                            uint8_t t892 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t893 = (uint8_t)(t892 == ((uint8_t)35ULL));
                            if (!t893) {
                                goto gm_lb79;
                            }
                            gm_b16_8 t894 = GM_LD(gm_b16_8, ((((uint8_t *)t107) + 8) + 8));
                            gm_l103 = t894;
                            goto gm_lb78;
                        }
gm_lb79:;
                        {
                            uint8_t t895 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                            uint8_t t896 = (uint8_t)(t895 == ((uint8_t)36ULL));
                            if (!t896) {
                                goto gm_lb80;
                            }
                            gm_b16_8 t897 = GM_LD(gm_b16_8, ((((uint8_t *)t107) + 8) + 8));
                            gm_l103 = t897;
                            goto gm_lb78;
                        }
gm_lb80:;
                        goto gm_lb77;
                    }
gm_lb78:;
                    void * t898 = gm_l0;
                    gm_b16_8 t899 = gm_l103;
                    gm_b16_8 t900 = gm_f_exprs_r7m130e((void *)NULL, cx, NULL, t898, t899);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l105 = t900;
                    void * t901 = gm_l0;
                    void * t902 = gm_l1;
                    gm_t88rcucg1b7uk t903 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t902));
                    uint32_t t904 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t901, t903);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l106 = t904;
                    void * t905 = gm_l0;
                    uint32_t t906 = gm_l106;
                    gm_b16_8 t907 = gm_l105;
                    gm_tiilmfd16v3fc t908;
                    GM_ST(uint32_t, (uint8_t *)&t908 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t908 + 0), t906);
                    GM_ST(gm_b16_8, ((uint8_t *)&t908 + 8), t907);
                    void * t909 = gm_l1;
                    gm_tl874t2atnlh3 t910 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t909) + 72));
                    gm_th7badf4b6ee3 t911;
                    memset(&t911, 0, sizeof t911);
                    GM_ST(uint8_t, ((uint8_t *)&t911 + 0), ((uint8_t)9ULL));
                    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t911 + 16), t908);
                    gm_b0_1 t912 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t905, &t911, t910);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t913 = gm_l106;
                    gm_t7b7esi3n9r90 t914;
                    memset(&t914, 0, sizeof t914);
                    GM_ST(uint8_t, ((uint8_t *)&t914 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t914 + 16), t913);
                    memmove(gm_out, ((uint8_t *)&t914), 48);
                    goto gm_fdone;
                }
gm_lb77:;
                {
                    uint8_t t915 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t916 = (uint8_t)(t915 == ((uint8_t)38ULL));
                    if (!t916) {
                        goto gm_lb81;
                    }
                    gm_ttggq7ng4kp3p t917 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t107) + 8) + 8));
                    gm_l107 = t917;
                    gm_tf8kfhcnval78 t918;
                    memset(&t918, 0, sizeof t918);
                    F.l108 = t918;
                    {
                        {
                            uint8_t t919 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l107) + 8)) != 0);
                            if (!t919) {
                                goto gm_lb83;
                            }
                            void * t920 = GM_LD(void *, (((uint8_t *)&gm_l107) + 8));
                            gm_l109 = t920;
                            void * t921 = gm_l0;
                            void * t922 = gm_l109;
                            gm_t7b7esi3n9r90 t923;
                            gm_f_expr_tp86bm9((void *)&t923, (void *)NULL, cx, NULL, t921, t922);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tf8kfhcnval78 t924;
                            GM_ST(uint8_t, (uint8_t *)&t924 + 49, 0);
                            GM_ST(uint16_t, (uint8_t *)&t924 + 50, 0);
                            GM_ST(uint32_t, (uint8_t *)&t924 + 52, 0);
                            GM_ST(uint64_t, (uint8_t *)&t924 + 56, 0);
                            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t924), t923);
                            ((uint8_t *)&t924)[48] = 1;
                            F.l108 = t924;
                            goto gm_lb82;
                        }
gm_lb83:;
                    }
gm_lb82:;
                    void * t925 = gm_l0;
                    void * t926 = gm_l1;
                    gm_t88rcucg1b7uk t927 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t926));
                    uint32_t t928 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t925, t927);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l110 = t928;
                    void * t929 = gm_l0;
                    uint32_t t930 = gm_l110;
                    uint32_t t931 = GM_LD(uint32_t, ((uint8_t *)&gm_l107));
                    gm_tf8kfhcnval78 t932 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&F.l108));
                    gm_tvv8lnu6a3ppj t933;
                    GM_ST(uint64_t, (uint8_t *)&t933 + 8, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t933 + 0), t930);
                    GM_ST(uint32_t, ((uint8_t *)&t933 + 4), t931);
                    GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t933 + 16), t932);
                    void * t934 = gm_l1;
                    gm_tl874t2atnlh3 t935 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t934) + 72));
                    gm_th7badf4b6ee3 t936;
                    memset(&t936, 0, sizeof t936);
                    GM_ST(uint8_t, ((uint8_t *)&t936 + 0), ((uint8_t)14ULL));
                    GM_ST(gm_tvv8lnu6a3ppj, ((uint8_t *)&t936 + 16), t933);
                    gm_b0_1 t937 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t929, &t936, t935);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t938 = gm_l110;
                    gm_t7b7esi3n9r90 t939;
                    memset(&t939, 0, sizeof t939);
                    GM_ST(uint8_t, ((uint8_t *)&t939 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t939 + 16), t938);
                    memmove(gm_out, ((uint8_t *)&t939), 48);
                    goto gm_fdone;
                }
gm_lb81:;
            }
            {
                {
                    uint8_t t940 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t941 = (uint8_t)(t940 == ((uint8_t)39ULL));
                    if (!t941) {
                        goto gm_lb84;
                    }
                    void * t942 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l111 = t942;
                    void * t943 = gm_l0;
                    void * t944 = gm_l111;
                    gm_f_expr_tp86bm9((void *)&F.l112, (void *)NULL, cx, NULL, t943, t944);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t945 = gm_l0;
                    void * t946 = gm_l1;
                    gm_t88rcucg1b7uk t947 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t946));
                    uint32_t t948 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t945, t947);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l113 = t948;
                    void * t949 = gm_l0;
                    uint32_t t950 = gm_l113;
                    gm_t7b7esi3n9r90 t951 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l112));
                    gm_tf8kfhcnval78 t952;
                    GM_ST(uint8_t, (uint8_t *)&t952 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t952 + 50, 0);
                    GM_ST(uint32_t, (uint8_t *)&t952 + 52, 0);
                    GM_ST(uint64_t, (uint8_t *)&t952 + 56, 0);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t952), t951);
                    ((uint8_t *)&t952)[48] = 1;
                    gm_t9qkh0rsooc8d t953;
                    GM_ST(uint8_t, ((uint8_t *)&t953 + 0), ((uint8_t)3ULL));
                    gm_tg85uffmq8b38 t954;
                    GM_ST(uint8_t, (uint8_t *)&t954 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t954 + 6, 0);
                    GM_ST(uint64_t, (uint8_t *)&t954 + 8, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t954 + 0), t950);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t954 + 4), t953);
                    GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t954 + 16), t952);
                    void * t955 = gm_l1;
                    gm_tl874t2atnlh3 t956 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t955) + 72));
                    gm_th7badf4b6ee3 t957;
                    memset(&t957, 0, sizeof t957);
                    GM_ST(uint8_t, ((uint8_t *)&t957 + 0), ((uint8_t)15ULL));
                    GM_ST(gm_tg85uffmq8b38, ((uint8_t *)&t957 + 16), t954);
                    gm_b0_1 t958 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t949, &t957, t956);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t959 = gm_l113;
                    gm_t7b7esi3n9r90 t960;
                    memset(&t960, 0, sizeof t960);
                    GM_ST(uint8_t, ((uint8_t *)&t960 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t960 + 16), t959);
                    memmove(gm_out, ((uint8_t *)&t960), 48);
                    goto gm_fdone;
                }
gm_lb84:;
                {
                    uint8_t t961 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t962 = (uint8_t)(t961 == ((uint8_t)46ULL));
                    if (!t962) {
                        goto gm_lb85;
                    }
                    void * t963 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l114 = t963;
                    void * t964 = gm_l0;
                    void * t965 = gm_l114;
                    void * t966 = gm_l1;
                    gm_t88rcucg1b7uk t967 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t966));
                    gm_f_block_iujbbjb((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t964, t965, t967);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb85:;
                {
                    uint8_t t968 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t969 = (uint8_t)(t968 == ((uint8_t)47ULL));
                    if (!t969) {
                        goto gm_lb86;
                    }
                    memmove(((uint8_t *)&gm_l115), ((((uint8_t *)t107) + 8) + 8), 32);
                    void * t970 = gm_l0;
                    void * t971 = gm_l1;
                    gm_trl2khmuphncd t972 = GM_LD(gm_trl2khmuphncd, ((uint8_t *)&gm_l115));
                    gm_f_if_expr_bsdi44h((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t970, t971, &t972);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb86:;
                {
                    uint8_t t973 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t974 = (uint8_t)(t973 == ((uint8_t)48ULL));
                    if (!t974) {
                        goto gm_lb87;
                    }
                    memmove(((uint8_t *)&F.l116), ((((uint8_t *)t107) + 8) + 8), 40);
                    void * t975 = gm_l0;
                    void * t976 = gm_l1;
                    gm_t710lsrq1m8gk t977 = GM_LD(gm_t710lsrq1m8gk, ((uint8_t *)&F.l116));
                    gm_f_loop_expr_k6v92f2((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t975, t976, &t977);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb87:;
                {
                    uint8_t t978 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t979 = (uint8_t)(t978 == ((uint8_t)49ULL));
                    if (!t979) {
                        goto gm_lb88;
                    }
                    memmove(((uint8_t *)&gm_l117), ((((uint8_t *)t107) + 8) + 8), 24);
                    void * t980 = gm_l0;
                    gm_te9hrk0u35rev t981 = GM_LD(gm_te9hrk0u35rev, ((uint8_t *)&gm_l117));
                    gm_f_local_target_vvq8lnf((void *)&gm_l165, t980, t981);
                    uint8_t t982 = (uint8_t)((((uint8_t *)&gm_l165))[20] != 0);
                    if (t982) {
                        memmove(((uint8_t *)&gm_l166), ((uint8_t *)&gm_l165), 20);
                    } else {
                        gm_tf8kfhcnval78 t983;
                        memset(&t983, 0, sizeof t983);
                        F.l118 = t983;
                        {
                            {
                                uint8_t t984 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l117) + 16)) != 0);
                                if (!t984) {
                                    goto gm_lb90;
                                }
                                void * t985 = GM_LD(void *, (((uint8_t *)&gm_l117) + 16));
                                gm_l119 = t985;
                                void * t986 = gm_l0;
                                void * t987 = gm_l119;
                                gm_f_expr_tp86bm9((void *)&F.l120, (void *)NULL, cx, NULL, t986, t987);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                {
                                    {
                                        void * t988 = gm_l2;
                                        void * t989 = gm_l119;
                                        gm_t88rcucg1b7uk t990 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t989));
                                        gm_f_layout_a5ovnrm((void *)&gm_l167, (void *)NULL, cx, NULL, t988, t990);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint64_t t991 = GM_LD(uint64_t, ((uint8_t *)&gm_l167));
                                        uint8_t t992 = (t991 > ((uint64_t)0ULL));
                                        if (!t992) {
                                            goto gm_lb92;
                                        }
                                        void * t993 = gm_l0;
                                        void * t994 = gm_l119;
                                        uint8_t t995 = gm_f_diverges_p3okmol(t993, t994);
                                        uint8_t t996 = (uint8_t)!t995;
                                        if (!t996) {
                                            goto gm_lb92;
                                        }
                                        gm_t7b7esi3n9r90 t997 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l120));
                                        gm_tf8kfhcnval78 t998;
                                        GM_ST(uint8_t, (uint8_t *)&t998 + 49, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t998 + 50, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t998 + 52, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t998 + 56, 0);
                                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t998), t997);
                                        ((uint8_t *)&t998)[48] = 1;
                                        F.l118 = t998;
                                        goto gm_lb91;
                                    }
gm_lb92:;
                                }
gm_lb91:;
                                goto gm_lb89;
                            }
gm_lb90:;
                        }
gm_lb89:;
                        void * t999 = gm_l0;
                        gm_te9hrk0u35rev t1000 = GM_LD(gm_te9hrk0u35rev, ((uint8_t *)&gm_l117));
                        gm_t7pnmbsgd9mnq t1001;
                        GM_ST(uint8_t, (uint8_t *)&t1001 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1001 + 2, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t1001 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_te9hrk0u35rev, ((uint8_t *)&t1001 + 4), t1000);
                        gm_tf8kfhcnval78 t1002 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&F.l118));
                        gm_t7dcjsiqi95h3 t1003;
                        GM_ST(gm_t7pnmbsgd9mnq, ((uint8_t *)&t1003 + 0), t1001);
                        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t1003 + 16), t1002);
                        void * t1004 = gm_l1;
                        gm_tl874t2atnlh3 t1005 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1004) + 72));
                        gm_th7badf4b6ee3 t1006;
                        memset(&t1006, 0, sizeof t1006);
                        GM_ST(uint8_t, ((uint8_t *)&t1006 + 0), ((uint8_t)39ULL));
                        GM_ST(gm_t7dcjsiqi95h3, ((uint8_t *)&t1006 + 16), t1003);
                        gm_b0_1 t1007 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t999, &t1006, t1005);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t1008 = gm_l1;
                        gm_t88rcucg1b7uk t1009 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1008));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t1009));
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&gm_l121), ((uint8_t *)&gm_l166), 20);
                    uint32_t t1010 = GM_LD(uint32_t, ((uint8_t *)&gm_l121));
                    gm_l122 = t1010;
                    {
                        {
                            uint8_t t1011 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l117) + 16)) != 0);
                            if (!t1011) {
                                goto gm_lb94;
                            }
                            void * t1012 = GM_LD(void *, (((uint8_t *)&gm_l117) + 16));
                            gm_l123 = t1012;
                            void * t1013 = gm_l0;
                            void * t1014 = gm_l123;
                            gm_f_expr_tp86bm9((void *)&F.l124, (void *)NULL, cx, NULL, t1013, t1014);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            {
                                {
                                    void * t1015 = gm_l0;
                                    uint32_t t1016 = gm_l122;
                                    gm_t9805qtonbsag t1017 = gm_f_result_of_9m0s9vd(t1015, t1016);
                                    gm_l168 = t1017;
                                    uint8_t t1018 = (uint8_t)((((uint8_t *)&gm_l168))[4] != 0);
                                    if (!t1018) {
                                        goto gm_lb96;
                                    }
                                    uint32_t t1019 = GM_LD(uint32_t, ((uint8_t *)&gm_l168));
                                    gm_l125 = t1019;
                                    void * t1020 = gm_l0;
                                    uint32_t t1021 = gm_l125;
                                    gm_tlg23kb3oj23s t1022;
                                    gm_f_local_place_qnf3mb2((void *)&t1022, t1021);
                                    gm_t7b7esi3n9r90 t1023 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l124));
                                    gm_t6fp3q71vjrmk t1024;
                                    GM_ST(uint64_t, (uint8_t *)&t1024 + 56, 0);
                                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t1024 + 0), t1022);
                                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t1024 + 64), t1023);
                                    void * t1025 = gm_l1;
                                    gm_tl874t2atnlh3 t1026 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1025) + 72));
                                    gm_th7badf4b6ee3 t1027;
                                    memset(&t1027, 0, sizeof t1027);
                                    GM_ST(uint8_t, ((uint8_t *)&t1027 + 0), ((uint8_t)25ULL));
                                    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t1027 + 16), t1024);
                                    gm_b0_1 t1028 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t1020, &t1027, t1026);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb95;
                                }
gm_lb96:;
                            }
gm_lb95:;
                            goto gm_lb93;
                        }
gm_lb94:;
                    }
gm_lb93:;
                    void * t1029 = gm_l0;
                    uint32_t t1030 = GM_LD(uint32_t, (((uint8_t *)&gm_l121) + 4));
                    void * t1031 = gm_l1;
                    gm_tl874t2atnlh3 t1032 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1031) + 72));
                    gm_th7badf4b6ee3 t1033;
                    memset(&t1033, 0, sizeof t1033);
                    GM_ST(uint8_t, ((uint8_t *)&t1033 + 0), ((uint8_t)30ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t1033 + 16), t1030);
                    gm_b0_1 t1034 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t1029, &t1033, t1032);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t1035 = gm_l1;
                    gm_t88rcucg1b7uk t1036 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1035));
                    gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t1036));
                    goto gm_fdone;
                }
gm_lb88:;
            }
            {
                {
                    uint8_t t1037 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t1038 = (uint8_t)(t1037 == ((uint8_t)50ULL));
                    if (!t1038) {
                        goto gm_lb97;
                    }
                    gm_te9hrk0u35rev t1039 = GM_LD(gm_te9hrk0u35rev, ((((uint8_t *)t107) + 8) + 8));
                    gm_l126 = t1039;
                    void * t1040 = gm_l0;
                    gm_te9hrk0u35rev t1041 = gm_l126;
                    gm_f_local_target_vvq8lnf((void *)&gm_l169, t1040, t1041);
                    uint8_t t1042 = (uint8_t)((((uint8_t *)&gm_l169))[20] != 0);
                    if (t1042) {
                        memmove(((uint8_t *)&gm_l170), ((uint8_t *)&gm_l169), 20);
                    } else {
                        void * t1043 = gm_l0;
                        gm_te9hrk0u35rev t1044 = gm_l126;
                        gm_tf8kfhcnval78 t1045;
                        memset(&t1045, 0, sizeof t1045);
                        gm_t7pnmbsgd9mnq t1046;
                        GM_ST(uint8_t, (uint8_t *)&t1046 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1046 + 2, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t1046 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_te9hrk0u35rev, ((uint8_t *)&t1046 + 4), t1044);
                        gm_t7dcjsiqi95h3 t1047;
                        GM_ST(gm_t7pnmbsgd9mnq, ((uint8_t *)&t1047 + 0), t1046);
                        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t1047 + 16), t1045);
                        void * t1048 = gm_l1;
                        gm_tl874t2atnlh3 t1049 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1048) + 72));
                        gm_th7badf4b6ee3 t1050;
                        memset(&t1050, 0, sizeof t1050);
                        GM_ST(uint8_t, ((uint8_t *)&t1050 + 0), ((uint8_t)39ULL));
                        GM_ST(gm_t7dcjsiqi95h3, ((uint8_t *)&t1050 + 16), t1047);
                        gm_b0_1 t1051 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t1043, &t1050, t1049);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t1052 = gm_l1;
                        gm_t88rcucg1b7uk t1053 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1052));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t1053));
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&gm_l127), ((uint8_t *)&gm_l170), 20);
                    void * t1054 = gm_l0;
                    uint32_t t1055 = GM_LD(uint32_t, (((uint8_t *)&gm_l127) + 8));
                    void * t1056 = gm_l1;
                    gm_tl874t2atnlh3 t1057 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1056) + 72));
                    gm_th7badf4b6ee3 t1058;
                    memset(&t1058, 0, sizeof t1058);
                    GM_ST(uint8_t, ((uint8_t *)&t1058 + 0), ((uint8_t)30ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t1058 + 16), t1055);
                    gm_b0_1 t1059 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t1054, &t1058, t1057);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t1060 = gm_l1;
                    gm_t88rcucg1b7uk t1061 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1060));
                    gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t1061));
                    goto gm_fdone;
                }
gm_lb97:;
                {
                    uint8_t t1062 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t1063 = (uint8_t)(t1062 == ((uint8_t)51ULL));
                    if (!t1063) {
                        goto gm_lb98;
                    }
                    uint32_t t1064 = GM_LD(uint32_t, ((((uint8_t *)t107) + 8) + 8));
                    gm_l128 = t1064;
                    void * t1065 = GM_LD(void *, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l129 = t1065;
                    void * t1066 = gm_l0;
                    void * t1067 = gm_l129;
                    gm_f_expr_tp86bm9((void *)&F.l130, (void *)NULL, cx, NULL, t1066, t1067);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t1068 = gm_l128;
                    uint8_t t1069 = (!(t1068 == ((uint32_t)0ULL)));
                    if (t1069) {
                        {
                            {
                                void * t1070 = gm_l2;
                                void * t1071 = gm_l129;
                                gm_t88rcucg1b7uk t1072 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1071));
                                gm_f_layout_a5ovnrm((void *)&gm_l172, (void *)NULL, cx, NULL, t1070, t1072);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint64_t t1073 = GM_LD(uint64_t, ((uint8_t *)&gm_l172));
                                uint8_t t1074 = (t1073 > ((uint64_t)0ULL));
                                if (!t1074) {
                                    goto gm_lb100;
                                }
                                void * t1075 = gm_l0;
                                void * t1076 = gm_l129;
                                uint8_t t1077 = gm_f_diverges_p3okmol(t1075, t1076);
                                uint8_t t1078 = (uint8_t)!t1077;
                                if (!t1078) {
                                    goto gm_lb100;
                                }
                                gm_t7b7esi3n9r90 t1079 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l130));
                                gm_tf8kfhcnval78 t1080;
                                GM_ST(uint8_t, (uint8_t *)&t1080 + 49, 0);
                                GM_ST(uint16_t, (uint8_t *)&t1080 + 50, 0);
                                GM_ST(uint32_t, (uint8_t *)&t1080 + 52, 0);
                                GM_ST(uint64_t, (uint8_t *)&t1080 + 56, 0);
                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t1080), t1079);
                                ((uint8_t *)&t1080)[48] = 1;
                                gm_l171 = t1080;
                                goto gm_lb99;
                            }
gm_lb100:;
                            gm_tf8kfhcnval78 t1081;
                            memset(&t1081, 0, sizeof t1081);
                            gm_l171 = t1081;
                        }
gm_lb99:;
                        memmove(((uint8_t *)&F.l131), ((uint8_t *)&gm_l171), 64);
                        void * t1082 = gm_l0;
                        uint32_t t1083 = gm_l128;
                        gm_t7pnmbsgd9mnq t1084;
                        GM_ST(uint8_t, (uint8_t *)&t1084 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1084 + 2, 0);
                        GM_ST(uint64_t, (uint8_t *)&t1084 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t1084 + 0), ((uint8_t)2ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t1084 + 4), t1083);
                        gm_tf8kfhcnval78 t1085 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&F.l131));
                        gm_t7dcjsiqi95h3 t1086;
                        GM_ST(gm_t7pnmbsgd9mnq, ((uint8_t *)&t1086 + 0), t1084);
                        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t1086 + 16), t1085);
                        void * t1087 = gm_l1;
                        gm_tl874t2atnlh3 t1088 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1087) + 72));
                        gm_th7badf4b6ee3 t1089;
                        memset(&t1089, 0, sizeof t1089);
                        GM_ST(uint8_t, ((uint8_t *)&t1089 + 0), ((uint8_t)39ULL));
                        GM_ST(gm_t7dcjsiqi95h3, ((uint8_t *)&t1089 + 16), t1086);
                        gm_b0_1 t1090 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t1082, &t1089, t1088);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t1091 = gm_l1;
                        gm_t88rcucg1b7uk t1092 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1091));
                        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t1092));
                        goto gm_fdone;
                    }
                    void * t1093 = gm_l0;
                    gm_t7b7esi3n9r90 t1094 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l130));
                    gm_tf8kfhcnval78 t1095;
                    GM_ST(uint8_t, (uint8_t *)&t1095 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1095 + 50, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1095 + 52, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1095 + 56, 0);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t1095), t1094);
                    ((uint8_t *)&t1095)[48] = 1;
                    void * t1096 = gm_l1;
                    gm_tl874t2atnlh3 t1097 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1096) + 72));
                    gm_th7badf4b6ee3 t1098;
                    memset(&t1098, 0, sizeof t1098);
                    GM_ST(uint8_t, ((uint8_t *)&t1098 + 0), ((uint8_t)32ULL));
                    GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t1098 + 16), t1095);
                    gm_b0_1 t1099 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t1093, &t1098, t1097);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t1100 = gm_l1;
                    gm_t88rcucg1b7uk t1101 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1100));
                    gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t1101));
                    goto gm_fdone;
                }
gm_lb98:;
            }
            {
                {
                    uint8_t t1102 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t1103 = (uint8_t)(t1102 == ((uint8_t)52ULL));
                    if (!t1103) {
                        goto gm_lb101;
                    }
                    void * t1104 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l132 = t1104;
                    void * t1105 = gm_l0;
                    void * t1106 = gm_l132;
                    gm_f_expr_tp86bm9((void *)&F.l133, (void *)NULL, cx, NULL, t1105, t1106);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t1107 = gm_l0;
                    gm_t7b7esi3n9r90 t1108 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l133));
                    void * t1109 = gm_l1;
                    gm_tl874t2atnlh3 t1110 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1109) + 72));
                    gm_th7badf4b6ee3 t1111;
                    memset(&t1111, 0, sizeof t1111);
                    GM_ST(uint8_t, ((uint8_t *)&t1111 + 0), ((uint8_t)34ULL));
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t1111 + 16), t1108);
                    gm_b0_1 t1112 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t1107, &t1111, t1110);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t1113 = gm_l1;
                    gm_t88rcucg1b7uk t1114 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t1113));
                    gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t1114));
                    goto gm_fdone;
                }
gm_lb101:;
                {
                    uint8_t t1115 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                    uint8_t t1116 = (uint8_t)(t1115 == ((uint8_t)44ULL));
                    if (!t1116) {
                        goto gm_lb102;
                    }
                    void * t1117 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                    gm_l134 = t1117;
                    gm_b16_8 t1118 = GM_LD(gm_b16_8, (((((uint8_t *)t107) + 8) + 8) + 8));
                    gm_l135 = t1118;
                    void * t1119 = gm_l0;
                    void * t1120 = gm_l1;
                    void * t1121 = gm_l134;
                    gm_b16_8 t1122 = gm_l135;
                    gm_f_match_expr_rfas5u2((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t1119, t1120, t1121, t1122);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb102:;
                {
                    void * t1123 = gm_l0;
                    void * t1124 = gm_l1;
                    gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t1123, t1124);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb103:;
                gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 171, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_exprs_r7m130e(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_exprs_r7m130e F;
    void * gm_l0; /* lw */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 286u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l6;
    gm_trqfvmjtt4sij t7;
    gm_f_list_0202sdh((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t7;
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
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
                        void * t13 = (void *)((uint8_t *)&F.l2);
                        void * t14 = gm_l0;
                        void * t15 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t17 = gm_l5;
                        void * t18 = (void *)(((uint8_t *)t15) + (uintptr_t)t17 * 88);
                        gm_t7b7esi3n9r90 t19;
                        gm_f_expr_tp86bm9((void *)&t19, (void *)NULL, cx, NULL, t14, t18);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t20 = gm_f_push_0pgacsj(t13, &t19);
                        gm_l6 = t20;
                        uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                        if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srccujoh2vgr9aq, 433, 26);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 433, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t22 = gm_l4;
            uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
            uint64_t t25 = r23;
            gm_l4 = t25;
        }
    }
gm_lb1:;
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    F.ret = t26;
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
inline __attribute__((aligned(4))) void gm_f_float_value_o4b87lc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, double a2) {
    struct gm_fr_float_value_o4b87lc F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    double gm_l2; /* x */
    GM_STACK_CHECK(&F, 481u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t7b7esi3n9r90 gm_l3;
    gm_t53at80pfdus3 gm_l4;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    void * t9 = (void *)(((uint8_t *)t8) + 8);
    void * t10 = gm_l1;
    gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t10));
    gm_f_kind_jcnt4hm((void *)&gm_l4, t9, GM_CONV(gm_csguankko4lmhd, t11));
    {
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)3ULL));
            if (!t13) {
                goto gm_lb2;
            }
            uint16_t t14 = GM_LD(uint16_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t15 = ((t14 == ((uint16_t)16ULL)));
            if (!t15) {
                goto gm_lb2;
            }
            void * t16 = gm_l1;
            gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t16));
            double t18 = gm_l2;
            uint16_t t19 = gm_f_f16_bits_2us9irg(t18);
            uint64_t t20 = (uint64_t)t19;
            gm_tfbnciphf3261 t21;
            GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t21 + 0), t17);
            GM_ST(uint64_t, ((uint8_t *)&t21 + 8), t20);
            gm_t7b7esi3n9r90 t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t22 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t22 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t22 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t22 + 16), t21);
            gm_l3 = t22;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)3ULL));
            if (!t24) {
                goto gm_lb3;
            }
            uint16_t t25 = GM_LD(uint16_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t26 = ((t25 == ((uint16_t)32ULL)));
            if (!t26) {
                goto gm_lb3;
            }
            void * t27 = gm_l1;
            gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t27));
            double t29 = gm_l2;
            float t30 = (float)((double)t29);
            uint32_t t31;
            memset(&t31, 0, sizeof t31); memcpy(&t31, ((uint8_t *)&t30), sizeof t31 < 4 ? sizeof t31 : 4);
            uint64_t t32 = (uint64_t)t31;
            gm_tfbnciphf3261 t33;
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t33 + 0), t28);
            GM_ST(uint64_t, ((uint8_t *)&t33 + 8), t32);
            gm_t7b7esi3n9r90 t34;
            GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t34 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t34 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t34 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t34 + 16), t33);
            gm_l3 = t34;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)3ULL));
            if (!t36) {
                goto gm_lb4;
            }
            uint16_t t37 = GM_LD(uint16_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t38 = ((t37 == ((uint16_t)64ULL)));
            if (!t38) {
                goto gm_lb4;
            }
            void * t39 = gm_l1;
            gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t39));
            double t41 = gm_l2;
            uint64_t t42;
            memset(&t42, 0, sizeof t42); memcpy(&t42, ((uint8_t *)&t41), sizeof t42 < 8 ? sizeof t42 : 8);
            gm_tfbnciphf3261 t43;
            GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t43 + 0), t40);
            GM_ST(uint64_t, ((uint8_t *)&t43 + 8), t42);
            gm_t7b7esi3n9r90 t44;
            GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t44 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t44 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t44 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t44 + 16), t43);
            gm_l3 = t44;
            goto gm_lb1;
        }
gm_lb4:;
        {
            void * t45 = gm_l0;
            void * t46 = gm_l1;
            gm_f_escape_avsr9t6((void *)&gm_l3, (void *)NULL, cx, NULL, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 146, 70);
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&gm_l3), 48);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1fu50sd(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_24nvriq(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_24nvriq F;
    gm_b16_8 gm_l1; /* fail */
    GM_STACK_CHECK(&F, 492u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)((&F.h)->up))->l0));
    void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)((&F.h)->up))->l2));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)((&F.h)->up))->l2) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t4, gm_srccujoh2vgr9aq, 441, 23);
    void * t5 = (void *)(((uint8_t *)t3) + (uintptr_t)((uint64_t)0ULL) * 88);
    gm_f_expr_tp86bm9((void *)&F.l0, (void *)NULL, cx, NULL, t2, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)((&F.h)->up))->l0));
    gm_closure t7 = ((gm_closure){(void *)gm_f_fn_49psp5f, (void *)&F.h, cx});
    gm_b16_8 t8 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t8;
    void * t9 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)((&F.h)->up))->l0));
    gm_t7b7esi3n9r90 t10 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l0));
    gm_b16_8 t11 = gm_f_empty_slice_uvn29bg();
    gm_b16_8 t12 = gm_l1;
    gm_t1s3s9ds8udn4 t13;
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t13 + 0), t10);
    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 48), t11);
    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 64), t12);
    void * t14 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)((&F.h)->up))->l1));
    gm_tl874t2atnlh3 t15 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t14) + 72));
    gm_th7badf4b6ee3 t16;
    memset(&t16, 0, sizeof t16);
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)29ULL));
    GM_ST(gm_t1s3s9ds8udn4, ((uint8_t *)&t16 + 16), t13);
    gm_b0_1 t17 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t9, &t16, t15);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_36v8itm(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_36v8itm F;
    uint32_t gm_l1; /* w */
    GM_STACK_CHECK(&F, 1414u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tf8kfhcnval78 gm_l2;
    {
        {
            void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
            void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
            gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t5));
            gm_f_word_1sfojus((void *)&gm_l2, (void *)NULL, cx, NULL, t4, t6, ((uint64_t)0ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l2))[48] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l0), ((uint8_t *)&gm_l2), 48);
            void * t8 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
            uint32_t t9 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l21));
            gm_tlg23kb3oj23s t10;
            gm_f_local_place_qnf3mb2((void *)&t10, t9);
            gm_t7b7esi3n9r90 t11 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l0));
            gm_t6fp3q71vjrmk t12;
            GM_ST(uint64_t, (uint8_t *)&t12 + 56, 0);
            GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t12 + 0), t10);
            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t12 + 64), t11);
            void * t13 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
            gm_tl874t2atnlh3 t14 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t13) + 72));
            gm_th7badf4b6ee3 t15;
            memset(&t15, 0, sizeof t15);
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)25ULL));
            GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t15 + 16), t12);
            gm_b0_1 t16 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t8, &t15, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t17 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
    void * t18 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
    gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t18));
    uint32_t t20 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t17, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t20;
    void * t21 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
    uint32_t t22 = gm_l1;
    gm_tf8kfhcnval78 t23;
    memset(&t23, 0, sizeof t23);
    gm_t9qkh0rsooc8d t24;
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
    gm_tg85uffmq8b38 t25;
    GM_ST(uint8_t, (uint8_t *)&t25 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t25 + 6, 0);
    GM_ST(uint64_t, (uint8_t *)&t25 + 8, 0);
    GM_ST(uint32_t, ((uint8_t *)&t25 + 0), t22);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 4), t24);
    GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t25 + 16), t23);
    void * t26 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
    gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t26) + 72));
    gm_th7badf4b6ee3 t28;
    memset(&t28, 0, sizeof t28);
    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)15ULL));
    GM_ST(gm_tg85uffmq8b38, ((uint8_t *)&t28 + 16), t25);
    gm_b0_1 t29 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t21, &t28, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t30 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
    uint32_t t31 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l21));
    gm_tlg23kb3oj23s t32;
    gm_f_local_place_qnf3mb2((void *)&t32, t31);
    uint32_t t33 = gm_l1;
    gm_t7b7esi3n9r90 t34;
    memset(&t34, 0, sizeof t34);
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t34 + 16), t33);
    gm_t6fp3q71vjrmk t35;
    GM_ST(uint64_t, (uint8_t *)&t35 + 56, 0);
    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t35 + 0), t32);
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t35 + 64), t34);
    void * t36 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
    gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 72));
    gm_th7badf4b6ee3 t38;
    memset(&t38, 0, sizeof t38);
    GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)25ULL));
    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t38 + 16), t35);
    gm_b0_1 t39 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t30, &t38, t37);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_47srmke(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_48tkiti(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_49psp5f(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_49psp5f F;
    GM_STACK_CHECK(&F, 517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tf8kfhcnval78 t2;
    memset(&t2, 0, sizeof t2);
    F.l0 = t2;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)(((&F.h)->up)->up))->l2) + 8));
    uint8_t t4 = (t3 > ((uint64_t)1ULL));
    if (t4) {
        void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)(((&F.h)->up)->up))->l0));
        void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)(((&F.h)->up)->up))->l2));
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)(((&F.h)->up)->up))->l2) + 8));
        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t7, gm_srccujoh2vgr9aq, 444, 46);
        void * t8 = (void *)(((uint8_t *)t6) + (uintptr_t)((uint64_t)1ULL) * 88);
        gm_t7b7esi3n9r90 t9;
        gm_f_expr_tp86bm9((void *)&t9, (void *)NULL, cx, NULL, t5, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tf8kfhcnval78 t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 52, 0);
        GM_ST(uint64_t, (uint8_t *)&t10 + 56, 0);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t10), t9);
        ((uint8_t *)&t10)[48] = 1;
        F.l0 = t10;
    }
    void * t11 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)(((&F.h)->up)->up))->l0));
    gm_tf8kfhcnval78 t12 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&F.l0));
    void * t13 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assert__nol4kp3 *)(((&F.h)->up)->up))->l1));
    gm_tl874t2atnlh3 t14 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t13) + 72));
    gm_th7badf4b6ee3 t15;
    memset(&t15, 0, sizeof t15);
    GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)35ULL));
    GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t15 + 16), t12);
    gm_b0_1 t16 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t11, &t15, t14);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_506drml(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_506drml F;
    gm_b16_8 gm_l0; /* <tmp> */
    uint64_t gm_l1; /* <tmp> */
    gm_b16_8 gm_l4; /* insts */
    GM_STACK_CHECK(&F, 689u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)((&F.h)->up))->l3));
    gm_l0 = t7;
    gm_l1 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l1;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t14 = gm_l1;
                {
                    {
                        memmove(((uint8_t *)&F.l2), (((uint8_t *)t12) + (uintptr_t)t14 * 24), 24);
                        void * t15 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)((&F.h)->up))->l0));
                        uint32_t t16 = gm_f_label_3373lil(t15);
                        F.l3 = t16;
                        void * t17 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)((&F.h)->up))->l0));
                        gm_closure t18 = ((gm_closure){(void *)gm_f_fn_lldup98, (void *)&F.h, cx});
                        gm_b16_8 t19 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t17, t18);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_l4 = t19;
                        void * t20 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)((&F.h)->up))->l0));
                        uint32_t t21 = F.l3;
                        gm_b16_8 t22 = gm_l4;
                        gm_tiilmfd16v3fc t23;
                        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t23 + 0), t21);
                        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 8), t22);
                        void * t24 = GM_LD(void *, ((uint8_t *)&F.l2));
                        gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t24) + 120));
                        gm_th7badf4b6ee3 t26;
                        memset(&t26, 0, sizeof t26);
                        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)27ULL));
                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t26 + 16), t23);
                        gm_b0_1 t27 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t20, &t26, t25);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 965, 9);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t28 = gm_l1;
            uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
            uint64_t t31 = r29;
            gm_l1 = t31;
        }
    }
    goto gm_ls32;
gm_ll8:;
    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb1; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb3; }
    }
    goto gm_lad;
gm_ls32:;
gm_lb1:;
    void * t33 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)((&F.h)->up))->l0));
    void * t34 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)((&F.h)->up))->l1));
    gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t34) + 72));
    gm_th7badf4b6ee3 t36;
    memset(&t36, 0, sizeof t36);
    GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)42ULL));
    gm_b0_1 t37 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t33, &t36, t35);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_67nerak(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_67nerak F;
    gm_b16_8 gm_l0; /* inner */
    void * gm_l1; /* st */
    GM_STACK_CHECK(&F, 453u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l0));
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l2) + 8));
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l4));
    gm_b0_1 t7 = gm_f_conds_sqc7llk((void *)NULL, cx, NULL, t4, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t8 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l0));
    gm_closure t9 = ((gm_closure){(void *)gm_f_fn_roenhir, (void *)&F.h, cx});
    gm_b16_8 t10 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l0 = t10;
    void * t11 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l0));
    uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l6));
    gm_b16_8 t13 = gm_l0;
    gm_tiilmfd16v3fc t14;
    GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t14 + 0), t12);
    GM_ST(gm_b16_8, ((uint8_t *)&t14 + 8), t13);
    void * t15 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l2) + 24));
    gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t15) + 24));
    gm_th7badf4b6ee3 t17;
    memset(&t17, 0, sizeof t17);
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)27ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t17 + 16), t14);
    gm_b0_1 t18 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t11, &t17, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t19 = (uint8_t)(GM_LD(void *, (((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l2) + 32)) != 0);
            if (!t19) {
                goto gm_lb2;
            }
            void * t20 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l2) + 32));
            gm_l1 = t20;
            void * t21 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)(((&F.h)->up)->up))->l0));
            void * t22 = gm_l1;
            gm_t7b7esi3n9r90 t23;
            gm_f_expr_tp86bm9((void *)&t23, (void *)NULL, cx, NULL, t21, t22);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7h6ducr(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cccid3f(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_cccid3f F;
    gm_b16_8 gm_l0; /* looped */
    GM_STACK_CHECK(&F, 344u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)((&F.h)->up))->l0));
    gm_closure t3 = ((gm_closure){(void *)gm_f_fn_67nerak, (void *)&F.h, cx});
    gm_b16_8 t4 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l0 = t4;
    void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)((&F.h)->up))->l0));
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)((&F.h)->up))->l5));
    gm_b16_8 t7 = gm_l0;
    gm_tiilmfd16v3fc t8;
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t8 + 0), t6);
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 8), t7);
    void * t9 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_loop_expr_k6v92f2 *)((&F.h)->up))->l1));
    gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t9) + 72));
    gm_th7badf4b6ee3 t11;
    memset(&t11, 0, sizeof t11);
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)28ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t11 + 16), t8);
    gm_b0_1 t12 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t5, &t11, t10);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cfv665f(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_cfv665f F;
    gm_b16_8 gm_l0; /* inner */
    void * gm_l1; /* el */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l0));
    gm_closure t5 = ((gm_closure){(void *)gm_f_fn_t2qurbh, (void *)&F.h, cx});
    gm_b16_8 t6 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l0 = t6;
    void * t7 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l0));
    uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l9));
    gm_b16_8 t9 = gm_l0;
    gm_tiilmfd16v3fc t10;
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t10 + 0), t8);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 8), t9);
    void * t11 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l1));
    gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t11) + 72));
    gm_th7badf4b6ee3 t13;
    memset(&t13, 0, sizeof t13);
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)27ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t13 + 16), t10);
    gm_b0_1 t14 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t7, &t13, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t15 = (uint8_t)(GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l2) + 24)) != 0);
            if (!t15) {
                goto gm_lb2;
            }
            void * t16 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l2) + 24));
            gm_l1 = t16;
            void * t17 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l0));
            gm_t9805qtonbsag t18 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)((&F.h)->up))->l3));
            void * t19 = gm_l1;
            gm_b0_1 t20 = gm_f_store_result_fiq5f1j((void *)NULL, cx, NULL, t17, t18, t19);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cquk74j(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_cquk74j F;
    GM_STACK_CHECK(&F, 501u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t gm_l0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l0));
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l5));
    gm_tlg23kb3oj23s t4;
    gm_f_local_place_qnf3mb2((void *)&t4, t3);
    void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l1));
    gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t5));
    uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l2));
    if (t7) {
        gm_l0 = ((uint64_t)0ULL);
    } else {
        gm_l0 = ((uint64_t)1ULL);
    }
    uint64_t t8 = gm_l0;
    gm_tfbnciphf3261 t9;
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t9 + 0), t6);
    GM_ST(uint64_t, ((uint8_t *)&t9 + 8), t8);
    gm_t7b7esi3n9r90 t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 40, 0);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)1ULL));
    GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t10 + 16), t9);
    gm_t6fp3q71vjrmk t11;
    GM_ST(uint64_t, (uint8_t *)&t11 + 56, 0);
    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t11 + 0), t4);
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t11 + 64), t10);
    void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l1));
    gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t12) + 72));
    gm_th7badf4b6ee3 t14;
    memset(&t14, 0, sizeof t14);
    GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)25ULL));
    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t14 + 16), t11);
    gm_b0_1 t15 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t2, &t14, t13);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_d405sd4(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_d405sd4 F;
    uint32_t gm_l0; /* r */
    uint32_t gm_l3; /* w */
    GM_STACK_CHECK(&F, 2042u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint32_t gm_l4;
    gm_tbccvikg6b96e gm_l5;
    gm_t9qkh0rsooc8d gm_l6;
    uint8_t gm_l7;
    gm_t53at80pfdus3 gm_l8;
    gm_tbccvikg6b96e gm_l9;
    uint8_t t7 = (uint8_t)((((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l7))[4] != 0);
    if (t7) {
        uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l7));
        gm_l4 = t8;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    uint32_t t9 = gm_l4;
    gm_l0 = t9;
    {
        {
            void * t10 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
            gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t10));
            void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l2));
            gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t12));
            uint8_t t14 = (gm_eq9m6mvtmgde(((uint8_t *)&t11), ((uint8_t *)&t13)));
            if (!t14) {
                goto gm_lb2;
            }
            void * t15 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
            gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t15));
            gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l5));
            uint8_t t18 = (!gm_eq9m6mvtmgde(((uint8_t *)&t16), ((uint8_t *)&t17)));
            if (!t18) {
                goto gm_lb2;
            }
            void * t19 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
            void * t20 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
            gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t20));
            void * t22 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
            gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t22) + 72));
            gm_tlg23kb3oj23s t24 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l6));
            gm_f_load_go4fgkp((void *)&F.l1, (void *)NULL, cx, NULL, t19, &t24, t21, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t25 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
            uint32_t t26 = gm_l0;
            gm_tlg23kb3oj23s t27;
            gm_f_local_place_qnf3mb2((void *)&t27, t26);
            gm_t7b7esi3n9r90 t28 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l1));
            gm_t6fp3q71vjrmk t29;
            GM_ST(uint64_t, (uint8_t *)&t29 + 56, 0);
            GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t29 + 0), t27);
            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t29 + 64), t28);
            void * t30 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
            gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t30) + 72));
            gm_th7badf4b6ee3 t32;
            memset(&t32, 0, sizeof t32);
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)25ULL));
            GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t32 + 16), t29);
            gm_b0_1 t33 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t25, &t32, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l5));
    gm_t7b7esi3n9r90 t35;
    memset(&t35, 0, sizeof t35);
    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t35 + 16), t34);
    F.l2 = t35;
    void * t36 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l3));
    gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l5));
    gm_f_layout_a5ovnrm((void *)&gm_l5, (void *)NULL, cx, NULL, t36, t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    uint8_t t39 = (t38 > ((uint64_t)0ULL));
    if (t39) {
        void * t40 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
        uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l9));
        gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l5));
        gm_tlcm4lvu3g1a8 t43;
        GM_ST(uint32_t, (uint8_t *)&t43 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t43 + 0), t41);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t43 + 8), t42);
        gm_tfqmtuprvunod t44;
        memset(&t44, 0, sizeof t44);
        GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)4ULL));
        GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t44 + 16), t43);
        gm_tlg23kb3oj23s t45;
        gm_f_project_6hpd48e((void *)&t45, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l6)), &t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l5));
        void * t47 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t47) + 72));
        gm_f_load_go4fgkp((void *)&F.l2, (void *)NULL, cx, NULL, t40, &t45, t46, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t49 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
    gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t49));
    gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l5));
    uint8_t t52 = (!gm_eq9m6mvtmgde(((uint8_t *)&t50), ((uint8_t *)&t51)));
    if (t52) {
        void * t53 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
        void * t54 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
        gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t54));
        uint32_t t56 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t53, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t56;
        void * t57 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
        uint32_t t58 = gm_l3;
        void * t59 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l3));
        void * t60 = (void *)(((uint8_t *)t59) + 8);
        void * t61 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
        gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t61));
        gm_f_kind_jcnt4hm((void *)&gm_l8, t60, GM_CONV(gm_csguankko4lmhd, t62));
        {
            {
                uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                uint8_t t64 = (uint8_t)(t63 == ((uint8_t)13ULL));
                if (!t64) {
                    goto gm_lb4;
                }
                gm_l7 = ((uint8_t)1);
                goto gm_lb3;
            }
gm_lb4:;
            {
                gm_l7 = ((uint8_t)0);
                goto gm_lb3;
            }
gm_lb5:;
            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1153, 33);
        }
gm_lb3:;
        uint8_t t65 = gm_l7;
        if (t65) {
            gm_t9qkh0rsooc8d t66;
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)1ULL));
            gm_l6 = t66;
        } else {
            gm_t9qkh0rsooc8d t67;
            GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)2ULL));
            gm_l6 = t67;
        }
        gm_t9qkh0rsooc8d t68 = gm_l6;
        gm_t7b7esi3n9r90 t69 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l2));
        gm_tf8kfhcnval78 t70;
        GM_ST(uint8_t, (uint8_t *)&t70 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t70 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t70 + 52, 0);
        GM_ST(uint64_t, (uint8_t *)&t70 + 56, 0);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t70), t69);
        ((uint8_t *)&t70)[48] = 1;
        gm_tg85uffmq8b38 t71;
        GM_ST(uint8_t, (uint8_t *)&t71 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t71 + 6, 0);
        GM_ST(uint64_t, (uint8_t *)&t71 + 8, 0);
        GM_ST(uint32_t, ((uint8_t *)&t71 + 0), t58);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 4), t68);
        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t71 + 16), t70);
        void * t72 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t73 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t72) + 72));
        gm_th7badf4b6ee3 t74;
        memset(&t74, 0, sizeof t74);
        GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)15ULL));
        GM_ST(gm_tg85uffmq8b38, ((uint8_t *)&t74 + 16), t71);
        gm_b0_1 t75 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t57, &t74, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t76 = gm_l3;
        gm_t7b7esi3n9r90 t77;
        memset(&t77, 0, sizeof t77);
        GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t77 + 16), t76);
        F.l2 = t77;
    } else {
        void * t78 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l3));
        gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l5));
        gm_f_layout_a5ovnrm((void *)&gm_l9, (void *)NULL, cx, NULL, t78, t79);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t80 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
        uint8_t t81 = ((t80 == ((uint64_t)0ULL)));
        if (t81) {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
    }
    void * t82 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
    uint32_t t83 = gm_l0;
    gm_tlg23kb3oj23s t84;
    gm_f_local_place_qnf3mb2((void *)&t84, t83);
    gm_t7b7esi3n9r90 t85 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l2));
    gm_t6fp3q71vjrmk t86;
    GM_ST(uint64_t, (uint8_t *)&t86 + 56, 0);
    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t86 + 0), t84);
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t86 + 64), t85);
    void * t87 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
    gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t87) + 72));
    gm_th7badf4b6ee3 t89;
    memset(&t89, 0, sizeof t89);
    GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)25ULL));
    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t89 + 16), t86);
    gm_b0_1 t90 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t82, &t89, t88);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dduu5o8(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_dduu5o8 F;
    gm_b16_8 gm_l0; /* <tmp> */
    uint64_t gm_l1; /* <tmp> */
    gm_b16_8 gm_l4; /* insts */
    GM_STACK_CHECK(&F, 701u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)((&F.h)->up))->l42));
    gm_l0 = t7;
    gm_l1 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l1;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t14 = gm_l1;
                {
                    {
                        void * t15 = GM_LD(void *, (((uint8_t *)t12) + (uintptr_t)t14 * 8));
                        F.l2 = t15;
                        void * t16 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)((&F.h)->up))->l0));
                        uint32_t t17 = gm_f_label_3373lil(t16);
                        F.l3 = t17;
                        void * t18 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)((&F.h)->up))->l0));
                        gm_closure t19 = ((gm_closure){(void *)gm_f_fn_v8tau4j, (void *)&F.h, cx});
                        gm_b16_8 t20 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t18, t19);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_l4 = t20;
                        void * t21 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)((&F.h)->up))->l0));
                        uint32_t t22 = F.l3;
                        gm_b16_8 t23 = gm_l4;
                        gm_tiilmfd16v3fc t24;
                        GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t24 + 0), t22);
                        GM_ST(gm_b16_8, ((uint8_t *)&t24 + 8), t23);
                        void * t25 = F.l2;
                        gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t25) + 120));
                        gm_th7badf4b6ee3 t27;
                        memset(&t27, 0, sizeof t27);
                        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)27ULL));
                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t27 + 16), t24);
                        gm_b0_1 t28 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t21, &t27, t26);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1066, 17);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t29 = gm_l1;
            uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
            uint64_t t32 = r30;
            gm_l1 = t32;
        }
    }
    goto gm_ls33;
gm_ll8:;
    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb1; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb3; }
    }
    goto gm_lad;
gm_ls33:;
gm_lb1:;
    void * t34 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)((&F.h)->up))->l0));
    uint32_t t35 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)((&F.h)->up))->l3));
    void * t36 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)((&F.h)->up))->l1));
    gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 120));
    gm_th7badf4b6ee3 t38;
    memset(&t38, 0, sizeof t38);
    GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)30ULL));
    GM_ST(uint32_t, ((uint8_t *)&t38 + 16), t35);
    gm_b0_1 t39 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t34, &t38, t37);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_drdvs2i(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_drdvs2i F;
    GM_STACK_CHECK(&F, 472u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l0));
    void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l4));
    gm_f_expr_tp86bm9((void *)&F.l0, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l0));
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l5));
    gm_tlg23kb3oj23s t6;
    gm_f_local_place_qnf3mb2((void *)&t6, t5);
    gm_t7b7esi3n9r90 t7 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l0));
    gm_t6fp3q71vjrmk t8;
    GM_ST(uint64_t, (uint8_t *)&t8 + 56, 0);
    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t8 + 0), t6);
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t8 + 64), t7);
    void * t9 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_logic_9o6bp90 *)((&F.h)->up))->l4));
    gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t9) + 72));
    gm_th7badf4b6ee3 t11;
    memset(&t11, 0, sizeof t11);
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)25ULL));
    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t11 + 16), t8);
    gm_b0_1 t12 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t4, &t11, t10);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_k0ofpte(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_k0ofpte F;
    uint32_t gm_l1; /* d */
    gm_t88rcucg1b7uk gm_l2; /* err */
    uint32_t gm_l4; /* variant */
    gm_t88rcucg1b7uk gm_l5; /* ferr */
    uint32_t gm_l6; /* lifted */
    uint32_t gm_l7; /* d */
    GM_STACK_CHECK(&F, 2207u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tf8kfhcnval78 gm_l8;
    gm_t53at80pfdus3 gm_l9;
    gm_tbccvikg6b96e gm_l10;
    gm_t53at80pfdus3 gm_l11;
    uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l6));
    uint8_t t11 = (uint8_t)!t10;
    if (t11) {
        {
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
                gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l5));
                gm_f_word_1sfojus((void *)&gm_l8, (void *)NULL, cx, NULL, t12, t13, ((uint64_t)0ULL));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l8))[48] != 0);
                if (!t14) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&F.l0), ((uint8_t *)&gm_l8), 48);
                gm_closure t15 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l10));
                gm_closure t16 = t15;
                gm_node *ccx17 = t16.cx;
                void * t18 = t16.code;
                gm_b0_1 t19;
                if (GM_PTR_TAGGED(t18)) {
                    ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t18))((*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l0)));
                } else {
                    t19 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t18)(t16.env, ccx17, NULL, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l0)));
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t20 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l5));
        uint32_t t22 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t22;
        void * t23 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
        uint32_t t24 = gm_l1;
        gm_tf8kfhcnval78 t25;
        memset(&t25, 0, sizeof t25);
        gm_t9qkh0rsooc8d t26;
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
        gm_tg85uffmq8b38 t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t27 + 6, 0);
        GM_ST(uint64_t, (uint8_t *)&t27 + 8, 0);
        GM_ST(uint32_t, ((uint8_t *)&t27 + 0), t24);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t27 + 4), t26);
        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t27 + 16), t25);
        void * t28 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t28) + 72));
        gm_th7badf4b6ee3 t30;
        memset(&t30, 0, sizeof t30);
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)15ULL));
        GM_ST(gm_tg85uffmq8b38, ((uint8_t *)&t30 + 16), t27);
        gm_b0_1 t31 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t23, &t30, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_closure t32 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l10));
        uint32_t t33 = gm_l1;
        gm_t7b7esi3n9r90 t34;
        memset(&t34, 0, sizeof t34);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t34 + 16), t33);
        gm_closure t35 = t32;
        gm_node *ccx36 = t35.cx;
        void * t37 = t35.code;
        gm_b0_1 t38;
        if (GM_PTR_TAGGED(t37)) {
            ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t37))(t34);
        } else {
            t38 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t37)(t35.env, ccx36, NULL, &t34);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            void * t39 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l3));
            void * t40 = (void *)(((uint8_t *)t39) + 8);
            void * t41 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l2));
            gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t41));
            gm_f_kind_jcnt4hm((void *)&gm_l9, t40, GM_CONV(gm_csguankko4lmhd, t42));
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)14ULL));
            if (!t44) {
                goto gm_lb4;
            }
            gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l9) + 8));
            gm_l2 = t45;
            gm_t88rcucg1b7uk t46 = gm_l2;
            gm_t7b7esi3n9r90 t47;
            memset(&t47, 0, sizeof t47);
            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t47 + 16), t46);
            F.l3 = t47;
            void * t48 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l3));
            gm_t88rcucg1b7uk t49 = gm_l2;
            gm_f_layout_a5ovnrm((void *)&gm_l10, (void *)NULL, cx, NULL, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t50 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
            uint8_t t51 = (t50 > ((uint64_t)0ULL));
            if (t51) {
                void * t52 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
                uint64_t t53 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l9));
                gm_t88rcucg1b7uk t54 = gm_l2;
                gm_tlcm4lvu3g1a8 t55;
                GM_ST(uint32_t, (uint8_t *)&t55 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t55 + 0), t53);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t55 + 8), t54);
                gm_tfqmtuprvunod t56;
                memset(&t56, 0, sizeof t56);
                GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)4ULL));
                GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t56 + 16), t55);
                gm_tlg23kb3oj23s t57;
                gm_f_project_6hpd48e((void *)&t57, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l7)), &t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t58 = gm_l2;
                void * t59 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l1));
                gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t59) + 72));
                gm_f_load_go4fgkp((void *)&F.l3, (void *)NULL, cx, NULL, t52, &t57, t58, t60);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    uint8_t t61 = (uint8_t)(((((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l2) + 8))[4] != 0);
                    if (!t61) {
                        goto gm_lb6;
                    }
                    uint32_t t62 = GM_LD(uint32_t, (((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l2) + 8));
                    gm_l4 = t62;
                    {
                        {
                            void * t63 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l3));
                            void * t64 = (void *)(((uint8_t *)t63) + 8);
                            gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l5));
                            gm_f_kind_jcnt4hm((void *)&gm_l11, t64, GM_CONV(gm_csguankko4lmhd, t65));
                            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                            uint8_t t67 = (uint8_t)(t66 == ((uint8_t)14ULL));
                            if (!t67) {
                                goto gm_lb8;
                            }
                            gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l11) + 8));
                            gm_l5 = t68;
                            void * t69 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
                            gm_t88rcucg1b7uk t70 = gm_l5;
                            uint32_t t71 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t69, t70);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l6 = t71;
                            void * t72 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
                            uint32_t t73 = gm_l6;
                            uint32_t t74 = gm_l4;
                            gm_t7b7esi3n9r90 t75 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l3));
                            gm_tf8kfhcnval78 t76;
                            GM_ST(uint8_t, (uint8_t *)&t76 + 49, 0);
                            GM_ST(uint16_t, (uint8_t *)&t76 + 50, 0);
                            GM_ST(uint32_t, (uint8_t *)&t76 + 52, 0);
                            GM_ST(uint64_t, (uint8_t *)&t76 + 56, 0);
                            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t76), t75);
                            ((uint8_t *)&t76)[48] = 1;
                            gm_tvv8lnu6a3ppj t77;
                            GM_ST(uint64_t, (uint8_t *)&t77 + 8, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t77 + 0), t73);
                            GM_ST(uint32_t, ((uint8_t *)&t77 + 4), t74);
                            GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t77 + 16), t76);
                            void * t78 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l1));
                            gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t78) + 72));
                            gm_th7badf4b6ee3 t80;
                            memset(&t80, 0, sizeof t80);
                            GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)14ULL));
                            GM_ST(gm_tvv8lnu6a3ppj, ((uint8_t *)&t80 + 16), t77);
                            gm_b0_1 t81 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t72, &t80, t79);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint32_t t82 = gm_l6;
                            gm_t7b7esi3n9r90 t83;
                            memset(&t83, 0, sizeof t83);
                            GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)0ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t83 + 16), t82);
                            F.l3 = t83;
                            goto gm_lb7;
                        }
gm_lb8:;
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
gm_lb7:;
                    goto gm_lb5;
                }
gm_lb6:;
            }
gm_lb5:;
            void * t84 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
            gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l5));
            uint32_t t86 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t84, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t86;
            void * t87 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
            uint32_t t88 = gm_l7;
            gm_t7b7esi3n9r90 t89 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l3));
            gm_tf8kfhcnval78 t90;
            GM_ST(uint8_t, (uint8_t *)&t90 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t90 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t90 + 52, 0);
            GM_ST(uint64_t, (uint8_t *)&t90 + 56, 0);
            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t90), t89);
            ((uint8_t *)&t90)[48] = 1;
            gm_t9qkh0rsooc8d t91;
            GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)3ULL));
            gm_tg85uffmq8b38 t92;
            GM_ST(uint8_t, (uint8_t *)&t92 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t92 + 6, 0);
            GM_ST(uint64_t, (uint8_t *)&t92 + 8, 0);
            GM_ST(uint32_t, ((uint8_t *)&t92 + 0), t88);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t92 + 4), t91);
            GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t92 + 16), t90);
            void * t93 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l1));
            gm_tl874t2atnlh3 t94 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t93) + 72));
            gm_th7badf4b6ee3 t95;
            memset(&t95, 0, sizeof t95);
            GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)15ULL));
            GM_ST(gm_tg85uffmq8b38, ((uint8_t *)&t95 + 16), t92);
            gm_b0_1 t96 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t87, &t95, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_closure t97 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l10));
            uint32_t t98 = gm_l7;
            gm_t7b7esi3n9r90 t99;
            memset(&t99, 0, sizeof t99);
            GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t99 + 16), t98);
            gm_closure t100 = t97;
            gm_node *ccx101 = t100.cx;
            void * t102 = t100.code;
            gm_b0_1 t103;
            if (GM_PTR_TAGGED(t102)) {
                ((void (*)(gm_t7b7esi3n9r90))GM_PTR_UNTAG(t102))(t99);
            } else {
                t103 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))t102)(t100.env, ccx101, NULL, &t99);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb3;
        }
gm_lb4:;
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
gm_lb3:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k3pfo3n(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_k3pfo3n F;
    void * gm_l0; /* tail */
    uint32_t gm_l2; /* r */
    GM_STACK_CHECK(&F, 522u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_block_iujbbjb *)((&F.h)->up))->l0));
    void * t7 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_block_iujbbjb *)((&F.h)->up))->l1));
    gm_b0_1 t8 = gm_f_stmts_ctl06qt((void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            void * t9 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_block_iujbbjb *)((&F.h)->up))->l1));
            uint8_t t10 = (uint8_t)(GM_LD(void *, (((uint8_t *)t9) + 16)) != 0);
            if (!t10) {
                goto gm_lb2;
            }
            void * t11 = GM_LD(void *, (((uint8_t *)t9) + 16));
            gm_l0 = t11;
            void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_block_iujbbjb *)((&F.h)->up))->l0));
            void * t13 = gm_l0;
            gm_f_expr_tp86bm9((void *)&F.l1, (void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    uint8_t t14 = (uint8_t)((((uint8_t *)&((struct gm_fr_block_iujbbjb *)((&F.h)->up))->l9))[4] != 0);
                    if (!t14) {
                        goto gm_lb4;
                    }
                    uint32_t t15 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_block_iujbbjb *)((&F.h)->up))->l9));
                    gm_l2 = t15;
                    void * t16 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_block_iujbbjb *)((&F.h)->up))->l0));
                    uint32_t t17 = gm_l2;
                    gm_tlg23kb3oj23s t18;
                    gm_f_local_place_qnf3mb2((void *)&t18, t17);
                    gm_t7b7esi3n9r90 t19 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l1));
                    gm_t6fp3q71vjrmk t20;
                    GM_ST(uint64_t, (uint8_t *)&t20 + 56, 0);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t20 + 0), t18);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t20 + 64), t19);
                    void * t21 = gm_l0;
                    gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 72));
                    gm_th7badf4b6ee3 t23;
                    memset(&t23, 0, sizeof t23);
                    GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)25ULL));
                    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t23 + 16), t20);
                    gm_b0_1 t24 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t16, &t23, t22);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lldup98(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_lldup98 F;
    void * gm_l0; /* g */
    GM_STACK_CHECK(&F, 687u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l0));
    void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fn_506drml *)((&F.h)->up))->l2));
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_fn_506drml *)((&F.h)->up))->l3));
    gm_b0_1 t7 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t4, t5, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l5)), t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t8 = (uint8_t)(GM_LD(void *, (((uint8_t *)&((struct gm_fr_fn_506drml *)((&F.h)->up))->l2) + 8)) != 0);
            if (!t8) {
                goto gm_lb2;
            }
            void * t9 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_fn_506drml *)((&F.h)->up))->l2) + 8));
            gm_l0 = t9;
            void * t10 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l0));
            void * t11 = gm_l0;
            gm_f_expr_tp86bm9((void *)&F.l1, (void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l0));
            uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_fn_506drml *)((&F.h)->up))->l3));
            gm_t7b7esi3n9r90 t14 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l1));
            gm_tejc6vv5sqno9 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 50, 0);
            GM_ST(uint64_t, (uint8_t *)&t15 + 56, 0);
            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t15 + 0), t14);
            GM_ST(uint8_t, ((uint8_t *)&t15 + 48), ((uint8_t)1));
            GM_ST(uint32_t, ((uint8_t *)&t15 + 52), t13);
            void * t16 = gm_l0;
            gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t16) + 72));
            gm_th7badf4b6ee3 t18;
            memset(&t18, 0, sizeof t18);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)31ULL));
            GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t18 + 16), t15);
            gm_b0_1 t19 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t12, &t18, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t20 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l0));
    gm_t9805qtonbsag t21 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l4));
    void * t22 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_fn_506drml *)((&F.h)->up))->l2) + 16));
    gm_b0_1 t23 = gm_f_store_result_fiq5f1j((void *)NULL, cx, NULL, t20, t21, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t24 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l0));
    void * t25 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_fn_506drml *)((&F.h)->up))->l2) + 16));
    uint8_t t26 = gm_f_diverges_p3okmol(t24, t25);
    uint8_t t27 = (uint8_t)!t26;
    if (t27) {
        void * t28 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l0));
        uint32_t t29 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_match_expr_rfas5u2 *)(((&F.h)->up)->up))->l6));
        void * t30 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_fn_506drml *)((&F.h)->up))->l2) + 16));
        gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t30) + 72));
        gm_th7badf4b6ee3 t32;
        memset(&t32, 0, sizeof t32);
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)30ULL));
        GM_ST(uint32_t, ((uint8_t *)&t32 + 16), t29);
        gm_b0_1 t33 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t28, &t32, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_m7prcu1(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_m7prcu1 F;
    uint32_t gm_l0; /* p */
    uint32_t gm_l3; /* d */
    uint32_t gm_l4; /* w */
    GM_STACK_CHECK(&F, 2209u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tlg23kb3oj23s gm_l5;
    gm_tbccvikg6b96e gm_l6;
    {
        {
            uint8_t t4 = (uint8_t)((((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l12))[4] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l12));
            gm_l0 = t5;
            uint32_t t6 = gm_l0;
            uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l9));
            gm_t7b7esi3n9r90 t8 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l19));
            gm_t592joge1j4uv t9;
            GM_ST(uint64_t, (uint8_t *)&t9 + 56, 0);
            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t9 + 0), t8);
            GM_ST(uint64_t, ((uint8_t *)&t9 + 48), t7);
            gm_tu78tssgij60g t10;
            memset(&t10, 0, sizeof t10);
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)4ULL));
            GM_ST(uint32_t, ((uint8_t *)&t10 + 8), t6);
            gm_tfqmtuprvunod t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t592joge1j4uv, ((uint8_t *)&t11 + 16), t9);
            gm_b16_8 t12 = gm_f_one_n667r26((void *)NULL, cx, NULL, &t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tlg23kb3oj23s t13;
            GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t13 + 0), t10);
            GM_ST(gm_b16_8, ((uint8_t *)&t13 + 40), t12);
            gm_l5 = t13;
            goto gm_lb1;
        }
gm_lb2:;
        uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l9));
        gm_t7b7esi3n9r90 t15 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l19));
        gm_t592joge1j4uv t16;
        GM_ST(uint64_t, (uint8_t *)&t16 + 56, 0);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t16 + 0), t15);
        GM_ST(uint64_t, ((uint8_t *)&t16 + 48), t14);
        gm_tlg23kb3oj23s t17 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l11));
        gm_tfqmtuprvunod t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t592joge1j4uv, ((uint8_t *)&t18 + 16), t16);
        gm_f_project_6hpd48e((void *)&gm_l5, (void *)NULL, cx, NULL, &t17, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
gm_lb1:;
    memmove(((uint8_t *)&F.l1), ((uint8_t *)&gm_l5), 56);
    gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l7));
    gm_t7b7esi3n9r90 t20;
    memset(&t20, 0, sizeof t20);
    GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t20 + 16), t19);
    F.l2 = t20;
    gm_tvdg94bk7aa22 t21 = GM_LD(gm_tvdg94bk7aa22, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l2));
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
    uint8_t t23 = (uint8_t)((t22 == ((uint8_t)37ULL)));
    gm_tvdg94bk7aa22 t24;
    GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t24 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)37ULL));
    if (t23) {
        void * t25 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l6));
        uint32_t t27 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t27;
        void * t28 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
        uint32_t t29 = gm_l3;
        gm_tlg23kb3oj23s t30 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l1));
        gm_tlpcrb3vu6o2d t31;
        GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t31 + 0), t29);
        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t31 + 8), t30);
        void * t32 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 72));
        gm_th7badf4b6ee3 t34;
        memset(&t34, 0, sizeof t34);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)2ULL));
        GM_ST(gm_tlpcrb3vu6o2d, ((uint8_t *)&t34 + 16), t31);
        gm_b0_1 t35 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t28, &t34, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t36 = gm_l3;
        gm_t7b7esi3n9r90 t37;
        memset(&t37, 0, sizeof t37);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t37 + 16), t36);
        F.l2 = t37;
    } else {
        void * t38 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l4));
        gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l7));
        gm_f_layout_a5ovnrm((void *)&gm_l6, (void *)NULL, cx, NULL, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
        uint8_t t41 = (t40 > ((uint64_t)0ULL));
        if (t41) {
            void * t42 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
            gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l7));
            void * t44 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
            gm_tl874t2atnlh3 t45 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t44) + 72));
            gm_tlg23kb3oj23s t46 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l1));
            gm_f_load_go4fgkp((void *)&F.l2, (void *)NULL, cx, NULL, t42, &t46, t43, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    void * t47 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
    void * t48 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
    gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t48));
    uint32_t t50 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t47, t49);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t50;
    void * t51 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
    uint32_t t52 = gm_l4;
    gm_t7b7esi3n9r90 t53 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l2));
    gm_tf8kfhcnval78 t54;
    GM_ST(uint8_t, (uint8_t *)&t54 + 49, 0);
    GM_ST(uint16_t, (uint8_t *)&t54 + 50, 0);
    GM_ST(uint32_t, (uint8_t *)&t54 + 52, 0);
    GM_ST(uint64_t, (uint8_t *)&t54 + 56, 0);
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t54), t53);
    ((uint8_t *)&t54)[48] = 1;
    gm_t9qkh0rsooc8d t55;
    GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)1ULL));
    gm_tg85uffmq8b38 t56;
    GM_ST(uint8_t, (uint8_t *)&t56 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t56 + 6, 0);
    GM_ST(uint64_t, (uint8_t *)&t56 + 8, 0);
    GM_ST(uint32_t, ((uint8_t *)&t56 + 0), t52);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t56 + 4), t55);
    GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t56 + 16), t54);
    void * t57 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
    gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t57) + 72));
    gm_th7badf4b6ee3 t59;
    memset(&t59, 0, sizeof t59);
    GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)15ULL));
    GM_ST(gm_tg85uffmq8b38, ((uint8_t *)&t59 + 16), t56);
    gm_b0_1 t60 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t51, &t59, t58);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t61 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l0));
    uint32_t t62 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l21));
    gm_tlg23kb3oj23s t63;
    gm_f_local_place_qnf3mb2((void *)&t63, t62);
    uint32_t t64 = gm_l4;
    gm_t7b7esi3n9r90 t65;
    memset(&t65, 0, sizeof t65);
    GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t65 + 16), t64);
    gm_t6fp3q71vjrmk t66;
    GM_ST(uint64_t, (uint8_t *)&t66 + 56, 0);
    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t66 + 0), t63);
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t66 + 64), t65);
    void * t67 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_get_d0bmskj *)((&F.h)->up))->l1));
    gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 72));
    gm_th7badf4b6ee3 t69;
    memset(&t69, 0, sizeof t69);
    GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)25ULL));
    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t69 + 16), t66);
    gm_b0_1 t70 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t61, &t69, t68);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mh72upo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_mh72upo F;
    void * gm_l0; /* n */
    uint32_t gm_l1; /* levels */
    uint32_t gm_l2; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t6 = gm_l0;
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)16ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)t6) + 8) + 8));
            gm_l1 = t9;
            uint32_t t10 = GM_LD(uint32_t, (((((uint8_t *)t6) + 8) + 8) + 4));
            gm_l2 = t10;
            {
                {
                    uint32_t t11 = gm_l1;
                    uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_mark_captured_lpp77bm *)((&F.h)->up))->l2));
                    uint8_t t13 = ((t11 == t12));
                    if (!t13) {
                        goto gm_lb4;
                    }
                    uint32_t t14 = gm_l2;
                    uint64_t t15 = (uint64_t)t14;
                    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_mark_captured_lpp77bm *)((&F.h)->up))->l4) + 8));
                    uint8_t t17 = (t15 < t16);
                    if (!t17) {
                        goto gm_lb4;
                    }
                    void * t18 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mark_captured_lpp77bm *)((&F.h)->up))->l4));
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&((struct gm_fr_mark_captured_lpp77bm *)((&F.h)->up))->l4) + 8));
                    uint32_t t20 = gm_l2;
                    uint64_t t21 = (uint64_t)t20;
                    if ((uintptr_t)t21 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t21, t19, gm_srccujoh2vgr9aq, 673, 68);
                    GM_ST(uint8_t, ((((uint8_t *)t18) + (uintptr_t)t21 * 32) + 26), ((uint8_t)1));
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_mo8l3p0(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_mo8l3p0 F;
    gm_b16_8 gm_l0; /* <tmp> */
    uint64_t gm_l1; /* <tmp> */
    gm_t4lnf71mruqpt gm_l2; /* h */
    void * gm_l3; /* x */
    uint32_t gm_l4; /* id */
    uint32_t gm_l6; /* id */
    uint32_t gm_l7; /* i */
    gm_b16_8 gm_l8; /* decls */
    void * gm_l9; /* value */
    GM_STACK_CHECK(&F, 1545u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t53at80pfdus3 gm_l11;
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l2) + 8));
    gm_l0 = t14;
    gm_l1 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t15 = gm_l1;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t20 = gm_l1;
                {
                    {
                        memmove(((uint8_t *)&gm_l2), (((uint8_t *)t18) + (uintptr_t)t20 * 32), 32);
                        {
                            {
                                uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t22 = (uint8_t)(t21 == ((uint8_t)0ULL));
                                if (!t22) {
                                    goto gm_lb7;
                                }
                                void * t23 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
                                gm_l3 = t23;
                                {
                                    {
                                        void * t24 = gm_l3;
                                        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
                                        uint8_t t26 = (uint8_t)(t25 == ((uint8_t)28ULL));
                                        if (!t26) {
                                            goto gm_lb9;
                                        }
                                        uint32_t t27 = GM_LD(uint32_t, ((((uint8_t *)t24) + 8) + 8));
                                        gm_l4 = t27;
                                        void * t28 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l0));
                                        uint32_t t29 = gm_l4;
                                        gm_tkgci6o4gktbf t30;
                                        memset(&t30, 0, sizeof t30);
                                        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
                                        GM_ST(uint32_t, ((uint8_t *)&t30 + 16), t29);
                                        void * t31 = gm_l3;
                                        gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t31) + 72));
                                        gm_th7badf4b6ee3 t33;
                                        memset(&t33, 0, sizeof t33);
                                        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)38ULL));
                                        GM_ST(gm_tkgci6o4gktbf, ((uint8_t *)&t33 + 16), t30);
                                        gm_b0_1 t34 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t28, &t33, t32);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb3;
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                void * t35 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l0));
                                void * t36 = gm_l3;
                                gm_f_expr_tp86bm9((void *)&F.l5, (void *)NULL, cx, NULL, t35, t36);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l6 = ((uint32_t)0ULL);
                                {
                                    {
                                        void * t37 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l3));
                                        void * t38 = (void *)(((uint8_t *)t37) + 8);
                                        void * t39 = gm_l3;
                                        gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t39));
                                        gm_f_kind_jcnt4hm((void *)&gm_l11, t38, GM_CONV(gm_csguankko4lmhd, t40));
                                        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                                        uint8_t t42 = (uint8_t)(t41 == ((uint8_t)25ULL));
                                        if (!t42) {
                                            goto gm_lb11;
                                        }
                                        uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 8));
                                        gm_l7 = t43;
                                        uint32_t t44 = gm_l7;
                                        gm_l6 = t44;
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                }
gm_lb10:;
                                void * t45 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l0));
                                uint32_t t46 = gm_l6;
                                gm_t7b7esi3n9r90 t47 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l5));
                                gm_tf4lk4crue8vt t48;
                                GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t48 + 8, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t48 + 0), t46);
                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t48 + 16), t47);
                                gm_tkgci6o4gktbf t49;
                                GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t49 + 8, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_tf4lk4crue8vt, ((uint8_t *)&t49 + 16), t48);
                                void * t50 = gm_l3;
                                gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t50) + 72));
                                gm_th7badf4b6ee3 t52;
                                memset(&t52, 0, sizeof t52);
                                GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)38ULL));
                                GM_ST(gm_tkgci6o4gktbf, ((uint8_t *)&t52 + 16), t49);
                                gm_b0_1 t53 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t45, &t52, t51);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t55 = (uint8_t)(t54 == ((uint8_t)1ULL));
                                if (!t55) {
                                    goto gm_lb12;
                                }
                                gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
                                gm_l8 = t56;
                                void * t57 = GM_LD(void *, ((((uint8_t *)&gm_l2) + 8) + 16));
                                gm_l9 = t57;
                                void * t58 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l0));
                                void * t59 = gm_l9;
                                gm_f_expr_tp86bm9((void *)&F.l10, (void *)NULL, cx, NULL, t58, t59);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t60 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_with_expr_g16bcnu *)((&F.h)->up))->l0));
                                gm_b16_8 t61 = gm_l8;
                                gm_t7b7esi3n9r90 t62 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l10));
                                gm_tmvmieab1acso t63;
                                GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t61);
                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t63 + 16), t62);
                                gm_tkgci6o4gktbf t64;
                                GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t64 + 8, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)2ULL));
                                GM_ST(gm_tmvmieab1acso, ((uint8_t *)&t64 + 16), t63);
                                void * t65 = gm_l9;
                                gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 72));
                                gm_th7badf4b6ee3 t67;
                                memset(&t67, 0, sizeof t67);
                                GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)38ULL));
                                GM_ST(gm_tkgci6o4gktbf, ((uint8_t *)&t67 + 16), t64);
                                gm_b0_1 t68 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t60, &t67, t66);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb12:;
                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1234, 13);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1233, 9);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t69 = gm_l1;
            uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
            uint64_t t72 = r70;
            gm_l1 = t72;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_roenhir(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_s5r3poa(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_s5r3poa F;
    uint32_t gm_l0; /* c */
    gm_t88rcucg1b7uk gm_l1; /* err */
    GM_STACK_CHECK(&F, 832u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t53at80pfdus3 gm_l3;
    gm_tbccvikg6b96e gm_l4;
    {
        {
            uint8_t t6 = (uint8_t)(((((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l2) + 8))[4] != 0);
            if (!t6) {
                goto gm_lb2;
            }
            uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l2) + 8));
            gm_l0 = t7;
            {
                {
                    void * t8 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l3));
                    void * t9 = (void *)(((uint8_t *)t8) + 8);
                    void * t10 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l2));
                    gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t10));
                    gm_f_kind_jcnt4hm((void *)&gm_l3, t9, GM_CONV(gm_csguankko4lmhd, t11));
                    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t13 = (uint8_t)(t12 == ((uint8_t)14ULL));
                    if (!t13) {
                        goto gm_lb4;
                    }
                    gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 8));
                    gm_l1 = t14;
                    void * t15 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l3));
                    gm_t88rcucg1b7uk t16 = gm_l1;
                    gm_f_layout_a5ovnrm((void *)&gm_l4, (void *)NULL, cx, NULL, t15, t16);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
                    uint8_t t18 = (t17 > ((uint64_t)0ULL));
                    if (t18) {
                        void * t19 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
                        uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l9));
                        gm_t88rcucg1b7uk t21 = gm_l1;
                        gm_tlcm4lvu3g1a8 t22;
                        GM_ST(uint32_t, (uint8_t *)&t22 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t22 + 0), t20);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t22 + 8), t21);
                        gm_tfqmtuprvunod t23;
                        memset(&t23, 0, sizeof t23);
                        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)4ULL));
                        GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t23 + 16), t22);
                        gm_tlg23kb3oj23s t24;
                        gm_f_project_6hpd48e((void *)&t24, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l6)), &t23);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t88rcucg1b7uk t25 = gm_l1;
                        void * t26 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
                        gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t26) + 72));
                        gm_f_load_go4fgkp((void *)&F.l2, (void *)NULL, cx, NULL, t19, &t24, t25, t27);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t28 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
                        uint32_t t29 = gm_l0;
                        gm_tlg23kb3oj23s t30;
                        gm_f_local_place_qnf3mb2((void *)&t30, t29);
                        gm_t7b7esi3n9r90 t31 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l2));
                        gm_t6fp3q71vjrmk t32;
                        GM_ST(uint64_t, (uint8_t *)&t32 + 56, 0);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t32 + 0), t30);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t32 + 64), t31);
                        void * t33 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l1));
                        gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t33) + 72));
                        gm_th7badf4b6ee3 t35;
                        memset(&t35, 0, sizeof t35);
                        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)25ULL));
                        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t35 + 16), t32);
                        gm_b0_1 t36 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t28, &t35, t34);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t37 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l0));
    gm_t9805qtonbsag t38 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l7));
    void * t39 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_else_expr_lrum9ir *)((&F.h)->up))->l2) + 16));
    gm_b0_1 t40 = gm_f_store_result_fiq5f1j((void *)NULL, cx, NULL, t37, t38, t39);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t2qurbh(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_t2qurbh F;
    GM_STACK_CHECK(&F, 310u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l2));
    uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l9));
    gm_b0_1 t5 = gm_f_conds_sqc7llk((void *)NULL, cx, NULL, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l0));
    gm_t9805qtonbsag t7 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l3));
    void * t8 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l2) + 16));
    gm_b0_1 t9 = gm_f_store_result_fiq5f1j((void *)NULL, cx, NULL, t6, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t10 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l0));
    void * t11 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l2) + 16));
    uint8_t t12 = gm_f_diverges_p3okmol(t10, t11);
    uint8_t t13 = (uint8_t)!t12;
    if (t13) {
        void * t14 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l0));
        uint32_t t15 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l8));
        void * t16 = GM_LD(void *, (((uint8_t *)&((struct gm_fr_if_expr_bsdi44h *)(((&F.h)->up)->up))->l2) + 16));
        gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t16) + 72));
        gm_th7badf4b6ee3 t18;
        memset(&t18, 0, sizeof t18);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)30ULL));
        GM_ST(uint32_t, ((uint8_t *)&t18 + 16), t15);
        gm_b0_1 t19 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t14, &t18, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_u0v0t7q(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_v8tau4j(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_v8tau4j F;
    GM_STACK_CHECK(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)(((&F.h)->up)->up))->l0));
    void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fn_dduu5o8 *)((&F.h)->up))->l2));
    uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_fn_dduu5o8 *)((&F.h)->up))->l3));
    gm_b0_1 t5 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t2, t3, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)(((&F.h)->up)->up))->l2)), t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)(((&F.h)->up)->up))->l0));
    uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_pattern_od1h9r0 *)(((&F.h)->up)->up))->l43));
    void * t8 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fn_dduu5o8 *)((&F.h)->up))->l2));
    gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t8) + 120));
    gm_th7badf4b6ee3 t10;
    memset(&t10, 0, sizeof t10);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)30ULL));
    GM_ST(uint32_t, ((uint8_t *)&t10 + 16), t7);
    gm_b0_1 t11 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t6, &t10, t9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vkfbupa(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_vkfbupa F;
    void * gm_l0; /* n */
    void * gm_l1; /* x */
    void * gm_l2; /* x */
    gm_tao63pk3r18v5 gm_l3; /* sl */
    GM_STACK_CHECK(&F, 214u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l4;
    gm_t53at80pfdus3 gm_l5;
    void * t12 = gm_l0;
    {
        {
            {
                {
                    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
                    uint8_t t14 = (uint8_t)(t13 == ((uint8_t)24ULL));
                    if (!t14) {
                        goto gm_lb4;
                    }
                    void * t15 = GM_LD(void *, ((((uint8_t *)t12) + 8) + 8));
                    gm_l1 = t15;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
                    uint8_t t17 = (uint8_t)(t16 == ((uint8_t)25ULL));
                    if (!t17) {
                        goto gm_lb5;
                    }
                    void * t18 = GM_LD(void *, ((((uint8_t *)t12) + 8) + 8));
                    gm_l1 = t18;
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            void * t19 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_escape_avsr9t6 *)((&F.h)->up))->l0));
            void * t20 = gm_l1;
            gm_f_mark_root_b973vvi(t19, t20);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)21ULL));
            if (!t22) {
                goto gm_lb6;
            }
            memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t12) + 8) + 8), 32);
            void * t23 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_escape_avsr9t6 *)((&F.h)->up))->l0));
            void * t24 = GM_LD(void *, ((uint8_t *)t23));
            void * t25 = (void *)(((uint8_t *)t24) + 8);
            void * t26 = GM_LD(void *, ((uint8_t *)&gm_l3));
            gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t26));
            gm_f_kind_jcnt4hm((void *)&gm_l5, t25, GM_CONV(gm_csguankko4lmhd, t27));
            {
                {
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)11ULL));
                    if (!t29) {
                        goto gm_lb8;
                    }
                    gm_l4 = ((uint8_t)1);
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    gm_l4 = ((uint8_t)0);
                    goto gm_lb7;
                }
gm_lb9:;
                gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 132, 30);
            }
gm_lb7:;
            uint8_t t30 = gm_l4;
            if (t30) {
                void * t31 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_escape_avsr9t6 *)((&F.h)->up))->l0));
                void * t32 = GM_LD(void *, ((uint8_t *)&gm_l3));
                gm_f_mark_root_b973vvi(t31, t32);
            }
            goto gm_lb1;
        }
gm_lb6:;
        {
            goto gm_lb1;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 130, 9);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_get_d0bmskj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3) {
    struct gm_fr_get_d0bmskj F;
    gm_b16_8 gm_l3; /* args */
    gm_t88rcucg1b7uk gm_l5; /* usize_ty */
    gm_t88rcucg1b7uk gm_l8; /* pointee */
    void * gm_l10; /* base */
    uint64_t gm_l14; /* n */
    uint8_t gm_l15; /* is_const */
    gm_t88rcucg1b7uk gm_l16; /* el */
    gm_t88rcucg1b7uk gm_l18; /* pt */
    uint32_t gm_l20; /* d */
    uint32_t gm_l22; /* ok */
    gm_b16_8 gm_l23; /* found */
    gm_b16_8 gm_l24; /* missing */
    GM_STACK_CHECK(&F, 3510u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    F.l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l25;
    gm_t53at80pfdus3 gm_l26;
    gm_tbccvikg6b96e gm_l27;
    gm_t53at80pfdus3 gm_l28;
    gm_tfonjjajjj3jq gm_l29;
    gm_tlg23kb3oj23s gm_l30;
    gm_tfonjjajjj3jq gm_l31;
    gm_tlg23kb3oj23s gm_l32;
    {
        void * t14 = F.l0;
        void * t15 = GM_LD(void *, ((uint8_t *)t14));
        F.l4 = t15;
        void * t16 = F.l4;
        void * t17 = (void *)(((uint8_t *)t16) + 8);
        gm_t88rcucg1b7uk t18 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t18;
        {
            {
                void * t19 = F.l4;
                void * t20 = (void *)(((uint8_t *)t19) + 8);
                void * t21 = F.l1;
                gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t21));
                gm_f_kind_jcnt4hm((void *)&gm_l25, t20, GM_CONV(gm_csguankko4lmhd, t22));
                uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                uint8_t t24 = (uint8_t)(t23 == ((uint8_t)13ULL));
                if (!t24) {
                    goto gm_lb2;
                }
                gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l25) + 8));
                F.l6 = t25;
                gm_t88rcucg1b7uk t26 = F.l6;
                F.l7 = t26;
                gm_tvdg94bk7aa22 t27 = F.l2;
                uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
                uint8_t t29 = (uint8_t)((t28 == ((uint8_t)37ULL)));
                gm_tvdg94bk7aa22 t30;
                GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t30 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)37ULL));
                if (t29) {
                    {
                        {
                            void * t31 = F.l4;
                            void * t32 = (void *)(((uint8_t *)t31) + 8);
                            gm_t88rcucg1b7uk t33 = F.l6;
                            gm_f_kind_jcnt4hm((void *)&gm_l26, t32, GM_CONV(gm_csguankko4lmhd, t33));
                            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)8ULL));
                            if (!t35) {
                                goto gm_lb4;
                            }
                            gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l26) + 8) + 4));
                            gm_l8 = t36;
                            gm_t88rcucg1b7uk t37 = gm_l8;
                            F.l7 = t37;
                            goto gm_lb3;
                        }
gm_lb4:;
                    }
gm_lb3:;
                }
                void * t38 = F.l4;
                gm_t88rcucg1b7uk t39 = F.l7;
                gm_f_layout_a5ovnrm((void *)&gm_l27, (void *)NULL, cx, NULL, t38, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&gm_l27));
                F.l9 = t40;
                void * t41 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t42)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t42, gm_srccujoh2vgr9aq, 463, 14);
                void * t43 = (void *)(((uint8_t *)t41) + (uintptr_t)((uint64_t)0ULL) * 88);
                gm_l10 = t43;
                gm_tlg23kb3oj23s t44;
                gm_f_local_place_qnf3mb2((void *)&t44, ((uint32_t)0ULL));
                F.l11 = t44;
                gm_t9805qtonbsag t45;
                memset(&t45, 0, sizeof t45);
                F.l12 = t45;
                gm_t88rcucg1b7uk t46 = gm_l5;
                gm_tfbnciphf3261 t47;
                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t47 + 0), t46);
                GM_ST(uint64_t, ((uint8_t *)&t47 + 8), ((uint64_t)0ULL));
                gm_t7b7esi3n9r90 t48;
                GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t48 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t48 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t48 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t48 + 16), t47);
                F.l13 = t48;
                void * t49 = F.l4;
                void * t50 = (void *)(((uint8_t *)t49) + 8);
                void * t51 = gm_l10;
                gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t51));
                gm_f_kind_jcnt4hm((void *)&gm_l28, t50, GM_CONV(gm_csguankko4lmhd, t52));
                {
                    {
                        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                        uint8_t t54 = (uint8_t)(t53 == ((uint8_t)11ULL));
                        if (!t54) {
                            goto gm_lb6;
                        }
                        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                        gm_l14 = t55;
                        void * t56 = F.l0;
                        void * t57 = gm_l10;
                        gm_f_operand_place_cdle4ls((void *)&gm_l29, (void *)NULL, cx, NULL, t56, t57);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t58 = (uint8_t)((((uint8_t *)&gm_l29))[56] != 0);
                        if (t58) {
                            memmove(((uint8_t *)&gm_l30), ((uint8_t *)&gm_l29), 56);
                        } else {
                            void * t59 = F.l0;
                            void * t60 = F.l1;
                            gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t59, t60);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
                        memmove(((uint8_t *)&F.l11), ((uint8_t *)&gm_l30), 56);
                        gm_tvdg94bk7aa22 t61 = F.l2;
                        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t61) + 0));
                        uint8_t t63 = (uint8_t)((t62 == ((uint8_t)37ULL)));
                        gm_tvdg94bk7aa22 t64;
                        GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t64 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)37ULL));
                        if (t63) {
                            void * t65 = F.l0;
                            void * t66 = (void *)((uint8_t *)&F.l11);
                            gm_f_mark_addressed_t8qojin(t65, t66);
                        }
                        gm_t88rcucg1b7uk t67 = gm_l5;
                        uint64_t t68 = gm_l14;
                        gm_tfbnciphf3261 t69;
                        GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t69 + 0), t67);
                        GM_ST(uint64_t, ((uint8_t *)&t69 + 8), t68);
                        gm_t7b7esi3n9r90 t70;
                        GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t70 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t70 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t70 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t70 + 16), t69);
                        F.l13 = t70;
                        goto gm_lb5;
                    }
gm_lb6:;
                    {
                        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                        uint8_t t72 = (uint8_t)(t71 == ((uint8_t)10ULL));
                        if (!t72) {
                            goto gm_lb7;
                        }
                        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 8));
                        gm_l15 = t73;
                        gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l28) + 8) + 4));
                        gm_l16 = t74;
                        void * t75 = F.l0;
                        void * t76 = gm_l10;
                        gm_f_operand_place_cdle4ls((void *)&gm_l31, (void *)NULL, cx, NULL, t75, t76);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t77 = (uint8_t)((((uint8_t *)&gm_l31))[56] != 0);
                        if (t77) {
                            memmove(((uint8_t *)&gm_l32), ((uint8_t *)&gm_l31), 56);
                        } else {
                            void * t78 = F.l0;
                            void * t79 = F.l1;
                            gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t78, t79);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
                        memmove(((uint8_t *)&F.l17), ((uint8_t *)&gm_l32), 56);
                        void * t80 = F.l4;
                        void * t81 = (void *)(((uint8_t *)t80) + 8);
                        uint8_t t82 = gm_l15;
                        gm_t88rcucg1b7uk t83 = gm_l16;
                        gm_tbngattl1fo51 t84;
                        GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t84 + 0), t82);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t84 + 4), t83);
                        gm_t53at80pfdus3 t85;
                        memset(&t85, 0, sizeof t85);
                        GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)9ULL));
                        GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t85 + 8), t84);
                        gm_t88rcucg1b7uk t86 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t81, &t85);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l18 = t86;
                        void * t87 = F.l0;
                        void * t88 = F.l0;
                        gm_tb31k5l794t7p t89;
                        GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t89 + 0), ((uint32_t)0ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t89 + 8), ((uint64_t)0ULL));
                        gm_tfqmtuprvunod t90;
                        memset(&t90, 0, sizeof t90);
                        GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t90 + 16), t89);
                        gm_tlg23kb3oj23s t91;
                        gm_f_project_6hpd48e((void *)&t91, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l17)), &t90);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t88rcucg1b7uk t92 = gm_l18;
                        void * t93 = F.l1;
                        gm_tl874t2atnlh3 t94 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t93) + 72));
                        gm_t7b7esi3n9r90 t95;
                        gm_f_load_go4fgkp((void *)&t95, (void *)NULL, cx, NULL, t88, &t91, t92, t94);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t88rcucg1b7uk t96 = gm_l18;
                        void * t97 = F.l1;
                        gm_tl874t2atnlh3 t98 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t97) + 72));
                        uint32_t t99 = gm_f_in_temp_t7gs0ds((void *)NULL, cx, NULL, t87, &t95, t96, t98);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t9805qtonbsag t100;
                        GM_ST(uint8_t, (uint8_t *)&t100 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t100 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t100), t99);
                        ((uint8_t *)&t100)[4] = 1;
                        F.l12 = t100;
                        void * t101 = F.l0;
                        void * t102 = F.l0;
                        gm_tlg23kb3oj23s t103 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l17));
                        uint64_t t104 = gm_f_ptr_size_lig81gu((void *)NULL, cx, NULL, t102);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tb31k5l794t7p t105;
                        GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t105 + 0), ((uint32_t)1ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t105 + 8), t104);
                        gm_tfqmtuprvunod t106;
                        memset(&t106, 0, sizeof t106);
                        GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t106 + 16), t105);
                        gm_tlg23kb3oj23s t107;
                        gm_f_project_6hpd48e((void *)&t107, (void *)NULL, cx, NULL, &t103, &t106);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t88rcucg1b7uk t108 = gm_l5;
                        void * t109 = F.l1;
                        gm_tl874t2atnlh3 t110 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t109) + 72));
                        gm_f_load_go4fgkp((void *)&F.l13, (void *)NULL, cx, NULL, t101, &t107, t108, t110);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb5;
                    }
gm_lb7:;
                    {
                        void * t111 = F.l0;
                        void * t112 = F.l1;
                        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t111, t112);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 468, 5);
                }
gm_lb5:;
                gm_t88rcucg1b7uk t113 = gm_l5;
                gm_tfbnciphf3261 t114;
                GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t114 + 0), t113);
                GM_ST(uint64_t, ((uint8_t *)&t114 + 8), ((uint64_t)0ULL));
                gm_t7b7esi3n9r90 t115;
                GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t115 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t115 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t115 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t115 + 16), t114);
                F.l19 = t115;
                {
                    {
                        uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                        uint8_t t117 = (uint8_t)(t116 == ((uint8_t)38ULL));
                        if (!t117) {
                            goto gm_lb10;
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    {
                        uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                        uint8_t t119 = (uint8_t)(t118 == ((uint8_t)39ULL));
                        if (!t119) {
                            goto gm_lb11;
                        }
                        void * t120 = F.l0;
                        gm_t88rcucg1b7uk t121 = gm_l5;
                        uint32_t t122 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t120, t121);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l20 = t122;
                        void * t123 = F.l0;
                        uint32_t t124 = gm_l20;
                        gm_t9qkh0rsooc8d t125;
                        GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)1ULL));
                        gm_t88rcucg1b7uk t126 = gm_l5;
                        gm_tfbnciphf3261 t127;
                        GM_ST(uint32_t, (uint8_t *)&t127 + 4, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t127 + 0), t126);
                        GM_ST(uint64_t, ((uint8_t *)&t127 + 8), ((uint64_t)1ULL));
                        gm_ts6oqppjh0ba3 t128;
                        GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t128 + 1), t125);
                        gm_t7b7esi3n9r90 t129 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l13));
                        gm_t7b7esi3n9r90 t130;
                        GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t130 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t130 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t130 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t130 + 16), t127);
                        gm_t1ctgp8j1v2as t131;
                        GM_ST(uint16_t, (uint8_t *)&t131 + 6, 0);
                        GM_ST(uint64_t, (uint8_t *)&t131 + 8, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t131 + 0), t124);
                        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t131 + 4), t128);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t131 + 16), t129);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t131 + 64), t130);
                        void * t132 = F.l1;
                        gm_tl874t2atnlh3 t133 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t132) + 72));
                        gm_th7badf4b6ee3 t134;
                        memset(&t134, 0, sizeof t134);
                        GM_ST(uint8_t, ((uint8_t *)&t134 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_t1ctgp8j1v2as, ((uint8_t *)&t134 + 16), t131);
                        gm_b0_1 t135 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t123, &t134, t133);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint32_t t136 = gm_l20;
                        gm_t7b7esi3n9r90 t137;
                        memset(&t137, 0, sizeof t137);
                        GM_ST(uint8_t, ((uint8_t *)&t137 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t137 + 16), t136);
                        F.l19 = t137;
                        goto gm_lb9;
                    }
gm_lb11:;
                    {
                        void * t138 = F.l0;
                        void * t139 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t140)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t140, gm_srccujoh2vgr9aq, 490, 29);
                        void * t141 = (void *)(((uint8_t *)t139) + (uintptr_t)((uint64_t)1ULL) * 88);
                        gm_t7b7esi3n9r90 t142;
                        gm_f_expr_tp86bm9((void *)&t142, (void *)NULL, cx, NULL, t138, t141);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l19 = t142;
                        goto gm_lb9;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 483, 5);
                }
gm_lb9:;
                void * t143 = F.l0;
                void * t144 = F.l1;
                gm_t88rcucg1b7uk t145 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t144));
                uint32_t t146 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t143, t145);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l21 = t146;
                void * t147 = F.l0;
                void * t148 = F.l4;
                void * t149 = (void *)(((uint8_t *)t148) + 8);
                gm_t88rcucg1b7uk t150 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t151 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t147, t150);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l22 = t151;
                void * t152 = F.l0;
                uint32_t t153 = gm_l22;
                gm_ts6oqppjh0ba3 t154;
                GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)10ULL));
                gm_t7b7esi3n9r90 t155 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l19));
                gm_t7b7esi3n9r90 t156 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l13));
                gm_t1ctgp8j1v2as t157;
                GM_ST(uint16_t, (uint8_t *)&t157 + 6, 0);
                GM_ST(uint64_t, (uint8_t *)&t157 + 8, 0);
                GM_ST(uint32_t, ((uint8_t *)&t157 + 0), t153);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t157 + 4), t154);
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t157 + 16), t155);
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t157 + 64), t156);
                void * t158 = F.l1;
                gm_tl874t2atnlh3 t159 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t158) + 72));
                gm_th7badf4b6ee3 t160;
                memset(&t160, 0, sizeof t160);
                GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)3ULL));
                GM_ST(gm_t1ctgp8j1v2as, ((uint8_t *)&t160 + 16), t157);
                gm_b0_1 t161 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t152, &t160, t159);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t162 = F.l0;
                gm_closure t163 = ((gm_closure){(void *)gm_f_fn_m7prcu1, (void *)&F.h, cx});
                gm_b16_8 t164 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t162, t163);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l23 = t164;
                void * t165 = F.l0;
                gm_closure t166 = ((gm_closure){(void *)gm_f_fn_36v8itm, (void *)&F.h, cx});
                gm_b16_8 t167 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t165, t166);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t167;
                void * t168 = F.l0;
                uint32_t t169 = gm_l22;
                gm_b16_8 t170 = gm_l23;
                gm_b16_8 t171 = gm_l24;
                gm_t7b7esi3n9r90 t172;
                memset(&t172, 0, sizeof t172);
                GM_ST(uint8_t, ((uint8_t *)&t172 + 0), ((uint8_t)0ULL));
                GM_ST(uint32_t, ((uint8_t *)&t172 + 16), t169);
                gm_t1s3s9ds8udn4 t173;
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t173 + 0), t172);
                GM_ST(gm_b16_8, ((uint8_t *)&t173 + 48), t170);
                GM_ST(gm_b16_8, ((uint8_t *)&t173 + 64), t171);
                void * t174 = F.l1;
                gm_tl874t2atnlh3 t175 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t174) + 72));
                gm_th7badf4b6ee3 t176;
                memset(&t176, 0, sizeof t176);
                GM_ST(uint8_t, ((uint8_t *)&t176 + 0), ((uint8_t)29ULL));
                GM_ST(gm_t1s3s9ds8udn4, ((uint8_t *)&t176 + 16), t173);
                gm_b0_1 t177 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t168, &t176, t175);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t178 = F.l0;
                uint32_t t179 = F.l21;
                gm_tlg23kb3oj23s t180;
                gm_f_local_place_qnf3mb2((void *)&t180, t179);
                void * t181 = F.l1;
                gm_t88rcucg1b7uk t182 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t181));
                void * t183 = F.l1;
                gm_tl874t2atnlh3 t184 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t183) + 72));
                gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t178, &t180, t182, t184);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
gm_lb2:;
            void * t185 = F.l0;
            void * t186 = F.l1;
            gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t185, t186);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
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
inline __attribute__((aligned(4))) void gm_f_if_expr_bsdi44h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_if_expr_bsdi44h F;
    void * gm_l4; /* c */
    gm_b16_8 gm_l6; /* then */
    gm_b16_8 gm_l7; /* else_ */
    gm_b16_8 gm_l10; /* outer */
    GM_STACK_CHECK(&F, 1075u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    void * t4 = F.l0;
    void * t5 = F.l1;
    gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t5));
    gm_t9805qtonbsag t7 = gm_f_result_local_pp1s755((void *)NULL, cx, NULL, t4, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t7;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
    uint8_t t9 = ((t8 == ((uint64_t)1ULL)));
    if (t9) {
        {
            {
                void * t10 = GM_LD(void *, ((uint8_t *)&F.l2));
                uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t11, gm_srccujoh2vgr9aq, 864, 24);
                uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t10) + (uintptr_t)((uint64_t)0ULL) * 24) + 0));
                uint8_t t13 = (uint8_t)(t12 == ((uint8_t)0ULL));
                if (!t13) {
                    goto gm_lb2;
                }
                void * t14 = GM_LD(void *, ((((uint8_t *)t10) + (uintptr_t)((uint64_t)0ULL) * 24) + 8));
                gm_l4 = t14;
                void * t15 = F.l0;
                void * t16 = gm_l4;
                gm_f_expr_tp86bm9((void *)&F.l5, (void *)NULL, cx, NULL, t15, t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t17 = F.l0;
                gm_closure t18 = ((gm_closure){(void *)gm_f_fn_1fu50sd, (void *)&F.h, cx});
                gm_b16_8 t19 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t17, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t19;
                void * t20 = F.l0;
                gm_closure t21 = ((gm_closure){(void *)gm_f_fn_48tkiti, (void *)&F.h, cx});
                gm_b16_8 t22 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t20, t21);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t22;
                void * t23 = F.l0;
                gm_b16_8 t24 = gm_l6;
                gm_b16_8 t25 = gm_l7;
                gm_t7b7esi3n9r90 t26 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l5));
                gm_t1s3s9ds8udn4 t27;
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t27 + 0), t26);
                GM_ST(gm_b16_8, ((uint8_t *)&t27 + 48), t24);
                GM_ST(gm_b16_8, ((uint8_t *)&t27 + 64), t25);
                void * t28 = F.l1;
                gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t28) + 72));
                gm_th7badf4b6ee3 t30;
                memset(&t30, 0, sizeof t30);
                GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)29ULL));
                GM_ST(gm_t1s3s9ds8udn4, ((uint8_t *)&t30 + 16), t27);
                gm_b0_1 t31 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t23, &t30, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t32 = F.l0;
                gm_t9805qtonbsag t33 = F.l3;
                void * t34 = F.l1;
                gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t34));
                void * t36 = F.l1;
                gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 72));
                gm_f_read_result_8csahoh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t32, t33, t35, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    void * t38 = F.l0;
    uint32_t t39 = gm_f_label_3373lil(t38);
    F.l8 = t39;
    void * t40 = F.l0;
    uint32_t t41 = gm_f_label_3373lil(t40);
    F.l9 = t41;
    void * t42 = F.l0;
    gm_closure t43 = ((gm_closure){(void *)gm_f_fn_cfv665f, (void *)&F.h, cx});
    gm_b16_8 t44 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t42, t43);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t44;
    void * t45 = F.l0;
    uint32_t t46 = F.l8;
    gm_b16_8 t47 = gm_l10;
    gm_tiilmfd16v3fc t48;
    GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t48 + 0), t46);
    GM_ST(gm_b16_8, ((uint8_t *)&t48 + 8), t47);
    void * t49 = F.l1;
    gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t49) + 72));
    gm_th7badf4b6ee3 t51;
    memset(&t51, 0, sizeof t51);
    GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)27ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t51 + 16), t48);
    gm_b0_1 t52 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t45, &t51, t50);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t53 = F.l0;
    gm_t9805qtonbsag t54 = F.l3;
    void * t55 = F.l1;
    gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t55));
    void * t57 = F.l1;
    gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t57) + 72));
    gm_f_read_result_8csahoh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t53, t54, t56, t58);
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
inline __attribute__((aligned(4))) uint32_t gm_f_in_temp_t7gs0ds(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_in_temp_t7gs0ds F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* x */
    uint32_t gm_l5; /* d */
    GM_STACK_CHECK(&F, 414u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t5 = (uint8_t)(t4 == ((uint8_t)0ULL));
            if (!t5) {
                goto gm_lb2;
            }
            uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
            gm_l4 = t6;
            uint32_t t7 = gm_l4;
            F.ret = t7;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t8 = gm_l0;
    gm_t88rcucg1b7uk t9 = gm_l2;
    uint32_t t10 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t10;
    void * t11 = gm_l0;
    uint32_t t12 = gm_l5;
    gm_t7b7esi3n9r90 t13 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)a1));
    gm_tf4lk4crue8vt t14;
    GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t14 + 8, 0);
    GM_ST(uint32_t, ((uint8_t *)&t14 + 0), t12);
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t14 + 16), t13);
    gm_tl874t2atnlh3 t15 = gm_l3;
    gm_th7badf4b6ee3 t16;
    memset(&t16, 0, sizeof t16);
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tf4lk4crue8vt, ((uint8_t *)&t16 + 16), t14);
    gm_b0_1 t17 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t11, &t16, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t18 = gm_l5;
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
__attribute__((aligned(4))) void gm_f_index_place_3gfu5dn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, uint8_t a4) {
    struct gm_fr_index_place_3gfu5dn F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    void * gm_l3; /* index */
    uint8_t gm_l4; /* checked */
    void * gm_l5; /* s */
    uint64_t gm_l6; /* esz */
    gm_t88rcucg1b7uk gm_l7; /* usize_ty */
    uint64_t gm_l8; /* len */
    uint8_t gm_l11; /* is_const */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    uint32_t gm_l17; /* t */
    gm_t88rcucg1b7uk gm_l20; /* pointee */
    uint64_t gm_l21; /* len */
    GM_STACK_CHECK(&F, 3373u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tbccvikg6b96e gm_l24;
    gm_t53at80pfdus3 gm_l25;
    gm_tfonjjajjj3jq gm_l26;
    gm_tlg23kb3oj23s gm_l27;
    gm_tfonjjajjj3jq gm_l28;
    gm_tlg23kb3oj23s gm_l29;
    gm_t53at80pfdus3 gm_l30;
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    gm_l5 = t13;
    void * t14 = gm_l5;
    void * t15 = gm_l1;
    gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t15));
    gm_f_layout_a5ovnrm((void *)&gm_l24, (void *)NULL, cx, NULL, t14, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&gm_l24));
    gm_l6 = t17;
    void * t18 = gm_l5;
    void * t19 = (void *)(((uint8_t *)t18) + 8);
    gm_t88rcucg1b7uk t20 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t20;
    void * t21 = gm_l5;
    void * t22 = (void *)(((uint8_t *)t21) + 8);
    void * t23 = gm_l2;
    gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t23));
    gm_f_kind_jcnt4hm((void *)&gm_l25, t22, GM_CONV(gm_csguankko4lmhd, t24));
    {
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)11ULL));
            if (!t26) {
                goto gm_lb2;
            }
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
            gm_l8 = t27;
            void * t28 = gm_l0;
            void * t29 = gm_l2;
            gm_f_operand_place_cdle4ls((void *)&gm_l26, (void *)NULL, cx, NULL, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t30 = (uint8_t)((((uint8_t *)&gm_l26))[56] != 0);
            if (t30) {
                memmove(((uint8_t *)&gm_l27), ((uint8_t *)&gm_l26), 56);
            } else {
                gm_tfonjjajjj3jq t31;
                memset(&t31, 0, sizeof t31);
                memmove(gm_out, ((uint8_t *)&t31), 64);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&F.l9), ((uint8_t *)&gm_l27), 56);
            void * t32 = gm_l0;
            void * t33 = gm_l3;
            gm_f_expr_tp86bm9((void *)&F.l10, (void *)NULL, cx, NULL, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t34 = gm_l4;
            if (t34) {
                void * t35 = gm_l0;
                gm_t88rcucg1b7uk t36 = gm_l7;
                uint64_t t37 = gm_l8;
                gm_tfbnciphf3261 t38;
                GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t38 + 0), t36);
                GM_ST(uint64_t, ((uint8_t *)&t38 + 8), t37);
                void * t39 = gm_l1;
                gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t39) + 72));
                gm_t7b7esi3n9r90 t41;
                GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t41 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t41 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t41 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t41 + 16), t38);
                gm_b0_1 t42 = gm_f_check_bounds_6f9s0q1((void *)NULL, cx, NULL, t35, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l10)), &t41, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint64_t t43 = gm_l6;
            gm_t7b7esi3n9r90 t44 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l10));
            gm_t592joge1j4uv t45;
            GM_ST(uint64_t, (uint8_t *)&t45 + 56, 0);
            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t45 + 0), t44);
            GM_ST(uint64_t, ((uint8_t *)&t45 + 48), t43);
            gm_tfqmtuprvunod t46;
            GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t46 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t592joge1j4uv, ((uint8_t *)&t46 + 16), t45);
            gm_tlg23kb3oj23s t47;
            gm_f_project_6hpd48e((void *)&t47, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l9)), &t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tfonjjajjj3jq t48;
            GM_ST(uint8_t, (uint8_t *)&t48 + 57, 0);
            GM_ST(uint16_t, (uint8_t *)&t48 + 58, 0);
            GM_ST(uint32_t, (uint8_t *)&t48 + 60, 0);
            GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t48), t47);
            ((uint8_t *)&t48)[56] = 1;
            memmove(gm_out, ((uint8_t *)&t48), 64);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)10ULL));
            if (!t50) {
                goto gm_lb3;
            }
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 8));
            gm_l11 = t51;
            gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l25) + 8) + 4));
            gm_l12 = t52;
            void * t53 = gm_l0;
            void * t54 = gm_l2;
            gm_f_operand_place_cdle4ls((void *)&gm_l28, (void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t55 = (uint8_t)((((uint8_t *)&gm_l28))[56] != 0);
            if (t55) {
                memmove(((uint8_t *)&gm_l29), ((uint8_t *)&gm_l28), 56);
            } else {
                gm_tfonjjajjj3jq t56;
                memset(&t56, 0, sizeof t56);
                memmove(gm_out, ((uint8_t *)&t56), 64);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&F.l13), ((uint8_t *)&gm_l29), 56);
            void * t57 = gm_l0;
            gm_tb31k5l794t7p t58;
            GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t58 + 0), ((uint32_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t58 + 8), ((uint64_t)0ULL));
            gm_tfqmtuprvunod t59;
            memset(&t59, 0, sizeof t59);
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t59 + 16), t58);
            gm_tlg23kb3oj23s t60;
            gm_f_project_6hpd48e((void *)&t60, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l13)), &t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t61 = gm_l5;
            void * t62 = (void *)(((uint8_t *)t61) + 8);
            uint8_t t63 = gm_l11;
            gm_t88rcucg1b7uk t64 = gm_l12;
            gm_tbngattl1fo51 t65;
            GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), t63);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t65 + 4), t64);
            gm_t53at80pfdus3 t66;
            memset(&t66, 0, sizeof t66);
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)9ULL));
            GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t66 + 8), t65);
            gm_t88rcucg1b7uk t67 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t62, &t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t68 = gm_l1;
            gm_tl874t2atnlh3 t69 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t68) + 72));
            gm_f_load_go4fgkp((void *)&F.l14, (void *)NULL, cx, NULL, t57, &t60, t67, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t70 = gm_l0;
            void * t71 = gm_l0;
            gm_tlg23kb3oj23s t72 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l13));
            uint64_t t73 = gm_f_ptr_size_lig81gu((void *)NULL, cx, NULL, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tb31k5l794t7p t74;
            GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t74 + 0), ((uint32_t)1ULL));
            GM_ST(uint64_t, ((uint8_t *)&t74 + 8), t73);
            gm_tfqmtuprvunod t75;
            memset(&t75, 0, sizeof t75);
            GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t75 + 16), t74);
            gm_tlg23kb3oj23s t76;
            gm_f_project_6hpd48e((void *)&t76, (void *)NULL, cx, NULL, &t72, &t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t77 = gm_l7;
            void * t78 = gm_l1;
            gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t78) + 72));
            gm_f_load_go4fgkp((void *)&F.l15, (void *)NULL, cx, NULL, t70, &t76, t77, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t80 = gm_l0;
            void * t81 = gm_l3;
            gm_f_expr_tp86bm9((void *)&F.l16, (void *)NULL, cx, NULL, t80, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t82 = gm_l4;
            if (t82) {
                void * t83 = gm_l0;
                void * t84 = gm_l1;
                gm_tl874t2atnlh3 t85 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t84) + 72));
                gm_b0_1 t86 = gm_f_check_bounds_6f9s0q1((void *)NULL, cx, NULL, t83, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l16)), &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l15)), t85);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                    uint8_t t88 = (uint8_t)(t87 == ((uint8_t)0ULL));
                    if (!t88) {
                        goto gm_lb5;
                    }
                    uint32_t t89 = GM_LD(uint32_t, (((uint8_t *)&F.l14) + 16));
                    gm_l17 = t89;
                    uint32_t t90 = gm_l17;
                    uint64_t t91 = gm_l6;
                    gm_t7b7esi3n9r90 t92 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l16));
                    gm_t592joge1j4uv t93;
                    GM_ST(uint64_t, (uint8_t *)&t93 + 56, 0);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t93 + 0), t92);
                    GM_ST(uint64_t, ((uint8_t *)&t93 + 48), t91);
                    gm_tu78tssgij60g t94;
                    memset(&t94, 0, sizeof t94);
                    GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)4ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t94 + 8), t90);
                    gm_tfqmtuprvunod t95;
                    GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t95 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_t592joge1j4uv, ((uint8_t *)&t95 + 16), t93);
                    gm_b16_8 t96 = gm_f_one_n667r26((void *)NULL, cx, NULL, &t95);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tlg23kb3oj23s t97;
                    GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t97 + 0), t94);
                    GM_ST(gm_b16_8, ((uint8_t *)&t97 + 40), t96);
                    gm_tfonjjajjj3jq t98;
                    GM_ST(uint8_t, (uint8_t *)&t98 + 57, 0);
                    GM_ST(uint16_t, (uint8_t *)&t98 + 58, 0);
                    GM_ST(uint32_t, (uint8_t *)&t98 + 60, 0);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t98), t97);
                    ((uint8_t *)&t98)[56] = 1;
                    memmove(gm_out, ((uint8_t *)&t98), 64);
                    goto gm_fdone;
                }
gm_lb5:;
                gm_tfonjjajjj3jq t99;
                memset(&t99, 0, sizeof t99);
                memmove(gm_out, ((uint8_t *)&t99), 64);
                goto gm_fdone;
            }
gm_lb4:;
        }
gm_lb3:;
        {
            uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t101 = (uint8_t)(t100 == ((uint8_t)9ULL));
            if (!t101) {
                goto gm_lb6;
            }
            void * t102 = gm_l0;
            void * t103 = gm_l2;
            gm_f_expr_tp86bm9((void *)&F.l18, (void *)NULL, cx, NULL, t102, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t104 = gm_l0;
            void * t105 = gm_l3;
            gm_f_expr_tp86bm9((void *)&F.l19, (void *)NULL, cx, NULL, t104, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t106 = gm_l0;
            void * t107 = gm_l2;
            gm_t88rcucg1b7uk t108 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t107));
            void * t109 = gm_l2;
            gm_tl874t2atnlh3 t110 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t109) + 72));
            uint32_t t111 = gm_f_in_temp_t7gs0ds((void *)NULL, cx, NULL, t106, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l18)), t108, t110);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t112 = gm_l6;
            gm_t7b7esi3n9r90 t113 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l19));
            gm_t592joge1j4uv t114;
            GM_ST(uint64_t, (uint8_t *)&t114 + 56, 0);
            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t114 + 0), t113);
            GM_ST(uint64_t, ((uint8_t *)&t114 + 48), t112);
            gm_tu78tssgij60g t115;
            memset(&t115, 0, sizeof t115);
            GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)4ULL));
            GM_ST(uint32_t, ((uint8_t *)&t115 + 8), t111);
            gm_tfqmtuprvunod t116;
            GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t116 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t592joge1j4uv, ((uint8_t *)&t116 + 16), t114);
            gm_b16_8 t117 = gm_f_one_n667r26((void *)NULL, cx, NULL, &t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tlg23kb3oj23s t118;
            GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t118 + 0), t115);
            GM_ST(gm_b16_8, ((uint8_t *)&t118 + 40), t117);
            gm_tfonjjajjj3jq t119;
            GM_ST(uint8_t, (uint8_t *)&t119 + 57, 0);
            GM_ST(uint16_t, (uint8_t *)&t119 + 58, 0);
            GM_ST(uint32_t, (uint8_t *)&t119 + 60, 0);
            GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t119), t118);
            ((uint8_t *)&t119)[56] = 1;
            memmove(gm_out, ((uint8_t *)&t119), 64);
            goto gm_fdone;
        }
gm_lb6:;
        {
            uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t121 = (uint8_t)(t120 == ((uint8_t)8ULL));
            if (!t121) {
                goto gm_lb7;
            }
            gm_t88rcucg1b7uk t122 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l25) + 8) + 4));
            gm_l20 = t122;
            {
                {
                    void * t123 = gm_l5;
                    void * t124 = (void *)(((uint8_t *)t123) + 8);
                    gm_t88rcucg1b7uk t125 = gm_l20;
                    gm_f_kind_jcnt4hm((void *)&gm_l30, t124, GM_CONV(gm_csguankko4lmhd, t125));
                    uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&gm_l30) + 0));
                    uint8_t t127 = (uint8_t)(t126 == ((uint8_t)11ULL));
                    if (!t127) {
                        goto gm_lb9;
                    }
                    uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                    gm_l21 = t128;
                    void * t129 = gm_l0;
                    void * t130 = gm_l2;
                    gm_f_expr_tp86bm9((void *)&F.l22, (void *)NULL, cx, NULL, t129, t130);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t131 = gm_l0;
                    void * t132 = gm_l3;
                    gm_f_expr_tp86bm9((void *)&F.l23, (void *)NULL, cx, NULL, t131, t132);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t133 = gm_l4;
                    if (t133) {
                        void * t134 = gm_l0;
                        gm_t88rcucg1b7uk t135 = gm_l7;
                        uint64_t t136 = gm_l21;
                        gm_tfbnciphf3261 t137;
                        GM_ST(uint32_t, (uint8_t *)&t137 + 4, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t137 + 0), t135);
                        GM_ST(uint64_t, ((uint8_t *)&t137 + 8), t136);
                        void * t138 = gm_l1;
                        gm_tl874t2atnlh3 t139 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t138) + 72));
                        gm_t7b7esi3n9r90 t140;
                        GM_ST(uint8_t, (uint8_t *)&t140 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t140 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t140 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t140 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t140 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t140 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t140 + 16), t137);
                        gm_b0_1 t141 = gm_f_check_bounds_6f9s0q1((void *)NULL, cx, NULL, t134, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l23)), &t140, t139);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t142 = gm_l0;
                    void * t143 = gm_l2;
                    gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t143));
                    void * t145 = gm_l2;
                    gm_tl874t2atnlh3 t146 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t145) + 72));
                    uint32_t t147 = gm_f_in_temp_t7gs0ds((void *)NULL, cx, NULL, t142, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l22)), t144, t146);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t148 = gm_l6;
                    gm_t7b7esi3n9r90 t149 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l23));
                    gm_t592joge1j4uv t150;
                    GM_ST(uint64_t, (uint8_t *)&t150 + 56, 0);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t150 + 0), t149);
                    GM_ST(uint64_t, ((uint8_t *)&t150 + 48), t148);
                    gm_tu78tssgij60g t151;
                    memset(&t151, 0, sizeof t151);
                    GM_ST(uint8_t, ((uint8_t *)&t151 + 0), ((uint8_t)4ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t151 + 8), t147);
                    gm_tfqmtuprvunod t152;
                    GM_ST(uint8_t, (uint8_t *)&t152 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t152 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t152 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t152 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_t592joge1j4uv, ((uint8_t *)&t152 + 16), t150);
                    gm_b16_8 t153 = gm_f_one_n667r26((void *)NULL, cx, NULL, &t152);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tlg23kb3oj23s t154;
                    GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t154 + 0), t151);
                    GM_ST(gm_b16_8, ((uint8_t *)&t154 + 40), t153);
                    gm_tfonjjajjj3jq t155;
                    GM_ST(uint8_t, (uint8_t *)&t155 + 57, 0);
                    GM_ST(uint16_t, (uint8_t *)&t155 + 58, 0);
                    GM_ST(uint32_t, (uint8_t *)&t155 + 60, 0);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t155), t154);
                    ((uint8_t *)&t155)[56] = 1;
                    memmove(gm_out, ((uint8_t *)&t155), 64);
                    goto gm_fdone;
                }
gm_lb9:;
                gm_tfonjjajjj3jq t156;
                memset(&t156, 0, sizeof t156);
                memmove(gm_out, ((uint8_t *)&t156), 64);
                goto gm_fdone;
            }
gm_lb8:;
        }
gm_lb7:;
        {
            gm_tfonjjajjj3jq t157;
            memset(&t157, 0, sizeof t157);
            memmove(gm_out, ((uint8_t *)&t157), 64);
            goto gm_fdone;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 606, 5);
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
extern __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_int_bits_6bio9a8(const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_int_bits_6bio9a8_c(gm_tbci5ospk5nnn a0) { return gm_f_int_bits_6bio9a8(&a0); }
inline __attribute__((aligned(4))) void gm_f_int_value_ttu5547(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2) {
    struct gm_fr_int_value_ttu5547 F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l3; /* v */
    GM_STACK_CHECK(&F, 480u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l4;
    gm_tf5srh4mp4b3i gm_l5;
    uint64_t gm_l6;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_integer_8d4f9ek(t4, GM_CONV(gm_csguankko4lmhd, t5));
    uint8_t t7 = (uint8_t)!t6;
    if (t7) {
        gm_tf8kfhcnval78 t8;
        memset(&t8, 0, sizeof t8);
        memmove(gm_out, ((uint8_t *)&t8), 64);
        goto gm_fdone;
    }
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    gm_t88rcucg1b7uk t11 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&gm_l4, (void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
    uint8_t t13 = (t12 > ((uint64_t)8ULL));
    if (t13) {
        gm_t88rcucg1b7uk t14 = gm_l1;
        unsigned __int128 t15 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a2)));
        gm_tl5jbgvjjt15g t16;
        GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t16 + 8, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t16 + 0), t14);
        GM_ST(unsigned __int128, ((uint8_t *)&t16 + 16), t15);
        gm_t7b7esi3n9r90 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t17 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)2ULL));
        GM_ST(gm_tl5jbgvjjt15g, ((uint8_t *)&t17 + 16), t16);
        gm_tf8kfhcnval78 t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 52, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 56, 0);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t18), t17);
        ((uint8_t *)&t18)[48] = 1;
        memmove(gm_out, ((uint8_t *)&t18), 64);
        goto gm_fdone;
    }
    gm_tf5srh4mp4b3i t19 = gm_f_int_bits_6bio9a8(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a2)));
    gm_l5 = t19;
    uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
    if (t20) {
        uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
        gm_l6 = t21;
    } else {
        gm_tf8kfhcnval78 t22;
        memset(&t22, 0, sizeof t22);
        memmove(gm_out, ((uint8_t *)&t22), 64);
        goto gm_fdone;
    }
    uint64_t t23 = gm_l6;
    gm_l3 = t23;
    void * t24 = gm_l0;
    gm_t88rcucg1b7uk t25 = gm_l1;
    uint64_t t26 = gm_l3;
    gm_f_word_1sfojus((void *)&(*(gm_tf8kfhcnval78 *)gm_out), (void *)NULL, cx, NULL, t24, t25, t26);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_bits_pbijkuf(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_bits_pbijkuf F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l4;
    uint8_t gm_l5;
    {
        {
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, ((uint8_t *)t9));
            void * t11 = (void *)(((uint8_t *)t10) + 8);
            gm_t88rcucg1b7uk t12 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l4, t11, GM_CONV(gm_csguankko4lmhd, t12));
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)17ULL));
            if (!t14) {
                goto gm_lb2;
            }
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
            gm_l2 = t15;
            {
                {
                    void * t16 = gm_l0;
                    void * t17 = GM_LD(void *, ((uint8_t *)t16));
                    void * t18 = GM_LD(void *, (((uint8_t *)t17) + 1088));
                    uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)t17) + 1088) + 8));
                    uint32_t t20 = gm_l2;
                    uint64_t t21 = (uint64_t)t20;
                    if ((uintptr_t)t21 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t21, t19, gm_srccujoh2vgr9aq, 597, 21);
                    uint8_t t22 = GM_LD(uint8_t, (((((uint8_t *)t18) + (uintptr_t)t21 * 184) + 24) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)0ULL));
                    if (!t23) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&F.l3), (((((uint8_t *)t18) + (uintptr_t)t21 * 184) + 24) + 8), 48);
                    {
                        {
                            uint8_t t24 = (uint8_t)(((((uint8_t *)&F.l3) + 40))[4] != 0);
                            if (t24) {
                                goto gm_lb6;
                            }
                            gm_l5 = ((uint8_t)0);
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            gm_l5 = ((uint8_t)1);
                            goto gm_lb5;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 598, 12);
                    }
gm_lb5:;
                    uint8_t t25 = gm_l5;
                    F.ret = t25;
                    goto gm_fret;
                }
gm_lb4:;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
gm_lb3:;
        }
gm_lb2:;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_place_expr_6llket3(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_place_expr_6llket3 F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    void * gm_l3; /* base */
    GM_STACK_CHECK(&F, 198u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l4;
    uint8_t gm_l5;
    uint8_t gm_l6;
    gm_t53at80pfdus3 gm_l7;
    void * t16 = gm_l1;
    {
        {
            {
                {
                    uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)15ULL));
                    if (!t18) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)16ULL));
                    if (!t20) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t22 = (uint8_t)(t21 == ((uint8_t)17ULL));
                    if (!t22) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)18ULL));
                    if (!t24) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)14ULL));
                    if (!t26) {
                        goto gm_lb8;
                    }
                    goto gm_lb3;
                }
gm_lb8:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)19ULL));
            if (!t28) {
                goto gm_lb9;
            }
            void * t29 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
            gm_l2 = t29;
            void * t30 = gm_l0;
            void * t31 = gm_l2;
            uint8_t t32 = gm_f_is_place_expr_6llket3(t30, t31);
            gm_l4 = t32;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)20ULL));
            if (!t34) {
                goto gm_lb10;
            }
            void * t35 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
            gm_l3 = t35;
            void * t36 = gm_l0;
            void * t37 = (void *)(((uint8_t *)t36) + 8);
            void * t38 = gm_l3;
            gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t38));
            gm_f_kind_jcnt4hm((void *)&gm_l7, t37, GM_CONV(gm_csguankko4lmhd, t39));
            {
                {
                    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)11ULL));
                    if (!t41) {
                        goto gm_lb12;
                    }
                    gm_l6 = ((uint8_t)1);
                    goto gm_lb11;
                }
gm_lb12:;
                {
                    gm_l6 = ((uint8_t)0);
                    goto gm_lb11;
                }
gm_lb13:;
                gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 584, 30);
            }
gm_lb11:;
            uint8_t t42 = gm_l6;
            if (t42) {
                void * t43 = gm_l0;
                void * t44 = gm_l3;
                uint8_t t45 = gm_f_is_place_expr_6llket3(t43, t44);
                gm_l5 = t45;
            } else {
                gm_l5 = ((uint8_t)1);
            }
            uint8_t t46 = gm_l5;
            gm_l4 = t46;
            goto gm_lb1;
        }
gm_lb10:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb14:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 581, 58);
    }
gm_lb1:;
    uint8_t t47 = gm_l4;
    F.ret = t47;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint32_t gm_f_label_3373lil(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_leave_ehrof6p(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_leave_ehrof6p F;
    GM_STACK_CHECK(&F, 1046u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tf8kfhcnval78 gm_l1;
    gm_tbccvikg6b96e gm_l2;
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l2) + 16));
    uint8_t t3 = ((t2 == ((uint32_t)0ULL)));
    if (t3) {
        void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
        gm_t7b7esi3n9r90 t5 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l0));
        gm_tf8kfhcnval78 t6;
        GM_ST(uint8_t, (uint8_t *)&t6 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t6 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t6 + 52, 0);
        GM_ST(uint64_t, (uint8_t *)&t6 + 56, 0);
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t6), t5);
        ((uint8_t *)&t6)[48] = 1;
        void * t7 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t7) + 72));
        gm_th7badf4b6ee3 t9;
        memset(&t9, 0, sizeof t9);
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)32ULL));
        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t9 + 16), t6);
        gm_b0_1 t10 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t4, &t9, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        void * t11 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l0));
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l2) + 16));
        void * t13 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l3));
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l5));
        gm_t7pnmbsgd9mnq t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
        GM_ST(uint64_t, (uint8_t *)&t15 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)2ULL));
        GM_ST(uint32_t, ((uint8_t *)&t15 + 4), t12);
        gm_f_layout_a5ovnrm((void *)&gm_l2, (void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        uint8_t t17 = (t16 > ((uint64_t)0ULL));
        if (t17) {
            gm_t7b7esi3n9r90 t18 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l0));
            gm_tf8kfhcnval78 t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 52, 0);
            GM_ST(uint64_t, (uint8_t *)&t19 + 56, 0);
            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t19), t18);
            ((uint8_t *)&t19)[48] = 1;
            gm_l1 = t19;
        } else {
            gm_tf8kfhcnval78 t20;
            memset(&t20, 0, sizeof t20);
            gm_l1 = t20;
        }
        gm_tf8kfhcnval78 t21 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&gm_l1));
        gm_t7dcjsiqi95h3 t22;
        GM_ST(gm_t7pnmbsgd9mnq, ((uint8_t *)&t22 + 0), t15);
        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t22 + 16), t21);
        void * t23 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_try_expr_2gjs5f7 *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t23) + 72));
        gm_th7badf4b6ee3 t25;
        memset(&t25, 0, sizeof t25);
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)39ULL));
        GM_ST(gm_t7dcjsiqi95h3, ((uint8_t *)&t25 + 16), t22);
        gm_b0_1 t26 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t11, &t25, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) void gm_f_load_go4fgkp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_load_go4fgkp F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* d */
    GM_STACK_CHECK(&F, 448u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l2;
    uint32_t t4 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t4;
    void * t5 = gm_l0;
    uint32_t t6 = gm_l4;
    gm_tlg23kb3oj23s t7 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l1));
    gm_tlpcrb3vu6o2d t8;
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t8 + 0), t6);
    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t8 + 8), t7);
    gm_tl874t2atnlh3 t9 = gm_l3;
    gm_th7badf4b6ee3 t10;
    memset(&t10, 0, sizeof t10);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)1ULL));
    GM_ST(gm_tlpcrb3vu6o2d, ((uint8_t *)&t10 + 16), t8);
    gm_b0_1 t11 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t5, &t10, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t12 = gm_l4;
    gm_t7b7esi3n9r90 t13;
    memset(&t13, 0, sizeof t13);
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t13 + 16), t12);
    memmove(gm_out, ((uint8_t *)&t13), 48);
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
extern __attribute__((aligned(4))) void gm_f_local_place_qnf3mb2(void *gm_out, uint32_t a0);
__attribute__((aligned(4))) gm_tlg23kb3oj23s gm_f_local_place_qnf3mb2_c(uint32_t a0) { gm_tlg23kb3oj23s gm_r; gm_f_local_place_qnf3mb2(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_local_target_vvq8lnf(void *gm_out, void * a0, gm_te9hrk0u35rev a1);
__attribute__((aligned(4))) gm_t7isn7qnmqh8i gm_f_local_target_vvq8lnf_c(void * a0, gm_te9hrk0u35rev a1) { gm_t7isn7qnmqh8i gm_r; gm_f_local_target_vvq8lnf(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_logic_9o6bp90(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, void * a3, void * a4) {
    struct gm_fr_logic_9o6bp90 F;
    void * gm_l3; /* l */
    gm_b16_8 gm_l7; /* rest */
    gm_b16_8 gm_l8; /* decided */
    GM_STACK_CHECK(&F, 1069u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    F.l2 = a2;
    gm_l3 = a3;
    F.l4 = a4;
    void * t2 = F.l0;
    void * t3 = F.l1;
    gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t3));
    uint32_t t5 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t2, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t5;
    void * t6 = F.l0;
    void * t7 = gm_l3;
    gm_f_expr_tp86bm9((void *)&F.l6, (void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t8 = F.l0;
    gm_closure t9 = ((gm_closure){(void *)gm_f_fn_drdvs2i, (void *)&F.h, cx});
    gm_b16_8 t10 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t10;
    void * t11 = F.l0;
    gm_closure t12 = ((gm_closure){(void *)gm_f_fn_cquk74j, (void *)&F.h, cx});
    gm_b16_8 t13 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t13;
    uint8_t t14 = F.l2;
    if (t14) {
        void * t15 = F.l0;
        gm_b16_8 t16 = gm_l7;
        gm_b16_8 t17 = gm_l8;
        gm_t7b7esi3n9r90 t18 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l6));
        gm_t1s3s9ds8udn4 t19;
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t19 + 0), t18);
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 48), t16);
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 64), t17);
        void * t20 = F.l1;
        gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 72));
        gm_th7badf4b6ee3 t22;
        memset(&t22, 0, sizeof t22);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)29ULL));
        GM_ST(gm_t1s3s9ds8udn4, ((uint8_t *)&t22 + 16), t19);
        gm_b0_1 t23 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t15, &t22, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        void * t24 = F.l0;
        gm_b16_8 t25 = gm_l8;
        gm_b16_8 t26 = gm_l7;
        gm_t7b7esi3n9r90 t27 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l6));
        gm_t1s3s9ds8udn4 t28;
        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t28 + 0), t27);
        GM_ST(gm_b16_8, ((uint8_t *)&t28 + 48), t25);
        GM_ST(gm_b16_8, ((uint8_t *)&t28 + 64), t26);
        void * t29 = F.l1;
        gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t29) + 72));
        gm_th7badf4b6ee3 t31;
        memset(&t31, 0, sizeof t31);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)29ULL));
        GM_ST(gm_t1s3s9ds8udn4, ((uint8_t *)&t31 + 16), t28);
        gm_b0_1 t32 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t24, &t31, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t33 = F.l0;
    uint32_t t34 = F.l5;
    gm_tlg23kb3oj23s t35;
    gm_f_local_place_qnf3mb2((void *)&t35, t34);
    void * t36 = F.l1;
    gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t36));
    void * t38 = F.l1;
    gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 72));
    gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t33, &t35, t37, t39);
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
inline __attribute__((aligned(4))) void gm_f_loop_expr_k6v92f2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_loop_expr_k6v92f2 F;
    gm_t9805qtonbsag gm_l3; /* res */
    gm_b16_8 gm_l7; /* outer */
    GM_STACK_CHECK(&F, 627u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    void * t5 = F.l0;
    void * t6 = F.l1;
    gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t6));
    gm_t9805qtonbsag t8 = gm_f_result_local_pp1s755((void *)NULL, cx, NULL, t5, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t8;
    void * t9 = F.l0;
    uint32_t t10 = gm_f_label_3373lil(t9);
    F.l4 = t10;
    void * t11 = F.l0;
    uint32_t t12 = gm_f_label_3373lil(t11);
    F.l5 = t12;
    void * t13 = F.l0;
    uint32_t t14 = gm_f_label_3373lil(t13);
    F.l6 = t14;
    void * t15 = F.l0;
    void * t16 = (void *)(((uint8_t *)t15) + 144);
    uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
    uint32_t t18 = F.l4;
    uint32_t t19 = F.l6;
    gm_t9805qtonbsag t20 = gm_l3;
    gm_tp4rm55bg84ue t21;
    GM_ST(uint32_t, ((uint8_t *)&t21 + 0), t17);
    GM_ST(uint32_t, ((uint8_t *)&t21 + 4), t18);
    GM_ST(uint32_t, ((uint8_t *)&t21 + 8), t19);
    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t21 + 12), t20);
    gm_tftb1odc739t5 t22 = gm_f_push_e2qt8as(t16, &t21);
    gm_l8 = t22;
    uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srccujoh2vgr9aq, 938, 5);
    {
        for (;;) {
            void * t25 = F.l0;
            uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t25) + 192) + 8));
            uint32_t t27 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
            uint64_t t28 = (uint64_t)t27;
            uint8_t t29 = (t26 <= t28);
            if (!t29) {
                goto gm_lb1;
            }
            {
                void * t30 = F.l0;
                void * t31 = (void *)(((uint8_t *)t30) + 192);
                gm_t9805qtonbsag t32;
                memset(&t32, 0, sizeof t32);
                gm_tftb1odc739t5 t33 = gm_f_push_r4haklv(t31, t32);
                gm_l9 = t33;
                uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srccujoh2vgr9aq, 939, 52);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t35 = F.l0;
    void * t36 = GM_LD(void *, (((uint8_t *)t35) + 192));
    uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)t35) + 192) + 8));
    uint32_t t38 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
    uint64_t t39 = (uint64_t)t38;
    if ((uintptr_t)t39 >= (uintptr_t)(t37)) gm_trap_index((uintptr_t)t39, t37, gm_srccujoh2vgr9aq, 940, 5);
    gm_t9805qtonbsag t40 = gm_l3;
    GM_ST(gm_t9805qtonbsag, (((uint8_t *)t36) + (uintptr_t)t39 * 8), t40);
    void * t41 = F.l0;
    gm_closure t42 = ((gm_closure){(void *)gm_f_fn_cccid3f, (void *)&F.h, cx});
    gm_b16_8 t43 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t41, t42);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t43;
    void * t44 = F.l0;
    uint32_t t45 = F.l4;
    gm_b16_8 t46 = gm_l7;
    gm_tiilmfd16v3fc t47;
    GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t47 + 0), t45);
    GM_ST(gm_b16_8, ((uint8_t *)&t47 + 8), t46);
    void * t48 = F.l1;
    gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t48) + 72));
    gm_th7badf4b6ee3 t50;
    memset(&t50, 0, sizeof t50);
    GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)27ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t50 + 16), t47);
    gm_b0_1 t51 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t44, &t50, t49);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t52 = F.l0;
    gm_t9805qtonbsag t53 = gm_l3;
    void * t54 = F.l1;
    gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t54));
    void * t56 = F.l1;
    gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t56) + 72));
    gm_f_read_result_8csahoh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t52, t53, t55, t57);
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
inline __attribute__((aligned(4))) void gm_f_loop_labels_uvqimfl(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_loop_labels_uvqimfl F;
    void * gm_l0; /* lw */
    uint32_t gm_l1; /* tl */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tp4rm55bg84ue gm_l4; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 214u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 144));
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
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t12) + (uintptr_t)t14 * 20), 20);
                        uint32_t t15 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                        uint32_t t16 = gm_l1;
                        uint8_t t17 = ((t15 == t16));
                        if (t17) {
                            gm_tp4rm55bg84ue t18 = GM_LD(gm_tp4rm55bg84ue, ((uint8_t *)&gm_l4));
                            gm_t7isn7qnmqh8i t19;
                            GM_ST(uint8_t, (uint8_t *)&t19 + 21, 0);
                            GM_ST(uint16_t, (uint8_t *)&t19 + 22, 0);
                            GM_ST(gm_tp4rm55bg84ue, ((uint8_t *)&t19), t18);
                            ((uint8_t *)&t19)[20] = 1;
                            memmove(gm_out, ((uint8_t *)&t19), 24);
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 921, 5);
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
    gm_t7isn7qnmqh8i t24;
    memset(&t24, 0, sizeof t24);
    memmove(gm_out, ((uint8_t *)&t24), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t7isn7qnmqh8i gm_f_loop_labels_uvqimfl_c(void * a0, uint32_t a1) { gm_t7isn7qnmqh8i gm_r; gm_f_loop_labels_uvqimfl(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_lower_rh7s5j6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_lower_rh7s5j6 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* s */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    gm_tbccvikg6b96e gm_l9; /* lay */
    gm_t88rcucg1b7uk gm_l10; /* ret */
    void * gm_l11; /* b */
    GM_STACK_CHECK(&F, 1637u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t5im2v50m3hk6 gm_l14;
    uint8_t gm_l15;
    gm_tf5srh4mp4b3i gm_l16;
    gm_tftb1odc739t5 gm_l17;
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    gm_l2 = t13;
    void * t14 = gm_l2;
    uint32_t t15 = gm_l1;
    gm_f_def_of_m512sbu((void *)&F.l3, t14, t15);
    void * t16 = gm_l2;
    uint32_t t17 = gm_l1;
    gm_trqfvmjtt4sij t18;
    gm_f_list_a3ut5mo((void *)&t18, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t19;
    gm_f_list_nof7vfj((void *)&t19, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t20;
    gm_f_list_0ngbo25((void *)&t20, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t21;
    gm_f_list_gaq8h4q((void *)&t21, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t22;
    gm_f_list_rb5mlsd((void *)&t22, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tm81239bir3h6 t23;
    GM_ST(uint32_t, (uint8_t *)&t23 + 12, 0);
    GM_ST(uint32_t, (uint8_t *)&t23 + 140, 0);
    GM_ST(uint32_t, (uint8_t *)&t23 + 188, 0);
    GM_ST(uint32_t, (uint8_t *)&t23 + 236, 0);
    GM_ST(void *, ((uint8_t *)&t23 + 0), t16);
    GM_ST(uint32_t, ((uint8_t *)&t23 + 8), t17);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t23 + 16), t18);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t23 + 56), t19);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t23 + 96), t20);
    GM_ST(uint32_t, ((uint8_t *)&t23 + 136), ((uint32_t)0ULL));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t23 + 144), t21);
    GM_ST(uint32_t, ((uint8_t *)&t23 + 184), ((uint32_t)0ULL));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t23 + 192), t22);
    GM_ST(uint32_t, ((uint8_t *)&t23 + 232), ((uint32_t)0ULL));
    F.l4 = t23;
    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
    gm_l5 = t25;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t26 = gm_l6;
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t28 = (t26 < t27);
            if (!t28) {
                goto gm_lb1;
            }
            {
                uint64_t t29 = gm_l6;
                void * t30 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t32 = gm_l6;
                gm_tsoie1ppgdvcg t33 = GM_LD(gm_tsoie1ppgdvcg, (((uint8_t *)t30) + (uintptr_t)t32 * 40));
                gm_t5im2v50m3hk6 t34;
                GM_ST(uint64_t, ((uint8_t *)&t34 + 0), t29);
                GM_ST(gm_tsoie1ppgdvcg, ((uint8_t *)&t34 + 8), t33);
                gm_l14 = t34;
                {
                    {
                        uint64_t t35 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                        gm_l7 = t35;
                        memmove(((uint8_t *)&F.l8), (((uint8_t *)&gm_l14) + 8), 40);
                        void * t36 = gm_l2;
                        gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                        gm_f_layout_a5ovnrm((void *)&gm_l9, (void *)NULL, cx, NULL, t36, t37);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t38 = (void *)(((uint8_t *)&F.l4) + 16);
                        gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                        uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
                        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                        uint64_t t44 = gm_l7;
                        uint32_t t45 = (uint32_t)t44;
                        gm_tf5srh4mp4b3i t46 = gm_f_index_of_41b084m(t43, t45);
                        gm_l16 = t46;
                        {
                            {
                                uint8_t t47 = (uint8_t)((((uint8_t *)&gm_l16))[8] != 0);
                                if (t47) {
                                    goto gm_lb7;
                                }
                                gm_l15 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l15 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 32, 81);
                        }
gm_lb6:;
                        uint8_t t48 = gm_l15;
                        gm_t0o0svomecjg6 t49;
                        GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t49 + 28, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t49 + 0), t39);
                        GM_ST(uint64_t, ((uint8_t *)&t49 + 8), t40);
                        GM_ST(uint64_t, ((uint8_t *)&t49 + 16), t41);
                        GM_ST(uint8_t, ((uint8_t *)&t49 + 24), t48);
                        GM_ST(uint8_t, ((uint8_t *)&t49 + 25), ((uint8_t)0));
                        GM_ST(uint8_t, ((uint8_t *)&t49 + 26), ((uint8_t)0));
                        GM_ST(uint8_t, ((uint8_t *)&t49 + 27), ((uint8_t)0));
                        gm_tftb1odc739t5 t50 = gm_f_push_u8i76or(t38, &t49);
                        gm_l17 = t50;
                        uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                        if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srccujoh2vgr9aq, 32, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 30, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t52 = gm_l6;
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            uint64_t t55 = r53;
            gm_l6 = t55;
        }
    }
gm_lb1:;
    void * t56 = gm_l0;
    uint32_t t57 = gm_l1;
    uint32_t t58 = gm_l1;
    gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 16));
    gm_f_mark_captured_lpp77bm(t56, t57, ((uint32_t)1ULL), t58, t59);
    void * t60 = gm_l2;
    void * t61 = GM_LD(void *, (((uint8_t *)t60) + 344));
    uint64_t t62 = GM_LD(uint64_t, ((((uint8_t *)t60) + 344) + 8));
    uint32_t t63 = gm_l1;
    uint64_t t64 = (uint64_t)t63;
    if ((uintptr_t)t64 >= (uintptr_t)(t62)) gm_trap_index((uintptr_t)t64, t62, gm_srccujoh2vgr9aq, 35, 12);
    gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t61) + (uintptr_t)t64 * 136) + 56));
    gm_l10 = t65;
    {
        {
            uint8_t t66 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l3) + 40)) != 0);
            if (!t66) {
                goto gm_lb10;
            }
            void * t67 = GM_LD(void *, (((uint8_t *)&F.l3) + 40));
            gm_l11 = t67;
            void * t68 = (void *)((uint8_t *)&F.l4);
            void * t69 = gm_l11;
            gm_f_expr_tp86bm9((void *)&F.l12, (void *)NULL, cx, NULL, t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t70 = (void *)((uint8_t *)&F.l4);
            void * t71 = gm_l11;
            uint8_t t72 = gm_f_diverges_p3okmol(t70, t71);
            uint8_t t73 = (uint8_t)!t72;
            if (t73) {
                void * t74 = (void *)((uint8_t *)&F.l4);
                gm_t7b7esi3n9r90 t75 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l12));
                gm_tf8kfhcnval78 t76;
                GM_ST(uint8_t, (uint8_t *)&t76 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&t76 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&t76 + 52, 0);
                GM_ST(uint64_t, (uint8_t *)&t76 + 56, 0);
                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t76), t75);
                ((uint8_t *)&t76)[48] = 1;
                void * t77 = gm_l11;
                gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t77) + 72));
                gm_th7badf4b6ee3 t79;
                memset(&t79, 0, sizeof t79);
                GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)32ULL));
                GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t79 + 16), t76);
                gm_b0_1 t80 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t74, &t79, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb9;
        }
gm_lb10:;
    }
gm_lb9:;
    uint32_t t81 = gm_l1;
    gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 16));
    gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 56));
    gm_t88rcucg1b7uk t84 = gm_l10;
    gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 96));
    uint32_t t86 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 136));
    gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 144));
    uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 184));
    gm_tqh25f25b69k5 t89;
    GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t89 + 44, 0);
    GM_ST(uint32_t, (uint8_t *)&t89 + 68, 0);
    GM_ST(uint32_t, (uint8_t *)&t89 + 92, 0);
    GM_ST(uint32_t, ((uint8_t *)&t89 + 0), t81);
    GM_ST(gm_b16_8, ((uint8_t *)&t89 + 8), t82);
    GM_ST(gm_b16_8, ((uint8_t *)&t89 + 24), t83);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t89 + 40), t84);
    GM_ST(gm_b16_8, ((uint8_t *)&t89 + 48), t85);
    GM_ST(uint32_t, ((uint8_t *)&t89 + 64), t86);
    GM_ST(gm_b16_8, ((uint8_t *)&t89 + 72), t87);
    GM_ST(uint32_t, ((uint8_t *)&t89 + 88), t88);
    F.l13 = t89;
    uint32_t t90 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 232));
    uint8_t t91 = (t90 > ((uint32_t)0ULL));
    if (t91) {
        void * t92 = gm_l2;
        void * t93 = (void *)((uint8_t *)&F.l13);
        uint32_t t94 = gm_f_elide_checks_s9bgk65((void *)NULL, cx, NULL, t92, t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    memmove(gm_out, ((uint8_t *)&F.l13), 96);
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
inline __attribute__((aligned(4))) void gm_f_mark_addressed_t8qojin(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_addressed_t8qojin F;
    void * gm_l0; /* lw */
    void * gm_l1; /* p */
    uint32_t gm_l2; /* l */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t9 = gm_l1;
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t9) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)t9) + 8));
            gm_l2 = t12;
            void * t13 = gm_l1;
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 40));
            gm_l3 = t14;
            gm_l4 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t15 = gm_l4;
                    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t17 = (t15 < t16);
                    if (!t17) {
                        goto gm_lb3;
                    }
                    {
                        void * t18 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t20 = gm_l4;
                        {
                            {
                                memmove(((uint8_t *)&F.l5), (((uint8_t *)t18) + (uintptr_t)t20 * 80), 80);
                                uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t22 = (uint8_t)((t21 == ((uint8_t)3ULL)));
                                gm_tfqmtuprvunod t23 = GM_LD(gm_tfqmtuprvunod, ((uint8_t *)&F.l5));
                                gm_tfqmtuprvunod t24;
                                memset(&t24, 0, sizeof t24);
                                GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)3ULL));
                                if (t22) {
                                    F.ret = GM_ZERO(gm_b0_1);
                                    goto gm_fret;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 653, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t25 = gm_l4;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l4 = t28;
                }
            }
gm_lb3:;
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 16));
            uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)t29) + 16) + 8));
            uint32_t t32 = gm_l2;
            uint64_t t33 = (uint64_t)t32;
            if ((uintptr_t)t33 >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)t33, t31, gm_srccujoh2vgr9aq, 656, 5);
            GM_ST(uint8_t, ((((uint8_t *)t30) + (uintptr_t)t33 * 32) + 25), ((uint8_t)1));
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_mark_captured_lpp77bm(void * a0, uint32_t a1, uint32_t a2, uint32_t a3, gm_b16_8 a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_captured_lpp77bm F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    uint32_t gm_l3; /* outer */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l7; /* g */
    void * gm_l8; /* b */
    GM_STACK_CHECK(&F, 290u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    gm_l3 = a3;
    F.l4 = a4;
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, (((uint8_t *)t9) + 8));
    uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t9) + 8) + 8));
    uint32_t t12 = gm_l3;
    uint64_t t13 = (uint64_t)t12;
    if ((uintptr_t)t13 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t13, t11, gm_srccujoh2vgr9aq, 669, 14);
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t10) + (uintptr_t)t13 * 40));
    gm_l5 = t14;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t16 = gm_l6;
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t18 = (t16 < t17);
            if (!t18) {
                goto gm_lb1;
            }
            {
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t21 = gm_l6;
                {
                    {
                        uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)t19) + (uintptr_t)t21 * 4));
                        gm_l7 = t22;
                        {
                            {
                                void * t23 = gm_l0;
                                void * t24 = GM_LD(void *, ((uint8_t *)t23));
                                void * t25 = GM_LD(void *, (((uint8_t *)t24) + 384));
                                uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t24) + 384) + 8));
                                uint32_t t27 = gm_l7;
                                uint64_t t28 = (uint64_t)t27;
                                if ((uintptr_t)t28 >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)t28, t26, gm_srccujoh2vgr9aq, 670, 17);
                                uint8_t t29 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t25) + (uintptr_t)t28 * 64) + 40)) != 0);
                                if (!t29) {
                                    goto gm_lb7;
                                }
                                void * t30 = GM_LD(void *, ((((uint8_t *)t25) + (uintptr_t)t28 * 64) + 40));
                                gm_l8 = t30;
                                void * t31 = gm_l8;
                                gm_closure t32 = ((gm_closure){(void *)gm_f_fn_mh72upo, (void *)&F.h, cx});
                                gm_f_visit_ajtfk2n(t31, t32);
                                if (gm_unw.kind) {
                                    goto gm_ll15;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        void * t33 = gm_l0;
                        uint32_t t34 = gm_l1;
                        uint32_t t35 = F.l2;
                        uint32_t r36; bool o37 = __builtin_add_overflow(t35, ((uint32_t)1ULL), &r36);
                        if (o37) gm_trap("integer overflow in `+`", gm_srccujoh2vgr9aq, 677, 30);
                        uint32_t t38 = r36;
                        uint32_t t39 = gm_l7;
                        gm_b16_8 t40 = F.l4;
                        gm_f_mark_captured_lpp77bm(t33, t34, t38, t39, t40);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 669, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t41 = gm_l6;
            uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
            uint64_t t44 = r42;
            gm_l6 = t44;
        }
    }
    goto gm_ls45;
gm_ll15:;
    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb1; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb3; }
    }
    goto gm_lad;
gm_ls45:;
gm_lb1:;
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
inline __attribute__((aligned(4))) void gm_f_mark_root_b973vvi(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_root_b973vvi F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    uint32_t gm_l2; /* l */
    GM_STACK_CHECK(&F, 138u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t9805qtonbsag gm_l3;
    uint32_t gm_l4;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = gm_l1;
    gm_t9805qtonbsag t5 = gm_f_root_local_40frg7f(t3, t4);
    gm_l3 = t5;
    uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
    if (t6) {
        uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
        gm_l4 = t7;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    uint32_t t8 = gm_l4;
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
        if ((uintptr_t)t18 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t18, t16, gm_srccujoh2vgr9aq, 663, 41);
        GM_ST(uint8_t, ((((uint8_t *)t15) + (uintptr_t)t18 * 32) + 25), ((uint8_t)1));
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_match_expr_rfas5u2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_match_expr_rfas5u2 F;
    void * gm_l2; /* scrutinee */
    gm_b16_8 gm_l7; /* body */
    GM_STACK_CHECK(&F, 593u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    F.l3 = a3;
    gm_tfonjjajjj3jq gm_l8;
    gm_tlg23kb3oj23s gm_l9;
    void * t2 = F.l0;
    void * t3 = F.l1;
    gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t3));
    gm_t9805qtonbsag t5 = gm_f_result_local_pp1s755((void *)NULL, cx, NULL, t2, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t5;
    void * t6 = F.l0;
    void * t7 = gm_l2;
    gm_f_operand_place_cdle4ls((void *)&gm_l8, (void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l8))[56] != 0);
    if (t8) {
        memmove(((uint8_t *)&gm_l9), ((uint8_t *)&gm_l8), 56);
    } else {
        void * t9 = F.l0;
        void * t10 = F.l1;
        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l5), ((uint8_t *)&gm_l9), 56);
    void * t11 = F.l0;
    uint32_t t12 = gm_f_label_3373lil(t11);
    F.l6 = t12;
    void * t13 = F.l0;
    gm_closure t14 = ((gm_closure){(void *)gm_f_fn_506drml, (void *)&F.h, cx});
    gm_b16_8 t15 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t15;
    void * t16 = F.l0;
    uint32_t t17 = F.l6;
    gm_b16_8 t18 = gm_l7;
    gm_tiilmfd16v3fc t19;
    GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t19 + 0), t17);
    GM_ST(gm_b16_8, ((uint8_t *)&t19 + 8), t18);
    void * t20 = F.l1;
    gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 72));
    gm_th7badf4b6ee3 t22;
    memset(&t22, 0, sizeof t22);
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)27ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t22 + 16), t19);
    gm_b0_1 t23 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t16, &t22, t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t24 = F.l0;
    gm_t9805qtonbsag t25 = F.l4;
    void * t26 = F.l1;
    gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t26));
    void * t28 = F.l1;
    gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t28) + 72));
    gm_f_read_result_8csahoh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t24, t25, t27, t29);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_one_n667r26(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_operand_place_cdle4ls(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_operand_place_cdle4ls F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    uint32_t gm_l3; /* l */
    GM_STACK_CHECK(&F, 699u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((uint8_t *)t4));
            void * t6 = gm_l1;
            uint8_t t7 = gm_f_is_place_expr_6llket3(t5, t6);
            if (!t7) {
                goto gm_lb2;
            }
            void * t8 = gm_l0;
            void * t9 = gm_l1;
            uint8_t t10 = gm_f_bits_field_cdqu5on(t8, t9);
            uint8_t t11 = (uint8_t)!t10;
            if (!t11) {
                goto gm_lb2;
            }
            void * t12 = gm_l0;
            void * t13 = gm_l1;
            gm_f_place_oohr45n((void *)&(*(gm_tfonjjajjj3jq *)gm_out), (void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t14 = gm_l0;
    void * t15 = gm_l1;
    gm_f_expr_tp86bm9((void *)&F.l2, (void *)NULL, cx, NULL, t14, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t16 = gm_l0;
    void * t17 = gm_l1;
    gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t17));
    uint32_t t19 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t16, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t19;
    void * t20 = gm_l0;
    uint32_t t21 = gm_l3;
    gm_tlg23kb3oj23s t22;
    gm_f_local_place_qnf3mb2((void *)&t22, t21);
    gm_t7b7esi3n9r90 t23 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l2));
    gm_t6fp3q71vjrmk t24;
    GM_ST(uint64_t, (uint8_t *)&t24 + 56, 0);
    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t24 + 0), t22);
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t24 + 64), t23);
    void * t25 = gm_l1;
    gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t25) + 72));
    gm_th7badf4b6ee3 t27;
    memset(&t27, 0, sizeof t27);
    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)25ULL));
    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t27 + 16), t24);
    gm_b0_1 t28 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t20, &t27, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t29 = gm_l3;
    gm_tlg23kb3oj23s t30;
    gm_f_local_place_qnf3mb2((void *)&t30, t29);
    gm_tfonjjajjj3jq t31;
    GM_ST(uint8_t, (uint8_t *)&t31 + 57, 0);
    GM_ST(uint16_t, (uint8_t *)&t31 + 58, 0);
    GM_ST(uint32_t, (uint8_t *)&t31 + 60, 0);
    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t31), t30);
    ((uint8_t *)&t31)[56] = 1;
    memmove(gm_out, ((uint8_t *)&t31), 64);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_pattern_od1h9r0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint32_t a3) {
    struct gm_fr_pattern_od1h9r0 F;
    void * gm_l4; /* s */
    gm_t88rcucg1b7uk gm_l5; /* bool_ty */
    uint32_t gm_l6; /* l */
    void * gm_l8; /* c */
    uint32_t gm_l11; /* d */
    uint8_t gm_l14; /* inclusive */
    uint32_t gm_l18; /* ge */
    uint32_t gm_l19; /* le */
    uint32_t gm_l20; /* d */
    void * gm_l21; /* q */
    uint32_t gm_l22; /* d */
    void * gm_l23; /* q */
    void * gm_l24; /* q */
    uint32_t gm_l25; /* d */
    uint32_t gm_l26; /* v */
    void * gm_l27; /* payload */
    uint32_t gm_l28; /* d */
    void * gm_l29; /* q */
    gm_b16_8 gm_l30; /* fs */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l34; /* x */
    uint32_t gm_l35; /* d */
    uint32_t gm_l36; /* l */
    gm_b16_8 gm_l37; /* offs */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l40; /* x */
    uint64_t gm_l41; /* off */
    gm_b16_8 gm_l44; /* body */
    GM_STACK_CHECK(&F, 9707u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    F.l3 = a3;
    gm_tbccvikg6b96e gm_l45;
    gm_tf8kfhcnval78 gm_l46;
    gm_t7b7esi3n9r90 gm_l47;
    gm_tf8kfhcnval78 gm_l48;
    gm_t7b7esi3n9r90 gm_l49;
    gm_ts6oqppjh0ba3 gm_l50;
    uint8_t gm_l51;
    gm_tbccvikg6b96e gm_l52;
    uint64_t gm_l53;
    {
        void * t31 = F.l0;
        void * t32 = GM_LD(void *, ((uint8_t *)t31));
        gm_l4 = t32;
        void * t33 = gm_l4;
        void * t34 = (void *)(((uint8_t *)t33) + 8);
        gm_t88rcucg1b7uk t35 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t35;
        void * t36 = F.l1;
        {
            {
                {
                    uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t38 = (uint8_t)(t37 == ((uint8_t)0ULL));
                    if (!t38) {
                        goto gm_lb2;
                    }
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t40 = (uint8_t)(t39 == ((uint8_t)1ULL));
                    if (!t40) {
                        goto gm_lb3;
                    }
                    uint32_t t41 = GM_LD(uint32_t, ((((uint8_t *)t36) + 8) + 8));
                    gm_l6 = t41;
                    void * t42 = gm_l4;
                    void * t43 = F.l1;
                    gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t43));
                    gm_f_layout_a5ovnrm((void *)&gm_l45, (void *)NULL, cx, NULL, t42, t44);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t45 = GM_LD(uint64_t, ((uint8_t *)&gm_l45));
                    uint8_t t46 = ((t45 == ((uint64_t)0ULL)));
                    if (t46) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t47 = F.l0;
                    void * t48 = F.l1;
                    gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t48));
                    void * t50 = F.l1;
                    gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t50) + 120));
                    gm_tlg23kb3oj23s t52 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                    gm_f_load_go4fgkp((void *)&F.l7, (void *)NULL, cx, NULL, t47, &t52, t49, t51);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t53 = F.l0;
                    uint32_t t54 = gm_l6;
                    gm_tlg23kb3oj23s t55;
                    gm_f_local_place_qnf3mb2((void *)&t55, t54);
                    gm_t7b7esi3n9r90 t56 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l7));
                    gm_t6fp3q71vjrmk t57;
                    GM_ST(uint64_t, (uint8_t *)&t57 + 56, 0);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t57 + 0), t55);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t57 + 64), t56);
                    void * t58 = F.l1;
                    gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t58) + 120));
                    gm_th7badf4b6ee3 t60;
                    memset(&t60, 0, sizeof t60);
                    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)25ULL));
                    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t60 + 16), t57);
                    gm_b0_1 t61 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t53, &t60, t59);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t63 = (uint8_t)(t62 == ((uint8_t)2ULL));
                    if (!t63) {
                        goto gm_lb4;
                    }
                    void * t64 = GM_LD(void *, ((((uint8_t *)t36) + 8) + 8));
                    gm_l8 = t64;
                    void * t65 = F.l0;
                    void * t66 = F.l1;
                    gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t66));
                    void * t68 = F.l1;
                    gm_tl874t2atnlh3 t69 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t68) + 120));
                    gm_tlg23kb3oj23s t70 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                    gm_f_load_go4fgkp((void *)&F.l9, (void *)NULL, cx, NULL, t65, &t70, t67, t69);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t71 = F.l0;
                    void * t72 = gm_l8;
                    gm_f_expr_tp86bm9((void *)&F.l10, (void *)NULL, cx, NULL, t71, t72);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t73 = F.l0;
                    gm_t88rcucg1b7uk t74 = gm_l5;
                    uint32_t t75 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t73, t74);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l11 = t75;
                    void * t76 = F.l0;
                    uint32_t t77 = gm_l11;
                    gm_t7b7esi3n9r90 t78 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l9));
                    gm_t7b7esi3n9r90 t79 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l10));
                    gm_tr6ll1dg9imrl t80;
                    GM_ST(uint8_t, (uint8_t *)&t80 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t80 + 6, 0);
                    GM_ST(uint64_t, (uint8_t *)&t80 + 8, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t80 + 0), t77);
                    GM_ST(uint8_t, ((uint8_t *)&t80 + 4), ((uint8_t)0));
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t80 + 16), t78);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t80 + 64), t79);
                    void * t81 = F.l1;
                    gm_tl874t2atnlh3 t82 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t81) + 120));
                    gm_th7badf4b6ee3 t83;
                    memset(&t83, 0, sizeof t83);
                    GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)5ULL));
                    GM_ST(gm_tr6ll1dg9imrl, ((uint8_t *)&t83 + 16), t80);
                    gm_b0_1 t84 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t76, &t83, t82);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t85 = F.l0;
                    uint32_t t86 = gm_l11;
                    uint32_t t87 = F.l3;
                    gm_t7b7esi3n9r90 t88;
                    memset(&t88, 0, sizeof t88);
                    GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t88 + 16), t86);
                    gm_tejc6vv5sqno9 t89;
                    GM_ST(uint8_t, (uint8_t *)&t89 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t89 + 50, 0);
                    GM_ST(uint64_t, (uint8_t *)&t89 + 56, 0);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t89 + 0), t88);
                    GM_ST(uint8_t, ((uint8_t *)&t89 + 48), ((uint8_t)1));
                    GM_ST(uint32_t, ((uint8_t *)&t89 + 52), t87);
                    void * t90 = F.l1;
                    gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t90) + 120));
                    gm_th7badf4b6ee3 t92;
                    memset(&t92, 0, sizeof t92);
                    GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)31ULL));
                    GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t92 + 16), t89);
                    gm_b0_1 t93 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t85, &t92, t91);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb4:;
            }
            {
                {
                    uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t95 = (uint8_t)(t94 == ((uint8_t)3ULL));
                    if (!t95) {
                        goto gm_lb5;
                    }
                    memmove(((uint8_t *)&F.l12), ((((uint8_t *)t36) + 8) + 8), 48);
                    memmove(((uint8_t *)&F.l13), (((((uint8_t *)t36) + 8) + 8) + 48), 48);
                    uint8_t t96 = GM_LD(uint8_t, (((((uint8_t *)t36) + 8) + 8) + 96));
                    gm_l14 = t96;
                    void * t97 = F.l0;
                    void * t98 = F.l1;
                    gm_t88rcucg1b7uk t99 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t98));
                    void * t100 = F.l1;
                    gm_tl874t2atnlh3 t101 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t100) + 120));
                    gm_tlg23kb3oj23s t102 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                    gm_f_load_go4fgkp((void *)&F.l15, (void *)NULL, cx, NULL, t97, &t102, t99, t101);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t103 = F.l0;
                    void * t104 = F.l1;
                    gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t104));
                    gm_tbci5ospk5nnn t106 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l12));
                    gm_f_int_value_ttu5547((void *)&gm_l46, (void *)NULL, cx, NULL, t103, t105, &t106);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t107 = (uint8_t)((((uint8_t *)&gm_l46))[48] != 0);
                    if (t107) {
                        memmove(((uint8_t *)&gm_l47), ((uint8_t *)&gm_l46), 48);
                    } else {
                        void * t108 = F.l0;
                        void * t109 = F.l1;
                        uint32_t t110 = F.l3;
                        gm_b0_1 t111 = gm_f_escape_pattern_6lpj7tc((void *)NULL, cx, NULL, t108, t109, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l2)), t110);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    memmove(((uint8_t *)&F.l16), ((uint8_t *)&gm_l47), 48);
                    void * t112 = F.l0;
                    void * t113 = F.l1;
                    gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t113));
                    gm_tbci5ospk5nnn t115 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l13));
                    gm_f_int_value_ttu5547((void *)&gm_l48, (void *)NULL, cx, NULL, t112, t114, &t115);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t116 = (uint8_t)((((uint8_t *)&gm_l48))[48] != 0);
                    if (t116) {
                        memmove(((uint8_t *)&gm_l49), ((uint8_t *)&gm_l48), 48);
                    } else {
                        void * t117 = F.l0;
                        void * t118 = F.l1;
                        uint32_t t119 = F.l3;
                        gm_b0_1 t120 = gm_f_escape_pattern_6lpj7tc((void *)NULL, cx, NULL, t117, t118, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l2)), t119);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    memmove(((uint8_t *)&F.l17), ((uint8_t *)&gm_l49), 48);
                    void * t121 = F.l0;
                    gm_t88rcucg1b7uk t122 = gm_l5;
                    uint32_t t123 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t121, t122);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l18 = t123;
                    void * t124 = F.l0;
                    uint32_t t125 = gm_l18;
                    gm_ts6oqppjh0ba3 t126;
                    GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)13ULL));
                    gm_t7b7esi3n9r90 t127 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l15));
                    gm_t7b7esi3n9r90 t128 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l16));
                    gm_t1ctgp8j1v2as t129;
                    GM_ST(uint16_t, (uint8_t *)&t129 + 6, 0);
                    GM_ST(uint64_t, (uint8_t *)&t129 + 8, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t129 + 0), t125);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t129 + 4), t126);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t129 + 16), t127);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t129 + 64), t128);
                    void * t130 = F.l1;
                    gm_tl874t2atnlh3 t131 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t130) + 120));
                    gm_th7badf4b6ee3 t132;
                    memset(&t132, 0, sizeof t132);
                    GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)3ULL));
                    GM_ST(gm_t1ctgp8j1v2as, ((uint8_t *)&t132 + 16), t129);
                    gm_b0_1 t133 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t124, &t132, t131);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t134 = F.l0;
                    uint32_t t135 = gm_l18;
                    uint32_t t136 = F.l3;
                    gm_t7b7esi3n9r90 t137;
                    memset(&t137, 0, sizeof t137);
                    GM_ST(uint8_t, ((uint8_t *)&t137 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t137 + 16), t135);
                    gm_tejc6vv5sqno9 t138;
                    GM_ST(uint8_t, (uint8_t *)&t138 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t138 + 50, 0);
                    GM_ST(uint64_t, (uint8_t *)&t138 + 56, 0);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t138 + 0), t137);
                    GM_ST(uint8_t, ((uint8_t *)&t138 + 48), ((uint8_t)1));
                    GM_ST(uint32_t, ((uint8_t *)&t138 + 52), t136);
                    void * t139 = F.l1;
                    gm_tl874t2atnlh3 t140 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t139) + 120));
                    gm_th7badf4b6ee3 t141;
                    memset(&t141, 0, sizeof t141);
                    GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)31ULL));
                    GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t141 + 16), t138);
                    gm_b0_1 t142 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t134, &t141, t140);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t143 = F.l0;
                    gm_t88rcucg1b7uk t144 = gm_l5;
                    uint32_t t145 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t143, t144);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l19 = t145;
                    void * t146 = F.l0;
                    uint32_t t147 = gm_l19;
                    uint8_t t148 = gm_l14;
                    if (t148) {
                        gm_ts6oqppjh0ba3 t149;
                        GM_ST(uint8_t, (uint8_t *)&t149 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)11ULL));
                        gm_l50 = t149;
                    } else {
                        gm_ts6oqppjh0ba3 t150;
                        GM_ST(uint8_t, (uint8_t *)&t150 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)10ULL));
                        gm_l50 = t150;
                    }
                    gm_ts6oqppjh0ba3 t151 = gm_l50;
                    gm_t7b7esi3n9r90 t152 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l15));
                    gm_t7b7esi3n9r90 t153 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l17));
                    gm_t1ctgp8j1v2as t154;
                    GM_ST(uint16_t, (uint8_t *)&t154 + 6, 0);
                    GM_ST(uint64_t, (uint8_t *)&t154 + 8, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t154 + 0), t147);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t154 + 4), t151);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t154 + 16), t152);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t154 + 64), t153);
                    void * t155 = F.l1;
                    gm_tl874t2atnlh3 t156 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t155) + 120));
                    gm_th7badf4b6ee3 t157;
                    memset(&t157, 0, sizeof t157);
                    GM_ST(uint8_t, ((uint8_t *)&t157 + 0), ((uint8_t)3ULL));
                    GM_ST(gm_t1ctgp8j1v2as, ((uint8_t *)&t157 + 16), t154);
                    gm_b0_1 t158 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t146, &t157, t156);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t159 = F.l0;
                    uint32_t t160 = gm_l19;
                    uint32_t t161 = F.l3;
                    gm_t7b7esi3n9r90 t162;
                    memset(&t162, 0, sizeof t162);
                    GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t162 + 16), t160);
                    gm_tejc6vv5sqno9 t163;
                    GM_ST(uint8_t, (uint8_t *)&t163 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t163 + 50, 0);
                    GM_ST(uint64_t, (uint8_t *)&t163 + 56, 0);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t163 + 0), t162);
                    GM_ST(uint8_t, ((uint8_t *)&t163 + 48), ((uint8_t)1));
                    GM_ST(uint32_t, ((uint8_t *)&t163 + 52), t161);
                    void * t164 = F.l1;
                    gm_tl874t2atnlh3 t165 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t164) + 120));
                    gm_th7badf4b6ee3 t166;
                    memset(&t166, 0, sizeof t166);
                    GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)31ULL));
                    GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t166 + 16), t163);
                    gm_b0_1 t167 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t159, &t166, t165);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t168 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t169 = (uint8_t)(t168 == ((uint8_t)4ULL));
                    if (!t169) {
                        goto gm_lb6;
                    }
                    void * t170 = F.l0;
                    gm_t88rcucg1b7uk t171 = gm_l5;
                    uint32_t t172 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t170, t171);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l20 = t172;
                    void * t173 = F.l0;
                    uint32_t t174 = gm_l20;
                    gm_tlg23kb3oj23s t175 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                    gm_t3349g713hmcr t176;
                    GM_ST(uint32_t, (uint8_t *)&t176 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&t176 + 65, 0);
                    GM_ST(uint16_t, (uint8_t *)&t176 + 66, 0);
                    GM_ST(uint32_t, (uint8_t *)&t176 + 68, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t176 + 0), t174);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t176 + 8), t175);
                    GM_ST(uint8_t, ((uint8_t *)&t176 + 64), ((uint8_t)0));
                    void * t177 = F.l1;
                    gm_tl874t2atnlh3 t178 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t177) + 120));
                    gm_th7badf4b6ee3 t179;
                    memset(&t179, 0, sizeof t179);
                    GM_ST(uint8_t, ((uint8_t *)&t179 + 0), ((uint8_t)23ULL));
                    GM_ST(gm_t3349g713hmcr, ((uint8_t *)&t179 + 16), t176);
                    gm_b0_1 t180 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t173, &t179, t178);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t181 = F.l0;
                    uint32_t t182 = gm_l20;
                    uint32_t t183 = F.l3;
                    gm_t7b7esi3n9r90 t184;
                    memset(&t184, 0, sizeof t184);
                    GM_ST(uint8_t, ((uint8_t *)&t184 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t184 + 16), t182);
                    gm_tejc6vv5sqno9 t185;
                    GM_ST(uint8_t, (uint8_t *)&t185 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t185 + 50, 0);
                    GM_ST(uint64_t, (uint8_t *)&t185 + 56, 0);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t185 + 0), t184);
                    GM_ST(uint8_t, ((uint8_t *)&t185 + 48), ((uint8_t)0));
                    GM_ST(uint32_t, ((uint8_t *)&t185 + 52), t183);
                    void * t186 = F.l1;
                    gm_tl874t2atnlh3 t187 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t186) + 120));
                    gm_th7badf4b6ee3 t188;
                    memset(&t188, 0, sizeof t188);
                    GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)31ULL));
                    GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t188 + 16), t185);
                    gm_b0_1 t189 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t181, &t188, t187);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb6:;
                {
                    uint8_t t190 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t191 = (uint8_t)(t190 == ((uint8_t)5ULL));
                    if (!t191) {
                        goto gm_lb7;
                    }
                    void * t192 = GM_LD(void *, ((((uint8_t *)t36) + 8) + 8));
                    gm_l21 = t192;
                    void * t193 = F.l0;
                    gm_t88rcucg1b7uk t194 = gm_l5;
                    uint32_t t195 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t193, t194);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l22 = t195;
                    void * t196 = F.l0;
                    uint32_t t197 = gm_l22;
                    gm_tlg23kb3oj23s t198 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                    gm_t3349g713hmcr t199;
                    GM_ST(uint32_t, (uint8_t *)&t199 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&t199 + 65, 0);
                    GM_ST(uint16_t, (uint8_t *)&t199 + 66, 0);
                    GM_ST(uint32_t, (uint8_t *)&t199 + 68, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t199 + 0), t197);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t199 + 8), t198);
                    GM_ST(uint8_t, ((uint8_t *)&t199 + 64), ((uint8_t)0));
                    void * t200 = F.l1;
                    gm_tl874t2atnlh3 t201 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t200) + 120));
                    gm_th7badf4b6ee3 t202;
                    memset(&t202, 0, sizeof t202);
                    GM_ST(uint8_t, ((uint8_t *)&t202 + 0), ((uint8_t)23ULL));
                    GM_ST(gm_t3349g713hmcr, ((uint8_t *)&t202 + 16), t199);
                    gm_b0_1 t203 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t196, &t202, t201);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t204 = F.l0;
                    uint32_t t205 = gm_l22;
                    uint32_t t206 = F.l3;
                    gm_t7b7esi3n9r90 t207;
                    memset(&t207, 0, sizeof t207);
                    GM_ST(uint8_t, ((uint8_t *)&t207 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t207 + 16), t205);
                    gm_tejc6vv5sqno9 t208;
                    GM_ST(uint8_t, (uint8_t *)&t208 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t208 + 50, 0);
                    GM_ST(uint64_t, (uint8_t *)&t208 + 56, 0);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t208 + 0), t207);
                    GM_ST(uint8_t, ((uint8_t *)&t208 + 48), ((uint8_t)1));
                    GM_ST(uint32_t, ((uint8_t *)&t208 + 52), t206);
                    void * t209 = F.l1;
                    gm_tl874t2atnlh3 t210 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t209) + 120));
                    gm_th7badf4b6ee3 t211;
                    memset(&t211, 0, sizeof t211);
                    GM_ST(uint8_t, ((uint8_t *)&t211 + 0), ((uint8_t)31ULL));
                    GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t211 + 16), t208);
                    gm_b0_1 t212 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t204, &t211, t210);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t213 = F.l0;
                    void * t214 = gm_l21;
                    void * t215 = gm_l21;
                    gm_t88rcucg1b7uk t216 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t215));
                    gm_tlcm4lvu3g1a8 t217;
                    GM_ST(uint32_t, (uint8_t *)&t217 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t217 + 0), ((uint64_t)0ULL));
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t217 + 8), t216);
                    gm_tfqmtuprvunod t218;
                    memset(&t218, 0, sizeof t218);
                    GM_ST(uint8_t, ((uint8_t *)&t218 + 0), ((uint8_t)4ULL));
                    GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t218 + 16), t217);
                    gm_tlg23kb3oj23s t219;
                    gm_f_project_6hpd48e((void *)&t219, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l2)), &t218);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t220 = F.l3;
                    gm_b0_1 t221 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t213, t214, &t219, t220);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    {
                        {
                            uint8_t t222 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                            uint8_t t223 = (uint8_t)(t222 == ((uint8_t)6ULL));
                            if (!t223) {
                                goto gm_lb10;
                            }
                            void * t224 = GM_LD(void *, ((((uint8_t *)t36) + 8) + 8));
                            gm_l23 = t224;
                            goto gm_lb9;
                        }
gm_lb10:;
                        {
                            uint8_t t225 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                            uint8_t t226 = (uint8_t)(t225 == ((uint8_t)7ULL));
                            if (!t226) {
                                goto gm_lb11;
                            }
                            void * t227 = GM_LD(void *, ((((uint8_t *)t36) + 8) + 8));
                            gm_l23 = t227;
                            goto gm_lb9;
                        }
gm_lb11:;
                        goto gm_lb8;
                    }
gm_lb9:;
                    void * t228 = F.l0;
                    gm_t88rcucg1b7uk t229 = gm_l5;
                    uint32_t t230 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t228, t229);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l25 = t230;
                    void * t231 = F.l0;
                    uint32_t t232 = gm_l25;
                    gm_tlg23kb3oj23s t233 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                    gm_t3349g713hmcr t234;
                    GM_ST(uint32_t, (uint8_t *)&t234 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&t234 + 65, 0);
                    GM_ST(uint16_t, (uint8_t *)&t234 + 66, 0);
                    GM_ST(uint32_t, (uint8_t *)&t234 + 68, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t234 + 0), t232);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t234 + 8), t233);
                    GM_ST(uint8_t, ((uint8_t *)&t234 + 64), ((uint8_t)1));
                    void * t235 = F.l1;
                    gm_tl874t2atnlh3 t236 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t235) + 120));
                    gm_th7badf4b6ee3 t237;
                    memset(&t237, 0, sizeof t237);
                    GM_ST(uint8_t, ((uint8_t *)&t237 + 0), ((uint8_t)23ULL));
                    GM_ST(gm_t3349g713hmcr, ((uint8_t *)&t237 + 16), t234);
                    gm_b0_1 t238 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t231, &t237, t236);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t239 = F.l0;
                    uint32_t t240 = gm_l25;
                    void * t241 = F.l1;
                    gm_t7b7esi3n9r90 t242;
                    memset(&t242, 0, sizeof t242);
                    GM_ST(uint8_t, ((uint8_t *)&t242 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t242 + 16), t240);
                    {
                        {
                            uint8_t t243 = GM_LD(uint8_t, ((((uint8_t *)t241) + 8) + 0));
                            uint8_t t244 = (uint8_t)(t243 == ((uint8_t)6ULL));
                            if (!t244) {
                                goto gm_lb13;
                            }
                            gm_l51 = ((uint8_t)1);
                            goto gm_lb12;
                        }
gm_lb13:;
                        {
                            gm_l51 = ((uint8_t)0);
                            goto gm_lb12;
                        }
gm_lb14:;
                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1034, 37);
                    }
gm_lb12:;
                    uint8_t t245 = gm_l51;
                    uint32_t t246 = F.l3;
                    gm_tejc6vv5sqno9 t247;
                    GM_ST(uint8_t, (uint8_t *)&t247 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t247 + 50, 0);
                    GM_ST(uint64_t, (uint8_t *)&t247 + 56, 0);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t247 + 0), t242);
                    GM_ST(uint8_t, ((uint8_t *)&t247 + 48), t245);
                    GM_ST(uint32_t, ((uint8_t *)&t247 + 52), t246);
                    void * t248 = F.l1;
                    gm_tl874t2atnlh3 t249 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t248) + 120));
                    gm_th7badf4b6ee3 t250;
                    memset(&t250, 0, sizeof t250);
                    GM_ST(uint8_t, ((uint8_t *)&t250 + 0), ((uint8_t)31ULL));
                    GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t250 + 16), t247);
                    gm_b0_1 t251 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t239, &t250, t249);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t252 = F.l0;
                    void * t253 = gm_l23;
                    void * t254 = F.l0;
                    void * t255 = F.l1;
                    gm_t88rcucg1b7uk t256 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t255));
                    gm_tlg23kb3oj23s t257 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                    uint64_t t258 = gm_f_payload_off_o6q9s7n((void *)NULL, cx, NULL, t254, t256);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t259 = gm_l23;
                    gm_t88rcucg1b7uk t260 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t259));
                    gm_tlcm4lvu3g1a8 t261;
                    GM_ST(uint32_t, (uint8_t *)&t261 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t261 + 0), t258);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t261 + 8), t260);
                    gm_tfqmtuprvunod t262;
                    memset(&t262, 0, sizeof t262);
                    GM_ST(uint8_t, ((uint8_t *)&t262 + 0), ((uint8_t)4ULL));
                    GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t262 + 16), t261);
                    gm_tlg23kb3oj23s t263;
                    gm_f_project_6hpd48e((void *)&t263, (void *)NULL, cx, NULL, &t257, &t262);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t264 = F.l3;
                    gm_b0_1 t265 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t252, t253, &t263, t264);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb8:;
            }
            {
                {
                    uint8_t t266 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t267 = (uint8_t)(t266 == ((uint8_t)8ULL));
                    if (!t267) {
                        goto gm_lb15;
                    }
                    uint32_t t268 = GM_LD(uint32_t, ((((uint8_t *)t36) + 8) + 8));
                    gm_l26 = t268;
                    void * t269 = GM_LD(void *, (((((uint8_t *)t36) + 8) + 8) + 8));
                    gm_l27 = t269;
                    void * t270 = F.l0;
                    gm_t88rcucg1b7uk t271 = gm_l5;
                    uint32_t t272 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t270, t271);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l28 = t272;
                    void * t273 = F.l0;
                    uint32_t t274 = gm_l28;
                    uint32_t t275 = gm_l26;
                    gm_tlg23kb3oj23s t276 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                    gm_t4j77n1ssj077 t277;
                    GM_ST(uint32_t, (uint8_t *)&t277 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t277 + 68, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t277 + 0), t274);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t277 + 8), t276);
                    GM_ST(uint32_t, ((uint8_t *)&t277 + 64), t275);
                    void * t278 = F.l1;
                    gm_tl874t2atnlh3 t279 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t278) + 120));
                    gm_th7badf4b6ee3 t280;
                    memset(&t280, 0, sizeof t280);
                    GM_ST(uint8_t, ((uint8_t *)&t280 + 0), ((uint8_t)24ULL));
                    GM_ST(gm_t4j77n1ssj077, ((uint8_t *)&t280 + 16), t277);
                    gm_b0_1 t281 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t273, &t280, t279);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t282 = F.l0;
                    uint32_t t283 = gm_l28;
                    uint32_t t284 = F.l3;
                    gm_t7b7esi3n9r90 t285;
                    memset(&t285, 0, sizeof t285);
                    GM_ST(uint8_t, ((uint8_t *)&t285 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t285 + 16), t283);
                    gm_tejc6vv5sqno9 t286;
                    GM_ST(uint8_t, (uint8_t *)&t286 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t286 + 50, 0);
                    GM_ST(uint64_t, (uint8_t *)&t286 + 56, 0);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t286 + 0), t285);
                    GM_ST(uint8_t, ((uint8_t *)&t286 + 48), ((uint8_t)1));
                    GM_ST(uint32_t, ((uint8_t *)&t286 + 52), t284);
                    void * t287 = F.l1;
                    gm_tl874t2atnlh3 t288 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t287) + 120));
                    gm_th7badf4b6ee3 t289;
                    memset(&t289, 0, sizeof t289);
                    GM_ST(uint8_t, ((uint8_t *)&t289 + 0), ((uint8_t)31ULL));
                    GM_ST(gm_tejc6vv5sqno9, ((uint8_t *)&t289 + 16), t286);
                    gm_b0_1 t290 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t282, &t289, t288);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        {
                            uint8_t t291 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l27)) != 0);
                            if (!t291) {
                                goto gm_lb17;
                            }
                            void * t292 = GM_LD(void *, ((uint8_t *)&gm_l27));
                            gm_l29 = t292;
                            void * t293 = F.l0;
                            void * t294 = gm_l29;
                            void * t295 = F.l0;
                            void * t296 = F.l1;
                            gm_t88rcucg1b7uk t297 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t296));
                            gm_tlg23kb3oj23s t298 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                            uint64_t t299 = gm_f_payload_off_o6q9s7n((void *)NULL, cx, NULL, t295, t297);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t300 = gm_l29;
                            gm_t88rcucg1b7uk t301 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t300));
                            gm_tlcm4lvu3g1a8 t302;
                            GM_ST(uint32_t, (uint8_t *)&t302 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t302 + 0), t299);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t302 + 8), t301);
                            gm_tfqmtuprvunod t303;
                            memset(&t303, 0, sizeof t303);
                            GM_ST(uint8_t, ((uint8_t *)&t303 + 0), ((uint8_t)4ULL));
                            GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t303 + 16), t302);
                            gm_tlg23kb3oj23s t304;
                            gm_f_project_6hpd48e((void *)&t304, (void *)NULL, cx, NULL, &t298, &t303);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint32_t t305 = F.l3;
                            gm_b0_1 t306 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t293, t294, &t304, t305);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb16;
                        }
gm_lb17:;
                    }
gm_lb16:;
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    uint8_t t307 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t308 = (uint8_t)(t307 == ((uint8_t)9ULL));
                    if (!t308) {
                        goto gm_lb18;
                    }
                    gm_b16_8 t309 = GM_LD(gm_b16_8, ((((uint8_t *)t36) + 8) + 8));
                    gm_l30 = t309;
                    void * t310 = F.l0;
                    void * t311 = F.l1;
                    gm_t88rcucg1b7uk t312 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t311));
                    uint8_t t313 = gm_f_is_bits_pbijkuf(t310, GM_CONV(gm_csguankko4lmhd, t312));
                    if (t313) {
                        void * t314 = F.l0;
                        void * t315 = F.l1;
                        gm_t88rcucg1b7uk t316 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t315));
                        void * t317 = F.l1;
                        gm_tl874t2atnlh3 t318 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t317) + 120));
                        gm_tlg23kb3oj23s t319 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l2));
                        gm_f_load_go4fgkp((void *)&F.l31, (void *)NULL, cx, NULL, t314, &t319, t316, t318);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t320 = gm_l30;
                        gm_l32 = t320;
                        gm_l33 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t322 = gm_l33;
                                uint64_t t323 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                uint8_t t324 = (t322 < t323);
                                if (!t324) {
                                    goto gm_lb19;
                                }
                                {
                                    void * t325 = GM_LD(void *, ((uint8_t *)&gm_l32));
                                    uint64_t t326 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                    uint64_t t327 = gm_l33;
                                    {
                                        {
                                            gm_ttggq7ng4kp3p t328 = GM_LD(gm_ttggq7ng4kp3p, (((uint8_t *)t325) + (uintptr_t)t327 * 16));
                                            gm_l34 = t328;
                                            void * t329 = F.l0;
                                            void * t330 = GM_LD(void *, (((uint8_t *)&gm_l34) + 8));
                                            gm_t88rcucg1b7uk t331 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t330));
                                            uint32_t t332 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t329, t331);
                                            if (gm_unw.kind) {
                                                goto gm_ll321;
                                            }
                                            gm_l35 = t332;
                                            void * t333 = F.l0;
                                            uint32_t t334 = gm_l35;
                                            uint32_t t335 = GM_LD(uint32_t, ((uint8_t *)&gm_l34));
                                            gm_t7b7esi3n9r90 t336 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l31));
                                            gm_tcpisgbff1dqv t337;
                                            GM_ST(uint32_t, (uint8_t *)&t337 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t337 + 8, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t337 + 68, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t337 + 72, 0);
                                            GM_ST(uint32_t, ((uint8_t *)&t337 + 0), t334);
                                            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t337 + 16), t336);
                                            GM_ST(uint32_t, ((uint8_t *)&t337 + 64), t335);
                                            void * t338 = F.l1;
                                            gm_tl874t2atnlh3 t339 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t338) + 120));
                                            gm_th7badf4b6ee3 t340;
                                            memset(&t340, 0, sizeof t340);
                                            GM_ST(uint8_t, ((uint8_t *)&t340 + 0), ((uint8_t)21ULL));
                                            GM_ST(gm_tcpisgbff1dqv, ((uint8_t *)&t340 + 16), t337);
                                            gm_b0_1 t341 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t333, &t340, t339);
                                            if (gm_unw.kind) {
                                                goto gm_ll321;
                                            }
                                            void * t342 = F.l0;
                                            void * t343 = GM_LD(void *, (((uint8_t *)&gm_l34) + 8));
                                            gm_t88rcucg1b7uk t344 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t343));
                                            uint32_t t345 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t342, t344);
                                            if (gm_unw.kind) {
                                                goto gm_ll321;
                                            }
                                            gm_l36 = t345;
                                            void * t346 = F.l0;
                                            uint32_t t347 = gm_l36;
                                            gm_tlg23kb3oj23s t348;
                                            gm_f_local_place_qnf3mb2((void *)&t348, t347);
                                            uint32_t t349 = gm_l35;
                                            gm_t7b7esi3n9r90 t350;
                                            memset(&t350, 0, sizeof t350);
                                            GM_ST(uint8_t, ((uint8_t *)&t350 + 0), ((uint8_t)0ULL));
                                            GM_ST(uint32_t, ((uint8_t *)&t350 + 16), t349);
                                            gm_t6fp3q71vjrmk t351;
                                            GM_ST(uint64_t, (uint8_t *)&t351 + 56, 0);
                                            GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t351 + 0), t348);
                                            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t351 + 64), t350);
                                            void * t352 = F.l1;
                                            gm_tl874t2atnlh3 t353 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t352) + 120));
                                            gm_th7badf4b6ee3 t354;
                                            memset(&t354, 0, sizeof t354);
                                            GM_ST(uint8_t, ((uint8_t *)&t354 + 0), ((uint8_t)25ULL));
                                            GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t354 + 16), t351);
                                            gm_b0_1 t355 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t346, &t354, t353);
                                            if (gm_unw.kind) {
                                                goto gm_ll321;
                                            }
                                            void * t356 = F.l0;
                                            void * t357 = GM_LD(void *, (((uint8_t *)&gm_l34) + 8));
                                            uint32_t t358 = gm_l36;
                                            gm_tlg23kb3oj23s t359;
                                            gm_f_local_place_qnf3mb2((void *)&t359, t358);
                                            uint32_t t360 = F.l3;
                                            gm_b0_1 t361 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t356, t357, &t359, t360);
                                            if (gm_unw.kind) {
                                                goto gm_ll321;
                                            }
                                            goto gm_lb22;
                                        }
gm_lb23:;
                                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1047, 17);
                                    }
gm_lb22:;
                                }
gm_lb21:;
                                uint64_t t362 = gm_l33;
                                uint64_t r363; bool o364 = __builtin_add_overflow(t362, ((uint64_t)1ULL), &r363);
                                uint64_t t365 = r363;
                                gm_l33 = t365;
                            }
                        }
                        goto gm_ls366;
gm_ll321:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb19; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb21; }
                        }
                        goto gm_lad;
gm_ls366:;
gm_lb19:;
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t367 = gm_l4;
                    void * t368 = F.l1;
                    gm_t88rcucg1b7uk t369 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t368));
                    gm_f_layout_a5ovnrm((void *)&gm_l52, (void *)NULL, cx, NULL, t367, t369);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t370 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l52) + 16));
                    gm_l37 = t370;
                    gm_b16_8 t371 = gm_l30;
                    gm_l38 = t371;
                    gm_l39 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t373 = gm_l39;
                            uint64_t t374 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                            uint8_t t375 = (t373 < t374);
                            if (!t375) {
                                goto gm_lb24;
                            }
                            {
                                void * t376 = GM_LD(void *, ((uint8_t *)&gm_l38));
                                uint64_t t377 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                uint64_t t378 = gm_l39;
                                {
                                    {
                                        gm_ttggq7ng4kp3p t379 = GM_LD(gm_ttggq7ng4kp3p, (((uint8_t *)t376) + (uintptr_t)t378 * 16));
                                        gm_l40 = t379;
                                        uint32_t t380 = GM_LD(uint32_t, ((uint8_t *)&gm_l40));
                                        uint64_t t381 = (uint64_t)t380;
                                        uint64_t t382 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                                        uint8_t t383 = (t381 < t382);
                                        if (t383) {
                                            void * t384 = GM_LD(void *, ((uint8_t *)&gm_l37));
                                            uint64_t t385 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                                            uint32_t t386 = GM_LD(uint32_t, ((uint8_t *)&gm_l40));
                                            uint64_t t387 = (uint64_t)t386;
                                            if ((uintptr_t)t387 >= (uintptr_t)(t385)) gm_trap_index((uintptr_t)t387, t385, gm_srccujoh2vgr9aq, 1058, 59);
                                            uint64_t t388 = GM_LD(uint64_t, (((uint8_t *)t384) + (uintptr_t)t387 * 8));
                                            gm_l53 = t388;
                                        } else {
                                            gm_l53 = ((uint64_t)0ULL);
                                        }
                                        uint64_t t389 = gm_l53;
                                        gm_l41 = t389;
                                        void * t390 = F.l0;
                                        void * t391 = GM_LD(void *, (((uint8_t *)&gm_l40) + 8));
                                        uint32_t t392 = GM_LD(uint32_t, ((uint8_t *)&gm_l40));
                                        uint64_t t393 = gm_l41;
                                        gm_tb31k5l794t7p t394;
                                        GM_ST(uint32_t, (uint8_t *)&t394 + 4, 0);
                                        GM_ST(uint32_t, ((uint8_t *)&t394 + 0), t392);
                                        GM_ST(uint64_t, ((uint8_t *)&t394 + 8), t393);
                                        gm_tfqmtuprvunod t395;
                                        memset(&t395, 0, sizeof t395);
                                        GM_ST(uint8_t, ((uint8_t *)&t395 + 0), ((uint8_t)0ULL));
                                        GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t395 + 16), t394);
                                        gm_tlg23kb3oj23s t396;
                                        gm_f_project_6hpd48e((void *)&t396, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l2)), &t395);
                                        if (gm_unw.kind) {
                                            goto gm_ll372;
                                        }
                                        uint32_t t397 = F.l3;
                                        gm_b0_1 t398 = gm_f_pattern_od1h9r0((void *)NULL, cx, NULL, t390, t391, &t396, t397);
                                        if (gm_unw.kind) {
                                            goto gm_ll372;
                                        }
                                        goto gm_lb27;
                                    }
gm_lb28:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1057, 13);
                                }
gm_lb27:;
                            }
gm_lb26:;
                            uint64_t t399 = gm_l39;
                            uint64_t r400; bool o401 = __builtin_add_overflow(t399, ((uint64_t)1ULL), &r400);
                            uint64_t t402 = r400;
                            gm_l39 = t402;
                        }
                    }
                    goto gm_ls403;
gm_ll372:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb24; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb26; }
                    }
                    goto gm_lad;
gm_ls403:;
gm_lb24:;
                    goto gm_lb1;
                }
gm_lb18:;
            }
            {
                {
                    uint8_t t404 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t405 = (uint8_t)(t404 == ((uint8_t)10ULL));
                    if (!t405) {
                        goto gm_lb29;
                    }
                    gm_b16_8 t406 = GM_LD(gm_b16_8, ((((uint8_t *)t36) + 8) + 8));
                    F.l42 = t406;
                    void * t407 = F.l0;
                    uint32_t t408 = gm_f_label_3373lil(t407);
                    F.l43 = t408;
                    void * t409 = F.l0;
                    gm_closure t410 = ((gm_closure){(void *)gm_f_fn_dduu5o8, (void *)&F.h, cx});
                    gm_b16_8 t411 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t409, t410);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l44 = t411;
                    void * t412 = F.l0;
                    uint32_t t413 = F.l43;
                    gm_b16_8 t414 = gm_l44;
                    gm_tiilmfd16v3fc t415;
                    GM_ST(uint32_t, (uint8_t *)&t415 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t415 + 0), t413);
                    GM_ST(gm_b16_8, ((uint8_t *)&t415 + 8), t414);
                    void * t416 = F.l1;
                    gm_tl874t2atnlh3 t417 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t416) + 120));
                    gm_th7badf4b6ee3 t418;
                    memset(&t418, 0, sizeof t418);
                    GM_ST(uint8_t, ((uint8_t *)&t418 + 0), ((uint8_t)27ULL));
                    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t418 + 16), t415);
                    gm_b0_1 t419 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t412, &t418, t417);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb29:;
                gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 989, 5);
            }
        }
gm_lb1:;
    }
    {
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
inline __attribute__((aligned(4))) uint64_t gm_f_payload_off_o6q9s7n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_payload_off_o6q9s7n F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* offs */
    GM_STACK_CHECK(&F, 249u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l3;
    gm_t53at80pfdus3 gm_l4;
    gm_tbccvikg6b96e gm_l5;
    uint64_t gm_l6;
    void * t5 = gm_l0;
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    void * t7 = (void *)(((uint8_t *)t6) + 8);
    gm_t88rcucg1b7uk t8 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l4, t7, GM_CONV(gm_csguankko4lmhd, t8));
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)13ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_l3 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1087, 8);
    }
gm_lb1:;
    uint8_t t11 = gm_l3;
    if (t11) {
        F.ret = ((uint64_t)0ULL);
        goto gm_fret;
    }
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    gm_t88rcucg1b7uk t14 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&gm_l5, (void *)NULL, cx, NULL, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
    gm_l2 = t15;
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t17 = (t16 > ((uint64_t)1ULL));
    if (t17) {
        void * t18 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t18) + (uintptr_t)((uint64_t)1ULL) * 8));
        gm_l6 = t20;
    } else {
        gm_l6 = ((uint64_t)0ULL);
    }
    uint64_t t21 = gm_l6;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_payload_ty_gn5u4tt(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_payload_ty_gn5u4tt F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* inner */
    gm_t88rcucg1b7uk gm_l3; /* ok */
    GM_STACK_CHECK_SHALLOW(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_tkn42u7na3t81 gm_l4;
    gm_t53at80pfdus3 gm_l5;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = (void *)(((uint8_t *)t7) + 8);
    gm_t88rcucg1b7uk t9 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l5, t8, GM_CONV(gm_csguankko4lmhd, t9));
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)13ULL));
            if (!t11) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
            gm_l2 = t12;
            gm_t88rcucg1b7uk t13 = gm_l2;
            gm_tkn42u7na3t81 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[4] = 1;
            gm_l4 = t14;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)14ULL));
            if (!t16) {
                goto gm_lb3;
            }
            gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l5) + 8) + 4));
            gm_l3 = t17;
            gm_t88rcucg1b7uk t18 = gm_l3;
            gm_tkn42u7na3t81 t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t19), t18);
            ((uint8_t *)&t19)[4] = 1;
            gm_l4 = t19;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_tkn42u7na3t81 t20;
            memset(&t20, 0, sizeof t20);
            gm_l4 = t20;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1122, 44);
    }
gm_lb1:;
    gm_tkn42u7na3t81 t21 = gm_l4;
    F.ret = t21;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_place_oohr45n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_place_oohr45n F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    void * gm_l2; /* s */
    uint32_t gm_l3; /* l */
    uint32_t gm_l4; /* levels */
    uint32_t gm_l5; /* l */
    uint64_t gm_l6; /* decl */
    gm_b16_8 gm_l7; /* name */
    gm_tl874t2atnlh3 gm_l8; /* span */
    void * gm_l9; /* x */
    void * gm_l11; /* base */
    uint32_t gm_l12; /* index */
    gm_b16_8 gm_l14; /* offs */
    uint64_t gm_l15; /* off */
    void * gm_l16; /* base */
    void * gm_l17; /* index */
    uint8_t gm_l18; /* checked */
    GM_STACK_CHECK(&F, 1992u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tfonjjajjj3jq gm_l19;
    gm_tlg23kb3oj23s gm_l20;
    gm_tbccvikg6b96e gm_l21;
    uint64_t gm_l22;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    gm_l2 = t12;
    void * t13 = gm_l1;
    {
        {
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)15ULL));
            if (!t15) {
                goto gm_lb2;
            }
            uint32_t t16 = GM_LD(uint32_t, ((((uint8_t *)t13) + 8) + 8));
            gm_l3 = t16;
            uint32_t t17 = gm_l3;
            gm_tlg23kb3oj23s t18;
            gm_f_local_place_qnf3mb2((void *)&t18, t17);
            gm_tfonjjajjj3jq t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 57, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 58, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 60, 0);
            GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t19), t18);
            ((uint8_t *)&t19)[56] = 1;
            memmove(gm_out, ((uint8_t *)&t19), 64);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)16ULL));
            if (!t21) {
                goto gm_lb3;
            }
            uint32_t t22 = GM_LD(uint32_t, ((((uint8_t *)t13) + 8) + 8));
            gm_l4 = t22;
            uint32_t t23 = GM_LD(uint32_t, (((((uint8_t *)t13) + 8) + 8) + 4));
            gm_l5 = t23;
            uint32_t t24 = gm_l4;
            uint32_t t25 = gm_l5;
            gm_tcc1rq4n1avb2 t26;
            GM_ST(uint32_t, ((uint8_t *)&t26 + 0), t24);
            GM_ST(uint32_t, ((uint8_t *)&t26 + 4), t25);
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)gm_cp0utnbgndu));
            gm_tu78tssgij60g t28;
            GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t28 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t28 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t28 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t28 + 8), t26);
            gm_tlg23kb3oj23s t29;
            GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t29 + 0), t28);
            GM_ST(gm_b16_8, ((uint8_t *)&t29 + 40), t27);
            gm_tfonjjajjj3jq t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 57, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 58, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 60, 0);
            GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t30), t29);
            ((uint8_t *)&t30)[56] = 1;
            memmove(gm_out, ((uint8_t *)&t30), 64);
            goto gm_fdone;
        }
gm_lb3:;
        {
            uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)17ULL));
            if (!t32) {
                goto gm_lb4;
            }
            uint64_t t33 = GM_LD(uint64_t, (((((uint8_t *)t13) + 8) + 8) + 16));
            gm_l6 = t33;
            uint64_t t34 = gm_l6;
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)gm_cp0utnbgndu));
            gm_tu78tssgij60g t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t36 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t36 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t36 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)2ULL));
            GM_ST(uint64_t, ((uint8_t *)&t36 + 8), t34);
            gm_tlg23kb3oj23s t37;
            GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t37 + 0), t36);
            GM_ST(gm_b16_8, ((uint8_t *)&t37 + 40), t35);
            gm_tfonjjajjj3jq t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 57, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 58, 0);
            GM_ST(uint32_t, (uint8_t *)&t38 + 60, 0);
            GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t38), t37);
            ((uint8_t *)&t38)[56] = 1;
            memmove(gm_out, ((uint8_t *)&t38), 64);
            goto gm_fdone;
        }
gm_lb4:;
        {
            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)14ULL));
            if (!t40) {
                goto gm_lb5;
            }
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((((uint8_t *)t13) + 8) + 8));
            gm_l7 = t41;
            gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t13) + 8) + 8) + 16));
            gm_l8 = t42;
            gm_b16_8 t43 = gm_l7;
            gm_tl874t2atnlh3 t44 = gm_l8;
            void * t45 = gm_l1;
            gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t45));
            gm_tpfol4ocdvoij t47;
            GM_ST(gm_b16_8, ((uint8_t *)&t47 + 0), t43);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t47 + 16), t44);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t47 + 28), t46);
            gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)gm_cp0utnbgndu));
            gm_tu78tssgij60g t49;
            GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)3ULL));
            GM_ST(gm_tpfol4ocdvoij, ((uint8_t *)&t49 + 8), t47);
            gm_tlg23kb3oj23s t50;
            GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t50 + 0), t49);
            GM_ST(gm_b16_8, ((uint8_t *)&t50 + 40), t48);
            gm_tfonjjajjj3jq t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 57, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 58, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 60, 0);
            GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t51), t50);
            ((uint8_t *)&t51)[56] = 1;
            memmove(gm_out, ((uint8_t *)&t51), 64);
            goto gm_fdone;
        }
gm_lb5:;
        {
            uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)18ULL));
            if (!t53) {
                goto gm_lb6;
            }
            void * t54 = GM_LD(void *, ((((uint8_t *)t13) + 8) + 8));
            gm_l9 = t54;
            void * t55 = gm_l0;
            void * t56 = gm_l9;
            gm_f_expr_tp86bm9((void *)&F.l10, (void *)NULL, cx, NULL, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t57 = gm_l0;
            void * t58 = gm_l9;
            gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t58));
            void * t60 = gm_l9;
            gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t60) + 72));
            uint32_t t62 = gm_f_in_temp_t7gs0ds((void *)NULL, cx, NULL, t57, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l10)), t59, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)gm_cp0utnbgndu));
            gm_tu78tssgij60g t64;
            memset(&t64, 0, sizeof t64);
            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)4ULL));
            GM_ST(uint32_t, ((uint8_t *)&t64 + 8), t62);
            gm_tlg23kb3oj23s t65;
            GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t65 + 0), t64);
            GM_ST(gm_b16_8, ((uint8_t *)&t65 + 40), t63);
            gm_tfonjjajjj3jq t66;
            GM_ST(uint8_t, (uint8_t *)&t66 + 57, 0);
            GM_ST(uint16_t, (uint8_t *)&t66 + 58, 0);
            GM_ST(uint32_t, (uint8_t *)&t66 + 60, 0);
            GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t66), t65);
            ((uint8_t *)&t66)[56] = 1;
            memmove(gm_out, ((uint8_t *)&t66), 64);
            goto gm_fdone;
        }
gm_lb6:;
        {
            uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t68 = (uint8_t)(t67 == ((uint8_t)19ULL));
            if (!t68) {
                goto gm_lb7;
            }
            void * t69 = GM_LD(void *, ((((uint8_t *)t13) + 8) + 8));
            gm_l11 = t69;
            uint32_t t70 = GM_LD(uint32_t, (((((uint8_t *)t13) + 8) + 8) + 8));
            gm_l12 = t70;
            void * t71 = gm_l0;
            void * t72 = gm_l11;
            gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t72));
            uint8_t t74 = gm_f_is_bits_pbijkuf(t71, GM_CONV(gm_csguankko4lmhd, t73));
            if (t74) {
                gm_tfonjjajjj3jq t75;
                memset(&t75, 0, sizeof t75);
                memmove(gm_out, ((uint8_t *)&t75), 64);
                goto gm_fdone;
            }
            void * t76 = gm_l0;
            void * t77 = gm_l11;
            gm_f_operand_place_cdle4ls((void *)&gm_l19, (void *)NULL, cx, NULL, t76, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t78 = (uint8_t)((((uint8_t *)&gm_l19))[56] != 0);
            if (t78) {
                memmove(((uint8_t *)&gm_l20), ((uint8_t *)&gm_l19), 56);
            } else {
                gm_tfonjjajjj3jq t79;
                memset(&t79, 0, sizeof t79);
                memmove(gm_out, ((uint8_t *)&t79), 64);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&F.l13), ((uint8_t *)&gm_l20), 56);
            void * t80 = gm_l2;
            void * t81 = gm_l11;
            gm_t88rcucg1b7uk t82 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t81));
            gm_f_layout_a5ovnrm((void *)&gm_l21, (void *)NULL, cx, NULL, t80, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 16));
            gm_l14 = t83;
            uint32_t t84 = gm_l12;
            uint64_t t85 = (uint64_t)t84;
            uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
            uint8_t t87 = (t85 < t86);
            if (t87) {
                void * t88 = GM_LD(void *, ((uint8_t *)&gm_l14));
                uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint32_t t90 = gm_l12;
                uint64_t t91 = (uint64_t)t90;
                if ((uintptr_t)t91 >= (uintptr_t)(t89)) gm_trap_index((uintptr_t)t91, t89, gm_srccujoh2vgr9aq, 562, 53);
                uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)t88) + (uintptr_t)t91 * 8));
                gm_l22 = t92;
            } else {
                gm_l22 = ((uint64_t)0ULL);
            }
            uint64_t t93 = gm_l22;
            gm_l15 = t93;
            uint32_t t94 = gm_l12;
            uint64_t t95 = gm_l15;
            gm_tb31k5l794t7p t96;
            GM_ST(uint32_t, (uint8_t *)&t96 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t96 + 0), t94);
            GM_ST(uint64_t, ((uint8_t *)&t96 + 8), t95);
            gm_tfqmtuprvunod t97;
            memset(&t97, 0, sizeof t97);
            GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t97 + 16), t96);
            gm_tlg23kb3oj23s t98;
            gm_f_project_6hpd48e((void *)&t98, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l13)), &t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tfonjjajjj3jq t99;
            GM_ST(uint8_t, (uint8_t *)&t99 + 57, 0);
            GM_ST(uint16_t, (uint8_t *)&t99 + 58, 0);
            GM_ST(uint32_t, (uint8_t *)&t99 + 60, 0);
            GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t99), t98);
            ((uint8_t *)&t99)[56] = 1;
            memmove(gm_out, ((uint8_t *)&t99), 64);
            goto gm_fdone;
        }
gm_lb7:;
        {
            uint8_t t100 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
            uint8_t t101 = (uint8_t)(t100 == ((uint8_t)20ULL));
            if (!t101) {
                goto gm_lb8;
            }
            void * t102 = GM_LD(void *, ((((uint8_t *)t13) + 8) + 8));
            gm_l16 = t102;
            void * t103 = GM_LD(void *, (((((uint8_t *)t13) + 8) + 8) + 8));
            gm_l17 = t103;
            uint8_t t104 = GM_LD(uint8_t, (((((uint8_t *)t13) + 8) + 8) + 16));
            gm_l18 = t104;
            void * t105 = gm_l0;
            void * t106 = gm_l1;
            void * t107 = gm_l16;
            void * t108 = gm_l17;
            uint8_t t109 = gm_l18;
            gm_f_index_place_3gfu5dn((void *)&(*(gm_tfonjjajjj3jq *)gm_out), (void *)NULL, cx, NULL, t105, t106, t107, t108, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb8:;
        {
            gm_tfonjjajjj3jq t110;
            memset(&t110, 0, sizeof t110);
            memmove(gm_out, ((uint8_t *)&t110), 64);
            goto gm_fdone;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 549, 5);
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
inline __attribute__((aligned(4))) void gm_f_project_6hpd48e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_project_6hpd48e F;
    GM_STACK_CHECK(&F, 210u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    gm_trqfvmjtt4sij t2;
    gm_f_list_hft9rao((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t2;
    void * t3 = (void *)((uint8_t *)&F.l2);
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 40));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    gm_tftb1odc739t5 t6 = gm_f_extend_vbdqifj(t3, t5);
    gm_l3 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srccujoh2vgr9aq, 98, 5);
    void * t8 = (void *)((uint8_t *)&F.l2);
    gm_tftb1odc739t5 t9 = gm_f_push_kki5o6e(t8, &(*(gm_tfqmtuprvunod *)((uint8_t *)&F.l1)));
    gm_l4 = t9;
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srccujoh2vgr9aq, 99, 5);
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_tu78tssgij60g t12 = GM_LD(gm_tu78tssgij60g, ((uint8_t *)&F.l0));
    gm_tlg23kb3oj23s t13;
    GM_ST(gm_tu78tssgij60g, ((uint8_t *)&t13 + 0), t12);
    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 40), t11);
    memmove(gm_out, ((uint8_t *)&t13), 56);
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
extern __attribute__((aligned(4))) uint64_t gm_f_ptr_size_lig81gu(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) void gm_f_read_result_8csahoh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9805qtonbsag a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_region_q7ivn7p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_result_local_pp1s755(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_result_local_pp1s755 F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tbccvikg6b96e gm_l3;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&gm_l3, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
    uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
    if (t6) {
        gm_l2 = ((uint8_t)1);
    } else {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        gm_t88rcucg1b7uk t10 = gm_l1;
        gm_t53at80pfdus3 t11;
        gm_f_kind_jcnt4hm((void *)&t11, t9, GM_CONV(gm_csguankko4lmhd, t10));
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
        uint8_t t13 = (uint8_t)((t12 == ((uint8_t)5ULL)));
        gm_t53at80pfdus3 t14;
        memset(&t14, 0, sizeof t14);
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)5ULL));
        gm_l2 = t13;
    }
    uint8_t t15 = gm_l2;
    if (t15) {
        gm_t9805qtonbsag t16;
        memset(&t16, 0, sizeof t16);
        F.ret = t16;
        goto gm_fret;
    }
    void * t17 = gm_l0;
    gm_t88rcucg1b7uk t18 = gm_l1;
    uint32_t t19 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t17, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t9805qtonbsag t20;
    GM_ST(uint8_t, (uint8_t *)&t20 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t20 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t20), t19);
    ((uint8_t *)&t20)[4] = 1;
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
extern __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_result_of_9m0s9vd(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_slice_dup5j25(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_slice_dup5j25 F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    gm_tao63pk3r18v5 gm_l2; /* sl */
    void * gm_l3; /* s */
    gm_t88rcucg1b7uk gm_l4; /* usize_ty */
    gm_t88rcucg1b7uk gm_l5; /* elem */
    uint64_t gm_l6; /* esz */
    uint64_t gm_l9; /* n */
    uint32_t gm_l11; /* d */
    uint8_t gm_l12; /* is_const */
    gm_t88rcucg1b7uk gm_l14; /* pointee */
    uint64_t gm_l15; /* n */
    void * gm_l17; /* x */
    uint8_t gm_l19; /* inclusive */
    void * gm_l20; /* x */
    uint32_t gm_l21; /* d */
    GM_STACK_CHECK(&F, 4149u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l22;
    gm_tbccvikg6b96e gm_l23;
    gm_t53at80pfdus3 gm_l24;
    gm_tfonjjajjj3jq gm_l25;
    gm_tlg23kb3oj23s gm_l26;
    gm_tfonjjajjj3jq gm_l27;
    gm_tlg23kb3oj23s gm_l28;
    uint8_t gm_l29;
    gm_t53at80pfdus3 gm_l30;
    gm_t7b7esi3n9r90 gm_l31;
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((uint8_t *)t19));
        gm_l3 = t20;
        void * t21 = gm_l3;
        void * t22 = (void *)(((uint8_t *)t21) + 8);
        gm_t88rcucg1b7uk t23 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t23;
        {
            {
                {
                    {
                        void * t24 = gm_l3;
                        void * t25 = (void *)(((uint8_t *)t24) + 8);
                        void * t26 = gm_l1;
                        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t26));
                        gm_f_kind_jcnt4hm((void *)&gm_l22, t25, GM_CONV(gm_csguankko4lmhd, t27));
                        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                        uint8_t t29 = (uint8_t)(t28 == ((uint8_t)10ULL));
                        if (!t29) {
                            goto gm_lb2;
                        }
                        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l22) + 8) + 4));
                        gm_l5 = t30;
                        void * t31 = gm_l3;
                        gm_t88rcucg1b7uk t32 = gm_l5;
                        gm_f_layout_a5ovnrm((void *)&gm_l23, (void *)NULL, cx, NULL, t31, t32);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t33 = GM_LD(uint64_t, ((uint8_t *)&gm_l23));
                        gm_l6 = t33;
                        gm_t88rcucg1b7uk t34 = gm_l4;
                        gm_t7b7esi3n9r90 t35;
                        memset(&t35, 0, sizeof t35);
                        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t35 + 16), t34);
                        F.l7 = t35;
                        gm_tf8kfhcnval78 t36;
                        memset(&t36, 0, sizeof t36);
                        F.l8 = t36;
                        void * t37 = gm_l3;
                        void * t38 = (void *)(((uint8_t *)t37) + 8);
                        void * t39 = GM_LD(void *, ((uint8_t *)a2));
                        gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t39));
                        gm_f_kind_jcnt4hm((void *)&gm_l24, t38, GM_CONV(gm_csguankko4lmhd, t40));
                        {
                            {
                                uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                uint8_t t42 = (uint8_t)(t41 == ((uint8_t)11ULL));
                                if (!t42) {
                                    goto gm_lb4;
                                }
                                uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                gm_l9 = t43;
                                void * t44 = gm_l3;
                                void * t45 = GM_LD(void *, ((uint8_t *)a2));
                                uint8_t t46 = gm_f_is_place_expr_6llket3(t44, t45);
                                uint8_t t47 = (uint8_t)!t46;
                                if (t47) {
                                    void * t48 = gm_l0;
                                    void * t49 = gm_l1;
                                    gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t48, t49);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_fdone;
                                }
                                void * t50 = gm_l0;
                                void * t51 = GM_LD(void *, ((uint8_t *)a2));
                                gm_f_place_oohr45n((void *)&gm_l25, (void *)NULL, cx, NULL, t50, t51);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t52 = (uint8_t)((((uint8_t *)&gm_l25))[56] != 0);
                                if (t52) {
                                    memmove(((uint8_t *)&gm_l26), ((uint8_t *)&gm_l25), 56);
                                } else {
                                    void * t53 = gm_l0;
                                    void * t54 = gm_l1;
                                    gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t53, t54);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_fdone;
                                }
                                memmove(((uint8_t *)&F.l10), ((uint8_t *)&gm_l26), 56);
                                void * t55 = gm_l0;
                                void * t56 = (void *)((uint8_t *)&F.l10);
                                gm_f_mark_addressed_t8qojin(t55, t56);
                                void * t57 = gm_l0;
                                void * t58 = gm_l3;
                                void * t59 = (void *)(((uint8_t *)t58) + 8);
                                gm_t88rcucg1b7uk t60 = gm_l5;
                                gm_tbngattl1fo51 t61;
                                GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)0));
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t61 + 4), t60);
                                gm_t53at80pfdus3 t62;
                                memset(&t62, 0, sizeof t62);
                                GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)9ULL));
                                GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t62 + 8), t61);
                                gm_t88rcucg1b7uk t63 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t59, &t62);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint32_t t64 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t57, t63);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l11 = t64;
                                void * t65 = gm_l0;
                                uint32_t t66 = gm_l11;
                                gm_tlg23kb3oj23s t67 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l10));
                                gm_tlpcrb3vu6o2d t68;
                                GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t68 + 0), t66);
                                GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t68 + 8), t67);
                                void * t69 = gm_l1;
                                gm_tl874t2atnlh3 t70 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t69) + 72));
                                gm_th7badf4b6ee3 t71;
                                memset(&t71, 0, sizeof t71);
                                GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)2ULL));
                                GM_ST(gm_tlpcrb3vu6o2d, ((uint8_t *)&t71 + 16), t68);
                                gm_b0_1 t72 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t65, &t71, t70);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint32_t t73 = gm_l11;
                                gm_t7b7esi3n9r90 t74;
                                memset(&t74, 0, sizeof t74);
                                GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)0ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t74 + 16), t73);
                                F.l7 = t74;
                                gm_t88rcucg1b7uk t75 = gm_l4;
                                uint64_t t76 = gm_l9;
                                gm_tfbnciphf3261 t77;
                                GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t77 + 0), t75);
                                GM_ST(uint64_t, ((uint8_t *)&t77 + 8), t76);
                                gm_t7b7esi3n9r90 t78;
                                GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t78 + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&t78 + 32, 0);
                                GM_ST(uint64_t, (uint8_t *)&t78 + 40, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t78 + 16), t77);
                                gm_tf8kfhcnval78 t79;
                                GM_ST(uint8_t, (uint8_t *)&t79 + 49, 0);
                                GM_ST(uint16_t, (uint8_t *)&t79 + 50, 0);
                                GM_ST(uint32_t, (uint8_t *)&t79 + 52, 0);
                                GM_ST(uint64_t, (uint8_t *)&t79 + 56, 0);
                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t79), t78);
                                ((uint8_t *)&t79)[48] = 1;
                                F.l8 = t79;
                                goto gm_lb3;
                            }
gm_lb4:;
                            {
                                uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                uint8_t t81 = (uint8_t)(t80 == ((uint8_t)10ULL));
                                if (!t81) {
                                    goto gm_lb5;
                                }
                                uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 8));
                                gm_l12 = t82;
                                void * t83 = gm_l0;
                                void * t84 = GM_LD(void *, ((uint8_t *)a2));
                                gm_f_operand_place_cdle4ls((void *)&gm_l27, (void *)NULL, cx, NULL, t83, t84);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t85 = (uint8_t)((((uint8_t *)&gm_l27))[56] != 0);
                                if (t85) {
                                    memmove(((uint8_t *)&gm_l28), ((uint8_t *)&gm_l27), 56);
                                } else {
                                    void * t86 = gm_l0;
                                    void * t87 = gm_l1;
                                    gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t86, t87);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_fdone;
                                }
                                memmove(((uint8_t *)&F.l13), ((uint8_t *)&gm_l28), 56);
                                void * t88 = gm_l0;
                                gm_tb31k5l794t7p t89;
                                GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t89 + 0), ((uint32_t)0ULL));
                                GM_ST(uint64_t, ((uint8_t *)&t89 + 8), ((uint64_t)0ULL));
                                gm_tfqmtuprvunod t90;
                                memset(&t90, 0, sizeof t90);
                                GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t90 + 16), t89);
                                gm_tlg23kb3oj23s t91;
                                gm_f_project_6hpd48e((void *)&t91, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l13)), &t90);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t92 = gm_l3;
                                void * t93 = (void *)(((uint8_t *)t92) + 8);
                                uint8_t t94 = gm_l12;
                                gm_t88rcucg1b7uk t95 = gm_l5;
                                gm_tbngattl1fo51 t96;
                                GM_ST(uint8_t, (uint8_t *)&t96 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t96 + 2, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t96 + 0), t94);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t96 + 4), t95);
                                gm_t53at80pfdus3 t97;
                                memset(&t97, 0, sizeof t97);
                                GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)9ULL));
                                GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t97 + 8), t96);
                                gm_t88rcucg1b7uk t98 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t93, &t97);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t99 = gm_l1;
                                gm_tl874t2atnlh3 t100 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t99) + 72));
                                gm_f_load_go4fgkp((void *)&F.l7, (void *)NULL, cx, NULL, t88, &t91, t98, t100);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t101 = gm_l0;
                                void * t102 = gm_l0;
                                gm_tlg23kb3oj23s t103 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l13));
                                uint64_t t104 = gm_f_ptr_size_lig81gu((void *)NULL, cx, NULL, t102);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tb31k5l794t7p t105;
                                GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t105 + 0), ((uint32_t)1ULL));
                                GM_ST(uint64_t, ((uint8_t *)&t105 + 8), t104);
                                gm_tfqmtuprvunod t106;
                                memset(&t106, 0, sizeof t106);
                                GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t106 + 16), t105);
                                gm_tlg23kb3oj23s t107;
                                gm_f_project_6hpd48e((void *)&t107, (void *)NULL, cx, NULL, &t103, &t106);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t88rcucg1b7uk t108 = gm_l4;
                                void * t109 = gm_l1;
                                gm_tl874t2atnlh3 t110 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t109) + 72));
                                gm_t7b7esi3n9r90 t111;
                                gm_f_load_go4fgkp((void *)&t111, (void *)NULL, cx, NULL, t101, &t107, t108, t110);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tf8kfhcnval78 t112;
                                GM_ST(uint8_t, (uint8_t *)&t112 + 49, 0);
                                GM_ST(uint16_t, (uint8_t *)&t112 + 50, 0);
                                GM_ST(uint32_t, (uint8_t *)&t112 + 52, 0);
                                GM_ST(uint64_t, (uint8_t *)&t112 + 56, 0);
                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t112), t111);
                                ((uint8_t *)&t112)[48] = 1;
                                F.l8 = t112;
                                goto gm_lb3;
                            }
gm_lb5:;
                            {
                                uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                uint8_t t114 = (uint8_t)(t113 == ((uint8_t)9ULL));
                                if (!t114) {
                                    goto gm_lb6;
                                }
                                {
                                    {
                                        uint8_t t115 = (uint8_t)(GM_LD(void *, (((uint8_t *)a2) + 16)) != 0);
                                        if (t115) {
                                            goto gm_lb8;
                                        }
                                        gm_l29 = ((uint8_t)1);
                                        goto gm_lb7;
                                    }
gm_lb8:;
                                    {
                                        gm_l29 = ((uint8_t)0);
                                        goto gm_lb7;
                                    }
gm_lb9:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1281, 16);
                                }
gm_lb7:;
                                uint8_t t116 = gm_l29;
                                if (t116) {
                                    void * t117 = gm_l0;
                                    void * t118 = gm_l1;
                                    gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t117, t118);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_fdone;
                                }
                                void * t119 = gm_l0;
                                void * t120 = GM_LD(void *, ((uint8_t *)a2));
                                gm_f_expr_tp86bm9((void *)&F.l7, (void *)NULL, cx, NULL, t119, t120);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb3;
                            }
gm_lb6:;
                            {
                                uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                uint8_t t122 = (uint8_t)(t121 == ((uint8_t)8ULL));
                                if (!t122) {
                                    goto gm_lb10;
                                }
                                gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l24) + 8) + 4));
                                gm_l14 = t123;
                                {
                                    {
                                        void * t124 = gm_l3;
                                        void * t125 = (void *)(((uint8_t *)t124) + 8);
                                        gm_t88rcucg1b7uk t126 = gm_l14;
                                        gm_f_kind_jcnt4hm((void *)&gm_l30, t125, GM_CONV(gm_csguankko4lmhd, t126));
                                        uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l30) + 0));
                                        uint8_t t128 = (uint8_t)(t127 == ((uint8_t)11ULL));
                                        if (!t128) {
                                            goto gm_lb12;
                                        }
                                        uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                        gm_l15 = t129;
                                        void * t130 = gm_l0;
                                        void * t131 = GM_LD(void *, ((uint8_t *)a2));
                                        gm_f_expr_tp86bm9((void *)&F.l7, (void *)NULL, cx, NULL, t130, t131);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_t88rcucg1b7uk t132 = gm_l4;
                                        uint64_t t133 = gm_l15;
                                        gm_tfbnciphf3261 t134;
                                        GM_ST(uint32_t, (uint8_t *)&t134 + 4, 0);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t134 + 0), t132);
                                        GM_ST(uint64_t, ((uint8_t *)&t134 + 8), t133);
                                        gm_t7b7esi3n9r90 t135;
                                        GM_ST(uint8_t, (uint8_t *)&t135 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t135 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t135 + 8, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t135 + 32, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t135 + 40, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)1ULL));
                                        GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t135 + 16), t134);
                                        gm_tf8kfhcnval78 t136;
                                        GM_ST(uint8_t, (uint8_t *)&t136 + 49, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t136 + 50, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t136 + 52, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t136 + 56, 0);
                                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t136), t135);
                                        ((uint8_t *)&t136)[48] = 1;
                                        F.l8 = t136;
                                        goto gm_lb11;
                                    }
gm_lb12:;
                                    void * t137 = gm_l0;
                                    void * t138 = gm_l1;
                                    gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t137, t138);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_fdone;
                                }
gm_lb11:;
                                goto gm_lb3;
                            }
gm_lb10:;
                            {
                                void * t139 = gm_l0;
                                void * t140 = gm_l1;
                                gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t139, t140);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_fdone;
                            }
gm_lb13:;
                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1265, 5);
                        }
gm_lb3:;
                    }
                    {
                        gm_t88rcucg1b7uk t141 = gm_l4;
                        gm_tfbnciphf3261 t142;
                        GM_ST(uint32_t, (uint8_t *)&t142 + 4, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t142 + 0), t141);
                        GM_ST(uint64_t, ((uint8_t *)&t142 + 8), ((uint64_t)0ULL));
                        gm_t7b7esi3n9r90 t143;
                        GM_ST(uint8_t, (uint8_t *)&t143 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t143 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t143 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t143 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t143 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t143 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t143 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t143 + 16), t142);
                        F.l16 = t143;
                        {
                            {
                                uint8_t t144 = (uint8_t)(GM_LD(void *, (((uint8_t *)a2) + 8)) != 0);
                                if (!t144) {
                                    goto gm_lb15;
                                }
                                void * t145 = GM_LD(void *, (((uint8_t *)a2) + 8));
                                gm_l17 = t145;
                                void * t146 = gm_l0;
                                void * t147 = gm_l17;
                                gm_f_expr_tp86bm9((void *)&F.l16, (void *)NULL, cx, NULL, t146, t147);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb14;
                            }
gm_lb15:;
                        }
gm_lb14:;
                        gm_t88rcucg1b7uk t148 = gm_l4;
                        gm_tfbnciphf3261 t149;
                        GM_ST(uint32_t, (uint8_t *)&t149 + 4, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t149 + 0), t148);
                        GM_ST(uint64_t, ((uint8_t *)&t149 + 8), ((uint64_t)0ULL));
                        gm_t7b7esi3n9r90 t150;
                        GM_ST(uint8_t, (uint8_t *)&t150 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t150 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t150 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t150 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t150 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t150 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t150 + 16), t149);
                        F.l18 = t150;
                        uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)a2) + 24));
                        gm_l19 = t151;
                        {
                            {
                                uint8_t t152 = (uint8_t)(GM_LD(void *, (((uint8_t *)a2) + 16)) != 0);
                                if (!t152) {
                                    goto gm_lb17;
                                }
                                void * t153 = GM_LD(void *, (((uint8_t *)a2) + 16));
                                gm_l20 = t153;
                                void * t154 = gm_l0;
                                void * t155 = gm_l20;
                                gm_f_expr_tp86bm9((void *)&F.l18, (void *)NULL, cx, NULL, t154, t155);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb16;
                            }
gm_lb17:;
                            uint8_t t156 = (uint8_t)((((uint8_t *)&F.l8))[48] != 0);
                            if (t156) {
                                memmove(((uint8_t *)&gm_l31), ((uint8_t *)&F.l8), 48);
                            } else {
                                void * t157 = gm_l0;
                                void * t158 = gm_l1;
                                gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t157, t158);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_fdone;
                            }
                            memmove(((uint8_t *)&F.l18), ((uint8_t *)&gm_l31), 48);
                            gm_l19 = ((uint8_t)0);
                        }
gm_lb16:;
                        void * t159 = gm_l0;
                        uint8_t t160 = gm_l19;
                        gm_t7b7esi3n9r90 t161 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l16));
                        gm_t7b7esi3n9r90 t162 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l18));
                        gm_tf8kfhcnval78 t163 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&F.l8));
                        gm_thvv7nb91atip t164;
                        GM_ST(uint8_t, (uint8_t *)&t164 + 161, 0);
                        GM_ST(uint16_t, (uint8_t *)&t164 + 162, 0);
                        GM_ST(uint32_t, (uint8_t *)&t164 + 164, 0);
                        GM_ST(uint64_t, (uint8_t *)&t164 + 168, 0);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t164 + 0), t161);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t164 + 48), t162);
                        GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t164 + 96), t163);
                        GM_ST(uint8_t, ((uint8_t *)&t164 + 160), t160);
                        gm_tkj0hg83g38nr t165;
                        GM_ST(uint8_t, (uint8_t *)&t165 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t165 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t165 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_thvv7nb91atip, ((uint8_t *)&t165 + 16), t164);
                        void * t166 = gm_l1;
                        gm_tl874t2atnlh3 t167 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t166) + 72));
                        gm_th7badf4b6ee3 t168;
                        GM_ST(uint8_t, (uint8_t *)&t168 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t168 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t168 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t168 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t168 + 0), ((uint8_t)26ULL));
                        GM_ST(gm_tkj0hg83g38nr, ((uint8_t *)&t168 + 16), t165);
                        gm_b0_1 t169 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t159, &t168, t167);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t170 = gm_l0;
                        void * t171 = gm_l1;
                        gm_t88rcucg1b7uk t172 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t171));
                        uint32_t t173 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t170, t172);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l21 = t173;
                        void * t174 = gm_l0;
                        uint32_t t175 = gm_l21;
                        uint8_t t176 = gm_l19;
                        uint64_t t177 = gm_l6;
                        gm_t7b7esi3n9r90 t178 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l7));
                        gm_t7b7esi3n9r90 t179 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l16));
                        gm_t7b7esi3n9r90 t180 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l18));
                        gm_tgroihbjsmdvm t181;
                        GM_ST(uint32_t, (uint8_t *)&t181 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t181 + 8, 0);
                        GM_ST(uint8_t, (uint8_t *)&t181 + 161, 0);
                        GM_ST(uint16_t, (uint8_t *)&t181 + 162, 0);
                        GM_ST(uint32_t, (uint8_t *)&t181 + 164, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t181 + 0), t175);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t181 + 16), t178);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t181 + 64), t179);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t181 + 112), t180);
                        GM_ST(uint8_t, ((uint8_t *)&t181 + 160), t176);
                        GM_ST(uint64_t, ((uint8_t *)&t181 + 168), t177);
                        void * t182 = gm_l1;
                        gm_tl874t2atnlh3 t183 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t182) + 72));
                        gm_th7badf4b6ee3 t184;
                        GM_ST(uint8_t, (uint8_t *)&t184 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t184 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t184 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t184 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t184 + 192, 0);
                        GM_ST(uint64_t, (uint8_t *)&t184 + 200, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t184 + 0), ((uint8_t)12ULL));
                        GM_ST(gm_tgroihbjsmdvm, ((uint8_t *)&t184 + 16), t181);
                        gm_b0_1 t185 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t174, &t184, t183);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint32_t t186 = gm_l21;
                        gm_t7b7esi3n9r90 t187;
                        memset(&t187, 0, sizeof t187);
                        GM_ST(uint8_t, ((uint8_t *)&t187 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t187 + 16), t186);
                        memmove(gm_out, ((uint8_t *)&t187), 48);
                        goto gm_fdone;
                    }
                }
gm_lb2:;
            }
            {
                void * t188 = gm_l0;
                void * t189 = gm_l1;
                gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t188, t189);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_stmt_k90qtjr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stmt_k90qtjr F;
    void * gm_l1; /* st */
    uint32_t gm_l2; /* l */
    void * gm_l3; /* init */
    void * gm_l4; /* x */
    gm_b16_8 gm_l6; /* locals */
    void * gm_l7; /* init */
    gm_b16_8 gm_l9; /* offs */
    gm_b16_8 gm_l10; /* elems */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    gm_t9805qtonbsag gm_l14; /* x */
    uint32_t gm_l15; /* l */
    void * gm_l17; /* place */
    void * gm_l18; /* value */
    void * gm_l20; /* root */
    void * gm_l21; /* base */
    uint64_t gm_l25; /* k */
    uint32_t gm_l26; /* index */
    uint32_t gm_l27; /* d */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* j */
    void * gm_l32; /* base */
    uint32_t gm_l33; /* index */
    uint32_t gm_l34; /* d */
    gm_b16_8 gm_l37; /* places */
    void * gm_l38; /* value */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    void * gm_l42; /* x */
    void * gm_l43; /* q */
    uint32_t gm_l45; /* l */
    gm_b16_8 gm_l46; /* offs */
    gm_b16_8 gm_l47; /* elems */
    gm_b16_8 gm_l48; /* <tmp> */
    uint64_t gm_l49; /* <tmp> */
    uint64_t gm_l50; /* i */
    void * gm_l54; /* x */
    uint8_t gm_l55; /* is_err */
    gm_b16_8 gm_l57; /* insts */
    GM_STACK_CHECK(&F, 8228u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    void * gm_l58;
    gm_tbccvikg6b96e gm_l59;
    gm_tfonjjajjj3jq gm_l60;
    gm_tlg23kb3oj23s gm_l61;
    gm_tbccvikg6b96e gm_l62;
    gm_t53at80pfdus3 gm_l63;
    gm_tp0pp2cmr6tvd gm_l64;
    uint32_t gm_l65;
    gm_tbccvikg6b96e gm_l66;
    gm_tftb1odc739t5 gm_l67;
    gm_tfonjjajjj3jq gm_l68;
    gm_tlg23kb3oj23s gm_l69;
    gm_tftb1odc739t5 gm_l70;
    gm_tftb1odc739t5 gm_l71;
    gm_tfonjjajjj3jq gm_l72;
    gm_tlg23kb3oj23s gm_l73;
    uint8_t gm_l74;
    gm_tbccvikg6b96e gm_l75;
    void * gm_l76;
    gm_tftb1odc739t5 gm_l77;
    gm_tfonjjajjj3jq gm_l78;
    gm_tlg23kb3oj23s gm_l79;
    gm_tftb1odc739t5 gm_l80;
    gm_tbccvikg6b96e gm_l81;
    gm_t53at80pfdus3 gm_l82;
    gm_t30kh7ksmrtsb gm_l83;
    gm_tlg23kb3oj23s gm_l84;
    gm_tbccvikg6b96e gm_l85;
    {
        void * t45 = gm_l1;
        {
            {
                {
                    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)t45) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)0ULL));
                    if (!t47) {
                        goto gm_lb2;
                    }
                    uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)t45) + 8));
                    gm_l2 = t48;
                    void * t49 = GM_LD(void *, ((((uint8_t *)t45) + 8) + 8));
                    gm_l3 = t49;
                    uint8_t t50 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l3)) != 0);
                    if (t50) {
                        void * t51 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        gm_l58 = t51;
                    } else {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t52 = gm_l58;
                    gm_l4 = t52;
                    void * t53 = F.l0;
                    void * t54 = gm_l4;
                    gm_f_expr_tp86bm9((void *)&F.l5, (void *)NULL, cx, NULL, t53, t54);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t55 = F.l0;
                    void * t56 = gm_l4;
                    uint8_t t57 = gm_f_diverges_p3okmol(t55, t56);
                    if (t57) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t58 = F.l0;
                    void * t59 = GM_LD(void *, ((uint8_t *)t58));
                    void * t60 = gm_l4;
                    gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t60));
                    gm_f_layout_a5ovnrm((void *)&gm_l59, (void *)NULL, cx, NULL, t59, t61);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t62 = GM_LD(uint64_t, ((uint8_t *)&gm_l59));
                    uint8_t t63 = ((t62 == ((uint64_t)0ULL)));
                    if (t63) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t64 = F.l0;
                    uint32_t t65 = gm_l2;
                    gm_tlg23kb3oj23s t66;
                    gm_f_local_place_qnf3mb2((void *)&t66, t65);
                    gm_t7b7esi3n9r90 t67 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l5));
                    gm_t6fp3q71vjrmk t68;
                    GM_ST(uint64_t, (uint8_t *)&t68 + 56, 0);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t68 + 0), t66);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t68 + 64), t67);
                    void * t69 = gm_l4;
                    gm_tl874t2atnlh3 t70 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t69) + 72));
                    gm_th7badf4b6ee3 t71;
                    memset(&t71, 0, sizeof t71);
                    GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)25ULL));
                    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t71 + 16), t68);
                    gm_b0_1 t72 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t64, &t71, t70);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)t45) + 0));
                    uint8_t t74 = (uint8_t)(t73 == ((uint8_t)1ULL));
                    if (!t74) {
                        goto gm_lb3;
                    }
                    gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)t45) + 8));
                    gm_l6 = t75;
                    void * t76 = GM_LD(void *, ((((uint8_t *)t45) + 8) + 16));
                    gm_l7 = t76;
                    void * t77 = F.l0;
                    void * t78 = gm_l7;
                    gm_f_operand_place_cdle4ls((void *)&gm_l60, (void *)NULL, cx, NULL, t77, t78);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t79 = (uint8_t)((((uint8_t *)&gm_l60))[56] != 0);
                    if (t79) {
                        memmove(((uint8_t *)&gm_l61), ((uint8_t *)&gm_l60), 56);
                    } else {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    memmove(((uint8_t *)&F.l8), ((uint8_t *)&gm_l61), 56);
                    void * t80 = F.l0;
                    void * t81 = GM_LD(void *, ((uint8_t *)t80));
                    void * t82 = gm_l7;
                    gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t82));
                    gm_f_layout_a5ovnrm((void *)&gm_l62, (void *)NULL, cx, NULL, t81, t83);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l62) + 16));
                    gm_l9 = t84;
                    {
                        {
                            void * t85 = F.l0;
                            void * t86 = GM_LD(void *, ((uint8_t *)t85));
                            void * t87 = (void *)(((uint8_t *)t86) + 8);
                            void * t88 = gm_l7;
                            gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t88));
                            gm_f_kind_jcnt4hm((void *)&gm_l63, t87, GM_CONV(gm_csguankko4lmhd, t89));
                            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l63) + 0));
                            uint8_t t91 = (uint8_t)(t90 == ((uint8_t)12ULL));
                            if (!t91) {
                                goto gm_lb5;
                            }
                            gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l63) + 8));
                            gm_l10 = t92;
                            gm_b16_8 t93 = gm_l6;
                            gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                            gm_l11 = t94;
                            gm_l12 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t96 = gm_l12;
                                    uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                    uint8_t t98 = (t96 < t97);
                                    if (!t98) {
                                        goto gm_lb6;
                                    }
                                    {
                                        uint64_t t99 = gm_l12;
                                        void * t100 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                        uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                        uint64_t t102 = gm_l12;
                                        gm_t9805qtonbsag t103 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t100) + (uintptr_t)t102 * 8));
                                        gm_tp0pp2cmr6tvd t104;
                                        GM_ST(uint64_t, ((uint8_t *)&t104 + 0), t99);
                                        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t104 + 8), t103);
                                        gm_l64 = t104;
                                        {
                                            {
                                                uint64_t t105 = GM_LD(uint64_t, ((uint8_t *)&gm_l64));
                                                gm_l13 = t105;
                                                gm_t9805qtonbsag t106 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l64) + 8));
                                                gm_l14 = t106;
                                                uint8_t t107 = (uint8_t)((((uint8_t *)&gm_l14))[4] != 0);
                                                if (t107) {
                                                    uint32_t t108 = GM_LD(uint32_t, ((uint8_t *)&gm_l14));
                                                    gm_l65 = t108;
                                                } else {
                                                    goto gm_lb8;
                                                }
                                                uint32_t t109 = gm_l65;
                                                gm_l15 = t109;
                                                void * t110 = F.l0;
                                                void * t111 = GM_LD(void *, ((uint8_t *)t110));
                                                void * t112 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                                uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                uint64_t t114 = gm_l13;
                                                if ((uintptr_t)t114 >= (uintptr_t)(t113)) gm_trap_index((uintptr_t)t114, t113, gm_srccujoh2vgr9aq, 737, 32);
                                                gm_t88rcucg1b7uk t115 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t112) + (uintptr_t)t114 * 4));
                                                gm_f_layout_a5ovnrm((void *)&gm_l66, (void *)NULL, cx, NULL, t111, t115);
                                                if (gm_unw.kind) {
                                                    goto gm_ll95;
                                                }
                                                uint64_t t116 = GM_LD(uint64_t, ((uint8_t *)&gm_l66));
                                                uint8_t t117 = ((t116 == ((uint64_t)0ULL)));
                                                if (t117) {
                                                    goto gm_lb8;
                                                }
                                                void * t118 = F.l0;
                                                uint64_t t119 = gm_l13;
                                                gm_tlg23kb3oj23s t120 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l8));
                                                uint32_t t121 = (uint32_t)t119;
                                                void * t122 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                                uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                                uint64_t t124 = gm_l13;
                                                if ((uintptr_t)t124 >= (uintptr_t)(t123)) gm_trap_index((uintptr_t)t124, t123, gm_srccujoh2vgr9aq, 738, 62);
                                                uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)t122) + (uintptr_t)t124 * 8));
                                                gm_tb31k5l794t7p t126;
                                                GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
                                                GM_ST(uint32_t, ((uint8_t *)&t126 + 0), t121);
                                                GM_ST(uint64_t, ((uint8_t *)&t126 + 8), t125);
                                                gm_tfqmtuprvunod t127;
                                                memset(&t127, 0, sizeof t127);
                                                GM_ST(uint8_t, ((uint8_t *)&t127 + 0), ((uint8_t)0ULL));
                                                GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t127 + 16), t126);
                                                gm_tlg23kb3oj23s t128;
                                                gm_f_project_6hpd48e((void *)&t128, (void *)NULL, cx, NULL, &t120, &t127);
                                                if (gm_unw.kind) {
                                                    goto gm_ll95;
                                                }
                                                void * t129 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                                uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                uint64_t t131 = gm_l13;
                                                if ((uintptr_t)t131 >= (uintptr_t)(t130)) gm_trap_index((uintptr_t)t131, t130, gm_srccujoh2vgr9aq, 738, 73);
                                                gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t129) + (uintptr_t)t131 * 4));
                                                void * t133 = gm_l7;
                                                gm_tl874t2atnlh3 t134 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t133) + 72));
                                                gm_f_load_go4fgkp((void *)&F.l16, (void *)NULL, cx, NULL, t118, &t128, t132, t134);
                                                if (gm_unw.kind) {
                                                    goto gm_ll95;
                                                }
                                                void * t135 = F.l0;
                                                uint32_t t136 = gm_l15;
                                                gm_tlg23kb3oj23s t137;
                                                gm_f_local_place_qnf3mb2((void *)&t137, t136);
                                                gm_t7b7esi3n9r90 t138 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l16));
                                                gm_t6fp3q71vjrmk t139;
                                                GM_ST(uint64_t, (uint8_t *)&t139 + 56, 0);
                                                GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t139 + 0), t137);
                                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t139 + 64), t138);
                                                void * t140 = gm_l7;
                                                gm_tl874t2atnlh3 t141 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t140) + 72));
                                                gm_th7badf4b6ee3 t142;
                                                memset(&t142, 0, sizeof t142);
                                                GM_ST(uint8_t, ((uint8_t *)&t142 + 0), ((uint8_t)25ULL));
                                                GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t142 + 16), t139);
                                                gm_b0_1 t143 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t135, &t142, t141);
                                                if (gm_unw.kind) {
                                                    goto gm_ll95;
                                                }
                                                goto gm_lb9;
                                            }
gm_lb10:;
                                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 735, 13);
                                        }
gm_lb9:;
                                    }
gm_lb8:;
                                    uint64_t t144 = gm_l12;
                                    uint64_t r145; bool o146 = __builtin_add_overflow(t144, ((uint64_t)1ULL), &r145);
                                    uint64_t t147 = r145;
                                    gm_l12 = t147;
                                }
                            }
                            goto gm_ls148;
gm_ll95:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb6; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb8; }
                            }
                            goto gm_lad;
gm_ls148:;
gm_lb6:;
                            goto gm_lb4;
                        }
gm_lb5:;
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
gm_lb4:;
                    goto gm_lb1;
                }
gm_lb3:;
            }
            {
                {
                    uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)t45) + 0));
                    uint8_t t150 = (uint8_t)(t149 == ((uint8_t)2ULL));
                    if (!t150) {
                        goto gm_lb11;
                    }
                    void * t151 = GM_LD(void *, (((uint8_t *)t45) + 8));
                    gm_l17 = t151;
                    void * t152 = GM_LD(void *, ((((uint8_t *)t45) + 8) + 8));
                    gm_l18 = t152;
                    void * t153 = F.l0;
                    void * t154 = gm_l17;
                    uint8_t t155 = gm_f_bits_field_cdqu5on(t153, t154);
                    if (t155) {
                        gm_trqfvmjtt4sij t156;
                        gm_f_list_s1kpkpr((void *)&t156, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l19 = t156;
                        void * t157 = gm_l17;
                        gm_l20 = t157;
                        {
                            for (;;) {
                                void * t159 = F.l0;
                                void * t160 = gm_l20;
                                uint8_t t161 = gm_f_bits_field_cdqu5on(t159, t160);
                                if (!t161) {
                                    goto gm_lb12;
                                }
                                {
                                    void * t162 = (void *)((uint8_t *)&F.l19);
                                    void * t163 = gm_l20;
                                    gm_tftb1odc739t5 t164 = gm_f_push_pf02nv6(t162, t163);
                                    gm_l67 = t164;
                                    uint8_t t165 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                                    if (!t165) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 1, gm_srccujoh2vgr9aq, 750, 21);
                                    {
                                        {
                                            void * t166 = gm_l20;
                                            uint8_t t167 = GM_LD(uint8_t, ((((uint8_t *)t166) + 8) + 0));
                                            uint8_t t168 = (uint8_t)(t167 == ((uint8_t)19ULL));
                                            if (!t168) {
                                                goto gm_lb16;
                                            }
                                            void * t169 = GM_LD(void *, ((((uint8_t *)t166) + 8) + 8));
                                            gm_l21 = t169;
                                            void * t170 = gm_l21;
                                            gm_l20 = t170;
                                            goto gm_lb15;
                                        }
gm_lb16:;
                                        goto gm_lb12;
                                    }
gm_lb15:;
                                }
gm_lb14:;
                            }
                        }
gm_lb12:;
                        void * t171 = F.l0;
                        void * t172 = gm_l20;
                        gm_f_place_oohr45n((void *)&gm_l68, (void *)NULL, cx, NULL, t171, t172);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t173 = (uint8_t)((((uint8_t *)&gm_l68))[56] != 0);
                        if (t173) {
                            memmove(((uint8_t *)&gm_l69), ((uint8_t *)&gm_l68), 56);
                        } else {
                            void * t174 = F.l0;
                            void * t175 = gm_l1;
                            void * t176 = gm_l17;
                            gm_tl874t2atnlh3 t177 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t176) + 72));
                            gm_b0_1 t178 = gm_f_escape_stmt_g02tr5c((void *)NULL, cx, NULL, t174, t175, t177);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = GM_ZERO(gm_b0_1);
                            goto gm_fret;
                        }
                        memmove(((uint8_t *)&F.l22), ((uint8_t *)&gm_l69), 56);
                        void * t179 = F.l0;
                        void * t180 = gm_l18;
                        gm_f_expr_tp86bm9((void *)&F.l23, (void *)NULL, cx, NULL, t179, t180);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t181 = F.l0;
                        void * t182 = gm_l18;
                        uint8_t t183 = gm_f_diverges_p3okmol(t181, t182);
                        if (t183) {
                            F.ret = GM_ZERO(gm_b0_1);
                            goto gm_fret;
                        }
                        gm_trqfvmjtt4sij t184;
                        gm_f_list_0202sdh((void *)&t184, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l24 = t184;
                        void * t185 = (void *)((uint8_t *)&F.l24);
                        void * t186 = F.l0;
                        void * t187 = gm_l20;
                        gm_t88rcucg1b7uk t188 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t187));
                        void * t189 = gm_l17;
                        gm_tl874t2atnlh3 t190 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t189) + 72));
                        gm_t7b7esi3n9r90 t191;
                        gm_f_load_go4fgkp((void *)&t191, (void *)NULL, cx, NULL, t186, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l22)), t188, t190);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t192 = gm_f_push_0pgacsj(t185, &t191);
                        gm_l70 = t192;
                        uint8_t t193 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                        if (!t193) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 1, gm_srccujoh2vgr9aq, 762, 17);
                        uint64_t t194 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 8));
                        gm_l25 = t194;
                        {
                            for (;;) {
                                uint64_t t196 = gm_l25;
                                uint8_t t197 = (t196 > ((uint64_t)1ULL));
                                if (!t197) {
                                    goto gm_lb17;
                                }
                                {
                                    uint64_t t198 = gm_l25;
                                    uint64_t r199; bool o200 = __builtin_sub_overflow(t198, ((uint64_t)1ULL), &r199);
                                    if (o200) gm_trap("integer overflow in `-`", gm_srccujoh2vgr9aq, 765, 21);
                                    uint64_t t201 = r199;
                                    gm_l25 = t201;
                                    {
                                        {
                                            void * t202 = GM_LD(void *, ((uint8_t *)&F.l19));
                                            uint64_t t203 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 8));
                                            uint64_t t204 = gm_l25;
                                            if ((uintptr_t)t204 >= (uintptr_t)(t203)) gm_trap_index((uintptr_t)t204, t203, gm_srccujoh2vgr9aq, 766, 41);
                                            void * t205 = GM_LD(void *, (((uint8_t *)t202) + (uintptr_t)t204 * 8));
                                            uint8_t t206 = GM_LD(uint8_t, ((((uint8_t *)t205) + 8) + 0));
                                            uint8_t t207 = (uint8_t)(t206 == ((uint8_t)19ULL));
                                            if (!t207) {
                                                goto gm_lb21;
                                            }
                                            uint32_t t208 = GM_LD(uint32_t, (((((uint8_t *)t205) + 8) + 8) + 8));
                                            gm_l26 = t208;
                                            void * t209 = F.l0;
                                            void * t210 = GM_LD(void *, ((uint8_t *)&F.l19));
                                            uint64_t t211 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 8));
                                            uint64_t t212 = gm_l25;
                                            if ((uintptr_t)t212 >= (uintptr_t)(t211)) gm_trap_index((uintptr_t)t212, t211, gm_srccujoh2vgr9aq, 767, 34);
                                            void * t213 = GM_LD(void *, (((uint8_t *)t210) + (uintptr_t)t212 * 8));
                                            gm_t88rcucg1b7uk t214 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t213));
                                            uint32_t t215 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t209, t214);
                                            if (gm_unw.kind) {
                                                goto gm_ll195;
                                            }
                                            gm_l27 = t215;
                                            void * t216 = F.l0;
                                            uint32_t t217 = gm_l27;
                                            void * t218 = GM_LD(void *, ((uint8_t *)&F.l24));
                                            uint64_t t219 = GM_LD(uint64_t, (((uint8_t *)&F.l24) + 8));
                                            uint64_t t220 = GM_LD(uint64_t, (((uint8_t *)&F.l24) + 8));
                                            uint64_t r221; bool o222 = __builtin_sub_overflow(t220, ((uint64_t)1ULL), &r221);
                                            if (o222) gm_trap("integer overflow in `-`", gm_srccujoh2vgr9aq, 768, 54);
                                            uint64_t t223 = r221;
                                            if ((uintptr_t)t223 >= (uintptr_t)(t219)) gm_trap_index((uintptr_t)t223, t219, gm_srccujoh2vgr9aq, 768, 41);
                                            uint32_t t224 = gm_l26;
                                            gm_t7b7esi3n9r90 t225 = GM_LD(gm_t7b7esi3n9r90, (((uint8_t *)t218) + (uintptr_t)t223 * 48));
                                            gm_tcpisgbff1dqv t226;
                                            GM_ST(uint32_t, (uint8_t *)&t226 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t226 + 8, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t226 + 68, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t226 + 72, 0);
                                            GM_ST(uint32_t, ((uint8_t *)&t226 + 0), t217);
                                            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t226 + 16), t225);
                                            GM_ST(uint32_t, ((uint8_t *)&t226 + 64), t224);
                                            void * t227 = gm_l17;
                                            gm_tl874t2atnlh3 t228 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t227) + 72));
                                            gm_th7badf4b6ee3 t229;
                                            memset(&t229, 0, sizeof t229);
                                            GM_ST(uint8_t, ((uint8_t *)&t229 + 0), ((uint8_t)21ULL));
                                            GM_ST(gm_tcpisgbff1dqv, ((uint8_t *)&t229 + 16), t226);
                                            gm_b0_1 t230 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t216, &t229, t228);
                                            if (gm_unw.kind) {
                                                goto gm_ll195;
                                            }
                                            void * t231 = (void *)((uint8_t *)&F.l24);
                                            uint32_t t232 = gm_l27;
                                            gm_t7b7esi3n9r90 t233;
                                            memset(&t233, 0, sizeof t233);
                                            GM_ST(uint8_t, ((uint8_t *)&t233 + 0), ((uint8_t)0ULL));
                                            GM_ST(uint32_t, ((uint8_t *)&t233 + 16), t232);
                                            gm_tftb1odc739t5 t234 = gm_f_push_0pgacsj(t231, &t233);
                                            gm_l71 = t234;
                                            uint8_t t235 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                                            if (!t235) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 1, gm_srccujoh2vgr9aq, 769, 21);
                                            goto gm_lb20;
                                        }
gm_lb21:;
                                        goto gm_lb17;
                                    }
gm_lb20:;
                                }
gm_lb19:;
                            }
                        }
                        goto gm_ls236;
gm_ll195:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb17; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb19; }
                        }
                        goto gm_lad;
gm_ls236:;
gm_lb17:;
                        memmove(((uint8_t *)&F.l28), ((uint8_t *)&F.l23), 48);
                        gm_l30 = ((uint64_t)0ULL);
                        uint64_t t237 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 8));
                        gm_l29 = t237;
                        {
                            for (;;) {
                                uint64_t t239 = gm_l30;
                                uint64_t t240 = gm_l29;
                                uint8_t t241 = (t239 < t240);
                                if (!t241) {
                                    goto gm_lb22;
                                }
                                {
                                    {
                                        {
                                            uint64_t t242 = gm_l30;
                                            gm_l31 = t242;
                                            {
                                                {
                                                    void * t243 = GM_LD(void *, ((uint8_t *)&F.l19));
                                                    uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 8));
                                                    uint64_t t245 = gm_l31;
                                                    if ((uintptr_t)t245 >= (uintptr_t)(t244)) gm_trap_index((uintptr_t)t245, t244, gm_srccujoh2vgr9aq, 774, 44);
                                                    void * t246 = GM_LD(void *, (((uint8_t *)t243) + (uintptr_t)t245 * 8));
                                                    uint8_t t247 = GM_LD(uint8_t, ((((uint8_t *)t246) + 8) + 0));
                                                    uint8_t t248 = (uint8_t)(t247 == ((uint8_t)19ULL));
                                                    if (!t248) {
                                                        goto gm_lb28;
                                                    }
                                                    void * t249 = GM_LD(void *, ((((uint8_t *)t246) + 8) + 8));
                                                    gm_l32 = t249;
                                                    uint32_t t250 = GM_LD(uint32_t, (((((uint8_t *)t246) + 8) + 8) + 8));
                                                    gm_l33 = t250;
                                                    void * t251 = F.l0;
                                                    void * t252 = gm_l32;
                                                    gm_t88rcucg1b7uk t253 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t252));
                                                    uint32_t t254 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t251, t253);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll238;
                                                    }
                                                    gm_l34 = t254;
                                                    void * t255 = F.l0;
                                                    uint32_t t256 = gm_l34;
                                                    void * t257 = GM_LD(void *, ((uint8_t *)&F.l24));
                                                    uint64_t t258 = GM_LD(uint64_t, (((uint8_t *)&F.l24) + 8));
                                                    uint64_t t259 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 8));
                                                    uint64_t r260; bool o261 = __builtin_sub_overflow(t259, ((uint64_t)1ULL), &r260);
                                                    if (o261) gm_trap("integer overflow in `-`", gm_srccujoh2vgr9aq, 776, 54);
                                                    uint64_t t262 = r260;
                                                    uint64_t t263 = gm_l31;
                                                    uint64_t r264; bool o265 = __builtin_sub_overflow(t262, t263, &r264);
                                                    if (o265) gm_trap("integer overflow in `-`", gm_srccujoh2vgr9aq, 776, 54);
                                                    uint64_t t266 = r264;
                                                    if ((uintptr_t)t266 >= (uintptr_t)(t258)) gm_trap_index((uintptr_t)t266, t258, gm_srccujoh2vgr9aq, 776, 41);
                                                    uint32_t t267 = gm_l33;
                                                    gm_t7b7esi3n9r90 t268 = GM_LD(gm_t7b7esi3n9r90, (((uint8_t *)t257) + (uintptr_t)t266 * 48));
                                                    gm_t7b7esi3n9r90 t269 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l28));
                                                    gm_t8rrrcunvsibs t270;
                                                    GM_ST(uint32_t, (uint8_t *)&t270 + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t270 + 8, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t270 + 68, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t270 + 72, 0);
                                                    GM_ST(uint32_t, ((uint8_t *)&t270 + 0), t256);
                                                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t270 + 16), t268);
                                                    GM_ST(uint32_t, ((uint8_t *)&t270 + 64), t267);
                                                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t270 + 80), t269);
                                                    void * t271 = gm_l17;
                                                    gm_tl874t2atnlh3 t272 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t271) + 72));
                                                    gm_th7badf4b6ee3 t273;
                                                    memset(&t273, 0, sizeof t273);
                                                    GM_ST(uint8_t, ((uint8_t *)&t273 + 0), ((uint8_t)22ULL));
                                                    GM_ST(gm_t8rrrcunvsibs, ((uint8_t *)&t273 + 16), t270);
                                                    gm_b0_1 t274 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t255, &t273, t272);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll238;
                                                    }
                                                    uint32_t t275 = gm_l34;
                                                    gm_t7b7esi3n9r90 t276;
                                                    memset(&t276, 0, sizeof t276);
                                                    GM_ST(uint8_t, ((uint8_t *)&t276 + 0), ((uint8_t)0ULL));
                                                    GM_ST(uint32_t, ((uint8_t *)&t276 + 16), t275);
                                                    F.l28 = t276;
                                                    goto gm_lb27;
                                                }
gm_lb28:;
                                                goto gm_lb22;
                                            }
gm_lb27:;
                                            goto gm_lb25;
                                        }
gm_lb26:;
                                        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 773, 17);
                                    }
gm_lb25:;
                                }
gm_lb24:;
                                uint64_t t277 = gm_l30;
                                uint64_t r278; bool o279 = __builtin_add_overflow(t277, ((uint64_t)1ULL), &r278);
                                uint64_t t280 = r278;
                                gm_l30 = t280;
                            }
                        }
                        goto gm_ls281;
gm_ll238:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb22; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb24; }
                        }
                        goto gm_lad;
gm_ls281:;
gm_lb22:;
                        void * t282 = F.l0;
                        gm_tlg23kb3oj23s t283 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l22));
                        gm_t7b7esi3n9r90 t284 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l28));
                        gm_t6fp3q71vjrmk t285;
                        GM_ST(uint64_t, (uint8_t *)&t285 + 56, 0);
                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t285 + 0), t283);
                        GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t285 + 64), t284);
                        void * t286 = gm_l18;
                        gm_tl874t2atnlh3 t287 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t286) + 72));
                        gm_th7badf4b6ee3 t288;
                        memset(&t288, 0, sizeof t288);
                        GM_ST(uint8_t, ((uint8_t *)&t288 + 0), ((uint8_t)25ULL));
                        GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t288 + 16), t285);
                        gm_b0_1 t289 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t282, &t288, t287);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t290 = F.l0;
                    void * t291 = gm_l17;
                    gm_f_place_oohr45n((void *)&gm_l72, (void *)NULL, cx, NULL, t290, t291);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t292 = (uint8_t)((((uint8_t *)&gm_l72))[56] != 0);
                    if (t292) {
                        memmove(((uint8_t *)&gm_l73), ((uint8_t *)&gm_l72), 56);
                    } else {
                        void * t293 = F.l0;
                        void * t294 = gm_l1;
                        void * t295 = gm_l17;
                        gm_tl874t2atnlh3 t296 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t295) + 72));
                        gm_b0_1 t297 = gm_f_escape_stmt_g02tr5c((void *)NULL, cx, NULL, t293, t294, t296);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    memmove(((uint8_t *)&F.l35), ((uint8_t *)&gm_l73), 56);
                    void * t298 = F.l0;
                    void * t299 = gm_l18;
                    gm_f_expr_tp86bm9((void *)&F.l36, (void *)NULL, cx, NULL, t298, t299);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t300 = F.l0;
                    void * t301 = gm_l18;
                    uint8_t t302 = gm_f_diverges_p3okmol(t300, t301);
                    if (t302) {
                        gm_l74 = ((uint8_t)1);
                    } else {
                        void * t303 = F.l0;
                        void * t304 = GM_LD(void *, ((uint8_t *)t303));
                        void * t305 = gm_l18;
                        gm_t88rcucg1b7uk t306 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t305));
                        gm_f_layout_a5ovnrm((void *)&gm_l75, (void *)NULL, cx, NULL, t304, t306);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t307 = GM_LD(uint64_t, ((uint8_t *)&gm_l75));
                        uint8_t t308 = ((t307 == ((uint64_t)0ULL)));
                        gm_l74 = t308;
                    }
                    uint8_t t309 = gm_l74;
                    if (t309) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t310 = F.l0;
                    gm_tlg23kb3oj23s t311 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l35));
                    gm_t7b7esi3n9r90 t312 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l36));
                    gm_t6fp3q71vjrmk t313;
                    GM_ST(uint64_t, (uint8_t *)&t313 + 56, 0);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t313 + 0), t311);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t313 + 64), t312);
                    void * t314 = gm_l18;
                    gm_tl874t2atnlh3 t315 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t314) + 72));
                    gm_th7badf4b6ee3 t316;
                    memset(&t316, 0, sizeof t316);
                    GM_ST(uint8_t, ((uint8_t *)&t316 + 0), ((uint8_t)25ULL));
                    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t316 + 16), t313);
                    gm_b0_1 t317 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t310, &t316, t315);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb11:;
            }
            {
                {
                    uint8_t t318 = GM_LD(uint8_t, (((uint8_t *)t45) + 0));
                    uint8_t t319 = (uint8_t)(t318 == ((uint8_t)3ULL));
                    if (!t319) {
                        goto gm_lb29;
                    }
                    gm_b16_8 t320 = GM_LD(gm_b16_8, (((uint8_t *)t45) + 8));
                    gm_l37 = t320;
                    void * t321 = GM_LD(void *, ((((uint8_t *)t45) + 8) + 16));
                    gm_l38 = t321;
                    gm_trqfvmjtt4sij t322;
                    gm_f_list_mgqfh8v((void *)&t322, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l39 = t322;
                    gm_b16_8 t323 = gm_l37;
                    gm_l40 = t323;
                    gm_l41 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t325 = gm_l41;
                            uint64_t t326 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                            uint8_t t327 = (t325 < t326);
                            if (!t327) {
                                goto gm_lb30;
                            }
                            {
                                void * t328 = GM_LD(void *, ((uint8_t *)&gm_l40));
                                uint64_t t329 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                                uint64_t t330 = gm_l41;
                                {
                                    {
                                        void * t331 = GM_LD(void *, (((uint8_t *)t328) + (uintptr_t)t330 * 8));
                                        gm_l42 = t331;
                                        uint8_t t332 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l42)) != 0);
                                        if (t332) {
                                            void * t333 = GM_LD(void *, ((uint8_t *)&gm_l42));
                                            gm_l76 = t333;
                                        } else {
                                            void * t334 = (void *)((uint8_t *)&F.l39);
                                            gm_tfonjjajjj3jq t335;
                                            memset(&t335, 0, sizeof t335);
                                            gm_tftb1odc739t5 t336 = gm_f_push_2v345ri(t334, &t335);
                                            gm_l77 = t336;
                                            uint8_t t337 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                                            if (!t337) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 1, gm_srccujoh2vgr9aq, 796, 21);
                                            goto gm_lb32;
                                        }
                                        void * t338 = gm_l76;
                                        gm_l43 = t338;
                                        void * t339 = F.l0;
                                        void * t340 = gm_l43;
                                        uint8_t t341 = gm_f_bits_field_cdqu5on(t339, t340);
                                        if (t341) {
                                            void * t342 = F.l0;
                                            void * t343 = gm_l1;
                                            void * t344 = gm_l38;
                                            gm_tl874t2atnlh3 t345 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t344) + 72));
                                            gm_b0_1 t346 = gm_f_escape_stmt_g02tr5c((void *)NULL, cx, NULL, t342, t343, t345);
                                            if (gm_unw.kind) {
                                                goto gm_ll324;
                                            }
                                            F.ret = GM_ZERO(gm_b0_1);
                                            goto gm_fret;
                                        }
                                        void * t347 = (void *)((uint8_t *)&F.l39);
                                        void * t348 = F.l0;
                                        void * t349 = gm_l43;
                                        gm_f_place_oohr45n((void *)&gm_l78, (void *)NULL, cx, NULL, t348, t349);
                                        if (gm_unw.kind) {
                                            goto gm_ll324;
                                        }
                                        uint8_t t350 = (uint8_t)((((uint8_t *)&gm_l78))[56] != 0);
                                        if (t350) {
                                            memmove(((uint8_t *)&gm_l79), ((uint8_t *)&gm_l78), 56);
                                        } else {
                                            void * t351 = F.l0;
                                            void * t352 = gm_l1;
                                            void * t353 = gm_l38;
                                            gm_tl874t2atnlh3 t354 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t353) + 72));
                                            gm_b0_1 t355 = gm_f_escape_stmt_g02tr5c((void *)NULL, cx, NULL, t351, t352, t354);
                                            if (gm_unw.kind) {
                                                goto gm_ll324;
                                            }
                                            F.ret = GM_ZERO(gm_b0_1);
                                            goto gm_fret;
                                        }
                                        gm_tlg23kb3oj23s t356 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&gm_l79));
                                        gm_tfonjjajjj3jq t357;
                                        GM_ST(uint8_t, (uint8_t *)&t357 + 57, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t357 + 58, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t357 + 60, 0);
                                        GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t357), t356);
                                        ((uint8_t *)&t357)[56] = 1;
                                        gm_tftb1odc739t5 t358 = gm_f_push_2v345ri(t347, &t357);
                                        gm_l80 = t358;
                                        uint8_t t359 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                                        if (!t359) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 1, gm_srccujoh2vgr9aq, 803, 17);
                                        goto gm_lb33;
                                    }
gm_lb34:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 794, 13);
                                }
gm_lb33:;
                            }
gm_lb32:;
                            uint64_t t360 = gm_l41;
                            uint64_t r361; bool o362 = __builtin_add_overflow(t360, ((uint64_t)1ULL), &r361);
                            uint64_t t363 = r361;
                            gm_l41 = t363;
                        }
                    }
                    goto gm_ls364;
gm_ll324:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb30; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb32; }
                    }
                    goto gm_lad;
gm_ls364:;
gm_lb30:;
                    void * t365 = F.l0;
                    void * t366 = gm_l38;
                    gm_f_expr_tp86bm9((void *)&F.l44, (void *)NULL, cx, NULL, t365, t366);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t367 = F.l0;
                    void * t368 = gm_l38;
                    uint8_t t369 = gm_f_diverges_p3okmol(t367, t368);
                    if (t369) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t370 = F.l0;
                    void * t371 = gm_l38;
                    gm_t88rcucg1b7uk t372 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t371));
                    uint32_t t373 = gm_f_synthetic_t3b2kd0((void *)NULL, cx, NULL, t370, t372);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l45 = t373;
                    void * t374 = F.l0;
                    uint32_t t375 = gm_l45;
                    gm_tlg23kb3oj23s t376;
                    gm_f_local_place_qnf3mb2((void *)&t376, t375);
                    gm_t7b7esi3n9r90 t377 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l44));
                    gm_t6fp3q71vjrmk t378;
                    GM_ST(uint64_t, (uint8_t *)&t378 + 56, 0);
                    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t378 + 0), t376);
                    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t378 + 64), t377);
                    void * t379 = gm_l38;
                    gm_tl874t2atnlh3 t380 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t379) + 72));
                    gm_th7badf4b6ee3 t381;
                    memset(&t381, 0, sizeof t381);
                    GM_ST(uint8_t, ((uint8_t *)&t381 + 0), ((uint8_t)25ULL));
                    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t381 + 16), t378);
                    gm_b0_1 t382 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t374, &t381, t380);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t383 = F.l0;
                    void * t384 = GM_LD(void *, ((uint8_t *)t383));
                    void * t385 = gm_l38;
                    gm_t88rcucg1b7uk t386 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t385));
                    gm_f_layout_a5ovnrm((void *)&gm_l81, (void *)NULL, cx, NULL, t384, t386);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t387 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l81) + 16));
                    gm_l46 = t387;
                    {
                        {
                            void * t388 = F.l0;
                            void * t389 = GM_LD(void *, ((uint8_t *)t388));
                            void * t390 = (void *)(((uint8_t *)t389) + 8);
                            void * t391 = gm_l38;
                            gm_t88rcucg1b7uk t392 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t391));
                            gm_f_kind_jcnt4hm((void *)&gm_l82, t390, GM_CONV(gm_csguankko4lmhd, t392));
                            uint8_t t393 = GM_LD(uint8_t, (((uint8_t *)&gm_l82) + 0));
                            uint8_t t394 = (uint8_t)(t393 == ((uint8_t)12ULL));
                            if (!t394) {
                                goto gm_lb36;
                            }
                            gm_b16_8 t395 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l82) + 8));
                            gm_l47 = t395;
                            gm_b16_8 t396 = GM_LD(gm_b16_8, ((uint8_t *)&F.l39));
                            gm_b16_8 t397 = GM_LD(gm_b16_8, ((uint8_t *)&t396));
                            gm_l48 = t397;
                            gm_l49 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t399 = gm_l49;
                                    uint64_t t400 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                                    uint8_t t401 = (t399 < t400);
                                    if (!t401) {
                                        goto gm_lb37;
                                    }
                                    {
                                        uint64_t t402 = gm_l49;
                                        void * t403 = GM_LD(void *, ((uint8_t *)&gm_l48));
                                        uint64_t t404 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                                        uint64_t t405 = gm_l49;
                                        gm_tfonjjajjj3jq t406 = GM_LD(gm_tfonjjajjj3jq, (((uint8_t *)t403) + (uintptr_t)t405 * 64));
                                        gm_t30kh7ksmrtsb t407;
                                        GM_ST(uint64_t, ((uint8_t *)&t407 + 0), t402);
                                        GM_ST(gm_tfonjjajjj3jq, ((uint8_t *)&t407 + 8), t406);
                                        gm_l83 = t407;
                                        {
                                            {
                                                uint64_t t408 = GM_LD(uint64_t, ((uint8_t *)&gm_l83));
                                                gm_l50 = t408;
                                                memmove(((uint8_t *)&F.l51), (((uint8_t *)&gm_l83) + 8), 64);
                                                uint8_t t409 = (uint8_t)((((uint8_t *)&F.l51))[56] != 0);
                                                if (t409) {
                                                    memmove(((uint8_t *)&gm_l84), ((uint8_t *)&F.l51), 56);
                                                } else {
                                                    goto gm_lb39;
                                                }
                                                memmove(((uint8_t *)&F.l52), ((uint8_t *)&gm_l84), 56);
                                                void * t410 = F.l0;
                                                void * t411 = GM_LD(void *, ((uint8_t *)t410));
                                                void * t412 = GM_LD(void *, ((uint8_t *)&gm_l47));
                                                uint64_t t413 = GM_LD(uint64_t, (((uint8_t *)&gm_l47) + 8));
                                                uint64_t t414 = gm_l50;
                                                if ((uintptr_t)t414 >= (uintptr_t)(t413)) gm_trap_index((uintptr_t)t414, t413, gm_srccujoh2vgr9aq, 816, 32);
                                                gm_t88rcucg1b7uk t415 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t412) + (uintptr_t)t414 * 4));
                                                gm_f_layout_a5ovnrm((void *)&gm_l85, (void *)NULL, cx, NULL, t411, t415);
                                                if (gm_unw.kind) {
                                                    goto gm_ll398;
                                                }
                                                uint64_t t416 = GM_LD(uint64_t, ((uint8_t *)&gm_l85));
                                                uint8_t t417 = ((t416 == ((uint64_t)0ULL)));
                                                if (t417) {
                                                    goto gm_lb39;
                                                }
                                                void * t418 = F.l0;
                                                uint32_t t419 = gm_l45;
                                                gm_tlg23kb3oj23s t420;
                                                gm_f_local_place_qnf3mb2((void *)&t420, t419);
                                                uint64_t t421 = gm_l50;
                                                uint32_t t422 = (uint32_t)t421;
                                                void * t423 = GM_LD(void *, ((uint8_t *)&gm_l46));
                                                uint64_t t424 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 8));
                                                uint64_t t425 = gm_l50;
                                                if ((uintptr_t)t425 >= (uintptr_t)(t424)) gm_trap_index((uintptr_t)t425, t424, gm_srccujoh2vgr9aq, 817, 75);
                                                uint64_t t426 = GM_LD(uint64_t, (((uint8_t *)t423) + (uintptr_t)t425 * 8));
                                                gm_tb31k5l794t7p t427;
                                                GM_ST(uint32_t, (uint8_t *)&t427 + 4, 0);
                                                GM_ST(uint32_t, ((uint8_t *)&t427 + 0), t422);
                                                GM_ST(uint64_t, ((uint8_t *)&t427 + 8), t426);
                                                gm_tfqmtuprvunod t428;
                                                memset(&t428, 0, sizeof t428);
                                                GM_ST(uint8_t, ((uint8_t *)&t428 + 0), ((uint8_t)0ULL));
                                                GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t428 + 16), t427);
                                                gm_tlg23kb3oj23s t429;
                                                gm_f_project_6hpd48e((void *)&t429, (void *)NULL, cx, NULL, &t420, &t428);
                                                if (gm_unw.kind) {
                                                    goto gm_ll398;
                                                }
                                                void * t430 = GM_LD(void *, ((uint8_t *)&gm_l47));
                                                uint64_t t431 = GM_LD(uint64_t, (((uint8_t *)&gm_l47) + 8));
                                                uint64_t t432 = gm_l50;
                                                if ((uintptr_t)t432 >= (uintptr_t)(t431)) gm_trap_index((uintptr_t)t432, t431, gm_srccujoh2vgr9aq, 817, 86);
                                                gm_t88rcucg1b7uk t433 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t430) + (uintptr_t)t432 * 4));
                                                void * t434 = gm_l38;
                                                gm_tl874t2atnlh3 t435 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t434) + 72));
                                                gm_f_load_go4fgkp((void *)&F.l53, (void *)NULL, cx, NULL, t418, &t429, t433, t435);
                                                if (gm_unw.kind) {
                                                    goto gm_ll398;
                                                }
                                                void * t436 = F.l0;
                                                gm_tlg23kb3oj23s t437 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l52));
                                                gm_t7b7esi3n9r90 t438 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l53));
                                                gm_t6fp3q71vjrmk t439;
                                                GM_ST(uint64_t, (uint8_t *)&t439 + 56, 0);
                                                GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t439 + 0), t437);
                                                GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t439 + 64), t438);
                                                void * t440 = gm_l38;
                                                gm_tl874t2atnlh3 t441 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t440) + 72));
                                                gm_th7badf4b6ee3 t442;
                                                memset(&t442, 0, sizeof t442);
                                                GM_ST(uint8_t, ((uint8_t *)&t442 + 0), ((uint8_t)25ULL));
                                                GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t442 + 16), t439);
                                                gm_b0_1 t443 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t436, &t442, t441);
                                                if (gm_unw.kind) {
                                                    goto gm_ll398;
                                                }
                                                goto gm_lb40;
                                            }
gm_lb41:;
                                            gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 814, 13);
                                        }
gm_lb40:;
                                    }
gm_lb39:;
                                    uint64_t t444 = gm_l49;
                                    uint64_t r445; bool o446 = __builtin_add_overflow(t444, ((uint64_t)1ULL), &r445);
                                    uint64_t t447 = r445;
                                    gm_l49 = t447;
                                }
                            }
                            goto gm_ls448;
gm_ll398:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb37; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb39; }
                            }
                            goto gm_lad;
gm_ls448:;
gm_lb37:;
                            goto gm_lb35;
                        }
gm_lb36:;
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
gm_lb35:;
                    goto gm_lb1;
                }
gm_lb29:;
            }
            {
                {
                    uint8_t t449 = GM_LD(uint8_t, (((uint8_t *)t45) + 0));
                    uint8_t t450 = (uint8_t)(t449 == ((uint8_t)4ULL));
                    if (!t450) {
                        goto gm_lb42;
                    }
                    void * t451 = GM_LD(void *, (((uint8_t *)t45) + 8));
                    gm_l54 = t451;
                    void * t452 = F.l0;
                    void * t453 = gm_l54;
                    gm_t7b7esi3n9r90 t454;
                    gm_f_expr_tp86bm9((void *)&t454, (void *)NULL, cx, NULL, t452, t453);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb42:;
                {
                    uint8_t t455 = GM_LD(uint8_t, (((uint8_t *)t45) + 0));
                    uint8_t t456 = (uint8_t)(t455 == ((uint8_t)5ULL));
                    if (!t456) {
                        goto gm_lb43;
                    }
                    uint8_t t457 = GM_LD(uint8_t, (((uint8_t *)t45) + 8));
                    gm_l55 = t457;
                    void * t458 = GM_LD(void *, ((((uint8_t *)t45) + 8) + 8));
                    F.l56 = t458;
                    void * t459 = F.l0;
                    gm_closure t460 = ((gm_closure){(void *)gm_f_fn_u0v0t7q, (void *)&F.h, cx});
                    gm_b16_8 t461 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t459, t460);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l57 = t461;
                    void * t462 = F.l0;
                    uint8_t t463 = gm_l55;
                    gm_b16_8 t464 = gm_l57;
                    gm_thervm8j23qet t465;
                    GM_ST(uint8_t, (uint8_t *)&t465 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t465 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t465 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t465 + 0), t463);
                    GM_ST(gm_b16_8, ((uint8_t *)&t465 + 8), t464);
                    void * t466 = F.l56;
                    gm_tl874t2atnlh3 t467 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t466) + 72));
                    gm_th7badf4b6ee3 t468;
                    memset(&t468, 0, sizeof t468);
                    GM_ST(uint8_t, ((uint8_t *)&t468 + 0), ((uint8_t)33ULL));
                    GM_ST(gm_thervm8j23qet, ((uint8_t *)&t468 + 16), t465);
                    gm_b0_1 t469 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t462, &t468, t467);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb43:;
                gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 723, 5);
            }
        }
gm_lb1:;
    }
    {
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_stmts_ctl06qt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stmts_ctl06qt F;
    void * gm_l0; /* lw */
    void * gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = ((uint64_t)0ULL);
    void * t7 = gm_l1;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    gm_l2 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l3;
            uint64_t t10 = gm_l2;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t12 = gm_l3;
                        gm_l4 = t12;
                        void * t13 = gm_l0;
                        void * t14 = gm_l1;
                        void * t15 = GM_LD(void *, ((uint8_t *)t14));
                        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t14) + 8));
                        uint64_t t17 = gm_l4;
                        if ((uintptr_t)t17 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t17, t16, gm_srccujoh2vgr9aq, 719, 40);
                        void * t18 = (void *)(((uint8_t *)t15) + (uintptr_t)t17 * 32);
                        gm_b0_1 t19 = gm_f_stmt_k90qtjr((void *)NULL, cx, NULL, t13, t18);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 719, 5);
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
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_store_result_fiq5f1j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9805qtonbsag a1, void * a2) {
    struct gm_fr_store_result_fiq5f1j F;
    void * gm_l0; /* lw */
    gm_t9805qtonbsag gm_l1; /* res */
    void * gm_l2; /* e */
    uint32_t gm_l4; /* r */
    GM_STACK_CHECK(&F, 530u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t gm_l5;
    void * t2 = gm_l0;
    void * t3 = gm_l2;
    gm_f_expr_tp86bm9((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l1))[4] != 0);
    if (t4) {
        uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
        gm_l5 = t5;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    uint32_t t6 = gm_l5;
    gm_l4 = t6;
    void * t7 = gm_l0;
    void * t8 = gm_l2;
    uint8_t t9 = gm_f_diverges_p3okmol(t7, t8);
    if (t9) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t10 = gm_l0;
    uint32_t t11 = gm_l4;
    gm_tlg23kb3oj23s t12;
    gm_f_local_place_qnf3mb2((void *)&t12, t11);
    gm_t7b7esi3n9r90 t13 = GM_LD(gm_t7b7esi3n9r90, ((uint8_t *)&F.l3));
    gm_t6fp3q71vjrmk t14;
    GM_ST(uint64_t, (uint8_t *)&t14 + 56, 0);
    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t14 + 0), t12);
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t14 + 64), t13);
    void * t15 = gm_l2;
    gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t15) + 72));
    gm_th7badf4b6ee3 t17;
    memset(&t17, 0, sizeof t17);
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)25ULL));
    GM_ST(gm_t6fp3q71vjrmk, ((uint8_t *)&t17 + 16), t14);
    gm_b0_1 t18 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t10, &t17, t16);
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
inline __attribute__((aligned(4))) uint32_t gm_f_synthetic_t3b2kd0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_synthetic_t3b2kd0 F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tbccvikg6b96e gm_l2; /* lay */
    GM_STACK_CHECK(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 16);
    gm_t88rcucg1b7uk t7 = gm_l1;
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    gm_t0o0svomecjg6 t10;
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 28, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t10 + 0), t7);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 8), t8);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 16), t9);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 24), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t10 + 25), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t10 + 26), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t10 + 27), ((uint8_t)1));
    gm_tftb1odc739t5 t11 = gm_f_push_u8i76or(t6, &t10);
    gm_l3 = t11;
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srccujoh2vgr9aq, 79, 5);
    void * t13 = gm_l0;
    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t13) + 16) + 8));
    uint64_t r15; bool o16 = __builtin_sub_overflow(t14, ((uint64_t)1ULL), &r15);
    if (o16) gm_trap("integer overflow in `-`", gm_srccujoh2vgr9aq, 80, 13);
    uint64_t t17 = r15;
    uint32_t t18 = (uint32_t)t17;
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
extern __attribute__((aligned(4))) uint32_t gm_f_temp_lneuo24(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_try_expr_2gjs5f7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_try_expr_2gjs5f7 F;
    uint32_t gm_l4; /* target */
    uint32_t gm_l8; /* present */
    gm_b16_8 gm_l11; /* fail */
    GM_STACK_CHECK(&F, 1576u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_t9805qtonbsag gm_l12;
    uint32_t gm_l13;
    uint8_t gm_l14;
    gm_t53at80pfdus3 gm_l15;
    gm_tfonjjajjj3jq gm_l16;
    gm_tlg23kb3oj23s gm_l17;
    gm_tbccvikg6b96e gm_l18;
    void * t5 = F.l0;
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    F.l3 = t6;
    void * t7 = F.l3;
    void * t8 = F.l0;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)t8) + 8));
    uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 16));
    gm_t9805qtonbsag t11 = gm_f_ancestor_o28bfm6(t7, t9, t10);
    gm_l12 = t11;
    uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l12))[4] != 0);
    if (t12) {
        uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)&gm_l12));
        gm_l13 = t13;
    } else {
        void * t14 = F.l0;
        void * t15 = F.l1;
        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    uint32_t t16 = gm_l13;
    gm_l4 = t16;
    void * t17 = F.l3;
    void * t18 = GM_LD(void *, (((uint8_t *)t17) + 344));
    uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)t17) + 344) + 8));
    uint32_t t20 = gm_l4;
    uint64_t t21 = (uint64_t)t20;
    if ((uintptr_t)t21 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t21, t19, gm_srccujoh2vgr9aq, 1179, 12);
    gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t18) + (uintptr_t)t21 * 136) + 56));
    F.l5 = t22;
    void * t23 = F.l3;
    void * t24 = (void *)(((uint8_t *)t23) + 8);
    void * t25 = GM_LD(void *, ((uint8_t *)&F.l2));
    gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t25));
    gm_f_kind_jcnt4hm((void *)&gm_l15, t24, GM_CONV(gm_csguankko4lmhd, t26));
    {
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)14ULL));
            if (!t28) {
                goto gm_lb2;
            }
            gm_l14 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l14 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 1180, 18);
    }
gm_lb1:;
    uint8_t t29 = gm_l14;
    F.l6 = t29;
    void * t30 = F.l0;
    void * t31 = GM_LD(void *, ((uint8_t *)&F.l2));
    gm_f_operand_place_cdle4ls((void *)&gm_l16, (void *)NULL, cx, NULL, t30, t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t32 = (uint8_t)((((uint8_t *)&gm_l16))[56] != 0);
    if (t32) {
        memmove(((uint8_t *)&gm_l17), ((uint8_t *)&gm_l16), 56);
    } else {
        void * t33 = F.l0;
        void * t34 = F.l1;
        gm_f_escape_avsr9t6((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l7), ((uint8_t *)&gm_l17), 56);
    void * t35 = F.l0;
    void * t36 = F.l3;
    void * t37 = (void *)(((uint8_t *)t36) + 8);
    gm_t88rcucg1b7uk t38 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t39 = gm_f_temp_lneuo24((void *)NULL, cx, NULL, t35, t38);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t39;
    void * t40 = F.l0;
    uint32_t t41 = gm_l8;
    uint8_t t42 = F.l6;
    gm_tlg23kb3oj23s t43 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l7));
    gm_t3349g713hmcr t44;
    GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t44 + 65, 0);
    GM_ST(uint16_t, (uint8_t *)&t44 + 66, 0);
    GM_ST(uint32_t, (uint8_t *)&t44 + 68, 0);
    GM_ST(uint32_t, ((uint8_t *)&t44 + 0), t41);
    GM_ST(gm_tlg23kb3oj23s, ((uint8_t *)&t44 + 8), t43);
    GM_ST(uint8_t, ((uint8_t *)&t44 + 64), t42);
    void * t45 = F.l1;
    gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t45) + 72));
    gm_th7badf4b6ee3 t47;
    memset(&t47, 0, sizeof t47);
    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)23ULL));
    GM_ST(gm_t3349g713hmcr, ((uint8_t *)&t47 + 16), t44);
    gm_b0_1 t48 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t40, &t47, t46);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t49 = F.l0;
    void * t50 = GM_LD(void *, ((uint8_t *)&F.l2));
    gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t50));
    uint64_t t52 = gm_f_payload_off_o6q9s7n((void *)NULL, cx, NULL, t49, t51);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l9 = t52;
    gm_closure t53 = ((gm_closure){(void *)gm_f_leave_ehrof6p, (void *)&F.h, cx});
    F.l10 = t53;
    void * t54 = F.l0;
    gm_closure t55 = ((gm_closure){(void *)gm_f_fn_k0ofpte, (void *)&F.h, cx});
    gm_b16_8 t56 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t54, t55);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t56;
    void * t57 = F.l0;
    uint32_t t58 = gm_l8;
    gm_t7b7esi3n9r90 t59;
    memset(&t59, 0, sizeof t59);
    GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t59 + 16), t58);
    gm_b16_8 t60 = gm_f_empty_slice_uvn29bg();
    gm_b16_8 t61 = gm_l11;
    gm_t1s3s9ds8udn4 t62;
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t62 + 0), t59);
    GM_ST(gm_b16_8, ((uint8_t *)&t62 + 48), t60);
    GM_ST(gm_b16_8, ((uint8_t *)&t62 + 64), t61);
    void * t63 = F.l1;
    gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 72));
    gm_th7badf4b6ee3 t65;
    memset(&t65, 0, sizeof t65);
    GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)29ULL));
    GM_ST(gm_t1s3s9ds8udn4, ((uint8_t *)&t65 + 16), t62);
    gm_b0_1 t66 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t57, &t65, t64);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t67 = F.l3;
    void * t68 = F.l1;
    gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t68));
    gm_f_layout_a5ovnrm((void *)&gm_l18, (void *)NULL, cx, NULL, t67, t69);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t70 = GM_LD(uint64_t, ((uint8_t *)&gm_l18));
    uint8_t t71 = ((t70 == ((uint64_t)0ULL)));
    if (t71) {
        void * t72 = F.l1;
        gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t72));
        gm_f_zero_sln4lkh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), GM_CONV(gm_csguankko4lmhd, t73));
        goto gm_fdone;
    }
    void * t74 = F.l0;
    uint64_t t75 = F.l9;
    void * t76 = F.l1;
    gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t76));
    gm_tlcm4lvu3g1a8 t78;
    GM_ST(uint32_t, (uint8_t *)&t78 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t78 + 0), t75);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t78 + 8), t77);
    gm_tfqmtuprvunod t79;
    memset(&t79, 0, sizeof t79);
    GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)4ULL));
    GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t79 + 16), t78);
    gm_tlg23kb3oj23s t80;
    gm_f_project_6hpd48e((void *)&t80, (void *)NULL, cx, NULL, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l7)), &t79);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t81 = F.l1;
    gm_t88rcucg1b7uk t82 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t81));
    void * t83 = F.l1;
    gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t83) + 72));
    gm_f_load_go4fgkp((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t74, &t80, t82, t84);
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
extern __attribute__((aligned(4))) void gm_f_unit_ggopu3h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) void gm_f_visit_ajtfk2n(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_with_expr_g16bcnu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_with_expr_g16bcnu F;
    void * gm_l1; /* e */
    void * gm_l5; /* st */
    uint32_t gm_l7; /* l */
    gm_b16_8 gm_l8; /* install */
    gm_b16_8 gm_l9; /* body */
    GM_STACK_CHECK(&F, 1068u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    void * t7 = F.l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    F.l3 = t8;
    gm_tf8kfhcnval78 t9;
    memset(&t9, 0, sizeof t9);
    F.l4 = t9;
    {
        {
            uint8_t t10 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l2) + 24)) != 0);
            if (!t10) {
                goto gm_lb2;
            }
            void * t11 = GM_LD(void *, (((uint8_t *)&F.l2) + 24));
            gm_l5 = t11;
            void * t12 = F.l0;
            void * t13 = gm_l5;
            gm_t7b7esi3n9r90 t14;
            gm_f_expr_tp86bm9((void *)&t14, (void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf8kfhcnval78 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 52, 0);
            GM_ST(uint64_t, (uint8_t *)&t15 + 56, 0);
            GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[48] = 1;
            F.l4 = t15;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t16 = F.l0;
    void * t17 = gm_l1;
    gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t17));
    gm_t9805qtonbsag t19 = gm_f_result_local_pp1s755((void *)NULL, cx, NULL, t16, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t19;
    void * t20 = F.l0;
    uint32_t t21 = gm_f_label_3373lil(t20);
    gm_l7 = t21;
    void * t22 = F.l0;
    void * t23 = (void *)(((uint8_t *)t22) + 144);
    uint32_t t24 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
    uint32_t t25 = gm_l7;
    uint32_t t26 = gm_l7;
    gm_t9805qtonbsag t27 = F.l6;
    gm_tp4rm55bg84ue t28;
    GM_ST(uint32_t, ((uint8_t *)&t28 + 0), t24);
    GM_ST(uint32_t, ((uint8_t *)&t28 + 4), t25);
    GM_ST(uint32_t, ((uint8_t *)&t28 + 8), t26);
    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t28 + 12), t27);
    gm_tftb1odc739t5 t29 = gm_f_push_e2qt8as(t23, &t28);
    gm_l10 = t29;
    uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srccujoh2vgr9aq, 1229, 5);
    {
        for (;;) {
            void * t32 = F.l0;
            uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)t32) + 192) + 8));
            uint32_t t34 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
            uint64_t t35 = (uint64_t)t34;
            uint8_t t36 = (t33 <= t35);
            if (!t36) {
                goto gm_lb3;
            }
            {
                void * t37 = F.l0;
                void * t38 = (void *)(((uint8_t *)t37) + 192);
                gm_t9805qtonbsag t39;
                memset(&t39, 0, sizeof t39);
                gm_tftb1odc739t5 t40 = gm_f_push_r4haklv(t38, t39);
                gm_l11 = t40;
                uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srccujoh2vgr9aq, 1230, 52);
            }
gm_lb5:;
        }
    }
gm_lb3:;
    void * t42 = F.l0;
    void * t43 = GM_LD(void *, (((uint8_t *)t42) + 192));
    uint64_t t44 = GM_LD(uint64_t, ((((uint8_t *)t42) + 192) + 8));
    uint32_t t45 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
    uint64_t t46 = (uint64_t)t45;
    if ((uintptr_t)t46 >= (uintptr_t)(t44)) gm_trap_index((uintptr_t)t46, t44, gm_srccujoh2vgr9aq, 1231, 5);
    gm_t9805qtonbsag t47 = F.l6;
    GM_ST(gm_t9805qtonbsag, (((uint8_t *)t43) + (uintptr_t)t46 * 8), t47);
    void * t48 = F.l0;
    gm_closure t49 = ((gm_closure){(void *)gm_f_fn_mo8l3p0, (void *)&F.h, cx});
    gm_b16_8 t50 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t48, t49);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t50;
    void * t51 = F.l0;
    gm_closure t52 = ((gm_closure){(void *)gm_f_fn_7h6ducr, (void *)&F.h, cx});
    gm_b16_8 t53 = gm_f_region_q7ivn7p((void *)NULL, cx, NULL, t51, t52);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t53;
    void * t54 = F.l0;
    uint32_t t55 = gm_l7;
    uint32_t t56 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
    gm_b16_8 t57 = gm_l8;
    gm_b16_8 t58 = gm_l9;
    gm_tf8kfhcnval78 t59 = GM_LD(gm_tf8kfhcnval78, ((uint8_t *)&F.l4));
    gm_tvdggnhat0k99 t60;
    GM_ST(uint64_t, (uint8_t *)&t60 + 24, 0);
    GM_ST(uint32_t, ((uint8_t *)&t60 + 0), t55);
    GM_ST(uint32_t, ((uint8_t *)&t60 + 4), t56);
    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 8), t57);
    GM_ST(gm_tf8kfhcnval78, ((uint8_t *)&t60 + 32), t59);
    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 96), t58);
    void * t61 = gm_l1;
    gm_tl874t2atnlh3 t62 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t61) + 72));
    gm_th7badf4b6ee3 t63;
    memset(&t63, 0, sizeof t63);
    GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)37ULL));
    GM_ST(gm_tvdggnhat0k99, ((uint8_t *)&t63 + 16), t60);
    gm_b0_1 t64 = gm_f_emit_q94msgt((void *)NULL, cx, NULL, t54, &t63, t62);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t65 = F.l0;
    gm_t9805qtonbsag t66 = F.l6;
    void * t67 = gm_l1;
    gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t67));
    void * t69 = gm_l1;
    gm_tl874t2atnlh3 t70 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t69) + 72));
    gm_f_read_result_8csahoh((void *)&(*(gm_t7b7esi3n9r90 *)gm_out), (void *)NULL, cx, NULL, t65, t66, t68, t70);
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
inline __attribute__((aligned(4))) void gm_f_word_1sfojus(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2) {
    struct gm_fr_word_1sfojus F;
    void * gm_l0; /* lw */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* bits */
    uint16_t gm_l3; /* b */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    GM_STACK_CHECK_SHALLOW(&F, 483u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l5;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    void * t13 = (void *)(((uint8_t *)t12) + 8);
    gm_t88rcucg1b7uk t14 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l5, t13, GM_CONV(gm_csguankko4lmhd, t14));
    {
        {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)0ULL));
            if (!t16) {
                goto gm_lb2;
            }
            uint16_t t17 = GM_LD(uint16_t, ((((uint8_t *)&gm_l5) + 8) + 2));
            gm_l3 = t17;
            uint16_t t18 = gm_l3;
            uint8_t t19 = (t18 > ((uint16_t)64ULL));
            if (t19) {
                gm_tf8kfhcnval78 t20;
                memset(&t20, 0, sizeof t20);
                memmove(gm_out, ((uint8_t *)&t20), 64);
                goto gm_fdone;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t22 = (uint8_t)(t21 == ((uint8_t)1ULL));
                    if (!t22) {
                        goto gm_lb5;
                    }
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)2ULL));
                    if (!t24) {
                        goto gm_lb6;
                    }
                    goto gm_lb4;
                }
gm_lb6:;
                {
                    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)4ULL));
                    if (!t26) {
                        goto gm_lb7;
                    }
                    goto gm_lb4;
                }
gm_lb7:;
                goto gm_lb3;
            }
gm_lb4:;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)13ULL));
            if (!t28) {
                goto gm_lb8;
            }
            gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
            gm_l4 = t29;
            void * t30 = gm_l0;
            void * t31 = GM_LD(void *, ((uint8_t *)t30));
            gm_t88rcucg1b7uk t32 = gm_l4;
            uint8_t t33 = gm_f_is_nullable_pointer_nkie06s(t31, GM_CONV(gm_csguankko4lmhd, t32));
            uint8_t t34 = (uint8_t)!t33;
            if (t34) {
                gm_tf8kfhcnval78 t35;
                memset(&t35, 0, sizeof t35);
                memmove(gm_out, ((uint8_t *)&t35), 64);
                goto gm_fdone;
            }
            goto gm_lb1;
        }
gm_lb8:;
        {
            gm_tf8kfhcnval78 t36;
            memset(&t36, 0, sizeof t36);
            memmove(gm_out, ((uint8_t *)&t36), 64);
            goto gm_fdone;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srccujoh2vgr9aq, 155, 5);
    }
gm_lb1:;
    gm_t88rcucg1b7uk t37 = gm_l1;
    uint64_t t38 = gm_l2;
    gm_tfbnciphf3261 t39;
    GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t39 + 0), t37);
    GM_ST(uint64_t, ((uint8_t *)&t39 + 8), t38);
    gm_t7b7esi3n9r90 t40;
    GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t40 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t40 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t40 + 40, 0);
    GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)1ULL));
    GM_ST(gm_tfbnciphf3261, ((uint8_t *)&t40 + 16), t39);
    gm_tf8kfhcnval78 t41;
    GM_ST(uint8_t, (uint8_t *)&t41 + 49, 0);
    GM_ST(uint16_t, (uint8_t *)&t41 + 50, 0);
    GM_ST(uint32_t, (uint8_t *)&t41 + 52, 0);
    GM_ST(uint64_t, (uint8_t *)&t41 + 56, 0);
    GM_ST(gm_t7b7esi3n9r90, ((uint8_t *)&t41), t40);
    ((uint8_t *)&t41)[48] = 1;
    memmove(gm_out, ((uint8_t *)&t41), 64);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_zero_sln4lkh(void *gm_out, gm_csguankko4lmhd a0);
__attribute__((aligned(4))) gm_t7b7esi3n9r90 gm_f_zero_sln4lkh_c(gm_csguankko4lmhd a0) { gm_t7b7esi3n9r90 gm_r; gm_f_zero_sln4lkh(&gm_r, a0); return gm_r; }
