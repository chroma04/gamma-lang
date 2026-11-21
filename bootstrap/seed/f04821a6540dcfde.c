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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t88rcucg1b7uk v1; } f1; } gm_t5e6kkuq21nig;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_t3mf58gv9kf3n f0; void * f1; uint64_t f2; uint64_t f3; uint64_t f4; } gm_tjt4ka19str13;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t2792tsg5eg72;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t5e6kkuq21nig) == 16 && _Alignof(gm_t5e6kkuq21nig) == 8 && offsetof(gm_t5e6kkuq21nig, f0) == 0 && offsetof(gm_t5e6kkuq21nig, f1) == 8, "layout of `ArrayLen`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_t4c7ipjj9s3fp f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_t4c7ipjj9s3fp f4; gm_trqfvmjtt4sij f5; gm_t4c7ipjj9s3fp f6; gm_b16_8 f7; gm_b16_8 f8; } gm_t6vagfvc3bffs;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; gm_tnpifv20irnph v1; gm_b16_8 v2; gm_b16_8 v3; gm_b16_8 v4; } f1; } gm_t8vuock3jnv59;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { gm_tnpifv20irnph f0; uint8_t f1; uint8_t pe[7]; } gm_teseu4tn5o422;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_th2lqbo77pvsc f0; uint8_t f1; uint8_t pe[7]; } gm_ti5ramhglnvdt;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; uint64_t f3; void * f4; gm_tkn42u7na3t81 f5; uint64_t f6; void * f7; gm_t9qkh0rsooc8d f8; uint8_t p9[7]; void * f9; } gm_tjcptv9sagfq8;
_Static_assert(sizeof(gm_tjt4ka19str13) == 48 && _Alignof(gm_tjt4ka19str13) == 8 && offsetof(gm_tjt4ka19str13, f0) == 0 && offsetof(gm_tjt4ka19str13, f1) == 16 && offsetof(gm_tjt4ka19str13, f2) == 24 && offsetof(gm_tjt4ka19str13, f3) == 32 && offsetof(gm_tjt4ka19str13, f4) == 40, "layout of `Arena`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_t4c7ipjj9s3fp f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; } gm_tpcj7kul6hlib;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_t2792tsg5eg72) == 64 && _Alignof(gm_t2792tsg5eg72) == 8 && offsetof(gm_t2792tsg5eg72, f0) == 0 && offsetof(gm_t2792tsg5eg72, f1) == 40 && offsetof(gm_t2792tsg5eg72, f2) == 48 && offsetof(gm_t2792tsg5eg72, f3) == 56, "layout of `Diagnostics`");
typedef struct { gm_t8vuock3jnv59 f0; uint8_t f1; uint8_t pe[7]; } gm_t3ph3kbiqt1ah;
_Static_assert(sizeof(gm_t6vagfvc3bffs) == 360 && _Alignof(gm_t6vagfvc3bffs) == 8 && offsetof(gm_t6vagfvc3bffs, f0) == 0 && offsetof(gm_t6vagfvc3bffs, f1) == 40 && offsetof(gm_t6vagfvc3bffs, f2) == 96 && offsetof(gm_t6vagfvc3bffs, f3) == 136 && offsetof(gm_t6vagfvc3bffs, f4) == 176 && offsetof(gm_t6vagfvc3bffs, f5) == 232 && offsetof(gm_t6vagfvc3bffs, f6) == 272 && offsetof(gm_t6vagfvc3bffs, f7) == 328 && offsetof(gm_t6vagfvc3bffs, f8) == 344, "layout of `IdeIndex`");
_Static_assert(sizeof(gm_t8vuock3jnv59) == 24 && _Alignof(gm_t8vuock3jnv59) == 8 && offsetof(gm_t8vuock3jnv59, f0) == 0 && offsetof(gm_t8vuock3jnv59, f1) == 8, "layout of `Value`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_teseu4tn5o422) == 24 && _Alignof(gm_teseu4tn5o422) == 8 && offsetof(gm_teseu4tn5o422, f0) == 0 && offsetof(gm_teseu4tn5o422, f1) == 16, "layout of `?EffectSet`");
typedef struct { void * f0; gm_tpcj7kul6hlib f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; uint8_t f9; uint8_t p10[7]; gm_trqfvmjtt4sij f10; gm_trqfvmjtt4sij f11; uint8_t f12; uint8_t p13[7]; gm_trqfvmjtt4sij f13; gm_trqfvmjtt4sij f14; gm_trqfvmjtt4sij f15; gm_trqfvmjtt4sij f16; gm_trqfvmjtt4sij f17; gm_trqfvmjtt4sij f18; gm_trqfvmjtt4sij f19; gm_t9805qtonbsag f20; gm_trqfvmjtt4sij f21; gm_trqfvmjtt4sij f22; gm_trqfvmjtt4sij f23; gm_trqfvmjtt4sij f24; gm_trqfvmjtt4sij f25; gm_trqfvmjtt4sij f26; gm_t4c7ipjj9s3fp f27; gm_t4c7ipjj9s3fp f28; gm_trqfvmjtt4sij f29; uint32_t f30; uint8_t p31[4]; gm_t4c7ipjj9s3fp f31; gm_t4c7ipjj9s3fp f32; uint8_t f33; uint8_t p34[7]; gm_trqfvmjtt4sij f34; gm_trqfvmjtt4sij f35; gm_trqfvmjtt4sij f36; gm_trqfvmjtt4sij f37; gm_trqfvmjtt4sij f38; gm_trqfvmjtt4sij f39; gm_trqfvmjtt4sij f40; gm_trqfvmjtt4sij f41; gm_trqfvmjtt4sij f42; gm_trqfvmjtt4sij f43; uint32_t f44; uint8_t p45[4]; gm_trqfvmjtt4sij f45; uint32_t f46; uint8_t p47[4]; gm_trqfvmjtt4sij f47; uint8_t f48; uint8_t p49[7]; gm_trqfvmjtt4sij f49; gm_trqfvmjtt4sij f50; uint32_t f51; gm_t9805qtonbsag f52; uint8_t p53[4]; gm_trqfvmjtt4sij f53; gm_t4c7ipjj9s3fp f54; gm_t4c7ipjj9s3fp f55; gm_trqfvmjtt4sij f56; uint32_t f57; uint8_t p58[4]; void * f58; void * f59; void * f60; gm_trqfvmjtt4sij f61; gm_trqfvmjtt4sij f62; gm_trqfvmjtt4sij f63; uint64_t f64; gm_t4c7ipjj9s3fp f65; gm_trqfvmjtt4sij f66; gm_trqfvmjtt4sij f67; gm_trqfvmjtt4sij f68; gm_trqfvmjtt4sij f69; uint8_t f70; uint8_t p71[7]; gm_trqfvmjtt4sij f71; gm_trqfvmjtt4sij f72; gm_t4c7ipjj9s3fp f73; gm_t4c7ipjj9s3fp f74; gm_t4c7ipjj9s3fp f75; } gm_tfbjaprcetfe7;
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_ti5ramhglnvdt) == 32 && _Alignof(gm_ti5ramhglnvdt) == 8 && offsetof(gm_ti5ramhglnvdt, f0) == 0 && offsetof(gm_ti5ramhglnvdt, f1) == 24, "layout of `??string`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tjcptv9sagfq8) == 88 && _Alignof(gm_tjcptv9sagfq8) == 8 && offsetof(gm_tjcptv9sagfq8, f0) == 0 && offsetof(gm_tjcptv9sagfq8, f1) == 16 && offsetof(gm_tjcptv9sagfq8, f2) == 28 && offsetof(gm_tjcptv9sagfq8, f3) == 32 && offsetof(gm_tjcptv9sagfq8, f4) == 40 && offsetof(gm_tjcptv9sagfq8, f5) == 48 && offsetof(gm_tjcptv9sagfq8, f6) == 56 && offsetof(gm_tjcptv9sagfq8, f7) == 64 && offsetof(gm_tjcptv9sagfq8, f8) == 72 && offsetof(gm_tjcptv9sagfq8, f9) == 80, "layout of `ConstDef`");
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { void * f0; gm_tin97tt4ud0s1 f1; gm_t2792tsg5eg72 f2; uint32_t f3; uint8_t p4[4]; gm_t6vagfvc3bffs f4; gm_t4c7ipjj9s3fp f5; gm_b16_8 f6; gm_t4c7ipjj9s3fp f7; gm_t4c7ipjj9s3fp f8; } gm_tohkrr5v3jd5r;
_Static_assert(sizeof(gm_tpcj7kul6hlib) == 296 && _Alignof(gm_tpcj7kul6hlib) == 8 && offsetof(gm_tpcj7kul6hlib, f0) == 0 && offsetof(gm_tpcj7kul6hlib, f1) == 40 && offsetof(gm_tpcj7kul6hlib, f2) == 80 && offsetof(gm_tpcj7kul6hlib, f3) == 136 && offsetof(gm_tpcj7kul6hlib, f4) == 176 && offsetof(gm_tpcj7kul6hlib, f5) == 216 && offsetof(gm_tpcj7kul6hlib, f6) == 256, "layout of `Types`");
typedef struct { gm_tin97tt4ud0s1 f0; uint8_t f1; uint8_t pe[7]; } gm_tv37d3dpt2opu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tv37d3dpt2opu v1; } f1; } gm_t074vrl1hb4f6;
_Static_assert(sizeof(gm_t3ph3kbiqt1ah) == 32 && _Alignof(gm_t3ph3kbiqt1ah) == 8 && offsetof(gm_t3ph3kbiqt1ah, f0) == 0 && offsetof(gm_t3ph3kbiqt1ah, f1) == 24, "layout of `?Value`");
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t f4; uint8_t p5[6]; gm_trqfvmjtt4sij f5; gm_b16_8 f6; gm_t0rfe1ou14h21 f7; gm_b16_8 f8; gm_b16_8 f9; gm_b16_8 f10; gm_trqfvmjtt4sij f11; gm_trqfvmjtt4sij f12; gm_trqfvmjtt4sij f13; gm_tf5srh4mp4b3i f14; gm_trqfvmjtt4sij f15; gm_tv37d3dpt2opu f16; void * f17; gm_trqfvmjtt4sij f18; gm_trqfvmjtt4sij f19; gm_trqfvmjtt4sij f20; } gm_tcuh1vih4g9cr;
_Static_assert(sizeof(gm_tfbjaprcetfe7) == 2912 && _Alignof(gm_tfbjaprcetfe7) == 8 && offsetof(gm_tfbjaprcetfe7, f0) == 0 && offsetof(gm_tfbjaprcetfe7, f1) == 8 && offsetof(gm_tfbjaprcetfe7, f2) == 304 && offsetof(gm_tfbjaprcetfe7, f3) == 344 && offsetof(gm_tfbjaprcetfe7, f4) == 384 && offsetof(gm_tfbjaprcetfe7, f5) == 424 && offsetof(gm_tfbjaprcetfe7, f6) == 464 && offsetof(gm_tfbjaprcetfe7, f7) == 504 && offsetof(gm_tfbjaprcetfe7, f8) == 544 && offsetof(gm_tfbjaprcetfe7, f9) == 584 && offsetof(gm_tfbjaprcetfe7, f10) == 592 && offsetof(gm_tfbjaprcetfe7, f11) == 632 && offsetof(gm_tfbjaprcetfe7, f12) == 672 && offsetof(gm_tfbjaprcetfe7, f13) == 680 && offsetof(gm_tfbjaprcetfe7, f14) == 720 && offsetof(gm_tfbjaprcetfe7, f15) == 760 && offsetof(gm_tfbjaprcetfe7, f16) == 800 && offsetof(gm_tfbjaprcetfe7, f17) == 840 && offsetof(gm_tfbjaprcetfe7, f18) == 880 && offsetof(gm_tfbjaprcetfe7, f19) == 920 && offsetof(gm_tfbjaprcetfe7, f20) == 960 && offsetof(gm_tfbjaprcetfe7, f21) == 968 && offsetof(gm_tfbjaprcetfe7, f22) == 1008 && offsetof(gm_tfbjaprcetfe7, f23) == 1048 && offsetof(gm_tfbjaprcetfe7, f24) == 1088 && offsetof(gm_tfbjaprcetfe7, f25) == 1128 && offsetof(gm_tfbjaprcetfe7, f26) == 1168 && offsetof(gm_tfbjaprcetfe7, f27) == 1208 && offsetof(gm_tfbjaprcetfe7, f28) == 1264 && offsetof(gm_tfbjaprcetfe7, f29) == 1320 && offsetof(gm_tfbjaprcetfe7, f30) == 1360 && offsetof(gm_tfbjaprcetfe7, f31) == 1368 && offsetof(gm_tfbjaprcetfe7, f32) == 1424 && offsetof(gm_tfbjaprcetfe7, f33) == 1480 && offsetof(gm_tfbjaprcetfe7, f34) == 1488 && offsetof(gm_tfbjaprcetfe7, f35) == 1528 && offsetof(gm_tfbjaprcetfe7, f36) == 1568 && offsetof(gm_tfbjaprcetfe7, f37) == 1608 && offsetof(gm_tfbjaprcetfe7, f38) == 1648 && offsetof(gm_tfbjaprcetfe7, f39) == 1688 && offsetof(gm_tfbjaprcetfe7, f40) == 1728 && offsetof(gm_tfbjaprcetfe7, f41) == 1768 && offsetof(gm_tfbjaprcetfe7, f42) == 1808 && offsetof(gm_tfbjaprcetfe7, f43) == 1848 && offsetof(gm_tfbjaprcetfe7, f44) == 1888 && offsetof(gm_tfbjaprcetfe7, f45) == 1896 && offsetof(gm_tfbjaprcetfe7, f46) == 1936 && offsetof(gm_tfbjaprcetfe7, f47) == 1944 && offsetof(gm_tfbjaprcetfe7, f48) == 1984 && offsetof(gm_tfbjaprcetfe7, f49) == 1992 && offsetof(gm_tfbjaprcetfe7, f50) == 2032 && offsetof(gm_tfbjaprcetfe7, f51) == 2072 && offsetof(gm_tfbjaprcetfe7, f52) == 2076 && offsetof(gm_tfbjaprcetfe7, f53) == 2088 && offsetof(gm_tfbjaprcetfe7, f54) == 2128 && offsetof(gm_tfbjaprcetfe7, f55) == 2184 && offsetof(gm_tfbjaprcetfe7, f56) == 2240 && offsetof(gm_tfbjaprcetfe7, f57) == 2280 && offsetof(gm_tfbjaprcetfe7, f58) == 2288 && offsetof(gm_tfbjaprcetfe7, f59) == 2296 && offsetof(gm_tfbjaprcetfe7, f60) == 2304 && offsetof(gm_tfbjaprcetfe7, f61) == 2312 && offsetof(gm_tfbjaprcetfe7, f62) == 2352 && offsetof(gm_tfbjaprcetfe7, f63) == 2392 && offsetof(gm_tfbjaprcetfe7, f64) == 2432 && offsetof(gm_tfbjaprcetfe7, f65) == 2440 && offsetof(gm_tfbjaprcetfe7, f66) == 2496 && offsetof(gm_tfbjaprcetfe7, f67) == 2536 && offsetof(gm_tfbjaprcetfe7, f68) == 2576 && offsetof(gm_tfbjaprcetfe7, f69) == 2616 && offsetof(gm_tfbjaprcetfe7, f70) == 2656 && offsetof(gm_tfbjaprcetfe7, f71) == 2664 && offsetof(gm_tfbjaprcetfe7, f72) == 2704 && offsetof(gm_tfbjaprcetfe7, f73) == 2744 && offsetof(gm_tfbjaprcetfe7, f74) == 2800 && offsetof(gm_tfbjaprcetfe7, f75) == 2856, "layout of `Sema`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tohkrr5v3jd5r) == 664 && _Alignof(gm_tohkrr5v3jd5r) == 8 && offsetof(gm_tohkrr5v3jd5r, f0) == 0 && offsetof(gm_tohkrr5v3jd5r, f1) == 8 && offsetof(gm_tohkrr5v3jd5r, f2) == 48 && offsetof(gm_tohkrr5v3jd5r, f3) == 112 && offsetof(gm_tohkrr5v3jd5r, f4) == 120 && offsetof(gm_tohkrr5v3jd5r, f5) == 480 && offsetof(gm_tohkrr5v3jd5r, f6) == 536 && offsetof(gm_tohkrr5v3jd5r, f7) == 552 && offsetof(gm_tohkrr5v3jd5r, f8) == 608, "layout of `Analysis`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_tv37d3dpt2opu) == 48 && _Alignof(gm_tv37d3dpt2opu) == 8 && offsetof(gm_tv37d3dpt2opu, f0) == 0 && offsetof(gm_tv37d3dpt2opu, f1) == 40, "layout of `?Resolution`");
_Static_assert(sizeof(gm_t074vrl1hb4f6) == 56 && _Alignof(gm_t074vrl1hb4f6) == 8 && offsetof(gm_t074vrl1hb4f6, f0) == 0 && offsetof(gm_t074vrl1hb4f6, f1) == 8, "layout of `string!?Resolution`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_tcuh1vih4g9cr) == 552 && _Alignof(gm_tcuh1vih4g9cr) == 8 && offsetof(gm_tcuh1vih4g9cr, f0) == 0 && offsetof(gm_tcuh1vih4g9cr, f1) == 8 && offsetof(gm_tcuh1vih4g9cr, f2) == 16 && offsetof(gm_tcuh1vih4g9cr, f3) == 24 && offsetof(gm_tcuh1vih4g9cr, f4) == 25 && offsetof(gm_tcuh1vih4g9cr, f5) == 32 && offsetof(gm_tcuh1vih4g9cr, f6) == 72 && offsetof(gm_tcuh1vih4g9cr, f7) == 88 && offsetof(gm_tcuh1vih4g9cr, f8) == 152 && offsetof(gm_tcuh1vih4g9cr, f9) == 168 && offsetof(gm_tcuh1vih4g9cr, f10) == 184 && offsetof(gm_tcuh1vih4g9cr, f11) == 200 && offsetof(gm_tcuh1vih4g9cr, f12) == 240 && offsetof(gm_tcuh1vih4g9cr, f13) == 280 && offsetof(gm_tcuh1vih4g9cr, f14) == 320 && offsetof(gm_tcuh1vih4g9cr, f15) == 336 && offsetof(gm_tcuh1vih4g9cr, f16) == 376 && offsetof(gm_tcuh1vih4g9cr, f17) == 424 && offsetof(gm_tcuh1vih4g9cr, f18) == 432 && offsetof(gm_tcuh1vih4g9cr, f19) == 472 && offsetof(gm_tcuh1vih4g9cr, f20) == 512, "layout of `Loader`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
struct gm_fr_allocator_5m783ja {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_analyze_idnr1pc {
    gm_frame h;
    gm_tcuh1vih4g9cr l4; /* l */
    gm_tv37d3dpt2opu l8; /* res */
    gm_tfbjaprcetfe7 l9; /* s */
    gm_b0_1 ret;
};
struct gm_fr_array_k32ana3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_char_len_k9gjmvv {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_check_constants_20kd087 {
    gm_frame h;
    gm_tjcptv9sagfq8 l4; /* c */
    gm_b0_1 ret;
};
struct gm_fr_check_module_6elsf9l {
    gm_frame h;
    void * ret;
    gm_b0_1 w0;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_core_path_for_g46v4ll {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_diagnostic_a0b47q4 {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* message */
    gm_trqfvmjtt4sij l9; /* related */
    gm_tin97tt4ud0s1 l14; /* loc */
    gm_tin97tt4ud0s1 l15; /* item */
    gm_tin97tt4ud0s1 l17; /* v */
    gm_trqfvmjtt4sij l18; /* fixes */
    gm_tl7hpf5nlilqq l21; /* fx */
    gm_tin97tt4ud0s1 l23; /* data */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_diagnostics_7ntqr3v {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_t4c7ipjj9s3fp l4; /* index */
    gm_trqfvmjtt4sij l14; /* items */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_diagnostics_n3kkhf4 {
    gm_frame h;
    gm_t2792tsg5eg72 ret;
};
struct gm_fr_dir_of_ho73hue {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_08mdrht {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7hq6cu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8jt91p4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ab79bmt_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dheabhi_4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_foftr3p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jagk2mc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_o4u155a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oq9aajs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_q1rgujj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_qmfcuqk_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_file_id_mif69v8 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fix_json_pbc9vbt {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* changes */
    gm_tin97tt4ud0s1 l10; /* edit */
    gm_trqfvmjtt4sij l16; /* es */
    gm_tin97tt4ud0s1 l17; /* cs */
    gm_tl7hpf5nlilqq l20; /* c */
    gm_tin97tt4ud0s1 l21; /* v */
    gm_t3ph3kbiqt1ah ret;
};
struct gm_fr_fn_6gkpg0v_2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_format_agi9f7f {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gej351p {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tat75d3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_m1knqqk {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_hash_map_1s9eugt {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_deo2ele {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_lljdp3h {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_rs5nlfs {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hex_value_08hqdl8 {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_install_db2gf1l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_int_95caidg {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_is_char_boundary_h62fhaa {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_core_rhbi4fa {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_line_index_from_dbptimh {
    gm_frame h;
    gm_t6j6smj7qoctu ret;
};
struct gm_fr_lines_0mpphhg {
    gm_frame h;
    gm_t6j6smj7qoctu ret;
};
struct gm_fr_list_0202sdh_4 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_4qf7cqo {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5j3106o {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_792g2go_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_bvkp9ir {
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
struct gm_fr_make_analysis_fj48u3v {
    gm_frame h;
    void * ret;
    gm_b0_1 w0;
};
struct gm_fr_make_read_only_a29eok8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_new_jr5ejs0 {
    gm_frame h;
    gm_tjt4ka19str13 ret;
};
struct gm_fr_obj_aoa7fl7 {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_open_path_6gmq4nk {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_path_to_uri_as8bikc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_pos_json_oo8uj70 {
    gm_frame h;
    gm_tin97tt4ud0s1 l2; /* o */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_push_0pgacsj_4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_302k12h {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_l5thl65 {
    gm_frame h;
    gm_tl7hpf5nlilqq l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_p48r1bd {
    gm_frame h;
    gm_tl7hpf5nlilqq l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_range_demc0bt {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_range_json_hlsolbo {
    gm_frame h;
    gm_tin97tt4ud0s1 l6; /* o */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_range_json_zero_orc6s8p {
    gm_frame h;
    gm_tin97tt4ud0s1 l0; /* o */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_real_path_al8cv9r {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5c6uc3s {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_bqmbgmv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ddqa9t6_4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ht5g7p4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_release_7gtbra1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_core_nv5iii8 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* ids */
    gm_b0_1 ret;
};
struct gm_fr_scan_lines_lhr1jn1 {
    gm_frame h;
    gm_tnpifv20irnph ret;
    gm_b0_1 w0;
};
struct gm_fr_sort_o401rjn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_source_map_4v8dg4b {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_source_text_7ru912k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_text_ls49dp6 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_uri_of_t6r79gp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_uri_to_path_kbof22r {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* out */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_value_pkuqal3 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_write_h3ti3fb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_analyze_idnr1pc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_array_k32ana3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_length_f852tll(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_build_aeq3nd5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) void gm_f_cache_dir_81h90qu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_canonical_path_q6lq4b6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_char_len_k9gjmvv(uint8_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_constants_20kd087(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_check_module_6elsf9l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_program_rltsqqp(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_chmod_tree_sj4qk6r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_hook_2qtf05o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_const_value_fr4coi4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_core_path_for_g46v4ll(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_core_root_ip442nf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_destroy_602qeev(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_diagnostic_a0b47q4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_t9qkh0rsooc8d a3, void * a4);
__attribute__((aligned(4))) void gm_f_diagnostics_7ntqr3v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dir_of_ho73hue(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_08mdrht(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7hq6cu2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_foftr3p(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jagk2mc(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_o4u155a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oq9aajs(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q1rgujj(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_qmfcuqk_3(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_file_id_mif69v8(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ve3qs5b(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_fix_json_pbc9vbt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v_2(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_format_agi9f7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_eara94q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_gej351p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_tat75d3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_get_9k2rdij(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_lk35mh7(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_m1knqqk(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_1s9eugt(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_deo2ele(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_lljdp3h(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_rs5nlfs(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_hex_value_08hqdl8(uint8_t a0);
__attribute__((aligned(4))) void gm_f_index_nki2g2t(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_install_db2gf1l(void * a0);
inline __attribute__((aligned(4))) void gm_f_int_95caidg(void *gm_out, void *env, gm_node *cx, gm_node *inst, int64_t a0);
__attribute__((aligned(4))) void gm_f_introsort_6k4m7ds(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_char_boundary_h62fhaa(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_core_rhbi4fa(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_line_index_from_dbptimh(void *gm_out, gm_b16_8 a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_line_starts_oja6lfl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_lines_0mpphhg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_4qf7cqo(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5j3106o(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_792g2go_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_load_core_du4go5i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_load_root_af4ud7b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_loader_on0up0a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, uint8_t a4);
__attribute__((aligned(4))) void * gm_f_make_analysis_fj48u3v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_make_dir_all_q6phjan(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_read_only_a29eok8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_6dt94nu(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_cqfitgo(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_obj_aoa7fl7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_open_path_6gmq4nk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_path_to_uri_as8bikc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_pos_json_oo8uj70(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, uint32_t a1);
__attribute__((aligned(4))) gm_tcc1rq4n1avb2 gm_f_position_fpk0ln3(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) void gm_f_prepare_4l27ul2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj_4(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_302k12h(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l5thl65(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_p48r1bd(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_074448v(void * a0, gm_b16_8 a1, uint32_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_91stqpj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_dm7u1sg(void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_idr4g4a(void * a0, uint32_t a1, gm_tnpifv20irnph a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qoojoqv(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_range_demc0bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) void gm_f_range_json_hlsolbo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) void gm_f_range_json_zero_orc6s8p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_real_path_al8cv9r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5c6uc3s(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bqmbgmv(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6_4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ht5g7p4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_release_7gtbra1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_ambient_6orl0vo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_core_nv5iii8(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_declarations_vrpg1fl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_scan_lines_lhr1jn1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_sema_r1nlnnm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_sort_o401rjn(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_source_text_7ru912k(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_std_root_e0juohb(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_temp_dir_i6apmsd(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_text_ls49dp6(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_of_expr_l8qm9vv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_uri_of_t6r79gp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_uri_to_path_kbof22r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_utf8_valid_a8oc1t8(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_value_pkuqal3(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_atomic_18k5vfb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_h3ti3fb(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uqn5uk3(void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_b16_8 a3);
extern uint8_t gm_cb1h8mol9po[16] __attribute__((aligned(8)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cjlj93gp0or[24] __attribute__((aligned(8)));
extern uint8_t gm_cro0h2panct[24] __attribute__((aligned(8)));
extern void *const gm_hd3t1u951rul[];
extern uint8_t gm_k3h72k4hfps[40] __attribute__((aligned(8)));
extern uint8_t gm_k4g6hon27vb[40] __attribute__((aligned(8)));
extern uint8_t gm_k66sf6gvqqo[40] __attribute__((aligned(8)));
extern uint8_t gm_kqfbmlr3820[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0tfhmgv1pal7[9];
extern const uint8_t gm_s19jg4ct3aq11[6];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s28fmt6o4roj7[10];
extern const uint8_t gm_s4769el4glhs1[4];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s6p5i58jsib94[8];
extern const uint8_t gm_s6vl5jv0tf2n4[8];
extern const uint8_t gm_s7mq585fepqnh[6];
extern const uint8_t gm_sb3c9ehk423nf[6];
extern const uint8_t gm_sbe2tiimd9c3v[8];
extern const uint8_t gm_sbgbo6kv938k3[19];
extern const uint8_t gm_sbo52miqad506[4];
extern const uint8_t gm_scifaj4dn1ok5[6];
extern const uint8_t gm_se23d1hoq85r1[5];
extern const uint8_t gm_sh9j083o80vrk[8];
extern const uint8_t gm_shf0g22rjm3o7[5];
extern const uint8_t gm_si702r0b37nml[6];
extern const uint8_t gm_sirpcjgkmlehr[8];
extern const uint8_t gm_sl4ns6hobicht[6];
extern const uint8_t gm_smr3sbhcj1goc[7];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_snjdrq8n0vphi[12];
extern const uint8_t gm_sou5f0qr4vdgr[8];
extern const uint8_t gm_sp6bf9nji9g1j[9];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src2u46tincrqmv[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcgp1oqfehgsjb[];
extern const char gm_srcip69en21ht7v[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_ssvcneuafnfds[8];
extern const uint8_t gm_st3p5nb1e6cc8[5];
extern const uint8_t gm_sujli4dml3ipq[8];
extern const uint8_t gm_svulttobk4q7t[4];
extern const gm_tmpl gm_tplbbopfj3f21eb;
extern const gm_tmpl gm_tpldm28gt81dafq;
extern const gm_tmpl gm_tplhr6emralh8q6;
extern const gm_tmpl gm_tplk34ojqiaho84;
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
inline __attribute__((aligned(4))) void gm_f_array_k32ana3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_array_k32ana3 F;
    gm_b16_8 gm_l0; /* items */
    GM_STACK_CHECK(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l2;
    gm_trqfvmjtt4sij t2;
    gm_f_list_0202sdh_4((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t4 = gm_l0;
    gm_tftb1odc739t5 t5 = gm_f_extend_qmfcuqk_3(t3, t4);
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcgp1oqfehgsjb, 42, 5);
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_t8vuock3jnv59 t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)4ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 8), t7);
    memmove(gm_out, ((uint8_t *)&t8), 24);
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
inline __attribute__((aligned(4))) uint64_t gm_f_char_len_k9gjmvv(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_char_len_k9gjmvv F;
    uint8_t gm_l0; /* lead */
    GM_STACK_CHECK_SHALLOW(&F, 25u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 < ((uint8_t)128ULL));
    if (t3) {
        F.ret = ((uint64_t)1ULL);
        goto gm_fret;
    }
    uint8_t t4 = gm_l0;
    uint8_t t5 = (t4 >= ((uint8_t)240ULL));
    if (t5) {
        F.ret = ((uint64_t)4ULL);
        goto gm_fret;
    }
    uint8_t t6 = gm_l0;
    uint8_t t7 = (t6 >= ((uint8_t)224ULL));
    if (t7) {
        F.ret = ((uint64_t)3ULL);
        goto gm_fret;
    }
    uint8_t t8 = gm_l0;
    uint8_t t9 = (t8 >= ((uint8_t)192ULL));
    if (t9) {
        F.ret = ((uint64_t)2ULL);
        goto gm_fret;
    }
    F.ret = ((uint64_t)1ULL);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dir_of_ho73hue(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_dir_of_ho73hue F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l1;
    gm_b16_8 gm_l2;
    gm_b16_8 t2 = gm_l0;
    gm_f_parent_lk0iero((void *)&gm_l1, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t3 = (uint8_t)((((uint8_t *)&gm_l1))[16] != 0);
    if (t3) {
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
        gm_l2 = t4;
    } else {
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(0));
        gm_l2 = t5;
    }
    gm_b16_8 t6 = gm_l2;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_08mdrht(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_08mdrht F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7hq6cu2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7hq6cu2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8jt91p4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ab79bmt_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dheabhi_4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_foftr3p(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_foftr3p F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jagk2mc(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jagk2mc F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_o4u155a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_o4u155a F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oq9aajs(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oq9aajs F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q1rgujj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_q1rgujj F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_qmfcuqk_3(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_qmfcuqk_3 F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_ddqa9t6_4(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 24);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 24)));
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_file_id_mif69v8(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_file_id_mif69v8 F;
    void * gm_l0; /* a */
    gm_b16_8 gm_l1; /* path */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 480);
    gm_b16_8 t4 = gm_l1;
    gm_t9805qtonbsag t5 = gm_f_get_m1knqqk(t3, t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v_2(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_6gkpg0v_2 F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)t2));
    void * t4 = gm_l1;
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)t4));
    uint8_t t6 = (t3 < t5);
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_agi9f7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_agi9f7f F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sou5f0qr4vdgr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhr6emralh8q6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_gej351p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0) {
    struct gm_fr_format_gej351p F;
    gm_tm98ap4112hee gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tm98ap4112hee gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tm98ap4112hee t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_eara94q((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_tat75d3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_tat75d3 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6p5i58jsib94));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbbopfj3f21eb), t3);
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_m1knqqk(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_m1knqqk F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    uint64_t gm_l4;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_ve3qs5b(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l4 = t6;
    } else {
        gm_t9805qtonbsag t7;
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
    uint32_t t13 = GM_LD(uint32_t, ((((uint8_t *)t10) + (uintptr_t)t12 * 32) + 24));
    gm_t9805qtonbsag t14;
    GM_ST(uint8_t, (uint8_t *)&t14 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t14 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t14), t13);
    ((uint8_t *)&t14)[4] = 1;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_1s9eugt(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_1s9eugt F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_08mdrht();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_deo2ele(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_deo2ele F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_foftr3p();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_lljdp3h(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_lljdp3h F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_7hq6cu2();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_rs5nlfs(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_rs5nlfs F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jagk2mc();
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
inline __attribute__((aligned(4))) void gm_f_install_db2gf1l(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_install_db2gf1l F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    GM_ST(void *, (((uint8_t *)t2) + 2288), ((void *)gm_f_const_hook_2qtf05o));
    void * t3 = gm_l0;
    GM_ST(void *, (((uint8_t *)t3) + 2296), ((void *)gm_f_array_length_f852tll));
    void * t4 = gm_l0;
    GM_ST(void *, (((uint8_t *)t4) + 2304), ((void *)gm_f_type_of_expr_l8qm9vv));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_int_95caidg(void *gm_out, void *env, gm_node *cx, gm_node *inst, int64_t a0) {
    struct gm_fr_int_95caidg F;
    int64_t gm_l0; /* n */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l1;
    int64_t t2 = gm_l0;
    gm_tm98ap4112hee t3;
    GM_ST(int64_t, ((uint8_t *)&t3 + 0), t2);
    gm_f_format_gej351p((void *)&gm_l1, (void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t4) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_srcgp1oqfehgsjb, 33, 74);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_tnpifv20irnph t7;
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t6);
    gm_t8vuock3jnv59 t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)2ULL));
    GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t8 + 8), t7);
    memmove(gm_out, ((uint8_t *)&t8), 24);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_char_boundary_h62fhaa(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_char_boundary_h62fhaa F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t4 = (t2 >= t3);
    if (t4) {
        uint64_t t5 = gm_l1;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint8_t t7 = ((t5 == t6));
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t10 = gm_l1;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t8) + (uintptr_t)t10 * 1));
    uint8_t t12 = (uint8_t)(t11 & ((uint8_t)192ULL));
    uint8_t t13 = (!(t12 == ((uint8_t)128ULL)));
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_core_rhbi4fa(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_core_rhbi4fa F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)gm_cb1h8mol9po));
    uint8_t t4 = gm_f_starts_with_6vvt01e(t2, t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_line_index_from_dbptimh(void *gm_out, gm_b16_8 a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_line_index_from_dbptimh F;
    gm_b16_8 gm_l0; /* text */
    gm_tnpifv20irnph gm_l1; /* starts */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
    gm_t6j6smj7qoctu t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 16), t3);
    memmove(gm_out, ((uint8_t *)&t4), 32);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0202sdh_4 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_dheabhi_4();
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
inline __attribute__((aligned(4))) void gm_f_list_4qf7cqo(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_4qf7cqo F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_q1rgujj();
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
inline __attribute__((aligned(4))) void gm_f_list_5j3106o(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5j3106o F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_o4u155a();
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
inline __attribute__((aligned(4))) void gm_f_list_792g2go_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_792g2go_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ab79bmt_2();
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
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_bvkp9ir F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8jt91p4();
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_read_only_a29eok8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_make_read_only_a29eok8 F;
    gm_b16_8 gm_l0; /* root */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svulttobk4q7t));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(3));
    gm_b16_8 t3 = gm_l0;
    gm_b0_1 t4 = gm_f_chmod_tree_sj4qk6r((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_obj_aoa7fl7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_obj_aoa7fl7 F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_792g2go_2((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tin97tt4ud0s1 t3;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t3 + 0), t2);
    memmove(gm_out, ((uint8_t *)&t3), 40);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj_4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0pgacsj_4 F;
    void * gm_l0; /* l */
    gm_t8vuock3jnv59 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_dheabhi_4();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ddqa9t6_4(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_302k12h(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_302k12h F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_oq9aajs();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_5c6uc3s(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l5thl65(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_l5thl65 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_o4u155a();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_bqmbgmv(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 56), ((uint8_t *)a1), 56);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 56)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_p48r1bd(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_p48r1bd F;
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
        gm_b16_8 t9 = gm_f_empty_slice_q1rgujj();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ht5g7p4(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 56), ((uint8_t *)a1), 56);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 56)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5c6uc3s(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5c6uc3s F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bqmbgmv(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_bqmbgmv F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6_4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ddqa9t6_4 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ht5g7p4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ht5g7p4 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_release_7gtbra1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_release_7gtbra1 F;
    void * gm_l0; /* a */
    void * gm_l1; /* ar */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_l1 = t3;
    void * t4 = gm_l1;
    gm_f_deinit_sejdpqv(t4);
    void * t5 = gm_l1;
    gm_b0_1 t6 = gm_f_destroy_602qeev((void *)NULL, cx, NULL, t5, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3h72k4hfps)));
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_scan_lines_lhr1jn1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_scan_lines_lhr1jn1 F;
    void * gm_l0; /* a */
    gm_b16_8 gm_l1; /* t */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, ((uint8_t *)t3));
        gm_t3mf58gv9kf3n t5 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t4));
        gm_t3mf58gv9kf3n t6 = t5;
        gm_node wn7 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t6, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t10 = gm_l1;
        gm_tnpifv20irnph t11 = gm_f_line_starts_oja6lfl((void *)NULL, (&wn7), NULL, t10);
        if (gm_unw.kind) {
            goto gm_ll9;
        }
        F.ret = t11;
        goto gm_fret;
    }
    goto gm_ls12;
gm_ll9:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we8; }
    goto gm_lad;
gm_ls12:;
gm_lb1:;
we8:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sort_o401rjn(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_o401rjn F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_closure t3 = ((gm_closure){GM_PTR_TAG(gm_f_fn_6gkpg0v_2), NULL, cx});
    gm_f_introsort_6k4m7ds(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_source_map_4v8dg4b F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_bvkp9ir((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tin97tt4ud0s1 t3;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t3 + 0), t2);
    memmove(gm_out, ((uint8_t *)&t3), 40);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_source_text_7ru912k(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_source_text_7ru912k F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* file */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    uint32_t t4 = gm_l1;
    void * t5 = gm_f_file_o15fqku(t3, t4);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t5) + 16));
    F.ret = t6;
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
inline __attribute__((aligned(4))) void gm_f_text_ls49dp6(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_text_ls49dp6 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_t8vuock3jnv59 t3;
    GM_ST(uint8_t, (uint8_t *)&t3 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t3 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t3 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)3ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 8), t2);
    memmove(gm_out, ((uint8_t *)&t3), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_uri_of_t6r79gp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_uri_of_t6r79gp F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* u */
    GM_STACK_CHECK(&F, 193u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l3;
    {
        {
            void * t4 = gm_l0;
            void * t5 = (void *)((uint8_t *)t4);
            gm_b16_8 t6 = gm_l1;
            gm_f_get_e3joqcc((void *)&gm_l3, t5, t6);
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            gm_l2 = t8;
            gm_b16_8 t9 = gm_l2;
            F.ret = t9;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t10 = gm_l1;
    gm_b16_8 t11 = gm_f_path_to_uri_as8bikc((void *)NULL, cx, NULL, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t11;
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
inline __attribute__((aligned(4))) void gm_f_value_pkuqal3(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_value_pkuqal3 F;
    void * gm_l0; /* o */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)t2));
    gm_t8vuock3jnv59 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t4 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)5ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 8), t3);
    memmove(gm_out, ((uint8_t *)&t4), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_h3ti3fb(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_h3ti3fb F;
    void * gm_l0; /* l */
    gm_t9qkh0rsooc8d gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t9qkh0rsooc8d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sh9j083o80vrk));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(7));
    gm_tftb1odc739t5 t5 = gm_f_write_uqn5uk3(t2, t3, ((void *)&gm_tpldm28gt81dafq), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_analyze_idnr1pc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_analyze_idnr1pc F;
    void * gm_l0; /* a */
    gm_b16_8 gm_l1; /* dir */
    gm_b16_8 gm_l2; /* overlays */
    gm_b16_8 gm_l3; /* std */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t6j6smj7qoctu gm_l7; /* o */
    gm_tf5srh4mp4b3i gm_l10; /* root_mod */
    uint64_t gm_l11; /* m */
    GM_STACK_CHECK(&F, 4420u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l12;
    gm_t074vrl1hb4f6 gm_l13;
    gm_b16_8 t11 = gm_f_std_root_e0juohb((void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t11;
    void * t12 = gm_l0;
    void * t13 = (void *)(((uint8_t *)t12) + 8);
    void * t14 = gm_l0;
    void * t15 = (void *)(((uint8_t *)t14) + 48);
    void * t16 = gm_l0;
    void * t17 = (void *)(((uint8_t *)t16) + 112);
    gm_b16_8 t18 = gm_l3;
    gm_tcuh1vih4g9cr t19;
    gm_f_loader_on0up0a((void *)&t19, (void *)NULL, cx, NULL, t13, t15, t17, t18, ((uint8_t)1));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t19;
    GM_ST(uint8_t, (((uint8_t *)&F.l4) + 25), ((uint8_t)1));
    gm_b16_8 t20 = gm_l2;
    gm_l5 = t20;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t21 = gm_l6;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t23 = (t21 < t22);
            if (!t23) {
                goto gm_lb1;
            }
            {
                void * t24 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t26 = gm_l6;
                {
                    {
                        memmove(((uint8_t *)&gm_l7), (((uint8_t *)t24) + (uintptr_t)t26 * 32), 32);
                        void * t27 = (void *)(((uint8_t *)&F.l4) + 32);
                        gm_tftb1odc739t5 t28 = gm_f_push_302k12h(t27, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l7)));
                        gm_l12 = t28;
                        uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_src2u46tincrqmv, 115, 25);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 115, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t30 = gm_l6;
            uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
            uint64_t t33 = r31;
            gm_l6 = t33;
        }
    }
gm_lb1:;
    {
        {
            gm_b16_8 t34 = gm_l1;
            gm_tqs5p4n2tq19v t35;
            GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)1));
            GM_ST(uint8_t, ((uint8_t *)&t35 + 1), ((uint8_t)1));
            gm_f_prepare_4l27ul2((void *)&gm_l13, (void *)NULL, cx, NULL, t34, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (!t36) {
                goto gm_lb7;
            }
            memmove(((uint8_t *)&F.l8), (((uint8_t *)&gm_l13) + 8), 48);
            memmove((((uint8_t *)&F.l4) + 376), ((uint8_t *)&F.l8), 48);
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
    void * t37 = (void *)((uint8_t *)&F.l4);
    gm_b16_8 t38 = gm_l3;
    gm_b16_8 t39 = gm_f_core_root_ip442nf((void *)NULL, cx, NULL, t38);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t40;
    GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sb3c9ehk423nf));
    GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(5));
    uint8_t t41 = gm_f_load_core_du4go5i((void *)NULL, cx, NULL, t37, t39, t40);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t42 = (uint8_t)!t41;
    if (t42) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t43 = (void *)((uint8_t *)&F.l4);
    gm_tfbjaprcetfe7 t44;
    gm_f_sema_r1nlnnm((void *)&t44, (void *)NULL, cx, NULL, t43);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l9 = t44;
    void * t45 = (void *)((uint8_t *)&F.l9);
    gm_f_install_db2gf1l(t45);
    void * t46 = (void *)((uint8_t *)&F.l9);
    gm_b0_1 t47 = gm_f_resolve_ambient_6orl0vo((void *)NULL, cx, NULL, t46);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t48 = (void *)((uint8_t *)&F.l4);
    gm_b16_8 t49 = gm_l1;
    gm_tf5srh4mp4b3i t50 = gm_f_load_root_af4ud7b((void *)NULL, cx, NULL, t48, t49);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t50;
    {
        {
            uint8_t t51 = (uint8_t)((((uint8_t *)&gm_l10))[8] != 0);
            if (!t51) {
                goto gm_lb9;
            }
            uint64_t t52 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
            gm_l11 = t52;
            void * t53 = (void *)((uint8_t *)&F.l9);
            gm_b0_1 t54 = gm_f_resolve_declarations_vrpg1fl((void *)NULL, cx, NULL, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t55 = (void *)((uint8_t *)&F.l9);
            gm_b0_1 t56 = gm_f_resolve_core_nv5iii8((void *)NULL, cx, NULL, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t57 = (void *)((uint8_t *)&F.l9);
            uint64_t t58 = gm_l11;
            gm_b0_1 t59 = gm_f_check_program_rltsqqp((void *)NULL, cx, NULL, t57, t58, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t60 = (void *)((uint8_t *)&F.l9);
            gm_b0_1 t61 = gm_f_check_constants_20kd087((void *)NULL, cx, NULL, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb8;
        }
gm_lb9:;
    }
gm_lb8:;
    void * t62 = gm_l0;
    void * t63 = (void *)((uint8_t *)&F.l9);
    gm_b16_8 t64 = gm_l1;
    gm_tf5srh4mp4b3i t65 = gm_l10;
    gm_t6vagfvc3bffs t66;
    gm_f_build_aeq3nd5((void *)&t66, (void *)NULL, cx, NULL, t63, t64, t65);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t6vagfvc3bffs, (((uint8_t *)t62) + 120), t66);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_constants_20kd087(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_constants_20kd087 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l5;
    gm_l2 = ((uint64_t)0ULL);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 2088) + 8));
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
                        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 2088));
                        uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 2088) + 8));
                        uint64_t t16 = gm_l3;
                        if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_src2u46tincrqmv, 138, 14);
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t14) + (uintptr_t)t16 * 88), 88);
                        gm_t9qkh0rsooc8d t17 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l4) + 72));
                        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
                        uint8_t t19 = (uint8_t)(!(t18 == ((uint8_t)0ULL)));
                        gm_t9qkh0rsooc8d t20;
                        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0ULL));
                        if (t19) {
                            gm_l5 = ((uint8_t)1);
                        } else {
                            void * t21 = gm_l0;
                            void * t22 = GM_LD(void *, ((uint8_t *)t21));
                            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 200));
                            uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)t22) + 200) + 8));
                            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 32));
                            if ((uintptr_t)t25 >= (uintptr_t)(t24)) gm_trap_index((uintptr_t)t25, t24, gm_src2u46tincrqmv, 139, 41);
                            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t23) + (uintptr_t)t25 * 184) + 112));
                            gm_l5 = t26;
                        }
                        uint8_t t27 = gm_l5;
                        if (t27) {
                            goto gm_lb3;
                        }
                        void * t28 = gm_l0;
                        gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 16));
                        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 28));
                        void * t31 = gm_f_const_value_fr4coi4((void *)NULL, cx, NULL, t28, t29, t30);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 137, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t32 = gm_l2;
            uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
            uint64_t t35 = r33;
            gm_l2 = t35;
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
inline __attribute__((aligned(4))) void * gm_f_check_module_6elsf9l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_check_module_6elsf9l F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* overlays */
    gm_b16_8 gm_l2; /* primary */
    void * gm_l3; /* ar */
    void * gm_l4; /* a */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    gm_b16_8 gm_l8; /* p */
    GM_STACK_CHECK(&F, 532u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tq7s98ihonkju gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tjt4ka19str13 t8;
    gm_f_new_jr5ejs0((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t9 = gm_f_new_cqfitgo((void *)NULL, cx, NULL, &t8, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kqfbmlr3820)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t9;
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src2u46tincrqmv, 89, 11);
    void * t11 = GM_LD(void *, (((uint8_t *)&gm_l9) + 8));
    gm_l3 = t11;
    void * t12 = gm_l3;
    gm_b16_8 t13 = gm_l2;
    void * t14 = gm_f_make_analysis_fj48u3v((void *)NULL, cx, NULL, t12, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t14;
    {
        void * t15 = gm_l3;
        gm_t3mf58gv9kf3n t16 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t15));
        gm_t3mf58gv9kf3n t17 = t16;
        gm_node wn18 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t17, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        void * t21 = gm_l4;
        gm_b16_8 t22 = gm_l0;
        gm_b16_8 t23 = gm_l1;
        gm_b0_1 t24 = gm_f_analyze_idnr1pc((void *)NULL, (&wn18), NULL, t21, t22, t23);
        if (gm_unw.kind) {
            goto gm_ll20;
        }
        gm_l6 = ((uint64_t)0ULL);
        void * t25 = gm_l4;
        uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t25) + 8) + 8));
        gm_l5 = t26;
        {
            for (;;) {
                uint64_t t27 = gm_l6;
                uint64_t t28 = gm_l5;
                uint8_t t29 = (t27 < t28);
                if (!t29) {
                    goto gm_lb2;
                }
                {
                    {
                        {
                            uint64_t t30 = gm_l6;
                            gm_l7 = t30;
                            void * t31 = gm_l4;
                            void * t32 = GM_LD(void *, (((uint8_t *)t31) + 8));
                            uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)t31) + 8) + 8));
                            uint64_t t34 = gm_l7;
                            if ((uintptr_t)t34 >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)t34, t33, gm_src2u46tincrqmv, 94, 18);
                            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)t32) + (uintptr_t)t34 * 80));
                            gm_l8 = t35;
                            gm_b16_8 t36 = gm_l8;
                            gm_b16_8 t37;
                            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s1f440st4mnkp));
                            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(1));
                            uint8_t t38 = gm_f_starts_with_6vvt01e(t36, t37);
                            if (t38) {
                                goto gm_lb4;
                            }
                            void * t39 = gm_l4;
                            void * t40 = (void *)(((uint8_t *)t39) + 480);
                            gm_b16_8 t41 = gm_l8;
                            gm_b16_8 t42 = gm_f_canonical_path_q6lq4b6((void *)NULL, (&wn18), NULL, t41);
                            if (gm_unw.kind) {
                                goto gm_ll20;
                            }
                            uint64_t t43 = gm_l7;
                            uint32_t t44 = (uint32_t)t43;
                            gm_tftb1odc739t5 t45 = gm_f_put_074448v(t40, t42, t44);
                            gm_l10 = t45;
                            uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                            if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_src2u46tincrqmv, 96, 13);
                            goto gm_lb5;
                        }
gm_lb6:;
                        gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 93, 9);
                    }
gm_lb5:;
                }
gm_lb4:;
                uint64_t t47 = gm_l6;
                uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                uint64_t t50 = r48;
                gm_l6 = t50;
            }
        }
gm_lb2:;
    }
    goto gm_ls51;
gm_ll20:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we19; }
    goto gm_lad;
gm_ls51:;
gm_lb1:;
we19:;
    void * t52 = gm_l4;
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
__attribute__((aligned(4))) void gm_f_core_path_for_g46v4ll(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_core_path_for_g46v4ll F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* body */
    gm_b16_8 gm_l2; /* file */
    gm_b16_8 gm_l3; /* in_tree */
    gm_b16_8 gm_l4; /* existing */
    uint64_t gm_l5; /* h */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* b */
    gm_b16_8 gm_l9; /* cache */
    gm_b16_8 gm_l10; /* dir */
    gm_b16_8 gm_l11; /* path */
    gm_b16_8 gm_l12; /* existing */
    GM_STACK_CHECK(&F, 1438u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    gm_tf5srh4mp4b3i gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tgvh15qshf643 gm_l18;
    gm_th2lqbo77pvsc gm_l19;
    gm_b16_8 gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tgvh15qshf643 gm_l22;
    gm_ttln0d0hd70en gm_l23;
    gm_ttln0d0hd70en gm_l24;
    void * t14 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)gm_cb1h8mol9po) + 8));
    uint64_t t17 = (uintptr_t)t16;
    uint64_t t18 = (uintptr_t)t15;
    if (t17 > t18 || t18 > (uintptr_t)t15) gm_trap_slice(t17, t18, t15, gm_src2u46tincrqmv, 379, 13);
    uint64_t t19 = ((uintptr_t)t15) - (uintptr_t)t16;
    gm_b16_8 t20;
    GM_ST(void *, (uint8_t *)&t20, (void *)(((uint8_t *)t14 + (uintptr_t)t16 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(t19));
    gm_l2 = t20;
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
    if (t22) {
        gm_l14 = ((uint8_t)1);
    } else {
        gm_b16_8 t23 = gm_l2;
        gm_tf5srh4mp4b3i t24 = gm_f_index_of_hdeg351(t23, ((uint8_t)47ULL));
        gm_l16 = t24;
        {
            {
                uint8_t t25 = (uint8_t)((((uint8_t *)&gm_l16))[8] != 0);
                if (t25) {
                    goto gm_lb2;
                }
                gm_l15 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l15 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 380, 25);
        }
gm_lb1:;
        uint8_t t26 = gm_l15;
        gm_l14 = t26;
    }
    uint8_t t27 = gm_l14;
    if (t27) {
        gm_l13 = ((uint8_t)1);
    } else {
        gm_b16_8 t28 = gm_l2;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_snenkhfev166s));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(2));
        uint8_t t30 = gm_f_contains_gkki4kq(t28, t29);
        gm_l13 = t30;
    }
    uint8_t t31 = gm_l13;
    if (t31) {
        gm_th2lqbo77pvsc t32;
        memset(&t32, 0, sizeof t32);
        memmove(gm_out, ((uint8_t *)&t32), 24);
        goto gm_fdone;
    }
    gm_b16_8 t33 = gm_f_std_root_e0juohb((void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t34;
    GM_ST(void *, (uint8_t *)&t34, (void *)(gm_snenkhfev166s));
    GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(2));
    gm_b16_8 t35 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t33, t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t36 = gm_l2;
    gm_tnpifv20irnph t37;
    GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
    gm_f_format_agi9f7f((void *)&gm_l17, (void *)NULL, cx, NULL, t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_src2u46tincrqmv, 381, 62);
    gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
    gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
    gm_b16_8 t41 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t35, t40);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t41;
    {
        {
            gm_b16_8 t42 = gm_l3;
            gm_f_read_all_r9rh9qd((void *)&gm_l18, (void *)NULL, cx, NULL, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t43) {
                goto gm_lb5;
            }
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
            gm_l4 = t44;
            gm_b16_8 t45 = gm_l4;
            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
            gm_b16_8 t47 = gm_l1;
            uint8_t t48 = (gm_eq0iqiubfm7e(((uint8_t *)&t46), ((uint8_t *)&t47)));
            if (t48) {
                gm_b16_8 t49 = gm_l3;
                gm_b16_8 t50 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t51;
                GM_ST(uint8_t, (uint8_t *)&t51 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t51 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t51 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t51), t50);
                ((uint8_t *)&t51)[16] = 1;
                memmove(gm_out, ((uint8_t *)&t51), 24);
                goto gm_fdone;
            }
            goto gm_lb4;
        }
gm_lb5:;
    }
gm_lb4:;
    gm_l5 = ((uint64_t)14695981039346656037ULL);
    gm_b16_8 t52 = gm_l1;
    gm_l6 = t52;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t53 = gm_l7;
            uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t55 = (t53 < t54);
            if (!t55) {
                goto gm_lb6;
            }
            {
                void * t56 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t58 = gm_l7;
                {
                    {
                        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)t56) + (uintptr_t)t58 * 1));
                        gm_l8 = t59;
                        uint64_t t60 = gm_l5;
                        uint8_t t61 = gm_l8;
                        uint64_t t62 = (uint64_t)t61;
                        uint64_t t63 = (uint64_t)(t60 ^ t62);
                        uint64_t r64; bool o65 = __builtin_mul_overflow(t63, ((uint64_t)1099511628211ULL), &r64);
                        uint64_t t66 = r64;
                        gm_l5 = t66;
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 386, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t67 = gm_l7;
            uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
            uint64_t t70 = r68;
            gm_l7 = t70;
        }
    }
gm_lb6:;
    gm_f_cache_dir_81h90qu((void *)&gm_l19, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t71 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
    if (t71) {
        gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
        gm_l20 = t72;
    } else {
        gm_b16_8 t73 = gm_f_temp_dir_i6apmsd((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t74;
        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_snjdrq8n0vphi));
        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(11));
        gm_b16_8 t75 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t73, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l20 = t75;
    }
    gm_b16_8 t76 = gm_l20;
    gm_l9 = t76;
    gm_b16_8 t77 = gm_l9;
    gm_b16_8 t78;
    GM_ST(void *, (uint8_t *)&t78, (void *)(gm_st3p5nb1e6cc8));
    GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(4));
    gm_b16_8 t79 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t77, t78);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t80 = gm_l5;
    gm_t4ce0jd3tdro4 t81;
    GM_ST(uint64_t, ((uint8_t *)&t81 + 0), t80);
    gm_f_format_tat75d3((void *)&gm_l21, (void *)NULL, cx, NULL, t81);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
    if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_src2u46tincrqmv, 388, 48);
    gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
    gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
    gm_b16_8 t85 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t79, t84);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t85;
    gm_b16_8 t86 = gm_l10;
    gm_b16_8 t87 = gm_l2;
    gm_b16_8 t88 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t86, t87);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t88;
    {
        {
            gm_b16_8 t89 = gm_l11;
            gm_f_read_all_r9rh9qd((void *)&gm_l22, (void *)NULL, cx, NULL, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
            if (!t90) {
                goto gm_lb12;
            }
            gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
            gm_l12 = t91;
            gm_b16_8 t92 = gm_l12;
            gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
            gm_b16_8 t94 = gm_l1;
            uint8_t t95 = (gm_eq0iqiubfm7e(((uint8_t *)&t93), ((uint8_t *)&t94)));
            if (t95) {
                gm_b16_8 t96 = gm_l11;
                gm_th2lqbo77pvsc t97;
                GM_ST(uint8_t, (uint8_t *)&t97 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t97 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t97 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t97), t96);
                ((uint8_t *)&t97)[16] = 1;
                memmove(gm_out, ((uint8_t *)&t97), 24);
                goto gm_fdone;
            }
            goto gm_lb11;
        }
gm_lb12:;
    }
gm_lb11:;
    gm_b16_8 t98 = gm_l10;
    gm_ttln0d0hd70en t99 = gm_f_make_dir_all_q6phjan((void *)NULL, cx, NULL, t98);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l23 = t99;
    uint8_t t100 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
    if (t100) {
    } else {
        gm_th2lqbo77pvsc t101;
        memset(&t101, 0, sizeof t101);
        memmove(gm_out, ((uint8_t *)&t101), 24);
        goto gm_fdone;
    }
    gm_b16_8 t102 = gm_l11;
    gm_b16_8 t103 = gm_l1;
    gm_ttln0d0hd70en t104 = gm_f_write_atomic_18k5vfb((void *)NULL, cx, NULL, t102, t103);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l24 = t104;
    uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
    if (t105) {
    } else {
        gm_th2lqbo77pvsc t106;
        memset(&t106, 0, sizeof t106);
        memmove(gm_out, ((uint8_t *)&t106), 24);
        goto gm_fdone;
    }
    gm_b16_8 t107 = gm_l11;
    gm_b0_1 t108 = gm_f_make_read_only_a29eok8((void *)NULL, cx, NULL, t107);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t109 = gm_l11;
    gm_th2lqbo77pvsc t110;
    GM_ST(uint8_t, (uint8_t *)&t110 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t110 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t110 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t110), t109);
    ((uint8_t *)&t110)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t110), 24);
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
__attribute__((aligned(4))) void gm_f_diagnostic_a0b47q4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_t9qkh0rsooc8d a3, void * a4) {
    struct gm_fr_diagnostic_a0b47q4 F;
    void * gm_l0; /* a */
    void * gm_l1; /* d */
    gm_t8vuock3jnv59 gm_l2; /* range_value */
    gm_t9qkh0rsooc8d gm_l3; /* enc */
    void * gm_l4; /* uris */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* n */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_tbgd14esf0jvi gm_l12; /* l */
    gm_b16_8 gm_l13; /* path */
    int64_t gm_l16; /* severity */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_t8vuock3jnv59 gm_l22; /* j */
    GM_STACK_CHECK(&F, 1850u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_th2lqbo77pvsc gm_l27;
    gm_b16_8 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    int64_t gm_l30;
    gm_t3ph3kbiqt1ah gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_trqfvmjtt4sij t23;
    gm_f_list_uvfu1k1((void *)&t23, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t23;
    void * t24 = (void *)((uint8_t *)&F.l5);
    void * t25 = gm_l1;
    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t25) + 8));
    gm_tftb1odc739t5 t27 = gm_f_extend_0cs85gs(t24, t26);
    gm_l24 = t27;
    uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
    if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_src2u46tincrqmv, 285, 5);
    void * t29 = gm_l1;
    gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)t29) + 80));
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
                        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)t34) + (uintptr_t)t36 * 16));
                        gm_l8 = t37;
                        void * t38 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t39;
                        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s6vl5jv0tf2n4));
                        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(7));
                        gm_tftb1odc739t5 t40 = gm_f_extend_0cs85gs(t38, t39);
                        gm_l25 = t40;
                        uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                        if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_src2u46tincrqmv, 287, 9);
                        void * t42 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t43 = gm_l8;
                        gm_tftb1odc739t5 t44 = gm_f_extend_0cs85gs(t42, t43);
                        gm_l26 = t44;
                        uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                        if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_src2u46tincrqmv, 288, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 286, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t46 = gm_l7;
            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
            uint64_t t49 = r47;
            gm_l7 = t49;
        }
    }
