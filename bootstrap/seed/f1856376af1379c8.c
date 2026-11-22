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
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { void * a[1]; } gm_tn3r4rvlh0rsv;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t88rcucg1b7uk v1; } f1; } gm_t5e6kkuq21nig;
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
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
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
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn3r4rvlh0rsv) == 8 && _Alignof(gm_tn3r4rvlh0rsv) == 8 && offsetof(gm_tn3r4rvlh0rsv, a) == 0, "layout of `[1]^Expr`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
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
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5e6kkuq21nig) == 16 && _Alignof(gm_t5e6kkuq21nig) == 8 && offsetof(gm_t5e6kkuq21nig, f0) == 0 && offsetof(gm_t5e6kkuq21nig, f1) == 8, "layout of `ArrayLen`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
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
typedef struct { gm_tbngattl1fo51 f0; uint8_t f1; uint8_t pe[3]; } gm_thetuhtouve7f;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_th2lqbo77pvsc f0; void * f1; } gm_tpouv9r7lrri2;
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?(variants: []VariantDef, tag: Ty)`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_thetuhtouve7f) == 12 && _Alignof(gm_thetuhtouve7f) == 4 && offsetof(gm_thetuhtouve7f, f0) == 0 && offsetof(gm_thetuhtouve7f, f1) == 8, "layout of `?(inclusive: bool, elem: Ty)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tpouv9r7lrri2) == 32 && _Alignof(gm_tpouv9r7lrri2) == 8 && offsetof(gm_tpouv9r7lrri2, f0) == 0 && offsetof(gm_tpouv9r7lrri2, f1) == 24, "layout of `SpreadPart`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tb38v5s9j1r32 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc9qv69frb08e;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_tc9qv69frb08e) == 136 && _Alignof(gm_tc9qv69frb08e) == 8 && offsetof(gm_tc9qv69frb08e, f0) == 0 && offsetof(gm_tc9qv69frb08e, f1) == 8 && offsetof(gm_tc9qv69frb08e, f2) == 120, "layout of `Pat`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_array_type_construction_vgjg706 {
    gm_frame h;
    void * ret;
};
struct gm_fr_box_mut_dlc4mlv {
    gm_frame h;
    void * ret;
};
struct gm_fr_construct_array_qkofm98 {
    gm_frame h;
    gm_tc3oht4ksth9d l8; /* e */
    gm_t7te2tdpebbt4 l13; /* a */
    gm_tc3oht4ksth9d l15; /* e */
    gm_trqfvmjtt4sij l22; /* values */
    gm_trqfvmjtt4sij l23; /* values */
    gm_t7te2tdpebbt4 l27; /* a */
    gm_trqfvmjtt4sij l28; /* stmts */
    gm_t7te2tdpebbt4 l31; /* a */
    gm_tc3oht4ksth9d l32; /* e */
    gm_tc3oht4ksth9d l34; /* e */
    void * ret;
};
struct gm_fr_construct_enum_array_d7cisuu {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* names */
    gm_tckdobunaulee l10; /* v */
    gm_tc3oht4ksth9d l12; /* x */
    gm_trqfvmjtt4sij l14; /* stmts */
    gm_trqfvmjtt4sij l15; /* slots */
    gm_trqfvmjtt4sij l18; /* written */
    gm_t7te2tdpebbt4 l22; /* a */
    gm_tc3oht4ksth9d l24; /* x */
    gm_trqfvmjtt4sij l37; /* missing */
    gm_trqfvmjtt4sij l42; /* values */
    void * ret;
};
struct gm_fr_construct_param_array_btk60nl {
    gm_frame h;
    gm_tc3oht4ksth9d l6; /* x */
    void * ret;
};
struct gm_fr_construction_elem_type_qm1ncub {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_empty_slice_3v2gjd4_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo {
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
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_enum_array_index_kma2hbd {
    gm_frame h;
    void * ret;
};
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
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
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_format_47t5q9n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_blrf705 {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tq3m5qgvkpje8 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d0bas9o {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e79cvtl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_frab70n {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h4jcodi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h75o0bg {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_i7ngra8 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n8a3chu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ti6vs6t {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v7qttjn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_given_27f1vc1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_math_vector_09mtuao {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_vector_gkq6b88 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_58nr42v_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
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
struct gm_fr_mk_pat_hr2l6c4 {
    gm_frame h;
    gm_tb38v5s9j1r32 l1; /* kind */
    void * ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_tcgrjq9toscke l1; /* x */
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
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h9tbucc_2 {
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
struct gm_fr_unsupported_r3r7qdh {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_variant_position_fdpecs3 {
    gm_frame h;
    gm_tckdobunaulee l10; /* var */
    gm_trqfvmjtt4sij l15; /* arms */
    void * ret;
};
struct gm_fr_without_call_t1kfc9e {
    gm_frame h;
    gm_t2gtbh39ptekd l10; /* kind */
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_len_eielif2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_array_type_construction_vgjg706(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void * gm_f_box_mut_dlc4mlv(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_builtin_a7cjd7d(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_construct_array_qkofm98(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1, gm_tf5srh4mp4b3i a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_enum_array_d7cisuu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_param_array_btk60nl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_construction_elem_type_qm1ncub(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) void * gm_f_enum_array_index_kma2hbd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, void * a4, gm_tl874t2atnlh3 a5);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_field_value_19vleud(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_format_01lon97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_47t5q9n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_blrf705(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_d0bas9o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fjqj2pj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_frab70n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_h75o0bg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_i7ngra8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_n8a3chu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_tbupn9l(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ti6vs6t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_v7qttjn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_given_27f1vc1(void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_o9r57ha(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_math_vector_09mtuao(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_length_is_type_soi53ah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_math_vector_conversion_0cttjm0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_math_vector_parts_49c7865(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_thetuhtouve7f gm_f_range_of_dmmo768(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_ty_uk6c1lp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_spread_parts_e62q2l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_variant_position_fdpecs3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_conversion_32a9cuf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_without_call_t1kfc9e(void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern uint8_t gm_ksksp29ghso[40] __attribute__((aligned(8)));
extern uint8_t gm_ksnfevokhod[40] __attribute__((aligned(8)));
extern uint8_t gm_kv89j160jqp[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0etnbnk4miq5[5];
extern const uint8_t gm_s0k8snlvukl86[66];
extern const uint8_t gm_s1btmm2esmgsd[27];
extern const uint8_t gm_s3nknbqjesra1[19];
extern const uint8_t gm_s7umjdfqes00m[52];
extern const uint8_t gm_s8do3iqjvck3j[45];
extern const uint8_t gm_sdjbnp04akbbb[50];
extern const uint8_t gm_sdpkm2aceiv8l[47];
extern const uint8_t gm_sg0jqm2pij3el[78];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sifq8gd3d748j[36];
extern const uint8_t gm_sig8ahbvm8eb8[10];
extern const uint8_t gm_sksrclikobn5n[37];
extern const uint8_t gm_sn048vvii9ap5[83];
extern const uint8_t gm_sngn7trhgdnc2[43];
extern const uint8_t gm_soiet16j0e8ar[34];
extern const uint8_t gm_spevct0q3cjgb[5];
extern const uint8_t gm_sqa5rf17c57e4[5];
extern const uint8_t gm_sqirjnd2segia[40];
extern const uint8_t gm_sqof8dgnasnqk[69];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src5cfou479ljhn[];
extern const char gm_srck5orfpjti60c[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_suki713bapaoe[7];
extern const uint8_t gm_suv2tallru6a1[2];
extern const gm_tmpl gm_tpl1iblbr9u646o;
extern const gm_tmpl gm_tpl4aiadg123uql;
extern const gm_tmpl gm_tpl4mli0f9rjihf;
extern const gm_tmpl gm_tplceqrbdtcd9p5;
extern const gm_tmpl gm_tpldhmlf1n8qcdi;
extern const gm_tmpl gm_tplfqk35ejq39la;
extern const gm_tmpl gm_tplh64dsjnr7d93;
extern const gm_tmpl gm_tpll8q455cp9mhi;
extern const gm_tmpl gm_tplsbqgr9k4itq0;
extern const gm_tmpl gm_tplso01g2paq6o6;
extern const gm_tmpl gm_tplvc9hnrt45n6d;
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
inline __attribute__((aligned(4))) void * gm_f_box_mut_dlc4mlv(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_box_mut_dlc4mlv F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tq7s98ihonkju gm_l1;
    void * t2 = gm_l0;
    gm_tq7s98ihonkju t3 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t2)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ksnfevokhod)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t3;
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t4) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_src5cfou479ljhn, 49, 63);
    void * t5 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
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
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l2;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)a1));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        int32_t t9 = gm_f_mag_cmp_s39veq6(t6, t8);
        uint8_t t10 = ((t9 == ((int32_t)0LL)));
        gm_l2 = t10;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t11 = gm_l2;
    F.ret = t11;
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
inline __attribute__((aligned(4))) void gm_f_format_47t5q9n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_47t5q9n F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sifq8gd3d748j));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1iblbr9u646o), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_blrf705(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_blrf705 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s8do3iqjvck3j));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(44));
    gm_tq3m5qgvkpje8 t3 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
    gm_f_format_tbupn9l((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpldhmlf1n8qcdi), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_d0bas9o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_d0bas9o F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sn048vvii9ap5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(82));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplh64dsjnr7d93), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_frab70n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_frab70n F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sngn7trhgdnc2));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(42));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4mli0f9rjihf), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_h75o0bg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_h75o0bg F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sdpkm2aceiv8l));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(46));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplso01g2paq6o6), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_i7ngra8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_i7ngra8 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s1btmm2esmgsd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfqk35ejq39la), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_n8a3chu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_n8a3chu F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suki713bapaoe));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplceqrbdtcd9p5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ti6vs6t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ti6vs6t F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg0jqm2pij3el));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(77));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4aiadg123uql), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_v7qttjn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_v7qttjn F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s7umjdfqes00m));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(51));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvc9hnrt45n6d), t2);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_given_27f1vc1(void *env, gm_node *cx, gm_node *inst, uint64_t a0) {
    struct gm_fr_given_27f1vc1 F;
    uint64_t gm_l0; /* n */
    GM_STACK_CHECK(&F, 170u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    gm_tabb710d9mq2e gm_l2;
    uint64_t t2 = gm_l0;
    uint8_t t3 = ((t2 == ((uint64_t)1ULL)));
    if (t3) {
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s0etnbnk4miq5));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(4));
        gm_l1 = t4;
    } else {
        uint64_t t5 = gm_l0;
        gm_t4ce0jd3tdro4 t6;
        GM_ST(uint64_t, ((uint8_t *)&t6 + 0), t5);
        gm_f_format_n8a3chu((void *)&gm_l2, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
        if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_src5cfou479ljhn, 244, 76);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_l1 = t9;
    }
    gm_b16_8 t10 = gm_l1;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_math_vector_09mtuao(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_math_vector_09mtuao F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    gm_tb7u19loo3j87 gm_l3;
    void * t5 = gm_l0;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_f_math_vector_parts_49c7865((void *)&gm_l3, t5, GM_CONV(gm_csguankko4lmhd, t6));
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
        gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 26, 62);
    }
gm_lb1:;
    uint8_t t8 = gm_l2;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_vector_gkq6b88 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    gm_tb7u19loo3j87 gm_l3;
    void * t5 = gm_l0;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_f_vector_parts_crrfopm((void *)&gm_l3, t5, GM_CONV(gm_csguankko4lmhd, t6));
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
        gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 25, 57);
    }