gm_lb1:;
    gm_trqfvmjtt4sij t50;
    gm_f_list_0202sdh_4((void *)&t50, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l9 = t50;
    void * t51 = gm_l1;
    gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)t51) + 40));
    gm_l10 = t52;
    gm_l11 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t53 = gm_l11;
            uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            uint8_t t55 = (t53 < t54);
            if (!t55) {
                goto gm_lb6;
            }
            {
                void * t56 = GM_LD(void *, ((uint8_t *)&gm_l10));
                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                uint64_t t58 = gm_l11;
                {
                    {
                        memmove(((uint8_t *)&gm_l12), (((uint8_t *)t56) + (uintptr_t)t58 * 32), 32);
                        gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l12));
                        gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                        uint8_t t61 = (gm_eqff85ac10o3(((uint8_t *)&t59), ((uint8_t *)&t60)));
                        if (t61) {
                            goto gm_lb8;
                        }
                        void * t62 = gm_l0;
                        uint32_t t63 = GM_LD(uint32_t, ((uint8_t *)&gm_l12));
                        gm_f_real_path_al8cv9r((void *)&gm_l27, (void *)NULL, cx, NULL, t62, t63);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t64 = (uint8_t)((((uint8_t *)&gm_l27))[16] != 0);
                        if (t64) {
                            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l27));
                            gm_l28 = t65;
                        } else {
                            goto gm_lb8;
                        }
                        gm_b16_8 t66 = gm_l28;
                        gm_l13 = t66;
                        gm_tin97tt4ud0s1 t67;
                        gm_f_obj_aoa7fl7((void *)&t67, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l14 = t67;
                        void * t68 = (void *)((uint8_t *)&F.l14);
                        gm_b16_8 t69;
                        GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sbo52miqad506));
                        GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(3));
                        void * t70 = gm_l4;
                        gm_b16_8 t71 = gm_l13;
                        gm_b16_8 t72 = gm_f_uri_of_t6r79gp((void *)NULL, cx, NULL, t70, t71);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t8vuock3jnv59 t73;
                        gm_f_text_ls49dp6((void *)&t73, t72);
                        gm_b0_1 t74 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t68, t69, &t73);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t75 = (void *)((uint8_t *)&F.l14);
                        gm_b16_8 t76;
                        GM_ST(void *, (uint8_t *)&t76, (void *)(gm_sl4ns6hobicht));
                        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(5));
                        void * t77 = gm_l0;
                        gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l12));
                        gm_t9qkh0rsooc8d t79 = gm_l3;
                        gm_t8vuock3jnv59 t80;
                        gm_f_range_demc0bt((void *)&t80, (void *)NULL, cx, NULL, t77, t78, t79);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b0_1 t81 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t75, t76, &t80);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tin97tt4ud0s1 t82;
                        gm_f_obj_aoa7fl7((void *)&t82, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l15 = t82;
                        void * t83 = (void *)((uint8_t *)&F.l15);
                        gm_b16_8 t84;
                        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_s0tfhmgv1pal7));
                        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(8));
                        void * t85 = (void *)((uint8_t *)&F.l14);
                        gm_t8vuock3jnv59 t86;
                        gm_f_value_pkuqal3((void *)&t86, t85);
                        gm_b0_1 t87 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t83, t84, &t86);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t88 = (void *)((uint8_t *)&F.l15);
                        gm_b16_8 t89;
                        GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sujli4dml3ipq));
                        GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(7));
                        gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                        gm_t8vuock3jnv59 t91;
                        gm_f_text_ls49dp6((void *)&t91, t90);
                        gm_b0_1 t92 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t88, t89, &t91);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t93 = (void *)((uint8_t *)&F.l9);
                        void * t94 = (void *)((uint8_t *)&F.l15);
                        gm_t8vuock3jnv59 t95;
                        gm_f_value_pkuqal3((void *)&t95, t94);
                        gm_tftb1odc739t5 t96 = gm_f_push_0pgacsj_4(t93, &t95);
                        gm_l29 = t96;
                        uint8_t t97 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                        if (!t97) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_src2u46tincrqmv, 300, 9);
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 291, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t98 = gm_l11;
            uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
            uint64_t t101 = r99;
            gm_l11 = t101;
        }
    }
gm_lb6:;
    void * t102 = gm_l1;
    {
        {
            uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)t102) + 0));
            uint8_t t104 = (uint8_t)(t103 == ((uint8_t)2ULL));
            if (!t104) {
                goto gm_lb12;
            }
            gm_l30 = ((int64_t)1LL);
            goto gm_lb11;
        }
gm_lb12:;
        {
            uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)t102) + 0));
            uint8_t t106 = (uint8_t)(t105 == ((uint8_t)1ULL));
            if (!t106) {
                goto gm_lb13;
            }
            gm_l30 = ((int64_t)2LL);
            goto gm_lb11;
        }
gm_lb13:;
        {
            uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)t102) + 0));
            uint8_t t108 = (uint8_t)(t107 == ((uint8_t)0ULL));
            if (!t108) {
                goto gm_lb14;
            }
            gm_l30 = ((int64_t)3LL);
            goto gm_lb11;
        }
gm_lb14:;
        gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 302, 21);
    }
gm_lb11:;
    int64_t t109 = gm_l30;
    gm_l16 = t109;
    gm_tin97tt4ud0s1 t110;
    gm_f_obj_aoa7fl7((void *)&t110, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l17 = t110;
    void * t111 = (void *)((uint8_t *)&F.l17);
    gm_b16_8 t112;
    GM_ST(void *, (uint8_t *)&t112, (void *)(gm_sl4ns6hobicht));
    GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(5));
    gm_b0_1 t113 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t111, t112, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t114 = (void *)((uint8_t *)&F.l17);
    gm_b16_8 t115;
    GM_ST(void *, (uint8_t *)&t115, (void *)(gm_sp6bf9nji9g1j));
    GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(8));
    int64_t t116 = gm_l16;
    gm_t8vuock3jnv59 t117;
    gm_f_int_95caidg((void *)&t117, (void *)NULL, cx, NULL, t116);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t118 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t114, t115, &t117);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t119 = (void *)((uint8_t *)&F.l17);
    gm_b16_8 t120;
    GM_ST(void *, (uint8_t *)&t120, (void *)(gm_smr3sbhcj1goc));
    GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(6));
    gm_b16_8 t121;
    GM_ST(void *, (uint8_t *)&t121, (void *)(gm_s19jg4ct3aq11));
    GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(5));
    gm_t8vuock3jnv59 t122;
    gm_f_text_ls49dp6((void *)&t122, t121);
    gm_b0_1 t123 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t119, t120, &t122);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t124 = (void *)((uint8_t *)&F.l17);
    gm_b16_8 t125;
    GM_ST(void *, (uint8_t *)&t125, (void *)(gm_sujli4dml3ipq));
    GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(7));
    gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
    gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
    gm_t8vuock3jnv59 t128;
    gm_f_text_ls49dp6((void *)&t128, t127);
    gm_b0_1 t129 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t124, t125, &t128);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
    uint8_t t131 = (t130 > ((uint64_t)0ULL));
    if (t131) {
        void * t132 = (void *)((uint8_t *)&F.l17);
        gm_b16_8 t133;
        GM_ST(void *, (uint8_t *)&t133, (void *)(gm_sbgbo6kv938k3));
        GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(18));
        gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
        gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
        gm_t8vuock3jnv59 t136;
        gm_f_array_k32ana3((void *)&t136, (void *)NULL, cx, NULL, t135);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t137 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t132, t133, &t136);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_trqfvmjtt4sij t138;
    gm_f_list_0202sdh_4((void *)&t138, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l18 = t138;
    void * t139 = gm_l1;
    gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)t139) + 120));
    gm_l19 = t140;
    gm_l20 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t141 = gm_l20;
            uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
            uint8_t t143 = (t141 < t142);
            if (!t143) {
                goto gm_lb15;
            }
            {
                void * t144 = GM_LD(void *, ((uint8_t *)&gm_l19));
                uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                uint64_t t146 = gm_l20;
                {
                    {
                        memmove(((uint8_t *)&F.l21), (((uint8_t *)t144) + (uintptr_t)t146 * 56), 56);
                        {
                            {
                                void * t147 = gm_l0;
                                void * t148 = (void *)((uint8_t *)&F.l21);
                                gm_t9qkh0rsooc8d t149 = gm_l3;
                                void * t150 = gm_l4;
                                gm_f_fix_json_pbc9vbt((void *)&gm_l31, (void *)NULL, cx, NULL, t147, t148, t149, t150);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t151 = (uint8_t)((((uint8_t *)&gm_l31))[24] != 0);
                                if (!t151) {
                                    goto gm_lb21;
                                }
                                memmove(((uint8_t *)&gm_l22), ((uint8_t *)&gm_l31), 24);
                                void * t152 = (void *)((uint8_t *)&F.l18);
                                gm_tftb1odc739t5 t153 = gm_f_push_0pgacsj_4(t152, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l22)));
                                gm_l32 = t153;
                                uint8_t t154 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                if (!t154) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_src2u46tincrqmv, 317, 46);
                                goto gm_lb20;
                            }
gm_lb21:;
                        }