gm_lb1:;
    uint8_t t8 = gm_l2;
    F.ret = t8;
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
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_r3r7qdh F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 132u);
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
    gm_b0_1 t6 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t3, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    gm_tl874t2atnlh3 t9 = gm_l1;
    void * t10 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t8, t9);
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
__attribute__((aligned(4))) void * gm_f_array_type_construction_vgjg706(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_array_type_construction_vgjg706 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* len_ast */
    void * gm_l3; /* elem */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* call */
    gm_b16_8 gm_l6; /* args */
    void * gm_l7; /* l */
    gm_t88rcucg1b7uk gm_l8; /* t */
    gm_t88rcucg1b7uk gm_l9; /* et */
    gm_tnnjeerolos0a gm_l10; /* kind */
    gm_t88rcucg1b7uk gm_l11; /* index */
    gm_t88rcucg1b7uk gm_l12; /* elem_ty */
    gm_tf5srh4mp4b3i gm_l13; /* len */
    void * gm_l14; /* l */
    uint64_t gm_l15; /* n */
    gm_t88rcucg1b7uk gm_l16; /* pl */
    gm_t88rcucg1b7uk gm_l17; /* t */
    GM_STACK_CHECK(&F, 1121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    void * gm_l18;
    void * gm_l19;
    gm_t53at80pfdus3 gm_l20;
    gm_tkn42u7na3t81 gm_l21;
    gm_t88rcucg1b7uk gm_l22;
    gm_t5e6kkuq21nig gm_l23;
    void * t14 = gm_l3;
    void * t15 = gm_f_construction_call_ktqtrpo(t14);
    gm_l18 = t15;
    uint8_t t16 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l18)) != 0);
    if (t16) {
        void * t17 = GM_LD(void *, ((uint8_t *)&gm_l18));
        gm_l19 = t17;
    } else {
        void * t18 = gm_l0;
        gm_tl874t2atnlh3 t19 = gm_l4;
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s3nknbqjesra1));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(18));
        void * t21 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t18, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t21;
        goto gm_fret;
    }
    void * t22 = gm_l19;
    gm_l5 = t22;
    {
        {
            void * t23 = gm_l5;
            uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t23) + 8) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)19ULL));
            if (!t25) {
                goto gm_lb2;
            }
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((((uint8_t *)t23) + 8) + 8) + 8));
            gm_l6 = t26;
            {
                {
                    uint8_t t27 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l2)) != 0);
                    if (!t27) {
                        goto gm_lb4;
                    }
                    void * t28 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    gm_l7 = t28;
                    void * t29 = gm_l0;
                    void * t30 = GM_LD(void *, ((uint8_t *)t29));
                    void * t31 = gm_l0;
                    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 8));
                    void * t33 = gm_l7;
                    uint8_t t34 = gm_f_length_is_type_soi53ah((void *)NULL, cx, NULL, t30, t32, t33);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (!t34) {
                        goto gm_lb4;
                    }
                    void * t35 = gm_l0;
                    void * t36 = gm_l1;
                    void * t37 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t36);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t38 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t35, t37);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l8 = t38;
                    {
                        {
                            void * t39 = gm_l0;
                            void * t40 = GM_LD(void *, ((uint8_t *)t39));
                            void * t41 = (void *)(((uint8_t *)t40) + 8);
                            gm_t88rcucg1b7uk t42 = gm_l8;
                            gm_f_kind_jcnt4hm((void *)&gm_l20, t41, GM_CONV(gm_csguankko4lmhd, t42));
                            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
                            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)11ULL));
                            if (!t44) {
                                goto gm_lb6;
                            }
                            gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l20) + 8) + 8));
                            gm_l9 = t45;
                            gm_tnnjeerolos0a t46 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&gm_l20) + 8) + 12));
                            gm_l10 = t46;
                            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)1ULL));
                            if (!t48) {
                                goto gm_lb6;
                            }
                            gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 4));
                            gm_l11 = t49;
                            void * t50 = gm_l0;
                            gm_t88rcucg1b7uk t51 = gm_l8;
                            gm_t88rcucg1b7uk t52 = gm_l11;
                            gm_t88rcucg1b7uk t53 = gm_l9;
                            gm_b16_8 t54 = gm_l6;
                            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                            gm_tl874t2atnlh3 t56 = gm_l4;
                            void * t57 = gm_f_construct_enum_array_d7cisuu((void *)NULL, cx, NULL, t50, t51, t52, t53, t55, t56);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t57;
                            goto gm_fret;
                        }
gm_lb6:;
                    }
gm_lb5:;
                    void * t58 = gm_l0;
                    void * t59 = GM_LD(void *, ((uint8_t *)t58));
                    gm_tl874t2atnlh3 t60 = gm_l4;
                    void * t61 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t59, t60);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t61;
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            void * t62 = gm_l0;
            void * t63 = gm_l3;
            gm_tkn42u7na3t81 t64 = gm_f_construction_elem_type_qm1ncub((void *)NULL, cx, NULL, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l21 = t64;
            uint8_t t65 = (uint8_t)((((uint8_t *)&gm_l21))[4] != 0);
            if (t65) {
                gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l21));
                gm_l22 = t66;
            } else {
                void * t67 = gm_l0;
                void * t68 = GM_LD(void *, ((uint8_t *)t67));
                gm_tl874t2atnlh3 t69 = gm_l4;
                void * t70 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t68, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t70;
                goto gm_fret;
            }
            gm_t88rcucg1b7uk t71 = gm_l22;
            gm_l12 = t71;
            gm_tf5srh4mp4b3i t72;
            memset(&t72, 0, sizeof t72);
            gm_l13 = t72;
            {
                {
                    uint8_t t73 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l2)) != 0);
                    if (!t73) {
                        goto gm_lb8;
                    }
                    void * t74 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    gm_l14 = t74;
                    void * t75 = gm_l0;
                    void * t76 = GM_LD(void *, ((uint8_t *)t75));
                    void * t77 = gm_l0;
                    uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)t77) + 8));
                    void * t79 = gm_l14;
                    gm_t5e6kkuq21nig t80 = gm_f_array_len_eielif2((void *)NULL, cx, NULL, t76, t78, t79);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l23 = t80;
                    {
                        {
                            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                            uint8_t t82 = (uint8_t)(t81 == ((uint8_t)0ULL));
                            if (!t82) {
                                goto gm_lb10;
                            }
                            uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                            gm_l15 = t83;
                            uint64_t t84 = gm_l15;
                            gm_tf5srh4mp4b3i t85;
                            GM_ST(uint8_t, (uint8_t *)&t85 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t85 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t85 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t85), t84);
                            ((uint8_t *)&t85)[8] = 1;
                            gm_l13 = t85;
                            goto gm_lb9;
                        }
gm_lb10:;
                        {
                            uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                            uint8_t t87 = (uint8_t)(t86 == ((uint8_t)1ULL));
                            if (!t87) {
                                goto gm_lb11;
                            }
                            gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l23) + 8));
                            gm_l16 = t88;
                            void * t89 = gm_l0;
                            void * t90 = GM_LD(void *, ((uint8_t *)t89));
                            void * t91 = (void *)(((uint8_t *)t90) + 8);
                            gm_t88rcucg1b7uk t92 = gm_l16;
                            gm_t88rcucg1b7uk t93 = gm_l12;
                            gm_tgglga8ro3isc t94;
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t94 + 0), t92);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t94 + 4), t93);
                            gm_t53at80pfdus3 t95;
                            memset(&t95, 0, sizeof t95);
                            GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)20ULL));
                            GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t95 + 8), t94);
                            gm_t88rcucg1b7uk t96 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t91, &t95);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l17 = t96;
                            void * t97 = gm_l0;
                            gm_t88rcucg1b7uk t98 = gm_l17;
                            gm_t88rcucg1b7uk t99 = gm_l12;
                            gm_b16_8 t100 = gm_l6;
                            gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
                            gm_tl874t2atnlh3 t102 = gm_l4;
                            void * t103 = gm_f_construct_param_array_btk60nl((void *)NULL, cx, NULL, t97, t98, t99, t101, t102);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t103;
                            goto gm_fret;
                        }
gm_lb11:;
                        {
                            uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                            uint8_t t105 = (uint8_t)(t104 == ((uint8_t)2ULL));
                            if (!t105) {
                                goto gm_lb12;
                            }
                            void * t106 = gm_l0;
                            void * t107 = GM_LD(void *, ((uint8_t *)t106));
                            gm_tl874t2atnlh3 t108 = gm_l4;
                            void * t109 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t107, t108);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t109;
                            goto gm_fret;
                        }
gm_lb12:;
                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 16, 9);
                    }
gm_lb9:;
                    goto gm_lb7;
                }
gm_lb8:;
            }
gm_lb7:;
            void * t110 = gm_l0;
            gm_tkn42u7na3t81 t111;
            memset(&t111, 0, sizeof t111);
            gm_tf5srh4mp4b3i t112 = gm_l13;
            gm_t88rcucg1b7uk t113 = gm_l12;
            gm_b16_8 t114 = gm_l6;
            gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
            gm_tl874t2atnlh3 t116 = gm_l4;
            void * t117 = gm_f_construct_array_qkofm98((void *)NULL, cx, NULL, t110, t111, t112, t113, t115, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t117;
            goto gm_fret;
        }
gm_lb2:;
        void * t118 = gm_l0;
        gm_tl874t2atnlh3 t119 = gm_l4;
        gm_b16_8 t120;
        GM_ST(void *, (uint8_t *)&t120, (void *)(gm_s3nknbqjesra1));
        GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(18));
        void * t121 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t118, t119, t120);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t121;
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
extern __attribute__((aligned(4))) void * gm_f_box_mut_dlc4mlv(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_construct_array_qkofm98(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1, gm_tf5srh4mp4b3i a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_construct_array_qkofm98 F;
    void * gm_l0; /* cx */
    gm_tkn42u7na3t81 gm_l1; /* ty */
    gm_tf5srh4mp4b3i gm_l2; /* len */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_t08olq97rscn3 gm_l7; /* n */
    uint64_t gm_l9; /* l */
    void * gm_l10; /* v */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t08olq97rscn3 gm_l14; /* n */
    uint64_t gm_l16; /* n */
    void * gm_l17; /* v */
    gm_t88rcucg1b7uk gm_l18; /* vt */
    gm_t88rcucg1b7uk gm_l19; /* target */
    void * gm_l20; /* c */
    void * gm_l21; /* c */
    uint8_t gm_l24; /* spreading */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    void * gm_l33; /* v */
    gm_b16_8 gm_l35; /* parts */
    gm_b16_8 gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    gm_tpouv9r7lrri2 gm_l38; /* p */
    uint64_t gm_l39; /* n */
    gm_t88rcucg1b7uk gm_l40; /* t */
    gm_t88rcucg1b7uk gm_l41; /* t */
    void * gm_l42; /* arr */
    GM_STACK_CHECK(&F, 3020u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    uint64_t gm_l43;
    gm_t88rcucg1b7uk gm_l44;
    gm_t88rcucg1b7uk gm_l45;
    uint8_t gm_l46;
    gm_t53at80pfdus3 gm_l47;
    uint8_t gm_l48;
    void * gm_l49;
    uint8_t gm_l50;
    void * gm_l51;
    gm_tabb710d9mq2e gm_l52;
    gm_tftb1odc739t5 gm_l53;
    uint8_t gm_l54;
    void * gm_l55;
    gm_tftb1odc739t5 gm_l56;
    gm_th2lqbo77pvsc gm_l57;
    gm_b16_8 gm_l58;
    uint8_t gm_l59;
    gm_tftb1odc739t5 gm_l60;
    gm_t88rcucg1b7uk gm_l61;
    gm_tabb710d9mq2e gm_l62;
    gm_t88rcucg1b7uk gm_l63;
    {
        void * t51 = gm_l0;
        void * t52 = GM_LD(void *, ((uint8_t *)t51));
        void * t53 = (void *)(((uint8_t *)t52) + 8);
        gm_l6 = t53;
        {
            {
                uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint8_t t55 = ((t54 == ((uint64_t)1ULL)));
                if (!t55) {
                    goto gm_lb2;
                }
                void * t56 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t57)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t57, gm_src5cfou479ljhn, 167, 42);
                uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t56) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                uint8_t t59 = (uint8_t)(t58 == ((uint8_t)1ULL));
                if (!t59) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&gm_l7), ((((uint8_t *)t56) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 32);
                memmove(((uint8_t *)&F.l8), (((((uint8_t *)t56) + (uintptr_t)((uint64_t)0ULL) * 144) + 8) + 32), 88);
                gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                gm_b16_8 t61;
                GM_ST(void *, (uint8_t *)&t61, (void *)(gm_spevct0q3cjgb));
                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(4));
                uint8_t t62 = (gm_eq0iqiubfm7e(((uint8_t *)&t60), ((uint8_t *)&t61)));
                if (!t62) {
                    goto gm_lb2;
                }
                uint8_t t63 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
                if (t63) {
                    uint64_t t64 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
                    gm_l43 = t64;
                } else {
                    void * t65 = gm_l0;
                    gm_tl874t2atnlh3 t66 = gm_l5;
                    gm_b16_8 t67;
                    GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sdjbnp04akbbb));
                    GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(49));
                    gm_b0_1 t68 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t65, t66, t67);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t69 = gm_l0;
                    void * t70 = GM_LD(void *, ((uint8_t *)t69));
                    gm_tl874t2atnlh3 t71 = gm_l5;
                    void * t72 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t70, t71);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t72;
                    goto gm_fret;
                }
                uint64_t t73 = gm_l43;
                gm_l9 = t73;
                void * t74 = gm_l0;
                void * t75 = (void *)((uint8_t *)&F.l8);
                gm_t88rcucg1b7uk t76 = gm_l3;
                void * t77 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t74, t75, t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t77;
                uint8_t t78 = (uint8_t)((((uint8_t *)&gm_l1))[4] != 0);
                if (t78) {
                    gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l1));
                    gm_l44 = t79;
                } else {
                    void * t80 = gm_l6;
                    uint64_t t81 = gm_l9;
                    gm_t88rcucg1b7uk t82 = gm_l3;
                    gm_t88rcucg1b7uk t83 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t80, t81, t82);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l44 = t83;
                }
                gm_t88rcucg1b7uk t84 = gm_l44;
                void * t85 = gm_l10;
                gm_tl874t2atnlh3 t86 = gm_l5;
                gm_t2b6tm8c0vgo1 t87;
                memset(&t87, 0, sizeof t87);
                GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)37ULL));
                GM_ST(void *, ((uint8_t *)&t87 + 8), t85);
                void * t88 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t84, &t87, t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t88;
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        gm_b16_8 t89 = gm_l4;
        gm_l11 = t89;
        gm_l12 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t90 = gm_l12;
                uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint8_t t92 = (t90 < t91);
                if (!t92) {
                    goto gm_lb3;
                }
                {
                    void * t93 = GM_LD(void *, ((uint8_t *)&gm_l11));
                    uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint64_t t95 = gm_l12;
                    {
                        {
                            memmove(((uint8_t *)&F.l13), (((uint8_t *)t93) + (uintptr_t)t95 * 144), 144);
                            {
                                {
                                    uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
                                    uint8_t t97 = (uint8_t)(t96 == ((uint8_t)1ULL));
                                    if (!t97) {
                                        goto gm_lb9;
                                    }
                                    memmove(((uint8_t *)&gm_l14), (((uint8_t *)&F.l13) + 8), 32);
                                    gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
                                    gm_b16_8 t99;
                                    GM_ST(void *, (uint8_t *)&t99, (void *)(gm_spevct0q3cjgb));
                                    GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(4));
                                    uint8_t t100 = (gm_eq0iqiubfm7e(((uint8_t *)&t98), ((uint8_t *)&t99)));
                                    if (!t100) {
                                        goto gm_lb9;
                                    }
                                    void * t101 = gm_l0;
                                    gm_tl874t2atnlh3 t102 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l13) + 128));
                                    gm_b16_8 t103;
                                    GM_ST(void *, (uint8_t *)&t103, (void *)(gm_soiet16j0e8ar));
                                    GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(33));
                                    gm_b0_1 t104 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t101, t102, t103);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t105 = gm_l0;
                                    void * t106 = GM_LD(void *, ((uint8_t *)t105));
                                    gm_tl874t2atnlh3 t107 = gm_l5;
                                    void * t108 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t106, t107);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t108;
                                    goto gm_fret;
                                }