gm_lb20:;
                        goto gm_lb18;
                    }
gm_lb19:;
                    gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 316, 5);
                }
gm_lb18:;
            }
gm_lb17:;
            uint64_t t155 = gm_l20;
            uint64_t r156; bool o157 = __builtin_add_overflow(t155, ((uint64_t)1ULL), &r156);
            uint64_t t158 = r156;
            gm_l20 = t158;
        }
    }
gm_lb15:;
    uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)&F.l18) + 8));
    uint8_t t160 = (t159 > ((uint64_t)0ULL));
    if (t160) {
        gm_tin97tt4ud0s1 t161;
        gm_f_obj_aoa7fl7((void *)&t161, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l23 = t161;
        void * t162 = (void *)((uint8_t *)&F.l23);
        gm_b16_8 t163;
        GM_ST(void *, (uint8_t *)&t163, (void *)(gm_si702r0b37nml));
        GM_ST(uintptr_t, (uint8_t *)&t163 + 8, (uintptr_t)(5));
        gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
        gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
        gm_t8vuock3jnv59 t166;
        gm_f_array_k32ana3((void *)&t166, (void *)NULL, cx, NULL, t165);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t167 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t162, t163, &t166);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t168 = (void *)((uint8_t *)&F.l17);
        gm_b16_8 t169;
        GM_ST(void *, (uint8_t *)&t169, (void *)(gm_se23d1hoq85r1));
        GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(4));
        void * t170 = (void *)((uint8_t *)&F.l23);
        gm_t8vuock3jnv59 t171;
        gm_f_value_pkuqal3((void *)&t171, t170);
        gm_b0_1 t172 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t168, t169, &t171);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t173 = (void *)((uint8_t *)&F.l17);
    gm_f_value_pkuqal3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), t173);
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
__attribute__((aligned(4))) void gm_f_diagnostics_7ntqr3v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2) {
    struct gm_fr_diagnostics_7ntqr3v F;
    void * gm_l0; /* a */
    gm_t9qkh0rsooc8d gm_l1; /* enc */
    void * gm_l2; /* uris */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    void * gm_l8; /* d */
    gm_b16_8 gm_l9; /* path */
    gm_t8vuock3jnv59 gm_l10; /* range_value */
    gm_b16_8 gm_l11; /* p */
    gm_t8vuock3jnv59 gm_l12; /* value */
    uint64_t gm_l13; /* k */
    GM_STACK_CHECK(&F, 891u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_th2lqbo77pvsc gm_l15;
    gm_b16_8 gm_l16;
    gm_tf5srh4mp4b3i gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_trqfvmjtt4sij t9;
    gm_f_list_5j3106o((void *)&t9, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t9;
    gm_t4c7ipjj9s3fp t10;
    gm_f_hash_map_lljdp3h((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t10;
    gm_l6 = ((uint64_t)0ULL);
    void * t11 = gm_l0;
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t11) + 48) + 8));
    gm_l5 = t12;
    {
        for (;;) {
            uint64_t t13 = gm_l6;
            uint64_t t14 = gm_l5;
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t16 = gm_l6;
                        gm_l7 = t16;
                        void * t17 = gm_l0;
                        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 48));
                        uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)t17) + 48) + 8));
                        uint64_t t20 = gm_l7;
                        if ((uintptr_t)t20 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t20, t19, gm_src2u46tincrqmv, 253, 15);
                        void * t21 = (void *)(((uint8_t *)t18) + (uintptr_t)t20 * 184);
                        gm_l8 = t21;
                        void * t22 = gm_l0;
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 536));
                        gm_l9 = t23;
                        memmove(((uint8_t *)&gm_l10), ((uint8_t *)gm_cro0h2panct), 24);
                        void * t24 = gm_l8;
                        gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t24) + 24));
                        gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                        uint8_t t27 = (gm_eqff85ac10o3(((uint8_t *)&t25), ((uint8_t *)&t26)));
                        if (t27) {
                            gm_f_range_json_zero_orc6s8p((void *)&gm_l10, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        } else {
                            void * t28 = gm_l0;
                            void * t29 = gm_l8;
                            uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)t29) + 24));
                            gm_f_real_path_al8cv9r((void *)&gm_l15, (void *)NULL, cx, NULL, t28, t30);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t31 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                            if (t31) {
                                gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                                gm_l16 = t32;
                            } else {
                                goto gm_lb3;
                            }
                            gm_b16_8 t33 = gm_l16;
                            gm_l11 = t33;
                            gm_b16_8 t34 = gm_l11;
                            gm_l9 = t34;
                            void * t35 = gm_l0;
                            void * t36 = gm_l8;
                            gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 24));
                            gm_t9qkh0rsooc8d t38 = gm_l1;
                            gm_f_range_demc0bt((void *)&gm_l10, (void *)NULL, cx, NULL, t35, t37, t38);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        void * t39 = gm_l0;
                        void * t40 = gm_l8;
                        gm_t9qkh0rsooc8d t41 = gm_l1;
                        void * t42 = gm_l2;
                        gm_t8vuock3jnv59 t43 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)&gm_l10));
                        gm_f_diagnostic_a0b47q4((void *)&gm_l12, (void *)NULL, cx, NULL, t39, t40, &t43, t41, t42);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        {
                            {
                                void * t44 = (void *)((uint8_t *)&F.l4);
                                gm_b16_8 t45 = gm_l9;
                                gm_tf5srh4mp4b3i t46 = gm_f_get_espvf96(t44, t45);
                                gm_l17 = t46;
                                uint8_t t47 = (uint8_t)((((uint8_t *)&gm_l17))[8] != 0);
                                if (!t47) {
                                    goto gm_lb7;
                                }
                                uint64_t t48 = GM_LD(uint64_t, ((uint8_t *)&gm_l17));
                                gm_l13 = t48;
                                void * t49 = GM_LD(void *, ((uint8_t *)&F.l3));
                                uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                uint64_t t51 = gm_l13;
                                if ((uintptr_t)t51 >= (uintptr_t)(t50)) gm_trap_index((uintptr_t)t51, t50, gm_src2u46tincrqmv, 265, 13);
                                void * t52 = (void *)((((uint8_t *)t49) + (uintptr_t)t51 * 56) + 16);
                                gm_tftb1odc739t5 t53 = gm_f_push_0pgacsj_4(t52, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l12)));
                                gm_l18 = t53;
                                uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_src2u46tincrqmv, 265, 13);
                                goto gm_lb6;
                            }