gm_lb9:;
                            }
gm_lb8:;
                            goto gm_lb6;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 175, 5);
                    }
gm_lb6:;
                }
gm_lb5:;
                uint64_t t109 = gm_l12;
                uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
                uint64_t t112 = r110;
                gm_l12 = t112;
            }
        }
gm_lb3:;
    }
    {
        {
            {
                uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint8_t t114 = ((t113 == ((uint64_t)1ULL)));
                if (!t114) {
                    goto gm_lb11;
                }
                void * t115 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t116)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t116, gm_src5cfou479ljhn, 182, 44);
                uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t115) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                uint8_t t118 = (uint8_t)(t117 == ((uint8_t)0ULL));
                if (!t118) {
                    goto gm_lb11;
                }
                memmove(((uint8_t *)&F.l15), ((((uint8_t *)t115) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                uint8_t t119 = GM_LD(uint8_t, ((((uint8_t *)&F.l15) + 8) + 0));
                uint8_t t120 = (uint8_t)(!(t119 == ((uint8_t)5ULL)));
                if (!t120) {
                    goto gm_lb11;
                }
                gm_t2gtbh39ptekd t121 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)&F.l15) + 8));
                gm_t2gtbh39ptekd t122;
                memset(&t122, 0, sizeof t122);
                GM_ST(uint8_t, ((uint8_t *)&t122 + 0), ((uint8_t)5ULL));
                {
                    {
                        uint8_t t123 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
                        if (!t123) {
                            goto gm_lb13;
                        }
                        uint64_t t124 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
                        gm_l16 = t124;
                        void * t125 = gm_l0;
                        void * t126 = (void *)((uint8_t *)&F.l15);
                        gm_t88rcucg1b7uk t127 = gm_l3;
                        gm_tkn42u7na3t81 t128;
                        GM_ST(uint8_t, (uint8_t *)&t128 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t128 + 6, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t128), t127);
                        ((uint8_t *)&t128)[4] = 1;
                        void * t129 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t125, t126, t128);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l17 = t129;
                        void * t130 = gm_l0;
                        void * t131 = (void *)(((uint8_t *)t130) + 96);
                        void * t132 = gm_l0;
                        void * t133 = GM_LD(void *, ((uint8_t *)t132));
                        void * t134 = gm_l17;
                        gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t134));
                        gm_t88rcucg1b7uk t136 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t131, t133, t135);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l18 = t136;
                        uint8_t t137 = (uint8_t)((((uint8_t *)&gm_l1))[4] != 0);
                        if (t137) {
                            gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l1));
                            gm_l45 = t138;
                        } else {
                            void * t139 = gm_l6;
                            uint64_t t140 = gm_l16;
                            gm_t88rcucg1b7uk t141 = gm_l3;
                            gm_t88rcucg1b7uk t142 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t139, t140, t141);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l45 = t142;
                        }
                        gm_t88rcucg1b7uk t143 = gm_l45;
                        gm_l19 = t143;
                        {
                            {
                                void * t144 = gm_l6;
                                gm_t88rcucg1b7uk t145 = gm_l18;
                                gm_f_kind_jcnt4hm((void *)&gm_l47, t144, GM_CONV(gm_csguankko4lmhd, t145));
                                {
                                    {
                                        uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l47) + 0));
                                        uint8_t t147 = (uint8_t)(t146 == ((uint8_t)11ULL));
                                        if (!t147) {
                                            goto gm_lb17;
                                        }
                                        gm_l46 = ((uint8_t)1);
                                        goto gm_lb16;
                                    }
gm_lb17:;
                                    {
                                        gm_l46 = ((uint8_t)0);
                                        goto gm_lb16;
                                    }
gm_lb18:;
                                    gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 187, 16);
                                }
gm_lb16:;
                                uint8_t t148 = gm_l46;
                                if (!t148) {
                                    goto gm_lb15;
                                }
                                gm_t88rcucg1b7uk t149 = gm_l18;
                                gm_t88rcucg1b7uk t150 = gm_l3;
                                uint8_t t151 = (!gm_eq9m6mvtmgde(((uint8_t *)&t149), ((uint8_t *)&t150)));
                                if (!t151) {
                                    goto gm_lb15;
                                }
                                void * t152 = gm_l6;
                                gm_t88rcucg1b7uk t153 = gm_l18;
                                uint8_t t154 = gm_f_is_vector_gkq6b88(t152, GM_CONV(gm_csguankko4lmhd, t153));
                                if (t154) {
                                    gm_l48 = ((uint8_t)1);
                                } else {
                                    void * t155 = gm_l6;
                                    gm_t88rcucg1b7uk t156 = gm_l19;
                                    uint8_t t157 = gm_f_is_vector_gkq6b88(t155, GM_CONV(gm_csguankko4lmhd, t156));
                                    gm_l48 = t157;
                                }
                                uint8_t t158 = gm_l48;
                                if (t158) {
                                    {
                                        {
                                            void * t159 = gm_l0;
                                            gm_t88rcucg1b7uk t160 = gm_l19;
                                            void * t161 = gm_l17;
                                            gm_tl874t2atnlh3 t162 = gm_l5;
                                            void * t163 = gm_f_vector_conversion_32a9cuf((void *)NULL, cx, NULL, t159, t160, t161, t162);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l49 = t163;
                                            uint8_t t164 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l49)) != 0);
                                            if (!t164) {
                                                goto gm_lb20;
                                            }
                                            void * t165 = GM_LD(void *, ((uint8_t *)&gm_l49));
                                            gm_l20 = t165;
                                            void * t166 = gm_l20;
                                            F.ret = t166;
                                            goto gm_fret;
                                        }
gm_lb20:;
                                    }
gm_lb19:;
                                }
                                void * t167 = gm_l6;
                                gm_t88rcucg1b7uk t168 = gm_l18;
                                uint8_t t169 = gm_f_is_math_vector_09mtuao(t167, GM_CONV(gm_csguankko4lmhd, t168));
                                if (t169) {
                                    gm_l50 = ((uint8_t)1);
                                } else {
                                    void * t170 = gm_l6;
                                    gm_t88rcucg1b7uk t171 = gm_l19;
                                    uint8_t t172 = gm_f_is_math_vector_09mtuao(t170, GM_CONV(gm_csguankko4lmhd, t171));
                                    gm_l50 = t172;
                                }
                                uint8_t t173 = gm_l50;
                                if (t173) {
                                    {
                                        {
                                            void * t174 = gm_l0;
                                            gm_t88rcucg1b7uk t175 = gm_l19;
                                            void * t176 = gm_l17;
                                            gm_tl874t2atnlh3 t177 = gm_l5;
                                            void * t178 = gm_f_math_vector_conversion_0cttjm0((void *)NULL, cx, NULL, t174, t175, t176, t177);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l51 = t178;
                                            uint8_t t179 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l51)) != 0);
                                            if (!t179) {
                                                goto gm_lb22;
                                            }
                                            void * t180 = GM_LD(void *, ((uint8_t *)&gm_l51));
                                            gm_l21 = t180;
                                            void * t181 = gm_l21;
                                            F.ret = t181;
                                            goto gm_fret;
                                        }
gm_lb22:;
                                    }
gm_lb21:;
                                }
                                goto gm_lb14;
                            }
gm_lb15:;
                        }
gm_lb14:;
                        void * t182 = gm_l0;
                        void * t183 = gm_l17;
                        gm_t88rcucg1b7uk t184 = gm_l3;
                        void * t185 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t182, t183, t184);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l17 = t185;
                        uint64_t t186 = gm_l16;
                        uint8_t t187 = (!(t186 == ((uint64_t)1ULL)));
                        if (t187) {
                            void * t188 = gm_l0;
                            gm_tl874t2atnlh3 t189 = gm_l5;
                            void * t190 = gm_l0;
                            gm_t88rcucg1b7uk t191 = gm_l19;
                            gm_b16_8 t192 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t190, t191);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t193 = gm_l16;
                            gm_t3877pk7lv748 t194;
                            GM_ST(gm_b16_8, ((uint8_t *)&t194 + 0), t192);
                            GM_ST(uint64_t, ((uint8_t *)&t194 + 16), t193);
                            gm_f_format_h75o0bg((void *)&gm_l52, (void *)NULL, cx, NULL, &t194);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t195 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                            if (!t195) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 8, gm_src5cfou479ljhn, 197, 32);
                            gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l52) + 8));
                            gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
                            gm_b0_1 t198 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t188, t189, t197);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t199 = gm_l0;
                            void * t200 = GM_LD(void *, ((uint8_t *)t199));
                            gm_tl874t2atnlh3 t201 = gm_l5;
                            void * t202 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t200, t201);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t202;
                            goto gm_fret;
                        }
                        gm_trqfvmjtt4sij t203;
                        gm_f_list_vp1djv1_2((void *)&t203, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l22 = t203;
                        void * t204 = (void *)((uint8_t *)&F.l22);
                        void * t205 = gm_l17;
                        gm_tftb1odc739t5 t206 = gm_f_push_d943vgp_2(t204, &(*(gm_tcgrjq9toscke *)((uint8_t *)t205)));
                        gm_l53 = t206;
                        uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                        if (!t207) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_src5cfou479ljhn, 201, 13);
                        gm_t88rcucg1b7uk t208 = gm_l19;
                        gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
                        gm_tl874t2atnlh3 t210 = gm_l5;
                        gm_t2b6tm8c0vgo1 t211;
                        memset(&t211, 0, sizeof t211);
                        GM_ST(uint8_t, ((uint8_t *)&t211 + 0), ((uint8_t)36ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t211 + 8), t209);
                        void * t212 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t208, &t211, t210);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t212;
                        goto gm_fret;
                    }
gm_lb13:;
                }
gm_lb12:;
                goto gm_lb10;
            }
gm_lb11:;
        }
gm_lb10:;
    }
    {
        gm_trqfvmjtt4sij t213;
        gm_f_list_vp1djv1_2((void *)&t213, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l23 = t213;
        gm_l24 = ((uint8_t)0);
        gm_b16_8 t214 = gm_l4;
        gm_l25 = t214;
        gm_l26 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t215 = gm_l26;
                uint64_t t216 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                uint8_t t217 = (t215 < t216);
                if (!t217) {
                    goto gm_lb23;
                }
                {
                    void * t218 = GM_LD(void *, ((uint8_t *)&gm_l25));
                    uint64_t t219 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                    uint64_t t220 = gm_l26;
                    {
                        {
                            memmove(((uint8_t *)&F.l27), (((uint8_t *)t218) + (uintptr_t)t220 * 144), 144);
                            {
                                {
                                    uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)&F.l27) + 0));
                                    uint8_t t222 = (uint8_t)(t221 == ((uint8_t)2ULL));
                                    if (!t222) {
                                        goto gm_lb29;
                                    }
                                    gm_l54 = ((uint8_t)1);
                                    goto gm_lb28;
                                }
gm_lb29:;
                                {
                                    gm_l54 = ((uint8_t)0);
                                    goto gm_lb28;
                                }
gm_lb30:;
                                gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 210, 12);
                            }