gm_lb7:;
                            void * t55 = (void *)((uint8_t *)&F.l4);
                            gm_b16_8 t56 = gm_l9;
                            uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                            gm_tftb1odc739t5 t58 = gm_f_put_qoojoqv(t55, t56, t57);
                            gm_l19 = t58;
                            uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                            if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_src2u46tincrqmv, 267, 13);
                            gm_trqfvmjtt4sij t60;
                            gm_f_list_0202sdh_4((void *)&t60, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l14 = t60;
                            void * t61 = (void *)((uint8_t *)&F.l14);
                            gm_tftb1odc739t5 t62 = gm_f_push_0pgacsj_4(t61, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l12)));
                            gm_l20 = t62;
                            uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                            if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_src2u46tincrqmv, 269, 13);
                            void * t64 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t65 = gm_l9;
                            gm_trqfvmjtt4sij t66 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l14));
                            gm_tl7hpf5nlilqq t67;
                            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t65);
                            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t67 + 16), t66);
                            gm_tftb1odc739t5 t68 = gm_f_push_l5thl65(t64, &t67);
                            gm_l21 = t68;
                            uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                            if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_src2u46tincrqmv, 270, 13);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 252, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t70 = gm_l6;
            uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
            uint64_t t73 = r71;
            gm_l6 = t73;
        }
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&F.l3), 40);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_dir_of_ho73hue(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_file_id_mif69v8(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_fix_json_pbc9vbt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3) {
    struct gm_fr_fix_json_pbc9vbt F;
    void * gm_l0; /* a */
    void * gm_l1; /* fx */
    gm_t9qkh0rsooc8d gm_l2; /* enc */
    void * gm_l3; /* uris */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tbgd14esf0jvi gm_l7; /* e */
    gm_b16_8 gm_l8; /* path */
    gm_t6j6smj7qoctu gm_l9; /* ix */
    gm_b16_8 gm_l11; /* uri */
    uint8_t gm_l12; /* found */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    GM_STACK_CHECK(&F, 1513u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_th2lqbo77pvsc gm_l22;
    gm_b16_8 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_trqfvmjtt4sij t17;
    gm_f_list_4qf7cqo((void *)&t17, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t17;
    void * t18 = gm_l1;
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)t18) + 16));
    gm_l5 = t19;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t20 = gm_l6;
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            {
                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t25 = gm_l6;
                {
                    {
                        memmove(((uint8_t *)&gm_l7), (((uint8_t *)t23) + (uintptr_t)t25 * 32), 32);
                        void * t26 = gm_l0;
                        uint32_t t27 = GM_LD(uint32_t, ((uint8_t *)&gm_l7));
                        gm_f_real_path_al8cv9r((void *)&gm_l22, (void *)NULL, cx, NULL, t26, t27);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t28 = (uint8_t)((((uint8_t *)&gm_l22))[16] != 0);
                        if (t28) {
                            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l22));
                            gm_l23 = t29;
                        } else {
                            gm_t3ph3kbiqt1ah t30;
                            memset(&t30, 0, sizeof t30);
                            memmove(gm_out, ((uint8_t *)&t30), 32);
                            goto gm_fdone;
                        }
                        gm_b16_8 t31 = gm_l23;
                        gm_l8 = t31;
                        void * t32 = gm_l0;
                        uint32_t t33 = GM_LD(uint32_t, ((uint8_t *)&gm_l7));
                        gm_t6j6smj7qoctu t34;
                        gm_f_lines_0mpphhg((void *)&t34, (void *)NULL, cx, NULL, t32, t33);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l9 = t34;
                        gm_tin97tt4ud0s1 t35;
                        gm_f_obj_aoa7fl7((void *)&t35, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l10 = t35;
                        void * t36 = (void *)((uint8_t *)&F.l10);
                        gm_b16_8 t37;
                        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sl4ns6hobicht));
                        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(5));
                        void * t38 = (void *)((uint8_t *)&gm_l9);
                        uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                        uint64_t t40 = (uint64_t)t39;
                        uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 8));
                        uint64_t t42 = (uint64_t)t41;
                        gm_t9qkh0rsooc8d t43 = gm_l2;
                        gm_t8vuock3jnv59 t44;
                        gm_f_range_json_hlsolbo((void *)&t44, (void *)NULL, cx, NULL, t38, t40, t42, t43);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b0_1 t45 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t36, t37, &t44);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t46 = (void *)((uint8_t *)&F.l10);
                        gm_b16_8 t47;
                        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sirpcjgkmlehr));
                        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(7));
                        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 16));
                        gm_t8vuock3jnv59 t49;
                        gm_f_text_ls49dp6((void *)&t49, t48);
                        gm_b0_1 t50 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t46, t47, &t49);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t51 = gm_l3;
                        gm_b16_8 t52 = gm_l8;
                        gm_b16_8 t53 = gm_f_uri_of_t6r79gp((void *)NULL, cx, NULL, t51, t52);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l11 = t53;
                        gm_l12 = ((uint8_t)0);
                        gm_l14 = ((uint64_t)0ULL);
                        uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                        gm_l13 = t54;
                        {
                            for (;;) {
                                uint64_t t55 = gm_l14;
                                uint64_t t56 = gm_l13;
                                uint8_t t57 = (t55 < t56);
                                if (!t57) {
                                    goto gm_lb6;
                                }
                                {
                                    {
                                        {
                                            uint64_t t58 = gm_l14;
                                            gm_l15 = t58;
                                            void * t59 = GM_LD(void *, ((uint8_t *)&F.l4));
                                            uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                                            uint64_t t61 = gm_l15;
                                            if ((uintptr_t)t61 >= (uintptr_t)(t60)) gm_trap_index((uintptr_t)t61, t60, gm_src2u46tincrqmv, 346, 16);
                                            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)t59) + (uintptr_t)t61 * 56));
                                            gm_b16_8 t63 = gm_l11;
                                            uint8_t t64 = (gm_eq0iqiubfm7e(((uint8_t *)&t62), ((uint8_t *)&t63)));
                                            if (t64) {
                                                void * t65 = GM_LD(void *, ((uint8_t *)&F.l4));
                                                uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                                                uint64_t t67 = gm_l15;
                                                if ((uintptr_t)t67 >= (uintptr_t)(t66)) gm_trap_index((uintptr_t)t67, t66, gm_src2u46tincrqmv, 347, 17);
                                                void * t68 = (void *)((((uint8_t *)t65) + (uintptr_t)t67 * 56) + 16);
                                                void * t69 = (void *)((uint8_t *)&F.l10);
                                                gm_t8vuock3jnv59 t70;
                                                gm_f_value_pkuqal3((void *)&t70, t69);
                                                gm_tftb1odc739t5 t71 = gm_f_push_0pgacsj_4(t68, &t70);
                                                gm_l24 = t71;
                                                uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                                if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_src2u46tincrqmv, 347, 17);
                                                gm_l12 = ((uint8_t)1);
                                                goto gm_lb6;
                                            }
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 345, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t73 = gm_l14;
                                uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                                uint64_t t76 = r74;
                                gm_l14 = t76;
                            }
                        }
gm_lb6:;
                        uint8_t t77 = gm_l12;
                        uint8_t t78 = (uint8_t)!t77;
                        if (t78) {
                            gm_trqfvmjtt4sij t79;
                            gm_f_list_0202sdh_4((void *)&t79, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l16 = t79;
                            void * t80 = (void *)((uint8_t *)&F.l16);
                            void * t81 = (void *)((uint8_t *)&F.l10);
                            gm_t8vuock3jnv59 t82;
                            gm_f_value_pkuqal3((void *)&t82, t81);
                            gm_tftb1odc739t5 t83 = gm_f_push_0pgacsj_4(t80, &t82);
                            gm_l25 = t83;
                            uint8_t t84 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                            if (!t84) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_src2u46tincrqmv, 354, 13);
                            void * t85 = (void *)((uint8_t *)&F.l4);
                            gm_b16_8 t86 = gm_l11;
                            gm_trqfvmjtt4sij t87 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l16));
                            gm_tl7hpf5nlilqq t88;
                            GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t86);
                            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t88 + 16), t87);
                            gm_tftb1odc739t5 t89 = gm_f_push_p48r1bd(t85, &t88);
                            gm_l26 = t89;
                            uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                            if (!t90) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_src2u46tincrqmv, 355, 13);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 336, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t91 = gm_l6;
            uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
            uint64_t t94 = r92;
            gm_l6 = t94;
        }
    }
gm_lb1:;
    gm_tin97tt4ud0s1 t95;
    gm_f_obj_aoa7fl7((void *)&t95, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l17 = t95;
    gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
    gm_l18 = t96;
    gm_l19 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t97 = gm_l19;
            uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
            uint8_t t99 = (t97 < t98);
            if (!t99) {
                goto gm_lb11;
            }
            {
                void * t100 = GM_LD(void *, ((uint8_t *)&gm_l18));
                uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                uint64_t t102 = gm_l19;
                {
                    {
                        memmove(((uint8_t *)&F.l20), (((uint8_t *)t100) + (uintptr_t)t102 * 56), 56);
                        void * t103 = (void *)((uint8_t *)&F.l17);
                        gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                        gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 16));
                        gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                        gm_t8vuock3jnv59 t107;
                        gm_f_array_k32ana3((void *)&t107, (void *)NULL, cx, NULL, t106);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b0_1 t108 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t103, t104, &t107);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb14;
                    }
gm_lb15:;
                    gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 359, 5);
                }
gm_lb14:;
            }
gm_lb13:;
            uint64_t t109 = gm_l19;
            uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
            uint64_t t112 = r110;
            gm_l19 = t112;
        }
    }
gm_lb11:;
    gm_tin97tt4ud0s1 t113;
    gm_f_obj_aoa7fl7((void *)&t113, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l21 = t113;
    void * t114 = (void *)((uint8_t *)&F.l21);
    gm_b16_8 t115;
    GM_ST(void *, (uint8_t *)&t115, (void *)(gm_scifaj4dn1ok5));
    GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(5));
    void * t116 = gm_l1;
    gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)t116));
    gm_t8vuock3jnv59 t118;
    gm_f_text_ls49dp6((void *)&t118, t117);
    gm_b0_1 t119 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t114, t115, &t118);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t120 = (void *)((uint8_t *)&F.l21);
    gm_b16_8 t121;
    GM_ST(void *, (uint8_t *)&t121, (void *)(gm_ssvcneuafnfds));
    GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(7));
    void * t122 = (void *)((uint8_t *)&F.l17);
    gm_t8vuock3jnv59 t123;
    gm_f_value_pkuqal3((void *)&t123, t122);
    gm_b0_1 t124 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t120, t121, &t123);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t125 = (void *)((uint8_t *)&F.l21);
    gm_t8vuock3jnv59 t126;
    gm_f_value_pkuqal3((void *)&t126, t125);
    gm_t3ph3kbiqt1ah t127;
    GM_ST(uint8_t, (uint8_t *)&t127 + 25, 0);
    GM_ST(uint16_t, (uint8_t *)&t127 + 26, 0);
    GM_ST(uint32_t, (uint8_t *)&t127 + 28, 0);
    GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t127), t126);
    ((uint8_t *)&t127)[24] = 1;
    memmove(gm_out, ((uint8_t *)&t127), 32);
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_hex_value_08hqdl8(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hex_value_08hqdl8 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 33u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            uint8_t t8 = gm_l0;
            uint8_t t9 = (t8 >= ((uint8_t)48ULL));
            if (!t9) {
                goto gm_lb2;
            }
            uint8_t t10 = gm_l0;
            uint8_t t11 = (t10 <= ((uint8_t)57ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint8_t t12 = gm_l0;
            uint8_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint8_t)48ULL), &r13);
            if (o14) gm_trap("integer overflow in `-`", gm_src2u46tincrqmv, 21, 39);
            uint8_t t15 = r13;
            gm_tqs5p4n2tq19v t16;
            GM_ST(uint8_t, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[1] = 1;
            F.ret = t16;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint8_t t17 = gm_l0;
            uint8_t t18 = (t17 >= ((uint8_t)97ULL));
            if (!t18) {
                goto gm_lb4;
            }
            uint8_t t19 = gm_l0;
            uint8_t t20 = (t19 <= ((uint8_t)102ULL));
            if (!t20) {
                goto gm_lb4;
            }
            uint8_t t21 = gm_l0;
            uint8_t r22; bool o23 = __builtin_sub_overflow(t21, ((uint8_t)97ULL), &r22);
            if (o23) gm_trap("integer overflow in `-`", gm_src2u46tincrqmv, 22, 39);
            uint8_t t24 = r22;
            uint8_t r25; bool o26 = __builtin_add_overflow(t24, ((uint8_t)10ULL), &r25);
            if (o26) gm_trap("integer overflow in `+`", gm_src2u46tincrqmv, 22, 39);
            uint8_t t27 = r25;
            gm_tqs5p4n2tq19v t28;
            GM_ST(uint8_t, ((uint8_t *)&t28), t27);
            ((uint8_t *)&t28)[1] = 1;
            F.ret = t28;
            goto gm_fret;
        }
gm_lb4:;
    }
gm_lb3:;
    {
        {
            uint8_t t29 = gm_l0;
            uint8_t t30 = (t29 >= ((uint8_t)65ULL));
            if (!t30) {
                goto gm_lb6;
            }
            uint8_t t31 = gm_l0;
            uint8_t t32 = (t31 <= ((uint8_t)70ULL));
            if (!t32) {
                goto gm_lb6;
            }
            uint8_t t33 = gm_l0;
            uint8_t r34; bool o35 = __builtin_sub_overflow(t33, ((uint8_t)65ULL), &r34);
            if (o35) gm_trap("integer overflow in `-`", gm_src2u46tincrqmv, 23, 39);
            uint8_t t36 = r34;
            uint8_t r37; bool o38 = __builtin_add_overflow(t36, ((uint8_t)10ULL), &r37);
            if (o38) gm_trap("integer overflow in `+`", gm_src2u46tincrqmv, 23, 39);
            uint8_t t39 = r37;
            gm_tqs5p4n2tq19v t40;
            GM_ST(uint8_t, ((uint8_t *)&t40), t39);
            ((uint8_t *)&t40)[1] = 1;
            F.ret = t40;
            goto gm_fret;
        }
gm_lb6:;
    }
gm_lb5:;
    gm_tqs5p4n2tq19v t41;
    memset(&t41, 0, sizeof t41);
    F.ret = t41;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_core_rhbi4fa(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_lines_0mpphhg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_lines_0mpphhg F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* file */
    gm_b16_8 gm_l2; /* t */
    gm_tnpifv20irnph gm_l3; /* known */
    gm_tnpifv20irnph gm_l4; /* starts */
    GM_STACK_CHECK(&F, 294u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_teseu4tn5o422 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    void * t4 = gm_l0;
    uint32_t t5 = gm_l1;
    gm_b16_8 t6 = gm_f_source_text_7ru912k(t4, t5);
    gm_l2 = t6;
    {
        {
            void * t7 = gm_l0;
            void * t8 = (void *)(((uint8_t *)t7) + 608);
            uint32_t t9 = gm_l1;
            gm_f_get_lk35mh7((void *)&gm_l5, t8, t9);
            uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
            if (!t10) {
                goto gm_lb2;
            }
            gm_tnpifv20irnph t11 = GM_LD(gm_tnpifv20irnph, ((uint8_t *)&gm_l5));
            gm_l3 = t11;
            gm_b16_8 t12 = gm_l2;
            gm_tnpifv20irnph t13 = gm_l3;
            gm_f_line_index_from_dbptimh((void *)&(*(gm_t6j6smj7qoctu *)gm_out), t12, t13);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t14 = gm_l0;
    gm_b16_8 t15 = gm_l2;
    gm_tnpifv20irnph t16 = gm_f_scan_lines_lhr1jn1((void *)NULL, cx, NULL, t14, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t16;
    void * t17 = gm_l0;
    void * t18 = (void *)(((uint8_t *)t17) + 608);
    uint32_t t19 = gm_l1;
    gm_tnpifv20irnph t20 = gm_l4;
    gm_tftb1odc739t5 t21 = gm_f_put_idr4g4a(t18, t19, t20);
    gm_l6 = t21;
    uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src2u46tincrqmv, 191, 5);
    gm_b16_8 t23 = gm_l2;
    gm_tnpifv20irnph t24 = gm_l4;
    gm_f_line_index_from_dbptimh((void *)&(*(gm_t6j6smj7qoctu *)gm_out), t23, t24);
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
inline __attribute__((aligned(4))) void * gm_f_make_analysis_fj48u3v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_make_analysis_fj48u3v F;
    void * gm_l0; /* ar */
    gm_b16_8 gm_l1; /* primary */
    GM_STACK_CHECK(&F, 1498u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    gm_tq7s98ihonkju gm_l3;
    {
        void * t3 = gm_l0;
        gm_t3mf58gv9kf3n t4 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t3));
        gm_t3mf58gv9kf3n t5 = t4;
        gm_node wn6 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t5, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        void * t9 = gm_l0;
        gm_tin97tt4ud0s1 t10;
        gm_f_source_map_4v8dg4b((void *)&t10, (void *)NULL, (&wn6), NULL);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        gm_t2792tsg5eg72 t11;
        gm_f_diagnostics_n3kkhf4((void *)&t11, (void *)NULL, (&wn6), NULL);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        gm_t6vagfvc3bffs t12;
        gm_f_index_nki2g2t((void *)&t12, (void *)NULL, (&wn6), NULL);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        gm_t4c7ipjj9s3fp t13;
        gm_f_hash_map_rs5nlfs((void *)&t13, (void *)NULL, (&wn6), NULL);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        gm_b16_8 t14 = gm_l1;
        gm_f_dupe_g9iqpvn((void *)&gm_l2, (void *)NULL, (&wn6), NULL, t14, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k4g6hon27vb)));
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
        if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_src2u46tincrqmv, 80, 22);
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        gm_t4c7ipjj9s3fp t18;
        gm_f_hash_map_1s9eugt((void *)&t18, (void *)NULL, (&wn6), NULL);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        gm_t4c7ipjj9s3fp t19;
        gm_f_hash_map_deo2ele((void *)&t19, (void *)NULL, (&wn6), NULL);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        gm_tohkrr5v3jd5r t20;
        GM_ST(uint32_t, (uint8_t *)&t20 + 116, 0);
        GM_ST(void *, ((uint8_t *)&t20 + 0), t9);
        GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&t20 + 8), t10);
        GM_ST(gm_t2792tsg5eg72, ((uint8_t *)&t20 + 48), t11);
        GM_ST(uint32_t, ((uint8_t *)&t20 + 112), ((uint32_t)0ULL));
        GM_ST(gm_t6vagfvc3bffs, ((uint8_t *)&t20 + 120), t12);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t20 + 480), t13);
        GM_ST(gm_b16_8, ((uint8_t *)&t20 + 536), t17);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t20 + 552), t18);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t20 + 608), t19);
        gm_tq7s98ihonkju t21 = gm_f_new_6dt94nu((void *)NULL, (&wn6), NULL, &t20, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k66sf6gvqqo)));
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        gm_l3 = t21;
        uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_src2u46tincrqmv, 73, 16);
        void * t23 = GM_LD(void *, (((uint8_t *)&gm_l3) + 8));
        F.ret = t23;
        goto gm_fret;
    }
    goto gm_ls24;