gm_lb28:;
                            uint8_t t223 = gm_l54;
                            if (t223) {
                                gm_l24 = ((uint8_t)1);
                            }
                            goto gm_lb26;
                        }
gm_lb27:;
                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 209, 5);
                    }
gm_lb26:;
                }
gm_lb25:;
                uint64_t t224 = gm_l26;
                uint64_t r225; bool o226 = __builtin_add_overflow(t224, ((uint64_t)1ULL), &r225);
                uint64_t t227 = r225;
                gm_l26 = t227;
            }
        }
gm_lb23:;
        gm_trqfvmjtt4sij t228;
        gm_f_list_ir0h3tc_2((void *)&t228, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l28 = t228;
        gm_b16_8 t229 = gm_l4;
        gm_l29 = t229;
        gm_l30 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t230 = gm_l30;
                uint64_t t231 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                uint8_t t232 = (t230 < t231);
                if (!t232) {
                    goto gm_lb31;
                }
                {
                    void * t233 = GM_LD(void *, ((uint8_t *)&gm_l29));
                    uint64_t t234 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                    uint64_t t235 = gm_l30;
                    {
                        {
                            memmove(((uint8_t *)&F.l31), (((uint8_t *)t233) + (uintptr_t)t235 * 144), 144);
                            {
                                {
                                    uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)&F.l31) + 0));
                                    uint8_t t237 = (uint8_t)(t236 == ((uint8_t)0ULL));
                                    if (!t237) {
                                        goto gm_lb37;
                                    }
                                    memmove(((uint8_t *)&F.l32), (((uint8_t *)&F.l31) + 8), 88);
                                    void * t238 = gm_l0;
                                    void * t239 = (void *)((uint8_t *)&F.l32);
                                    gm_t88rcucg1b7uk t240 = gm_l3;
                                    void * t241 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t238, t239, t240);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l33 = t241;
                                    void * t242 = (void *)((uint8_t *)&F.l23);
                                    uint8_t t243 = gm_l24;
                                    if (t243) {
                                        void * t244 = gm_l0;
                                        void * t245 = gm_l33;
                                        void * t246 = (void *)((uint8_t *)&F.l28);
                                        void * t247 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t244, t245, t246);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l55 = t247;
                                    } else {
                                        void * t248 = gm_l33;
                                        gm_l55 = t248;
                                    }
                                    void * t249 = gm_l55;
                                    gm_tftb1odc739t5 t250 = gm_f_push_d943vgp_2(t242, &(*(gm_tcgrjq9toscke *)((uint8_t *)t249)));
                                    gm_l56 = t250;
                                    uint8_t t251 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                    if (!t251) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 1, gm_src5cfou479ljhn, 217, 17);
                                    goto gm_lb36;
                                }
gm_lb37:;
                                {
                                    uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&F.l31) + 0));
                                    uint8_t t253 = (uint8_t)(t252 == ((uint8_t)2ULL));
                                    if (!t253) {
                                        goto gm_lb38;
                                    }
                                    memmove(((uint8_t *)&F.l34), (((uint8_t *)&F.l31) + 8), 88);
                                    void * t254 = gm_l0;
                                    void * t255 = (void *)((uint8_t *)&F.l34);
                                    void * t256 = (void *)((uint8_t *)&F.l28);
                                    gm_f_spread_parts_e62q2l2((void *)&gm_l57, (void *)NULL, cx, NULL, t254, t255, t256);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t257 = (uint8_t)((((uint8_t *)&gm_l57))[16] != 0);
                                    if (t257) {
                                        gm_b16_8 t258 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l57));
                                        gm_l58 = t258;
                                    } else {
                                        void * t259 = gm_l0;
                                        void * t260 = GM_LD(void *, ((uint8_t *)t259));
                                        gm_tl874t2atnlh3 t261 = gm_l5;
                                        void * t262 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t260, t261);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.ret = t262;
                                        goto gm_fret;
                                    }
                                    gm_b16_8 t263 = gm_l58;
                                    gm_l35 = t263;
                                    gm_b16_8 t264 = gm_l35;
                                    gm_l36 = t264;
                                    gm_l37 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t265 = gm_l37;
                                            uint64_t t266 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                                            uint8_t t267 = (t265 < t266);
                                            if (!t267) {
                                                goto gm_lb39;
                                            }
                                            {
                                                void * t268 = GM_LD(void *, ((uint8_t *)&gm_l36));
                                                uint64_t t269 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                                                uint64_t t270 = gm_l37;
                                                {
                                                    {
                                                        memmove(((uint8_t *)&gm_l38), (((uint8_t *)t268) + (uintptr_t)t270 * 32), 32);
                                                        {
                                                            {
                                                                uint8_t t271 = (uint8_t)((((uint8_t *)&gm_l38))[16] != 0);
                                                                if (t271) {
                                                                    goto gm_lb45;
                                                                }
                                                                gm_l59 = ((uint8_t)0);
                                                                goto gm_lb44;
                                                            }
gm_lb45:;
                                                            {
                                                                gm_l59 = ((uint8_t)1);
                                                                goto gm_lb44;
                                                            }
gm_lb46:;
                                                            gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 222, 24);
                                                        }
gm_lb44:;
                                                        uint8_t t272 = gm_l59;
                                                        if (t272) {
                                                            void * t273 = gm_l0;
                                                            gm_tl874t2atnlh3 t274 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l34) + 72));
                                                            gm_b16_8 t275;
                                                            GM_ST(void *, (uint8_t *)&t275, (void *)(gm_sqof8dgnasnqk));
                                                            GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(68));
                                                            gm_b0_1 t276 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t273, t274, t275);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            void * t277 = gm_l0;
                                                            void * t278 = GM_LD(void *, ((uint8_t *)t277));
                                                            gm_tl874t2atnlh3 t279 = gm_l5;
                                                            void * t280 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t278, t279);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            F.ret = t280;
                                                            goto gm_fret;
                                                        }
                                                        void * t281 = (void *)((uint8_t *)&F.l23);
                                                        void * t282 = gm_l0;
                                                        void * t283 = GM_LD(void *, (((uint8_t *)&gm_l38) + 24));
                                                        gm_t88rcucg1b7uk t284 = gm_l3;
                                                        void * t285 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t282, t283, t284);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_tftb1odc739t5 t286 = gm_f_push_d943vgp_2(t281, &(*(gm_tcgrjq9toscke *)((uint8_t *)t285)));
                                                        gm_l60 = t286;
                                                        uint8_t t287 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                                        if (!t287) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 1, gm_src5cfou479ljhn, 226, 21);
                                                        goto gm_lb42;
                                                    }
gm_lb43:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 221, 17);
                                                }
gm_lb42:;
                                            }
gm_lb41:;
                                            uint64_t t288 = gm_l37;
                                            uint64_t r289; bool o290 = __builtin_add_overflow(t288, ((uint64_t)1ULL), &r289);
                                            uint64_t t291 = r289;
                                            gm_l37 = t291;
                                        }
                                    }
gm_lb39:;
                                    goto gm_lb36;
                                }
gm_lb38:;
                                {
                                    void * t292 = gm_l0;
                                    gm_tl874t2atnlh3 t293 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l31) + 128));
                                    gm_b16_8 t294;
                                    GM_ST(void *, (uint8_t *)&t294, (void *)(gm_sqirjnd2segia));
                                    GM_ST(uintptr_t, (uint8_t *)&t294 + 8, (uintptr_t)(39));
                                    gm_b0_1 t295 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t292, t293, t294);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb36;
                                }
gm_lb47:;
                                gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 214, 9);
                            }
gm_lb36:;
                            goto gm_lb34;
                        }
gm_lb35:;
                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 213, 5);
                    }
gm_lb34:;
                }
gm_lb33:;
                uint64_t t296 = gm_l30;
                uint64_t r297; bool o298 = __builtin_add_overflow(t296, ((uint64_t)1ULL), &r297);
                uint64_t t299 = r297;
                gm_l30 = t299;
            }
        }
gm_lb31:;
    }
    {
        {
            {
                uint8_t t300 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
                if (!t300) {
                    goto gm_lb49;
                }
                uint64_t t301 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
                gm_l39 = t301;
                uint64_t t302 = gm_l39;
                uint64_t t303 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 8));
                uint64_t t304 = (uint64_t)t303;
                uint8_t t305 = (!(t302 == t304));
                if (!t305) {
                    goto gm_lb49;
                }
                uint8_t t306 = (uint8_t)((((uint8_t *)&gm_l1))[4] != 0);
                if (t306) {
                    gm_t88rcucg1b7uk t307 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l1));
                    gm_l61 = t307;
                } else {
                    void * t308 = gm_l6;
                    uint64_t t309 = gm_l39;
                    gm_t88rcucg1b7uk t310 = gm_l3;
                    gm_t88rcucg1b7uk t311 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t308, t309, t310);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l61 = t311;
                }
                gm_t88rcucg1b7uk t312 = gm_l61;
                gm_l40 = t312;
                void * t313 = gm_l0;
                gm_tl874t2atnlh3 t314 = gm_l5;
                void * t315 = gm_l0;
                gm_t88rcucg1b7uk t316 = gm_l40;
                gm_b16_8 t317 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t315, t316);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t318 = gm_l39;
                uint64_t t319 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 8));
                gm_b16_8 t320 = gm_f_given_27f1vc1((void *)NULL, cx, NULL, t319);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tq3m5qgvkpje8 t321;
                GM_ST(gm_b16_8, ((uint8_t *)&t321 + 0), t317);
                GM_ST(uint64_t, ((uint8_t *)&t321 + 16), t318);
                GM_ST(gm_b16_8, ((uint8_t *)&t321 + 24), t320);
                gm_f_format_blrf705((void *)&gm_l62, (void *)NULL, cx, NULL, &t321);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t322 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                if (!t322) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 8, gm_src5cfou479ljhn, 234, 24);
                gm_b16_8 t323 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l62) + 8));
                gm_b16_8 t324 = GM_LD(gm_b16_8, ((uint8_t *)&t323));
                gm_b0_1 t325 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t313, t314, t324);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t326 = gm_l0;
                void * t327 = GM_LD(void *, ((uint8_t *)t326));
                gm_tl874t2atnlh3 t328 = gm_l5;
                void * t329 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t327, t328);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t329;
                goto gm_fret;
            }
gm_lb49:;
        }