gm_ll8:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
    goto gm_lad;
gm_ls24:;
gm_lb1:;
we7:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_open_path_6gmq4nk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_open_path_6gmq4nk F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* file */
    gm_b16_8 gm_l2; /* p */
    void * gm_l3; /* f */
    gm_th2lqbo77pvsc gm_l4; /* known */
    gm_th2lqbo77pvsc gm_l5; /* found */
    GM_STACK_CHECK(&F, 434u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l6;
    gm_ti5ramhglnvdt gm_l7;
    gm_tftb1odc739t5 gm_l8;
    {
        {
            void * t6 = gm_l0;
            uint32_t t7 = gm_l1;
            gm_f_real_path_al8cv9r((void *)&gm_l6, (void *)NULL, cx, NULL, t6, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
            if (!t8) {
                goto gm_lb2;
            }
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
            gm_l2 = t9;
            gm_b16_8 t10 = gm_l2;
            gm_th2lqbo77pvsc t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t11), t10);
            ((uint8_t *)&t11)[16] = 1;
            memmove(gm_out, ((uint8_t *)&t11), 24);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    uint32_t t12 = gm_l1;
    uint64_t t13 = (uint64_t)t12;
    void * t14 = gm_l0;
    uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t14) + 8) + 8));
    uint8_t t16 = (t13 >= t15);
    if (t16) {
        gm_th2lqbo77pvsc t17;
        memset(&t17, 0, sizeof t17);
        memmove(gm_out, ((uint8_t *)&t17), 24);
        goto gm_fdone;
    }
    void * t18 = gm_l0;
    void * t19 = (void *)(((uint8_t *)t18) + 8);
    uint32_t t20 = gm_l1;
    void * t21 = gm_f_file_o15fqku(t19, t20);
    gm_l3 = t21;
    void * t22 = gm_l3;
    gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)t22));
    uint8_t t24 = gm_f_is_core_rhbi4fa(t23);
    uint8_t t25 = (uint8_t)!t24;
    if (t25) {
        gm_th2lqbo77pvsc t26;
        memset(&t26, 0, sizeof t26);
        memmove(gm_out, ((uint8_t *)&t26), 24);
        goto gm_fdone;
    }
    {
        {
            void * t27 = gm_l0;
            void * t28 = (void *)(((uint8_t *)t27) + 552);
            uint32_t t29 = gm_l1;
            gm_f_get_9k2rdij((void *)&gm_l7, t28, t29);
            uint8_t t30 = (uint8_t)((((uint8_t *)&gm_l7))[24] != 0);
            if (!t30) {
                goto gm_lb4;
            }
            memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l7), 24);
            memmove(gm_out, ((uint8_t *)&gm_l4), 24);
            goto gm_fdone;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t31 = gm_l3;
    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)t31));
    void * t33 = gm_l3;
    gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t33) + 16));
    gm_f_core_path_for_g46v4ll((void *)&gm_l5, (void *)NULL, cx, NULL, t32, t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t35 = gm_l0;
    void * t36 = (void *)(((uint8_t *)t35) + 552);
    uint32_t t37 = gm_l1;
    gm_tftb1odc739t5 t38 = gm_f_put_dm7u1sg(t36, t37, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l5)));
    gm_l8 = t38;
    uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_src2u46tincrqmv, 178, 5);
    memmove(gm_out, ((uint8_t *)&gm_l5), 24);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_path_to_uri_as8bikc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_path_to_uri_as8bikc F;
    gm_b16_8 gm_l0; /* path */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* b */
    GM_STACK_CHECK(&F, 298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_trqfvmjtt4sij t20;
    gm_f_list_uvfu1k1((void *)&t20, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t20;
    void * t21 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t22;
    GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sbe2tiimd9c3v));
    GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(7));
    gm_tftb1odc739t5 t23 = gm_f_extend_0cs85gs(t21, t22);
    gm_l5 = t23;
    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_src2u46tincrqmv, 10, 5);
    gm_b16_8 t25 = gm_l0;
    gm_l2 = t25;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t26 = gm_l3;
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t28 = (t26 < t27);
            if (!t28) {
                goto gm_lb1;
            }
            {
                void * t29 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t31 = gm_l3;
                {
                    {
                        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)t29) + (uintptr_t)t31 * 1));
                        gm_l4 = t32;
                        {
                            {
                                {
                                    {
                                        uint8_t t33 = gm_l4;
                                        uint8_t t34 = (t33 >= ((uint8_t)65ULL));
                                        if (!t34) {
                                            goto gm_lb9;
                                        }
                                        uint8_t t35 = (t33 <= ((uint8_t)90ULL));
                                        if (!t35) {
                                            goto gm_lb9;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    {
                                        uint8_t t36 = gm_l4;
                                        uint8_t t37 = (t36 >= ((uint8_t)97ULL));
                                        if (!t37) {
                                            goto gm_lb10;
                                        }
                                        uint8_t t38 = (t36 <= ((uint8_t)122ULL));
                                        if (!t38) {
                                            goto gm_lb10;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb10:;
                                    {
                                        uint8_t t39 = gm_l4;
                                        uint8_t t40 = (t39 >= ((uint8_t)48ULL));
                                        if (!t40) {
                                            goto gm_lb11;
                                        }
                                        uint8_t t41 = (t39 <= ((uint8_t)57ULL));
                                        if (!t41) {
                                            goto gm_lb11;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb11:;
                                    {
                                        uint8_t t42 = gm_l4;
                                        uint8_t t43 = ((t42 == ((uint8_t)45ULL)));
                                        if (!t43) {
                                            goto gm_lb12;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb12:;
                                    {
                                        uint8_t t44 = gm_l4;
                                        uint8_t t45 = ((t44 == ((uint8_t)46ULL)));
                                        if (!t45) {
                                            goto gm_lb13;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb13:;
                                    {
                                        uint8_t t46 = gm_l4;
                                        uint8_t t47 = ((t46 == ((uint8_t)95ULL)));
                                        if (!t47) {
                                            goto gm_lb14;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb14:;
                                    {
                                        uint8_t t48 = gm_l4;
                                        uint8_t t49 = ((t48 == ((uint8_t)126ULL)));
                                        if (!t49) {
                                            goto gm_lb15;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb15:;
                                    {
                                        uint8_t t50 = gm_l4;
                                        uint8_t t51 = ((t50 == ((uint8_t)47ULL)));
                                        if (!t51) {
                                            goto gm_lb16;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb16:;
                                    {
                                        uint8_t t52 = gm_l4;
                                        uint8_t t53 = ((t52 == ((uint8_t)58ULL)));
                                        if (!t53) {
                                            goto gm_lb17;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb17:;
                                    goto gm_lb7;
                                }
gm_lb8:;
                                void * t54 = (void *)((uint8_t *)&F.l1);
                                uint8_t t55 = gm_l4;
                                gm_tftb1odc739t5 t56 = gm_f_push_t83rg2m(t54, t55);
                                gm_l6 = t56;
                                uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                                if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src2u46tincrqmv, 13, 78);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                void * t58 = (void *)((uint8_t *)&F.l1);
                                uint8_t t59 = gm_l4;
                                gm_t9qkh0rsooc8d t60;
                                GM_ST(uint8_t, ((uint8_t *)&t60 + 0), t59);
                                gm_tftb1odc739t5 t61 = gm_f_write_h3ti3fb(t58, t60);
                                gm_l7 = t61;
                                uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                                if (!t62) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_src2u46tincrqmv, 14, 18);
                                goto gm_lb6;
                            }
gm_lb18:;
                            gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 12, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 11, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t63 = gm_l3;
            uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
            uint64_t t66 = r64;
            gm_l3 = t66;
        }
    }
gm_lb1:;
    gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
    F.ret = t68;
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
inline __attribute__((aligned(4))) void gm_f_pos_json_oo8uj70(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, uint32_t a1) {
    struct gm_fr_pos_json_oo8uj70 F;
    uint32_t gm_l0; /* line */
    uint32_t gm_l1; /* character */
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tin97tt4ud0s1 t2;
    gm_f_obj_aoa7fl7((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t2;
    void * t3 = (void *)((uint8_t *)&F.l2);
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_shf0g22rjm3o7));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(4));
    uint32_t t5 = gm_l0;
    int64_t t6 = (int64_t)t5;
    gm_t8vuock3jnv59 t7;
    gm_f_int_95caidg((void *)&t7, (void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t8 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t3, t4, &t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t9 = (void *)((uint8_t *)&F.l2);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s28fmt6o4roj7));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(9));
    uint32_t t11 = gm_l1;
    int64_t t12 = (int64_t)t11;
    gm_t8vuock3jnv59 t13;
    gm_f_int_95caidg((void *)&t13, (void *)NULL, cx, NULL, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t14 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t9, t10, &t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t15 = (void *)((uint8_t *)&F.l2);
    gm_f_value_pkuqal3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), t15);
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
__attribute__((aligned(4))) void gm_f_range_demc0bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_range_demc0bt F;
    void * gm_l0; /* a */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* enc */
    gm_b16_8 gm_l3; /* t */
    gm_t6j6smj7qoctu gm_l4; /* ix */
    uint64_t gm_l5; /* lo */
    uint64_t gm_l6; /* hi */
    uint64_t gm_l7; /* start */
    GM_STACK_CHECK(&F, 526u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t9 = gm_l0;
    uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    gm_b16_8 t11 = gm_f_source_text_7ru912k(t9, t10);
    gm_l3 = t11;
    void * t12 = gm_l0;
    uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    gm_t6j6smj7qoctu t14;
    gm_f_lines_0mpphhg((void *)&t14, (void *)NULL, cx, NULL, t12, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t14;
    uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t16 = (uint64_t)t15;
    gm_l5 = t16;
    uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t18 = (uint64_t)t17;
    gm_l6 = t18;
    uint64_t t19 = gm_l5;
    uint64_t t20 = gm_l6;
    uint8_t t21 = ((t19 == t20));
    if (t21) {
        {
            {
                uint64_t t22 = gm_l6;
                uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint8_t t24 = (t22 < t23);
                if (!t24) {
                    goto gm_lb2;
                }
                gm_b16_8 t25 = gm_l3;
                uint64_t t26 = gm_l6;
                uint8_t t27 = gm_f_is_char_boundary_h62fhaa(t25, t26);
                if (!t27) {
                    goto gm_lb2;
                }
                void * t28 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t30 = gm_l6;
                uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t28) + (uintptr_t)t30 * 1));
                uint8_t t32 = (!(t31 == ((uint8_t)10ULL)));
                if (!t32) {
                    goto gm_lb2;
                }
                void * t33 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t35 = gm_l6;
                uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)t33) + (uintptr_t)t35 * 1));
                uint8_t t37 = (!(t36 == ((uint8_t)13ULL)));
                if (!t37) {
                    goto gm_lb2;
                }
                uint64_t t38 = gm_l6;
                void * t39 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t41 = gm_l6;
                uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)t39) + (uintptr_t)t41 * 1));
                uint64_t t43 = gm_f_char_len_k9gjmvv(t42);
                uint64_t r44; bool o45 = __builtin_add_overflow(t38, t43, &r44);
                if (o45) gm_trap("integer overflow in `+`", gm_src2u46tincrqmv, 204, 13);
                uint64_t t46 = r44;
                gm_l6 = t46;
                goto gm_lb1;
            }
gm_lb2:;
            {
                {
                    uint64_t t47 = gm_l5;
                    uint8_t t48 = (t47 > ((uint64_t)0ULL));
                    if (!t48) {
                        goto gm_lb4;
                    }
                    uint64_t t49 = gm_l5;
                    uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t51 = (t49 <= t50);
                    if (!t51) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t52 = gm_l3;
                    uint64_t t53 = gm_l5;
                    uint8_t t54 = gm_f_is_char_boundary_h62fhaa(t52, t53);
                    if (!t54) {
                        goto gm_lb4;
                    }
                    uint64_t t55 = gm_l5;
                    uint64_t r56; bool o57 = __builtin_sub_overflow(t55, ((uint64_t)1ULL), &r56);
                    if (o57) gm_trap("integer overflow in `-`", gm_src2u46tincrqmv, 206, 22);
                    uint64_t t58 = r56;
                    gm_l7 = t58;
                    {
                        for (;;) {
                            uint64_t t59 = gm_l7;
                            uint8_t t60 = (t59 > ((uint64_t)0ULL));
                            if (!t60) {
                                goto gm_lb5;
                            }
                            gm_b16_8 t61 = gm_l3;
                            uint64_t t62 = gm_l7;
                            uint8_t t63 = gm_f_is_char_boundary_h62fhaa(t61, t62);
                            uint8_t t64 = (uint8_t)!t63;
                            if (!t64) {
                                goto gm_lb5;
                            }
                            {
                                uint64_t t65 = gm_l7;
                                uint64_t r66; bool o67 = __builtin_sub_overflow(t65, ((uint64_t)1ULL), &r66);
                                if (o67) gm_trap("integer overflow in `-`", gm_src2u46tincrqmv, 207, 66);
                                uint64_t t68 = r66;
                                gm_l7 = t68;
                            }
gm_lb7:;
                        }
                    }
gm_lb5:;
                    void * t69 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint64_t t71 = gm_l7;
                    if ((uintptr_t)t71 >= (uintptr_t)(t70)) gm_trap_index((uintptr_t)t71, t70, gm_src2u46tincrqmv, 208, 16);
                    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)t69) + (uintptr_t)t71 * 1));
                    uint8_t t73 = (!(t72 == ((uint8_t)10ULL)));
                    if (t73) {
                        uint64_t t74 = gm_l7;
                        gm_l5 = t74;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
        }
gm_lb1:;
    }
    void * t75 = (void *)((uint8_t *)&gm_l4);
    uint64_t t76 = gm_l5;
    uint64_t t77 = gm_l6;
    gm_t9qkh0rsooc8d t78 = gm_l2;
    gm_f_range_json_hlsolbo((void *)&(*(gm_t8vuock3jnv59 *)gm_out), (void *)NULL, cx, NULL, t75, t76, t77, t78);
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
inline __attribute__((aligned(4))) void gm_f_range_json_hlsolbo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3) {
    struct gm_fr_range_json_hlsolbo F;
    void * gm_l0; /* ix */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    gm_t9qkh0rsooc8d gm_l3; /* enc */
    gm_tcc1rq4n1avb2 gm_l4; /* s */
    gm_tcc1rq4n1avb2 gm_l5; /* e */
    GM_STACK_CHECK(&F, 251u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    gm_t9qkh0rsooc8d t4 = gm_l3;
    gm_tcc1rq4n1avb2 t5 = gm_f_position_fpk0ln3(t2, t3, t4);
    gm_l4 = t5;
    void * t6 = gm_l0;
    uint64_t t7 = gm_l2;
    gm_t9qkh0rsooc8d t8 = gm_l3;
    gm_tcc1rq4n1avb2 t9 = gm_f_position_fpk0ln3(t6, t7, t8);
    gm_l5 = t9;
    gm_tin97tt4ud0s1 t10;
    gm_f_obj_aoa7fl7((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t10;
    void * t11 = (void *)((uint8_t *)&F.l6);
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s7mq585fepqnh));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(5));
    uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
    uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
    gm_t8vuock3jnv59 t15;
    gm_f_pos_json_oo8uj70((void *)&t15, (void *)NULL, cx, NULL, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t16 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t11, t12, &t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t17 = (void *)((uint8_t *)&F.l6);
    gm_b16_8 t18;
    GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s4769el4glhs1));
    GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(3));
    uint32_t t19 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
    uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
    gm_t8vuock3jnv59 t21;
    gm_f_pos_json_oo8uj70((void *)&t21, (void *)NULL, cx, NULL, t19, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t22 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t17, t18, &t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t23 = (void *)((uint8_t *)&F.l6);
    gm_f_value_pkuqal3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_range_json_zero_orc6s8p(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_range_json_zero_orc6s8p F;
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tin97tt4ud0s1 t2;
    gm_f_obj_aoa7fl7((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l0 = t2;
    void * t3 = (void *)((uint8_t *)&F.l0);
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s7mq585fepqnh));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(5));
    gm_t8vuock3jnv59 t5;
    gm_f_pos_json_oo8uj70((void *)&t5, (void *)NULL, cx, NULL, ((uint32_t)0ULL), ((uint32_t)0ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t6 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t3, t4, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t7 = (void *)((uint8_t *)&F.l0);
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s4769el4glhs1));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(3));
    gm_t8vuock3jnv59 t9;
    gm_f_pos_json_oo8uj70((void *)&t9, (void *)NULL, cx, NULL, ((uint32_t)0ULL), ((uint32_t)0ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t10 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t7, t8, &t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t11 = (void *)((uint8_t *)&F.l0);
    gm_f_value_pkuqal3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), t11);
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
inline __attribute__((aligned(4))) void gm_f_real_path_al8cv9r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_real_path_al8cv9r F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* file */
    gm_b16_8 gm_l2; /* p */
    GM_STACK_CHECK(&F, 238u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t2 = gm_l1;
    uint64_t t3 = (uint64_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 8) + 8));
    uint8_t t6 = (t3 >= t5);
    if (t6) {
        gm_th2lqbo77pvsc t7;
        memset(&t7, 0, sizeof t7);
        memmove(gm_out, ((uint8_t *)&t7), 24);
        goto gm_fdone;
    }
    void * t8 = gm_l0;
    void * t9 = (void *)(((uint8_t *)t8) + 8);
    uint32_t t10 = gm_l1;
    void * t11 = gm_f_file_o15fqku(t9, t10);
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)t11));
    gm_l2 = t12;
    gm_b16_8 t13 = gm_l2;
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s1f440st4mnkp));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(1));
    uint8_t t15 = gm_f_starts_with_6vvt01e(t13, t14);
    if (t15) {
        gm_th2lqbo77pvsc t16;
        memset(&t16, 0, sizeof t16);
        memmove(gm_out, ((uint8_t *)&t16), 24);
        goto gm_fdone;
    }
    gm_b16_8 t17 = gm_l2;
    gm_b16_8 t18 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_th2lqbo77pvsc t19;
    GM_ST(uint8_t, (uint8_t *)&t19 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t19 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t19 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t19), t18);
    ((uint8_t *)&t19)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t19), 24);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_release_7gtbra1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_core_nv5iii8(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_resolve_core_nv5iii8 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* m */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t3877pk7lv748 gm_l7; /* n */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* d */
    GM_STACK_CHECK(&F, 714u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l11;
    gm_l2 = ((uint64_t)0ULL);
    void * t17 = gm_l0;
    void * t18 = GM_LD(void *, ((uint8_t *)t17));
    uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)t18) + 200) + 8));
    gm_l1 = t19;
    {
        for (;;) {
            uint64_t t20 = gm_l2;
            uint64_t t21 = gm_l1;
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t23 = gm_l2;
                        gm_l3 = t23;
                        void * t24 = gm_l0;
                        void * t25 = GM_LD(void *, ((uint8_t *)t24));
                        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 200));
                        uint64_t t27 = GM_LD(uint64_t, ((((uint8_t *)t25) + 200) + 8));
                        uint64_t t28 = gm_l3;
                        if ((uintptr_t)t28 >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)t28, t27, gm_src2u46tincrqmv, 148, 16);
                        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t26) + (uintptr_t)t28 * 184) + 112));
                        uint8_t t30 = (uint8_t)!t29;
                        if (t30) {
                            goto gm_lb3;
                        }
                        gm_trqfvmjtt4sij t31;
                        gm_f_list_5dhgg4i((void *)&t31, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l4 = t31;
                        void * t32 = gm_l0;
                        void * t33 = GM_LD(void *, ((uint8_t *)t32));
                        void * t34 = GM_LD(void *, (((uint8_t *)t33) + 200));
                        uint64_t t35 = GM_LD(uint64_t, ((((uint8_t *)t33) + 200) + 8));
                        uint64_t t36 = gm_l3;
                        if ((uintptr_t)t36 >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)t36, t35, gm_src2u46tincrqmv, 150, 18);
                        gm_b16_8 t37 = GM_LD(gm_b16_8, ((((uint8_t *)t34) + (uintptr_t)t36 * 184) + 16));
                        gm_l5 = t37;
                        gm_l6 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t38 = gm_l6;
                                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                uint8_t t40 = (t38 < t39);
                                if (!t40) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t41 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                    uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                    uint64_t t43 = gm_l6;
                                    {
                                        {
                                            memmove(((uint8_t *)&gm_l7), (((uint8_t *)t41) + (uintptr_t)t43 * 24), 24);
                                            void * t44 = (void *)((uint8_t *)&F.l4);
                                            uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 16));
                                            gm_tftb1odc739t5 t46 = gm_f_push_ucgmlo2(t44, t45);
                                            gm_l11 = t46;
                                            uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                            if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_src2u46tincrqmv, 150, 53);
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 150, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t48 = gm_l6;
                                uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                                uint64_t t51 = r49;
                                gm_l6 = t51;
                            }
                        }
gm_lb6:;
                        gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
                        gm_f_sort_o401rjn(t52);
                        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
                        gm_l8 = t53;
                        gm_l9 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t54 = gm_l9;
                                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                uint8_t t56 = (t54 < t55);
                                if (!t56) {
                                    goto gm_lb11;
                                }
                                {
                                    void * t57 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                    uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                    uint64_t t59 = gm_l9;
                                    {
                                        {
                                            uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)t57) + (uintptr_t)t59 * 8));
                                            gm_l10 = t60;
                                            void * t61 = gm_l0;
                                            uint64_t t62 = gm_l10;
                                            gm_t6qooom8qa2do t63;
                                            gm_f_resolve_decl_pus042v((void *)&t63, (void *)NULL, cx, NULL, t61, t62);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_lb14;
                                        }
gm_lb15:;
                                        gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 152, 9);
                                    }
gm_lb14:;
                                }
gm_lb13:;
                                uint64_t t64 = gm_l9;
                                uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                                uint64_t t67 = r65;
                                gm_l9 = t67;
                            }
                        }
gm_lb11:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 147, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t68 = gm_l2;
            uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
            uint64_t t71 = r69;
            gm_l2 = t71;
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
extern __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_scan_lines_lhr1jn1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_source_text_7ru912k(void * a0, uint32_t a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_uri_of_t6r79gp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_uri_to_path_kbof22r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_uri_to_path_kbof22r F;
    gm_b16_8 gm_l0; /* uri */
    gm_b16_8 gm_l1; /* rest */
    uint64_t gm_l2; /* slash */
    gm_b16_8 gm_l3; /* bytes */
    uint64_t gm_l5; /* i */
    gm_tqs5p4n2tq19v gm_l6; /* hi */
    gm_tqs5p4n2tq19v gm_l7; /* lo */
    GM_STACK_CHECK(&F, 734u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tf5srh4mp4b3i gm_l8;
    uint64_t gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_b16_8 t15 = gm_l0;
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sbe2tiimd9c3v));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(7));
    uint8_t t17 = gm_f_starts_with_6vvt01e(t15, t16);
    uint8_t t18 = (uint8_t)!t17;
    if (t18) {
        gm_th2lqbo77pvsc t19;
        memset(&t19, 0, sizeof t19);
        memmove(gm_out, ((uint8_t *)&t19), 24);
        goto gm_fdone;
    }
    void * t20 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t22 = (uintptr_t)((uint64_t)7ULL);
    uint64_t t23 = (uintptr_t)t21;
    if (t22 > t23 || t23 > (uintptr_t)t21) gm_trap_slice(t22, t23, t21, gm_src2u46tincrqmv, 30, 13);
    uint64_t t24 = ((uintptr_t)t21) - (uintptr_t)((uint64_t)7ULL);
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)7ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    gm_l1 = t25;
    gm_b16_8 t26 = gm_l1;
    gm_tf5srh4mp4b3i t27 = gm_f_index_of_hdeg351(t26, ((uint8_t)47ULL));
    gm_l8 = t27;
    uint8_t t28 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
    if (t28) {
        uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
        gm_l9 = t29;
    } else {
        gm_th2lqbo77pvsc t30;
        memset(&t30, 0, sizeof t30);
        memmove(gm_out, ((uint8_t *)&t30), 24);
        goto gm_fdone;
    }
    uint64_t t31 = gm_l9;
    gm_l2 = t31;
    void * t32 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t34 = gm_l2;
    uint64_t t35 = (uintptr_t)t34;
    uint64_t t36 = (uintptr_t)t33;
    if (t35 > t36 || t36 > (uintptr_t)t33) gm_trap_slice(t35, t36, t33, gm_src2u46tincrqmv, 33, 14);
    uint64_t t37 = ((uintptr_t)t33) - (uintptr_t)t34;
    gm_b16_8 t38;
    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t32 + (uintptr_t)t34 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37));
    gm_l3 = t38;
    gm_trqfvmjtt4sij t39;
    gm_f_list_uvfu1k1((void *)&t39, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t39;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t40 = gm_l5;
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t42 = (t40 < t41);
            if (!t42) {
                goto gm_lb1;
            }
            {
                {
                    {
                        void * t43 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t45 = gm_l5;
                        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)t43) + (uintptr_t)t45 * 1));
                        uint8_t t47 = ((t46 == ((uint8_t)37ULL)));
                        if (!t47) {
                            goto gm_lb5;
                        }
                        uint64_t t48 = gm_l5;
                        uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)2ULL), &r49);
                        if (o50) gm_trap("integer overflow in `+`", gm_src2u46tincrqmv, 37, 32);
                        uint64_t t51 = r49;
                        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint8_t t53 = (t51 < t52);
                        if (!t53) {
                            goto gm_lb5;
                        }
                        void * t54 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t56 = gm_l5;
                        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                        if (o58) gm_trap("integer overflow in `+`", gm_src2u46tincrqmv, 38, 35);
                        uint64_t t59 = r57;
                        if ((uintptr_t)t59 >= (uintptr_t)(t55)) gm_trap_index((uintptr_t)t59, t55, gm_src2u46tincrqmv, 38, 29);
                        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)t54) + (uintptr_t)t59 * 1));
                        gm_tqs5p4n2tq19v t61 = gm_f_hex_value_08hqdl8(t60);
                        gm_l6 = t61;
                        void * t62 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t64 = gm_l5;
                        uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)2ULL), &r65);
                        if (o66) gm_trap("integer overflow in `+`", gm_src2u46tincrqmv, 39, 35);
                        uint64_t t67 = r65;
                        if ((uintptr_t)t67 >= (uintptr_t)(t63)) gm_trap_index((uintptr_t)t67, t63, gm_src2u46tincrqmv, 39, 29);
                        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)t62) + (uintptr_t)t67 * 1));
                        gm_tqs5p4n2tq19v t69 = gm_f_hex_value_08hqdl8(t68);
                        gm_l7 = t69;
                        {
                            {
                                {
                                    {
                                        uint8_t t70 = (uint8_t)((((uint8_t *)&gm_l6))[1] != 0);
                                        if (t70) {
                                            goto gm_lb9;
                                        }
                                        gm_l10 = ((uint8_t)0);
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    {
                                        gm_l10 = ((uint8_t)1);
                                        goto gm_lb8;
                                    }
gm_lb10:;
                                    gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 40, 16);
                                }
gm_lb8:;
                                uint8_t t71 = gm_l10;
                                if (!t71) {
                                    goto gm_lb7;
                                }
                                {
                                    {
                                        uint8_t t72 = (uint8_t)((((uint8_t *)&gm_l7))[1] != 0);
                                        if (t72) {
                                            goto gm_lb12;
                                        }
                                        gm_l11 = ((uint8_t)0);
                                        goto gm_lb11;
                                    }
gm_lb12:;
                                    {
                                        gm_l11 = ((uint8_t)1);
                                        goto gm_lb11;
                                    }
gm_lb13:;
                                    gm_trap("no match arm matched the scrutinee", gm_src2u46tincrqmv, 40, 35);
                                }
gm_lb11:;
                                uint8_t t73 = gm_l11;
                                if (!t73) {
                                    goto gm_lb7;
                                }
                                void * t74 = (void *)((uint8_t *)&F.l4);
                                uint8_t t75 = (uint8_t)((((uint8_t *)&gm_l6))[1] != 0);
                                if (t75) {
                                    uint8_t t76 = GM_LD(uint8_t, ((uint8_t *)&gm_l6));
                                    gm_l12 = t76;
                                } else {
                                    gm_l12 = ((uint8_t)0ULL);
                                }
                                uint8_t t77 = gm_l12;
                                uint8_t r78; bool o79 = __builtin_mul_overflow(t77, ((uint8_t)16ULL), &r78);
                                if (o79) gm_trap("integer overflow in `*`", gm_src2u46tincrqmv, 41, 26);
                                uint8_t t80 = r78;
                                uint8_t t81 = (uint8_t)((((uint8_t *)&gm_l7))[1] != 0);
                                if (t81) {
                                    uint8_t t82 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
                                    gm_l13 = t82;
                                } else {
                                    gm_l13 = ((uint8_t)0ULL);
                                }
                                uint8_t t83 = gm_l13;
                                uint8_t r84; bool o85 = __builtin_add_overflow(t80, t83, &r84);
                                if (o85) gm_trap("integer overflow in `+`", gm_src2u46tincrqmv, 41, 26);
                                uint8_t t86 = r84;
                                gm_tftb1odc739t5 t87 = gm_f_push_t83rg2m(t74, t86);
                                gm_l14 = t87;
                                uint8_t t88 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                                if (!t88) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_src2u46tincrqmv, 41, 17);
                                uint64_t t89 = gm_l5;
                                uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)3ULL), &r90);
                                if (o91) gm_trap("integer overflow in `+`", gm_src2u46tincrqmv, 42, 17);
                                uint64_t t92 = r90;
                                gm_l5 = t92;
                                goto gm_lb3;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                }
gm_lb4:;
                void * t93 = (void *)((uint8_t *)&F.l4);
                void * t94 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t96 = gm_l5;
                if ((uintptr_t)t96 >= (uintptr_t)(t95)) gm_trap_index((uintptr_t)t96, t95, gm_src2u46tincrqmv, 46, 18);
                uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)t94) + (uintptr_t)t96 * 1));
                gm_tftb1odc739t5 t98 = gm_f_push_t83rg2m(t93, t97);
                gm_l15 = t98;
                uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                if (!t99) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_src2u46tincrqmv, 46, 9);
                uint64_t t100 = gm_l5;
                uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                if (o102) gm_trap("integer overflow in `+`", gm_src2u46tincrqmv, 47, 9);
                uint64_t t103 = r101;
                gm_l5 = t103;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
    gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
    uint8_t t106 = gm_f_utf8_valid_a8oc1t8(t105);
    uint8_t t107 = (uint8_t)!t106;
    if (t107) {
        gm_th2lqbo77pvsc t108;
        memset(&t108, 0, sizeof t108);
        memmove(gm_out, ((uint8_t *)&t108), 24);
        goto gm_fdone;
    }
    gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
    gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
    gm_th2lqbo77pvsc t111;
    GM_ST(uint8_t, (uint8_t *)&t111 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t111 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t111 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t111), t110);
    ((uint8_t *)&t111)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t111), 24);
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