gm_lb48:;
        uint8_t t330 = (uint8_t)((((uint8_t *)&gm_l1))[4] != 0);
        if (t330) {
            gm_t88rcucg1b7uk t331 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l1));
            gm_l63 = t331;
        } else {
            void * t332 = gm_l6;
            uint64_t t333 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 8));
            uint64_t t334 = (uint64_t)t333;
            gm_t88rcucg1b7uk t335 = gm_l3;
            gm_t88rcucg1b7uk t336 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t332, t334, t335);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l63 = t336;
        }
        gm_t88rcucg1b7uk t337 = gm_l63;
        gm_l41 = t337;
        gm_t88rcucg1b7uk t338 = gm_l41;
        gm_b16_8 t339 = GM_LD(gm_b16_8, ((uint8_t *)&F.l23));
        gm_tl874t2atnlh3 t340 = gm_l5;
        gm_t2b6tm8c0vgo1 t341;
        memset(&t341, 0, sizeof t341);
        GM_ST(uint8_t, ((uint8_t *)&t341 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t341 + 8), t339);
        void * t342 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t338, &t341, t340);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l42 = t342;
        uint64_t t343 = GM_LD(uint64_t, (((uint8_t *)&F.l28) + 8));
        uint8_t t344 = ((t343 == ((uint64_t)0ULL)));
        if (t344) {
            void * t345 = gm_l42;
            F.ret = t345;
            goto gm_fret;
        }
    }
    {
        gm_t88rcucg1b7uk t346 = gm_l41;
        gm_b16_8 t347 = GM_LD(gm_b16_8, ((uint8_t *)&F.l28));
        void * t348 = gm_l42;
        gm_tl874t2atnlh3 t349 = gm_l5;
        void * t350 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t346, t347, t348, t349);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t350;
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
__attribute__((aligned(4))) void * gm_f_construct_enum_array_d7cisuu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_construct_enum_array_d7cisuu F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_t88rcucg1b7uk gm_l2; /* index */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_tep2drsl9408b gm_l7; /* uv */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t08olq97rscn3 gm_l11; /* n */
    void * gm_l13; /* v */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint8_t gm_l19; /* bad_args */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_t08olq97rscn3 gm_l23; /* name */
    gm_b16_8 gm_l25; /* fill */
    uint64_t gm_l26; /* i */
    void * gm_l27; /* v */
    gm_b16_8 gm_l28; /* first */
    uint8_t gm_l29; /* out_of_order */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* k */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* w */
    void * gm_l36; /* v */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* i */
    gm_b16_8 gm_l41; /* n */
    gm_b16_8 gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    void * gm_l45; /* s */
    void * gm_l46; /* arr */
    GM_STACK_CHECK(&F, 3317u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tdlpv0q69lov1 gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tf5srh4mp4b3i gm_l50;
    uint64_t gm_l51;
    gm_b16_8 gm_l52;
    gm_tabb710d9mq2e gm_l53;
    uint8_t gm_l54;
    gm_tabb710d9mq2e gm_l55;
    gm_tftb1odc739t5 gm_l56;
    gm_b16_8 gm_l57;
    gm_tabb710d9mq2e gm_l58;
    gm_tf1bjg81u0glv gm_l59;
    uint8_t gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_tabb710d9mq2e gm_l62;
    gm_tftb1odc739t5 gm_l63;
    gm_tabb710d9mq2e gm_l64;
    void * gm_l65;
    gm_tftb1odc739t5 gm_l66;
    {
        gm_trqfvmjtt4sij t54;
        gm_f_list_epvducg((void *)&t54, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t54;
        {
            {
                void * t55 = gm_l0;
                void * t56 = GM_LD(void *, ((uint8_t *)t55));
                gm_t88rcucg1b7uk t57 = gm_l2;
                gm_f_union_variants_a7khaqp((void *)&gm_l47, t56, GM_CONV(gm_csguankko4lmhd, t57));
                uint8_t t58 = (uint8_t)((((uint8_t *)&gm_l47))[24] != 0);
                if (!t58) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&gm_l7), ((uint8_t *)&gm_l47), 24);
                gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                gm_l8 = t59;
                gm_l9 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t60 = gm_l9;
                        uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint8_t t62 = (t60 < t61);
                        if (!t62) {
                            goto gm_lb3;
                        }
                        {
                            void * t63 = GM_LD(void *, ((uint8_t *)&gm_l8));
                            uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            uint64_t t65 = gm_l9;
                            {
                                {
                                    memmove(((uint8_t *)&F.l10), (((uint8_t *)t63) + (uintptr_t)t65 * 72), 72);
                                    void * t66 = (void *)((uint8_t *)&F.l6);
                                    gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                    gm_tftb1odc739t5 t68 = gm_f_push_3ef746b(t66, t67);
                                    gm_l48 = t68;
                                    uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                    if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_src5cfou479ljhn, 67, 32);
                                    goto gm_lb6;
                                }
gm_lb7:;
                                gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 67, 9);
                            }
gm_lb6:;
                        }
gm_lb5:;
                        uint64_t t70 = gm_l9;
                        uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                        uint64_t t73 = r71;
                        gm_l9 = t73;
                    }
                }
gm_lb3:;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        {
            {
                uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint8_t t75 = ((t74 == ((uint64_t)1ULL)));
                if (!t75) {
                    goto gm_lb9;
                }
                void * t76 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t77)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t77, gm_src5cfou479ljhn, 69, 42);
                uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)t76) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                uint8_t t79 = (uint8_t)(t78 == ((uint8_t)1ULL));
                if (!t79) {
                    goto gm_lb9;
                }
                memmove(((uint8_t *)&gm_l11), ((((uint8_t *)t76) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 32);
                memmove(((uint8_t *)&F.l12), (((((uint8_t *)t76) + (uintptr_t)((uint64_t)0ULL) * 144) + 8) + 32), 88);
                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                gm_b16_8 t81;
                GM_ST(void *, (uint8_t *)&t81, (void *)(gm_spevct0q3cjgb));
                GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(4));
                uint8_t t82 = (gm_eq0iqiubfm7e(((uint8_t *)&t80), ((uint8_t *)&t81)));
                if (!t82) {
                    goto gm_lb9;
                }
                gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                uint8_t t85 = 0;
                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t84) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t84))) + i * 16, ((uint8_t *)&t83))) { t85 = 1; break; }
                uint8_t t86 = (uint8_t)(!t85);
                if (!t86) {
                    goto gm_lb9;
                }
                void * t87 = gm_l0;
                void * t88 = (void *)((uint8_t *)&F.l12);
                gm_t88rcucg1b7uk t89 = gm_l3;
                void * t90 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t87, t88, t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t90;
                gm_t88rcucg1b7uk t91 = gm_l1;
                void * t92 = gm_l13;
                gm_tl874t2atnlh3 t93 = gm_l5;
                gm_t2b6tm8c0vgo1 t94;
                memset(&t94, 0, sizeof t94);
                GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)37ULL));
                GM_ST(void *, ((uint8_t *)&t94 + 8), t92);
                void * t95 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t91, &t94, t93);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t95;
                goto gm_fret;
            }
gm_lb9:;
        }
gm_lb8:;
    }
    {
        gm_trqfvmjtt4sij t96;
        gm_f_list_ir0h3tc_2((void *)&t96, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l14 = t96;
        gm_trqfvmjtt4sij t97;
        gm_f_list_pmp06kq((void *)&t97, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l15 = t97;
        gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_l16 = t98;
        gm_l17 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t99 = gm_l17;
                uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                uint8_t t101 = (t99 < t100);
                if (!t101) {
                    goto gm_lb10;
                }
                {
                    void * t102 = GM_LD(void *, ((uint8_t *)&gm_l16));
                    uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint64_t t104 = gm_l17;
                    {
                        {
                            void * t105 = (void *)((uint8_t *)&F.l15);
                            gm_tftb1odc739t5 t106 = gm_f_push_r2k6rjf(t105, ((void *)0));
                            gm_l49 = t106;
                            uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                            if (!t107) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_src5cfou479ljhn, 75, 28);
                            goto gm_lb13;
                        }
gm_lb14:;
                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 75, 5);
                    }
gm_lb13:;
                }
gm_lb12:;
                uint64_t t108 = gm_l17;
                uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                uint64_t t111 = r109;
                gm_l17 = t111;
            }
        }
gm_lb10:;
        gm_trqfvmjtt4sij t112;
        gm_f_list_5dhgg4i((void *)&t112, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l18 = t112;
        gm_l19 = ((uint8_t)0);
        gm_b16_8 t113 = gm_l4;
        gm_l20 = t113;
        gm_l21 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t114 = gm_l21;
                uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                uint8_t t116 = (t114 < t115);
                if (!t116) {
                    goto gm_lb15;
                }
                {
                    void * t117 = GM_LD(void *, ((uint8_t *)&gm_l20));
                    uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                    uint64_t t119 = gm_l21;
                    {
                        {
                            memmove(((uint8_t *)&F.l22), (((uint8_t *)t117) + (uintptr_t)t119 * 144), 144);
                            {
                                {
                                    uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
                                    uint8_t t121 = (uint8_t)(t120 == ((uint8_t)1ULL));
                                    if (!t121) {
                                        goto gm_lb21;
                                    }
                                    memmove(((uint8_t *)&gm_l23), (((uint8_t *)&F.l22) + 8), 32);
                                    memmove(((uint8_t *)&F.l24), ((((uint8_t *)&F.l22) + 8) + 32), 88);
                                    gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                                    gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
                                    gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                                    gm_tf5srh4mp4b3i t125 = gm_f_index_of_o9r57ha(t123, t124);
                                    gm_l50 = t125;
                                    uint8_t t126 = (uint8_t)((((uint8_t *)&gm_l50))[8] != 0);
                                    if (t126) {
                                        uint64_t t127 = GM_LD(uint64_t, ((uint8_t *)&gm_l50));
                                        gm_l51 = t127;
                                    } else {
                                        gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                                        gm_b16_8 t129;
                                        GM_ST(void *, (uint8_t *)&t129, (void *)(gm_spevct0q3cjgb));
                                        GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(4));
                                        uint8_t t130 = (gm_eq0iqiubfm7e(((uint8_t *)&t128), ((uint8_t *)&t129)));
                                        if (t130) {
                                            gm_b16_8 t131;
                                            GM_ST(void *, (uint8_t *)&t131, (void *)(gm_sksrclikobn5n));
                                            GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(36));
                                            gm_l52 = t131;
                                        } else {
                                            gm_b16_8 t132;
                                            GM_ST(void *, (uint8_t *)&t132, (void *)(gm_sr4qbrk6tlgaa));
                                            GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(0));
                                            gm_l52 = t132;
                                        }
                                        gm_b16_8 t133 = gm_l52;
                                        gm_l25 = t133;
                                        void * t134 = gm_l0;
                                        gm_tl874t2atnlh3 t135 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l23) + 16));
                                        void * t136 = gm_l0;
                                        gm_t88rcucg1b7uk t137 = gm_l2;
                                        gm_b16_8 t138 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t136, t137);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                                        gm_b16_8 t140 = gm_l25;
                                        gm_tfeab279c16rr t141;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t141 + 0), t138);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t141 + 16), t139);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t141 + 32), t140);
                                        gm_f_format_i7ngra8((void *)&gm_l53, (void *)NULL, cx, NULL, &t141);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                                        if (!t142) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 8, gm_src5cfou479ljhn, 83, 41);
                                        gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l53) + 8));
                                        gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                                        gm_b0_1 t145 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t134, t135, t144);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t146 = gm_l0;
                                        void * t147 = (void *)((uint8_t *)&F.l24);
                                        gm_tkn42u7na3t81 t148;
                                        memset(&t148, 0, sizeof t148);
                                        void * t149 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t146, t147, t148);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l19 = ((uint8_t)1);
                                        goto gm_lb17;
                                    }
                                    uint64_t t150 = gm_l51;
                                    gm_l26 = t150;
                                    void * t151 = GM_LD(void *, ((uint8_t *)&F.l15));
                                    uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 8));
                                    uint64_t t153 = gm_l26;
                                    if ((uintptr_t)t153 >= (uintptr_t)(t152)) gm_trap_index((uintptr_t)t153, t152, gm_src5cfou479ljhn, 88, 20);
                                    {
                                        {
                                            uint8_t t154 = (uint8_t)(GM_LD(void *, (((uint8_t *)t151) + (uintptr_t)t153 * 8)) != 0);
                                            if (t154) {
                                                goto gm_lb23;
                                            }
                                            gm_l54 = ((uint8_t)0);
                                            goto gm_lb22;
                                        }
gm_lb23:;
                                        {
                                            gm_l54 = ((uint8_t)1);
                                            goto gm_lb22;
                                        }
gm_lb24:;
                                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 88, 20);
                                    }
gm_lb22:;
                                    uint8_t t155 = gm_l54;
                                    if (t155) {
                                        void * t156 = gm_l0;
                                        gm_tl874t2atnlh3 t157 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l23) + 16));
                                        gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                                        gm_tnpifv20irnph t159;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t159 + 0), t158);
                                        gm_f_format_47t5q9n((void *)&gm_l55, (void *)NULL, cx, NULL, t159);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t160 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                                        if (!t160) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 8, gm_src5cfou479ljhn, 88, 69);
                                        gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 8));
                                        gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
                                        gm_b0_1 t163 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t156, t157, t162);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    void * t164 = gm_l0;
                                    void * t165 = (void *)((uint8_t *)&F.l24);
                                    gm_t88rcucg1b7uk t166 = gm_l3;
                                    void * t167 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t164, t165, t166);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l27 = t167;
                                    void * t168 = (void *)((uint8_t *)&F.l18);
                                    uint64_t t169 = gm_l26;
                                    gm_tftb1odc739t5 t170 = gm_f_push_ucgmlo2(t168, t169);
                                    gm_l56 = t170;
                                    uint8_t t171 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                    if (!t171) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 1, gm_src5cfou479ljhn, 90, 17);
                                    void * t172 = GM_LD(void *, ((uint8_t *)&F.l15));
                                    uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 8));
                                    uint64_t t174 = gm_l26;
                                    if ((uintptr_t)t174 >= (uintptr_t)(t173)) gm_trap_index((uintptr_t)t174, t173, gm_src5cfou479ljhn, 91, 17);
                                    void * t175 = gm_l27;
                                    GM_ST(void *, (((uint8_t *)t172) + (uintptr_t)t174 * 8), t175);
                                    goto gm_lb20;
                                }
gm_lb21:;
                                {
                                    uint8_t t176 = gm_l19;
                                    uint8_t t177 = (uint8_t)!t176;
                                    if (t177) {
                                        uint64_t t178 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                                        uint8_t t179 = (t178 > ((uint64_t)0ULL));
                                        if (t179) {
                                            void * t180 = GM_LD(void *, ((uint8_t *)&F.l6));
                                            uint64_t t181 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t181)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t181, gm_src5cfou479ljhn, 95, 55);
                                            gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)t180) + (uintptr_t)((uint64_t)0ULL) * 16));
                                            gm_l57 = t182;
                                        } else {
                                            gm_b16_8 t183;
                                            GM_ST(void *, (uint8_t *)&t183, (void *)(gm_suv2tallru6a1));
                                            GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)(1));
                                            gm_l57 = t183;
                                        }
                                        gm_b16_8 t184 = gm_l57;
                                        gm_l28 = t184;
                                        void * t185 = gm_l0;
                                        gm_tl874t2atnlh3 t186 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l22) + 128));
                                        void * t187 = gm_l0;
                                        gm_t88rcucg1b7uk t188 = gm_l1;
                                        gm_b16_8 t189 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t187, t188);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t190 = gm_l0;
                                        gm_t88rcucg1b7uk t191 = gm_l2;
                                        gm_b16_8 t192 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t190, t191);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t193 = gm_l0;
                                        gm_t88rcucg1b7uk t194 = gm_l1;
                                        gm_b16_8 t195 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t193, t194);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t196 = gm_l28;
                                        gm_t0uuhaljsf410 t197;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t197 + 0), t189);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t197 + 16), t192);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t197 + 32), t195);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t197 + 48), t196);
                                        gm_f_format_d0bas9o((void *)&gm_l58, (void *)NULL, cx, NULL, &t197);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t198 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                        if (!t198) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 8, gm_src5cfou479ljhn, 96, 38);
                                        gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
                                        gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
                                        gm_b0_1 t201 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t185, t186, t200);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    gm_l19 = ((uint8_t)1);
                                    goto gm_lb20;
                                }
gm_lb25:;
                                gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 79, 9);
                            }
gm_lb20:;
                            goto gm_lb18;
                        }
gm_lb19:;
                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 78, 5);
                    }
gm_lb18:;
                }
gm_lb17:;
                uint64_t t202 = gm_l21;
                uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)1ULL), &r203);
                uint64_t t205 = r203;
                gm_l21 = t205;
            }
        }
gm_lb15:;
    }
    {
        gm_l29 = ((uint8_t)0);
        gm_l31 = ((uint64_t)1ULL);
        uint64_t t206 = GM_LD(uint64_t, (((uint8_t *)&F.l18) + 8));
        uint64_t t207 = (t206 > ((uint64_t)1ULL) ? t206 : ((uint64_t)1ULL));
        gm_l30 = t207;
        {
            for (;;) {
                uint64_t t208 = gm_l31;
                uint64_t t209 = gm_l30;
                uint8_t t210 = (t208 < t209);
                if (!t210) {
                    goto gm_lb26;
                }
                {
                    {
                        {
                            uint64_t t211 = gm_l31;
                            gm_l32 = t211;
                            void * t212 = GM_LD(void *, ((uint8_t *)&F.l18));
                            uint64_t t213 = GM_LD(uint64_t, (((uint8_t *)&F.l18) + 8));
                            uint64_t t214 = gm_l32;
                            uint64_t r215; bool o216 = __builtin_sub_overflow(t214, ((uint64_t)1ULL), &r215);
                            if (o216) gm_trap("integer overflow in `-`", gm_src5cfou479ljhn, 104, 26);
                            uint64_t t217 = r215;
                            if ((uintptr_t)t217 >= (uintptr_t)(t213)) gm_trap_index((uintptr_t)t217, t213, gm_src5cfou479ljhn, 104, 12);
                            uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)t212) + (uintptr_t)t217 * 8));
                            void * t219 = GM_LD(void *, ((uint8_t *)&F.l18));
                            uint64_t t220 = GM_LD(uint64_t, (((uint8_t *)&F.l18) + 8));
                            uint64_t t221 = gm_l32;
                            if ((uintptr_t)t221 >= (uintptr_t)(t220)) gm_trap_index((uintptr_t)t221, t220, gm_src5cfou479ljhn, 104, 35);
                            uint64_t t222 = GM_LD(uint64_t, (((uint8_t *)t219) + (uintptr_t)t221 * 8));
                            uint8_t t223 = (t218 > t222);
                            if (t223) {
                                gm_l29 = ((uint8_t)1);
                            }
                            goto gm_lb29;
                        }
gm_lb30:;
                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 103, 5);
                    }
gm_lb29:;
                }
gm_lb28:;
                uint64_t t224 = gm_l31;
                uint64_t r225; bool o226 = __builtin_add_overflow(t224, ((uint64_t)1ULL), &r225);
                uint64_t t227 = r225;
                gm_l31 = t227;
            }
        }
gm_lb26:;
        uint8_t t228 = gm_l29;
        if (t228) {
            gm_b16_8 t229 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
            gm_l33 = t229;
            gm_l34 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t230 = gm_l34;
                    uint64_t t231 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                    uint8_t t232 = (t230 < t231);
                    if (!t232) {
                        goto gm_lb31;
                    }
                    {
                        void * t233 = GM_LD(void *, ((uint8_t *)&gm_l33));
                        uint64_t t234 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                        uint64_t t235 = gm_l34;
                        {
                            {
                                uint64_t t236 = GM_LD(uint64_t, (((uint8_t *)t233) + (uintptr_t)t235 * 8));
                                gm_l35 = t236;
                                {
                                    {
                                        void * t237 = GM_LD(void *, ((uint8_t *)&F.l15));
                                        uint64_t t238 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 8));
                                        uint64_t t239 = gm_l35;
                                        if ((uintptr_t)t239 >= (uintptr_t)(t238)) gm_trap_index((uintptr_t)t239, t238, gm_src5cfou479ljhn, 108, 21);
                                        uint8_t t240 = (uint8_t)(GM_LD(void *, (((uint8_t *)t237) + (uintptr_t)t239 * 8)) != 0);
                                        if (!t240) {
                                            goto gm_lb37;
                                        }
                                        void * t241 = GM_LD(void *, (((uint8_t *)t237) + (uintptr_t)t239 * 8));
                                        gm_l36 = t241;
                                        void * t242 = GM_LD(void *, ((uint8_t *)&F.l15));
                                        uint64_t t243 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 8));
                                        uint64_t t244 = gm_l35;
                                        if ((uintptr_t)t244 >= (uintptr_t)(t243)) gm_trap_index((uintptr_t)t244, t243, gm_src5cfou479ljhn, 108, 38);
                                        void * t245 = gm_l0;
                                        void * t246 = gm_l36;
                                        void * t247 = (void *)((uint8_t *)&F.l14);
                                        void * t248 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t245, t246, t247);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        GM_ST(void *, (((uint8_t *)t242) + (uintptr_t)t244 * 8), t248);
                                        goto gm_lb36;
                                    }
gm_lb37:;
                                }
gm_lb36:;
                                goto gm_lb34;
                            }
gm_lb35:;
                            gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 107, 9);
                        }
gm_lb34:;
                    }
gm_lb33:;
                    uint64_t t249 = gm_l34;
                    uint64_t r250; bool o251 = __builtin_add_overflow(t249, ((uint64_t)1ULL), &r250);
                    uint64_t t252 = r250;
                    gm_l34 = t252;
                }
            }
gm_lb31:;
        }
    }
    {
        gm_trqfvmjtt4sij t253;
        gm_f_list_uvfu1k1((void *)&t253, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l37 = t253;
        gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
        gm_l38 = t255;
        gm_l39 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t256 = gm_l39;
                uint64_t t257 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                uint8_t t258 = (t256 < t257);
                if (!t258) {
                    goto gm_lb38;
                }
                {
                    uint64_t t259 = gm_l39;
                    void * t260 = GM_LD(void *, ((uint8_t *)&gm_l38));
                    uint64_t t261 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                    uint64_t t262 = gm_l39;
                    gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)t260) + (uintptr_t)t262 * 16));
                    gm_tf1bjg81u0glv t264;
                    GM_ST(uint64_t, ((uint8_t *)&t264 + 0), t259);
                    GM_ST(gm_b16_8, ((uint8_t *)&t264 + 8), t263);
                    gm_l59 = t264;
                    {
                        {
                            uint64_t t265 = GM_LD(uint64_t, ((uint8_t *)&gm_l59));
                            gm_l40 = t265;
                            gm_b16_8 t266 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l59) + 8));
                            gm_l41 = t266;
                            void * t267 = GM_LD(void *, ((uint8_t *)&F.l15));
                            uint64_t t268 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 8));
                            uint64_t t269 = gm_l40;
                            if ((uintptr_t)t269 >= (uintptr_t)(t268)) gm_trap_index((uintptr_t)t269, t268, gm_src5cfou479ljhn, 113, 12);
                            {
                                {
                                    uint8_t t270 = (uint8_t)(GM_LD(void *, (((uint8_t *)t267) + (uintptr_t)t269 * 8)) != 0);
                                    if (t270) {
                                        goto gm_lb44;
                                    }
                                    gm_l60 = ((uint8_t)1);
                                    goto gm_lb43;
                                }
gm_lb44:;
                                {
                                    gm_l60 = ((uint8_t)0);
                                    goto gm_lb43;
                                }
gm_lb45:;
                                gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 113, 12);
                            }
gm_lb43:;
                            uint8_t t271 = gm_l60;
                            if (t271) {
                                uint64_t t272 = GM_LD(uint64_t, (((uint8_t *)&F.l37) + 8));
                                uint8_t t273 = (t272 > ((uint64_t)0ULL));
                                if (t273) {
                                    void * t274 = (void *)((uint8_t *)&F.l37);
                                    gm_b16_8 t275;
                                    GM_ST(void *, (uint8_t *)&t275, (void *)(gm_sg5gdnrith2i6));
                                    GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(2));
                                    gm_tftb1odc739t5 t276 = gm_f_extend_0cs85gs(t274, t275);
                                    gm_l61 = t276;
                                    uint8_t t277 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                    if (!t277) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_src5cfou479ljhn, 114, 40);
                                }
                                void * t278 = (void *)((uint8_t *)&F.l37);
                                gm_b16_8 t279 = gm_l41;
                                gm_tnpifv20irnph t280;
                                GM_ST(gm_b16_8, ((uint8_t *)&t280 + 0), t279);
                                gm_f_format_h4jcodi((void *)&gm_l62, (void *)NULL, cx, NULL, t280);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t281 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                                if (!t281) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 8, gm_src5cfou479ljhn, 115, 28);
                                gm_b16_8 t282 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l62) + 8));
                                gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&t282));
                                gm_tftb1odc739t5 t284 = gm_f_extend_0cs85gs(t278, t283);
                                gm_l63 = t284;
                                uint8_t t285 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                                if (!t285) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 1, gm_src5cfou479ljhn, 115, 13);
                            }
                            goto gm_lb41;
                        }
gm_lb42:;
                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 112, 5);
                    }
gm_lb41:;
                }
gm_lb40:;
                uint64_t t286 = gm_l39;
                uint64_t r287; bool o288 = __builtin_add_overflow(t286, ((uint64_t)1ULL), &r287);
                uint64_t t289 = r287;
                gm_l39 = t289;
            }
        }
gm_lb38:;
    }
    {
        {
            {
                uint64_t t290 = GM_LD(uint64_t, (((uint8_t *)&F.l37) + 8));
                uint8_t t291 = (t290 > ((uint64_t)0ULL));
                if (!t291) {
                    goto gm_lb47;
                }
                uint8_t t292 = gm_l19;
                uint8_t t293 = (uint8_t)!t292;
                if (!t293) {
                    goto gm_lb47;
                }
                void * t294 = gm_l0;
                gm_tl874t2atnlh3 t295 = gm_l5;
                void * t296 = gm_l0;
                gm_t88rcucg1b7uk t297 = gm_l1;
                gm_b16_8 t298 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t296, t297);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)&F.l37));
                gm_t6j6smj7qoctu t300;
                GM_ST(gm_b16_8, ((uint8_t *)&t300 + 0), t298);
                GM_ST(gm_b16_8, ((uint8_t *)&t300 + 16), t299);
                gm_f_format_v7qttjn((void *)&gm_l64, (void *)NULL, cx, NULL, &t300);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t301 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                if (!t301) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 8, gm_src5cfou479ljhn, 119, 24);
                gm_b16_8 t302 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l64) + 8));
                gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)&t302));
                gm_b0_1 t304 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t294, t295, t303);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb46;
            }
gm_lb47:;
        }
gm_lb46:;
        gm_trqfvmjtt4sij t305;
        gm_f_list_vp1djv1_2((void *)&t305, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l42 = t305;
        gm_b16_8 t306 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
        gm_l43 = t306;
        gm_l44 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t307 = gm_l44;
                uint64_t t308 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                uint8_t t309 = (t307 < t308);
                if (!t309) {
                    goto gm_lb48;
                }
                {
                    void * t310 = GM_LD(void *, ((uint8_t *)&gm_l43));
                    uint64_t t311 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                    uint64_t t312 = gm_l44;
                    {
                        {
                            void * t313 = GM_LD(void *, (((uint8_t *)t310) + (uintptr_t)t312 * 8));
                            gm_l45 = t313;
                            void * t314 = (void *)((uint8_t *)&F.l42);
                            uint8_t t315 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l45)) != 0);
                            if (t315) {
                                void * t316 = GM_LD(void *, ((uint8_t *)&gm_l45));
                                gm_l65 = t316;
                            } else {
                                void * t317 = gm_l0;
                                void * t318 = GM_LD(void *, ((uint8_t *)t317));
                                gm_tl874t2atnlh3 t319 = gm_l5;
                                void * t320 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t318, t319);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l65 = t320;
                            }
                            void * t321 = gm_l65;
                            gm_tftb1odc739t5 t322 = gm_f_push_d943vgp_2(t314, &(*(gm_tcgrjq9toscke *)((uint8_t *)t321)));
                            gm_l66 = t322;
                            uint8_t t323 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                            if (!t323) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_src5cfou479ljhn, 122, 28);
                            goto gm_lb51;
                        }
gm_lb52:;
                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 122, 5);
                    }
gm_lb51:;
                }
gm_lb50:;
                uint64_t t324 = gm_l44;
                uint64_t r325; bool o326 = __builtin_add_overflow(t324, ((uint64_t)1ULL), &r325);
                uint64_t t327 = r325;
                gm_l44 = t327;
            }
        }
gm_lb48:;
    }
    {
        gm_t88rcucg1b7uk t328 = gm_l1;
        gm_b16_8 t329 = GM_LD(gm_b16_8, ((uint8_t *)&F.l42));
        gm_tl874t2atnlh3 t330 = gm_l5;
        gm_t2b6tm8c0vgo1 t331;
        memset(&t331, 0, sizeof t331);
        GM_ST(uint8_t, ((uint8_t *)&t331 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t331 + 8), t329);
        void * t332 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t328, &t331, t330);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l46 = t332;
        uint64_t t333 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
        uint8_t t334 = ((t333 == ((uint64_t)0ULL)));
        if (t334) {
            void * t335 = gm_l46;
            F.ret = t335;
            goto gm_fret;
        }
        gm_t88rcucg1b7uk t336 = gm_l1;
        gm_b16_8 t337 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
        void * t338 = gm_l46;
        gm_tl874t2atnlh3 t339 = gm_l5;
        void * t340 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t336, t337, t338, t339);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t340;
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
inline __attribute__((aligned(4))) void * gm_f_construct_param_array_btk60nl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_construct_param_array_btk60nl F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t08olq97rscn3 gm_l5; /* n */
    void * gm_l7; /* v */
    GM_STACK_CHECK(&F, 441u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l8;
    {
        {
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t5 = ((t4 == ((uint64_t)1ULL)));
            if (!t5) {
                goto gm_lb2;
            }
            void * t6 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t7, gm_src5cfou479ljhn, 54, 42);
            uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)1ULL));
            if (!t9) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l5), ((((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 32);
            memmove(((uint8_t *)&F.l6), (((((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 144) + 8) + 32), 88);
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_spevct0q3cjgb));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(4));
            uint8_t t12 = (gm_eq0iqiubfm7e(((uint8_t *)&t10), ((uint8_t *)&t11)));
            if (!t12) {
                goto gm_lb2;
            }
            void * t13 = gm_l0;
            void * t14 = (void *)((uint8_t *)&F.l6);
            gm_t88rcucg1b7uk t15 = gm_l2;
            void * t16 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t13, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t16;
            gm_t88rcucg1b7uk t17 = gm_l1;
            void * t18 = gm_l7;
            gm_tl874t2atnlh3 t19 = gm_l4;
            gm_t2b6tm8c0vgo1 t20;
            memset(&t20, 0, sizeof t20);
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)37ULL));
            GM_ST(void *, ((uint8_t *)&t20 + 8), t18);
            void * t21 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t17, &t20, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t21;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t22 = gm_l0;
    gm_tl874t2atnlh3 t23 = gm_l4;
    void * t24 = gm_l0;
    gm_t88rcucg1b7uk t25 = gm_l1;
    gm_b16_8 t26 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t24, t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t27;
    GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t26);
    gm_f_format_ti6vs6t((void *)&gm_l8, (void *)NULL, cx, NULL, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_src5cfou479ljhn, 58, 20);
    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
    gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
    gm_b0_1 t31 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t22, t23, t30);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t32 = gm_l0;
    void * t33 = GM_LD(void *, ((uint8_t *)t32));
    gm_tl874t2atnlh3 t34 = gm_l4;
    void * t35 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t33, t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t35;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_construction_elem_type_qm1ncub(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_construction_elem_type_qm1ncub F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tkn42u7na3t81 gm_l3;
    void * t2 = gm_l0;
    void * t3 = gm_l1;
    void * t4 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t5 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t2, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t5;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = (void *)(((uint8_t *)t7) + 8);
    gm_t88rcucg1b7uk t9 = gm_l2;
    uint8_t t10 = gm_f_is_error_8k27gd1(t8, GM_CONV(gm_csguankko4lmhd, t9));
    if (t10) {
        gm_tkn42u7na3t81 t11;
        memset(&t11, 0, sizeof t11);
        gm_l3 = t11;
    } else {
        gm_t88rcucg1b7uk t12 = gm_l2;
        gm_tkn42u7na3t81 t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t13), t12);
        ((uint8_t *)&t13)[4] = 1;
        gm_l3 = t13;
    }
    gm_tkn42u7na3t81 t14 = gm_l3;
    F.ret = t14;
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
__attribute__((aligned(4))) void * gm_f_enum_array_index_kma2hbd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, void * a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_enum_array_index_kma2hbd F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_t88rcucg1b7uk gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    void * gm_l4; /* index_ast */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* i */
    gm_t88rcucg1b7uk gm_l7; /* it */
    gm_t88rcucg1b7uk gm_l8; /* bt */
    gm_b16_8 gm_l9; /* hint */
    void * gm_l10; /* pos */
    GM_STACK_CHECK(&F, 991u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_b16_8 gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    gm_t53at80pfdus3 gm_l15;
    uint8_t gm_l16;
    gm_thetuhtouve7f gm_l17;
    gm_tabb710d9mq2e gm_l18;
    void * t8 = gm_l0;
    void * t9 = gm_l4;
    gm_t88rcucg1b7uk t10 = gm_l2;
    gm_tkn42u7na3t81 t11;
    GM_ST(uint8_t, (uint8_t *)&t11 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t11 + 6, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[4] = 1;
    void * t12 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t8, t9, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t12;
    void * t13 = gm_l0;
    void * t14 = (void *)(((uint8_t *)t13) + 96);
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    void * t17 = gm_l6;
    gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t17));
    gm_t88rcucg1b7uk t19 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t14, t16, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t19;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    void * t22 = (void *)(((uint8_t *)t21) + 8);
    gm_t88rcucg1b7uk t23 = gm_l7;
    uint8_t t24 = gm_f_is_error_8k27gd1(t22, GM_CONV(gm_csguankko4lmhd, t23));
    if (t24) {
        void * t25 = gm_l0;
        void * t26 = GM_LD(void *, ((uint8_t *)t25));
        gm_tl874t2atnlh3 t27 = gm_l5;
        void * t28 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t28;
        goto gm_fret;
    }
    gm_t88rcucg1b7uk t29 = gm_l7;
    gm_t88rcucg1b7uk t30 = gm_l2;
    uint8_t t31 = (!gm_eq9m6mvtmgde(((uint8_t *)&t29), ((uint8_t *)&t30)));
    if (t31) {
        void * t32 = gm_l0;
        void * t33 = (void *)(((uint8_t *)t32) + 96);
        void * t34 = gm_l0;
        void * t35 = GM_LD(void *, ((uint8_t *)t34));
        void * t36 = gm_l1;
        gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t36));
        gm_t88rcucg1b7uk t38 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t33, t35, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t38;
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, ((uint8_t *)t39));
        void * t41 = (void *)(((uint8_t *)t40) + 8);
        gm_t88rcucg1b7uk t42 = gm_l7;
        gm_f_kind_jcnt4hm((void *)&gm_l15, t41, GM_CONV(gm_csguankko4lmhd, t42));
        {
            {
                uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                uint8_t t44 = (uint8_t)(t43 == ((uint8_t)23ULL));
                if (!t44) {
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
            gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 136, 20);
        }
gm_lb1:;
        uint8_t t45 = gm_l14;
        if (t45) {
            gm_l13 = ((uint8_t)1);
        } else {
            void * t46 = gm_l0;
            void * t47 = GM_LD(void *, ((uint8_t *)t46));
            void * t48 = (void *)(((uint8_t *)t47) + 8);
            gm_t88rcucg1b7uk t49 = gm_l7;
            uint8_t t50 = gm_f_is_integer_8d4f9ek(t48, GM_CONV(gm_csguankko4lmhd, t49));
            gm_l13 = t50;
        }
        uint8_t t51 = gm_l13;
        if (t51) {
            gm_l12 = ((uint8_t)1);
        } else {
            void * t52 = gm_l0;
            void * t53 = GM_LD(void *, ((uint8_t *)t52));
            gm_t88rcucg1b7uk t54 = gm_l7;
            gm_thetuhtouve7f t55 = gm_f_range_of_dmmo768((void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t55;
            {
                {
                    uint8_t t56 = (uint8_t)((((uint8_t *)&gm_l17))[8] != 0);
                    if (t56) {
                        goto gm_lb5;
                    }
                    gm_l16 = ((uint8_t)0);
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    gm_l16 = ((uint8_t)1);
                    goto gm_lb4;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 136, 81);
            }
gm_lb4:;
            uint8_t t57 = gm_l16;
            gm_l12 = t57;
        }
        uint8_t t58 = gm_l12;
        if (t58) {
            gm_b16_8 t59;
            GM_ST(void *, (uint8_t *)&t59, (void *)(gm_s0k8snlvukl86));
            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(65));
            gm_l11 = t59;
        } else {
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(0));
            gm_l11 = t60;
        }
        gm_b16_8 t61 = gm_l11;
        gm_l9 = t61;
        void * t62 = gm_l0;
        void * t63 = gm_l6;
        gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 72));
        void * t65 = gm_l0;
        gm_t88rcucg1b7uk t66 = gm_l8;
        gm_b16_8 t67 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t65, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t68 = gm_l0;
        gm_t88rcucg1b7uk t69 = gm_l2;
        gm_b16_8 t70 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t68, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t71 = gm_l0;
        gm_t88rcucg1b7uk t72 = gm_l7;
        gm_b16_8 t73 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t71, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t74 = gm_l9;
        gm_t0uuhaljsf410 t75;
        GM_ST(gm_b16_8, ((uint8_t *)&t75 + 0), t67);
        GM_ST(gm_b16_8, ((uint8_t *)&t75 + 16), t70);
        GM_ST(gm_b16_8, ((uint8_t *)&t75 + 32), t73);
        GM_ST(gm_b16_8, ((uint8_t *)&t75 + 48), t74);
        gm_f_format_frab70n((void *)&gm_l18, (void *)NULL, cx, NULL, &t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
        if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src5cfou479ljhn, 137, 26);
        gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
        gm_b0_1 t79 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t62, t64, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t80 = gm_l0;
        void * t81 = GM_LD(void *, ((uint8_t *)t80));
        gm_tl874t2atnlh3 t82 = gm_l5;
        void * t83 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t81, t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t83;
        goto gm_fret;
    }
    void * t84 = gm_l0;
    void * t85 = gm_l6;
    gm_t88rcucg1b7uk t86 = gm_l2;
    gm_tl874t2atnlh3 t87 = gm_l5;
    void * t88 = gm_f_variant_position_fdpecs3((void *)NULL, cx, NULL, t84, t85, t86, t87);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t88;
    gm_t88rcucg1b7uk t89 = gm_l3;
    void * t90 = gm_l1;
    void * t91 = gm_l10;
    gm_tlcl4u558l6p5 t92;
    GM_ST(uint8_t, (uint8_t *)&t92 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t92 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t92 + 20, 0);
    GM_ST(void *, ((uint8_t *)&t92 + 0), t90);
    GM_ST(void *, ((uint8_t *)&t92 + 8), t91);
    GM_ST(uint8_t, ((uint8_t *)&t92 + 16), ((uint8_t)0));
    gm_tl874t2atnlh3 t93 = gm_l5;
    gm_t2b6tm8c0vgo1 t94;
    memset(&t94, 0, sizeof t94);
    GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)20ULL));
    GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t94 + 8), t92);
    void * t95 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t89, &t94, t93);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t95;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_given_27f1vc1(void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void * gm_f_variant_position_fdpecs3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_variant_position_fdpecs3 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* e */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_tep2drsl9408b gm_l5; /* uv */
    uint8_t gm_l6; /* counts_up */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint8_t gm_l11; /* signed */
    uint16_t gm_l12; /* bits */
    gm_tn3r4rvlh0rsv gm_l13; /* <temporary array> */
    void * gm_l14; /* t */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    GM_STACK_CHECK(&F, 1553u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tdlpv0q69lov1 gm_l19;
    gm_tep2drsl9408b gm_l20;
    gm_t3l8idb2m6c1r gm_l21;
    gm_t53at80pfdus3 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    void * t16 = (void *)(((uint8_t *)t15) + 8);
    gm_l4 = t16;
    void * t17 = gm_l0;
    void * t18 = GM_LD(void *, ((uint8_t *)t17));
    gm_t88rcucg1b7uk t19 = gm_l2;
    gm_f_union_variants_a7khaqp((void *)&gm_l19, t18, GM_CONV(gm_csguankko4lmhd, t19));
    uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l19))[24] != 0);
    if (t20) {
        memmove(((uint8_t *)&gm_l20), ((uint8_t *)&gm_l19), 24);
    } else {
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, ((uint8_t *)t21));
        gm_tl874t2atnlh3 t23 = gm_l3;
        void * t24 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t24;
        goto gm_fret;
    }
    memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l20), 24);
    gm_l6 = ((uint8_t)1);
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
    gm_l7 = t26;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t27 = gm_l8;
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t29 = (t27 < t28);
            if (!t29) {
                goto gm_lb1;
            }
            {
                uint64_t t30 = gm_l8;
                void * t31 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t33 = gm_l8;
                gm_tckdobunaulee t34 = GM_LD(gm_tckdobunaulee, (((uint8_t *)t31) + (uintptr_t)t33 * 72));
                gm_t3l8idb2m6c1r t35;
                GM_ST(uint64_t, ((uint8_t *)&t35 + 0), t30);
                GM_ST(gm_tckdobunaulee, ((uint8_t *)&t35 + 8), t34);
                gm_l21 = t35;
                {
                    {
                        uint64_t t36 = GM_LD(uint64_t, ((uint8_t *)&gm_l21));
                        gm_l9 = t36;
                        memmove(((uint8_t *)&F.l10), (((uint8_t *)&gm_l21) + 8), 72);
                        uint64_t t37 = gm_l9;
                        gm_tbci5ospk5nnn t38 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l10) + 24));
                        uint64_t t39 = (uint64_t)t37;
                        gm_tbci5ospk5nnn t40;
                        gm_f_from_u64_ud13q5v((void *)&t40, (void *)NULL, cx, NULL, t39);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t41 = gm_f_eq_60fjn21(&t38, &t40);
                        uint8_t t42 = (uint8_t)!t41;
                        if (t42) {
                            gm_l6 = ((uint8_t)0);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 150, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t43 = gm_l8;
            uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
            uint64_t t46 = r44;
            gm_l8 = t46;
        }
    }
gm_lb1:;
    {
        {
            uint8_t t47 = gm_l6;
            if (!t47) {
                goto gm_lb7;
            }
            void * t48 = gm_l4;
            gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 16));
            gm_f_kind_jcnt4hm((void *)&gm_l22, t48, GM_CONV(gm_csguankko4lmhd, t49));
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)0ULL));
            if (!t51) {
                goto gm_lb7;
            }
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 8));
            gm_l11 = t52;
            uint16_t t53 = GM_LD(uint16_t, ((((uint8_t *)&gm_l22) + 8) + 2));
            gm_l12 = t53;
            uint8_t t54 = gm_l11;
            uint8_t t55 = (uint8_t)!t54;
            if (!t55) {
                goto gm_lb7;
            }
            uint16_t t56 = gm_l12;
            uint8_t t57 = (t56 <= ((uint16_t)32ULL));
            if (!t57) {
                goto gm_lb7;
            }
            gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 16));
            void * t59 = gm_l1;
            gm_tn3r4rvlh0rsv t60;
            GM_ST(void *, ((uint8_t *)&t60 + 0), t59);
            gm_tvdg94bk7aa22 t61;
            GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t61 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)14ULL));
            gm_l13 = t60;
            void * t62 = (void *)((uint8_t *)&gm_l13);
            uint64_t t63 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t64 = (uintptr_t)((uint64_t)1ULL);
            if (t63 > t64 || t64 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t63, t64, ((uint64_t)1ULL), gm_src5cfou479ljhn, 154, 36);
            uint64_t t65 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t66;
            GM_ST(void *, (uint8_t *)&t66, (void *)(((uint8_t *)t62 + (uintptr_t)((uint64_t)0ULL) * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(t65));
            gm_tl874t2atnlh3 t67 = gm_l3;
            void * t68 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t61, t58, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t68;
            void * t69 = gm_l0;
            void * t70 = gm_l14;
            void * t71 = gm_l4;
            gm_t88rcucg1b7uk t72 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t73 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t69, t70, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t73;
            goto gm_fret;
        }
gm_lb7:;
    }
gm_lb6:;
    gm_trqfvmjtt4sij t74;
    gm_f_list_58nr42v_2((void *)&t74, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l15 = t74;
    gm_l17 = ((uint64_t)0ULL);
    uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    gm_l16 = t75;
    {
        for (;;) {
            uint64_t t76 = gm_l17;
            uint64_t t77 = gm_l16;
            uint8_t t78 = (t76 < t77);
            if (!t78) {
                goto gm_lb8;
            }
            {
                {
                    {
                        uint64_t t79 = gm_l17;
                        gm_l18 = t79;
                        void * t80 = (void *)((uint8_t *)&F.l15);
                        gm_t88rcucg1b7uk t81 = gm_l2;
                        uint64_t t82 = gm_l18;
                        uint32_t t83 = (uint32_t)t82;
                        gm_ttggq7ng4kp3p t84;
                        GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t84 + 0), t83);
                        GM_ST(void *, ((uint8_t *)&t84 + 8), ((void *)0));
                        gm_tl874t2atnlh3 t85 = gm_l3;
                        gm_tb38v5s9j1r32 t86;
                        memset(&t86, 0, sizeof t86);
                        GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)8ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t86 + 8), t84);
                        void * t87 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t81, &t86, t85);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t88 = gm_l4;
                        gm_t88rcucg1b7uk t89 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t88);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t90 = gm_l18;
                        uint64_t t91 = (uint64_t)t90;
                        gm_tbci5ospk5nnn t92;
                        gm_f_from_u64_ud13q5v((void *)&t92, (void *)NULL, cx, NULL, t91);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tl874t2atnlh3 t93 = gm_l3;
                        gm_t2b6tm8c0vgo1 t94;
                        GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t94 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t94 + 8), t92);
                        void * t95 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t89, &t94, t93);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tbbqefuhv2r8h t96;
                        GM_ST(void *, ((uint8_t *)&t96 + 0), t87);
                        GM_ST(void *, ((uint8_t *)&t96 + 8), ((void *)0));
                        GM_ST(void *, ((uint8_t *)&t96 + 16), t95);
                        gm_tftb1odc739t5 t97 = gm_f_push_bpovr2k_2(t80, &t96);
                        gm_l23 = t97;
                        uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                        if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_src5cfou479ljhn, 159, 9);
                        goto gm_lb11;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 158, 5);
                }
gm_lb11:;
            }
gm_lb10:;
            uint64_t t99 = gm_l17;
            uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
            uint64_t t102 = r100;
            gm_l17 = t102;
        }
    }
gm_lb8:;
    void * t103 = gm_l4;
    gm_t88rcucg1b7uk t104 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t103);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t105 = gm_l1;
    gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
    gm_tkaapcii86udk t107;
    GM_ST(void *, ((uint8_t *)&t107 + 0), t105);
    GM_ST(gm_b16_8, ((uint8_t *)&t107 + 8), t106);
    gm_tl874t2atnlh3 t108 = gm_l3;
    gm_t2b6tm8c0vgo1 t109;
    memset(&t109, 0, sizeof t109);
    GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)44ULL));
    GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t109 + 8), t107);
    void * t110 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t104, &t109, t108);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t110;
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
__attribute__((aligned(4))) void * gm_f_without_call_t1kfc9e(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_without_call_t1kfc9e F;
    void * gm_l0; /* e */
    void * gm_l1; /* callee */
    gm_t3mf58gv9kf3n gm_l2; /* a */
    void * gm_l3; /* inner */
    uint8_t gm_l4; /* c */
    void * gm_l5; /* inner */
    uint8_t gm_l6; /* c */
    void * gm_l7; /* inner */
    uint8_t gm_l8; /* c */
    void * gm_l9; /* inner */
    GM_STACK_CHECK(&F, 982u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2gtbh39ptekd gm_l11;
    gm_tq7s98ihonkju gm_l12;
    void * t10 = gm_l0;
    {
        {
            uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)19ULL));
            if (!t12) {
                goto gm_lb2;
            }
            void * t13 = GM_LD(void *, ((((uint8_t *)t10) + 8) + 8));
            gm_l1 = t13;
            void * t14 = gm_l1;
            F.ret = t14;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)28ULL));
            if (!t16) {
                goto gm_lb3;
            }
            gm_t3mf58gv9kf3n t17 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t10) + 8) + 8));
            gm_l2 = t17;
            void * t18 = GM_LD(void *, ((uint8_t *)&gm_l2));
            void * t19 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
            void * t20 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t21 = gm_f_box_mut_dlc4mlv((void *)NULL, cx, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t3mf58gv9kf3n t22;
            GM_ST(void *, ((uint8_t *)&t22 + 0), t18);
            GM_ST(void *, ((uint8_t *)&t22 + 8), t21);
            gm_t2gtbh39ptekd t23;
            memset(&t23, 0, sizeof t23);
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)28ULL));
            GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t23 + 8), t22);
            gm_l11 = t23;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)24ULL));
            if (!t25) {
                goto gm_lb4;
            }
            void * t26 = GM_LD(void *, ((((uint8_t *)t10) + 8) + 8));
            gm_l3 = t26;
            void * t27 = gm_l3;
            void * t28 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t29 = gm_f_box_mut_dlc4mlv((void *)NULL, cx, NULL, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t2gtbh39ptekd t30;
            memset(&t30, 0, sizeof t30);
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)24ULL));
            GM_ST(void *, ((uint8_t *)&t30 + 8), t29);
            gm_l11 = t30;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)25ULL));
            if (!t32) {
                goto gm_lb5;
            }
            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 8));
            gm_l4 = t33;
            void * t34 = GM_LD(void *, (((((uint8_t *)t10) + 8) + 8) + 8));
            gm_l5 = t34;
            uint8_t t35 = gm_l4;
            void * t36 = gm_l5;
            void * t37 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t38 = gm_f_box_mut_dlc4mlv((void *)NULL, cx, NULL, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tgtub0v94d5u5 t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), t35);
            GM_ST(void *, ((uint8_t *)&t39 + 8), t38);
            gm_t2gtbh39ptekd t40;
            memset(&t40, 0, sizeof t40);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)25ULL));
            GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t40 + 8), t39);
            gm_l11 = t40;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)26ULL));
            if (!t42) {
                goto gm_lb6;
            }
            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 8));
            gm_l6 = t43;
            void * t44 = GM_LD(void *, (((((uint8_t *)t10) + 8) + 8) + 8));
            gm_l7 = t44;
            uint8_t t45 = gm_l6;
            void * t46 = gm_l7;
            void * t47 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t48 = gm_f_box_mut_dlc4mlv((void *)NULL, cx, NULL, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tgtub0v94d5u5 t49;
            GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t49 + 0), t45);
            GM_ST(void *, ((uint8_t *)&t49 + 8), t48);
            gm_t2gtbh39ptekd t50;
            memset(&t50, 0, sizeof t50);
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)26ULL));
            GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t50 + 8), t49);
            gm_l11 = t50;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)27ULL));
            if (!t52) {
                goto gm_lb7;
            }
            uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 8));
            gm_l8 = t53;
            void * t54 = GM_LD(void *, (((((uint8_t *)t10) + 8) + 8) + 8));
            gm_l9 = t54;
            uint8_t t55 = gm_l8;
            void * t56 = gm_l9;
            void * t57 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t58 = gm_f_box_mut_dlc4mlv((void *)NULL, cx, NULL, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tgtub0v94d5u5 t59;
            GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), t55);
            GM_ST(void *, ((uint8_t *)&t59 + 8), t58);
            gm_t2gtbh39ptekd t60;
            memset(&t60, 0, sizeof t60);
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)27ULL));
            GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t60 + 8), t59);
            gm_l11 = t60;
            goto gm_lb1;
        }
gm_lb7:;
        {
            void * t61 = gm_l0;
            F.ret = t61;
            goto gm_fret;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 36, 26);
    }
gm_lb1:;
    memmove(((uint8_t *)&F.l10), ((uint8_t *)&gm_l11), 64);
    void * t62 = gm_l0;
    uint32_t t63 = GM_LD(uint32_t, ((uint8_t *)t62));
    void * t64 = gm_l0;
    gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t64) + 72));
    gm_t2gtbh39ptekd t66 = GM_LD(gm_t2gtbh39ptekd, ((uint8_t *)&F.l10));
    gm_tc3oht4ksth9d t67;
    GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t67 + 84, 0);
    GM_ST(uint32_t, ((uint8_t *)&t67 + 0), t63);
    GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&t67 + 8), t66);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t67 + 72), t65);
    gm_tq7s98ihonkju t68 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &t67, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kv89j160jqp)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l12 = t68;
    uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_src5cfou479ljhn, 45, 12);
    void * t70 = GM_LD(void *, (((uint8_t *)&gm_l12) + 8));
    F.ret = t70;
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
