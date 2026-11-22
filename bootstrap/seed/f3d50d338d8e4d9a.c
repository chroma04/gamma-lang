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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tammvh1iespi3;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_taqu3436kubri;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_tekl8c9g34a9k;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { uint64_t f0; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_tm9oc6v1he136;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t08olq97rscn3 f0; gm_b16_8 f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t99vmhfllumbh;
typedef struct { gm_b16_8 f0; gm_tiilmfd16v3fc f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; gm_tnpifv20irnph f3; gm_b16_8 f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_t9qmk75lrp9p4;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tammvh1iespi3) == 56 && _Alignof(gm_tammvh1iespi3) == 8 && offsetof(gm_tammvh1iespi3, f0) == 0 && offsetof(gm_tammvh1iespi3, f1) == 16 && offsetof(gm_tammvh1iespi3, f2) == 24 && offsetof(gm_tammvh1iespi3, f3) == 40, "layout of `EffectInfo`");
_Static_assert(sizeof(gm_taqu3436kubri) == 20 && _Alignof(gm_taqu3436kubri) == 4 && offsetof(gm_taqu3436kubri, f0) == 0 && offsetof(gm_taqu3436kubri, f1) == 4 && offsetof(gm_taqu3436kubri, f2) == 8, "layout of `HandlerBreak`");
typedef struct { uint64_t f0; void * f1; uint32_t f2; gm_t9qkh0rsooc8d f3; uint8_t p4[3]; gm_tiilmfd16v3fc f4; } gm_tbeui4bbnm52b;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tekl8c9g34a9k) == 32 && _Alignof(gm_tekl8c9g34a9k) == 8 && offsetof(gm_tekl8c9g34a9k, f0) == 0 && offsetof(gm_tekl8c9g34a9k, f1) == 16 && offsetof(gm_tekl8c9g34a9k, f2) == 20, "layout of `DeclWithCheck`");
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tplpbqf3duvn3 f0; uint8_t f1; uint8_t pe[3]; } gm_thdmr9qicg35r;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tm9oc6v1he136) == 32 && _Alignof(gm_tm9oc6v1he136) == 8 && offsetof(gm_tm9oc6v1he136, f0) == 0 && offsetof(gm_tm9oc6v1he136, f1) == 8 && offsetof(gm_tm9oc6v1he136, f2) == 24, "layout of `TemplateInstance`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_toceffb697p0u f0; uint8_t f1; uint8_t pe[3]; } gm_trj4m8pa99qp7;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t pe[4]; } gm_tuanp0jbrlb6j;
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
typedef struct { gm_tiilmfd16v3fc f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; gm_b16_8 f3; } gm_tvc83pqnepc2e;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t99vmhfllumbh) == 72 && _Alignof(gm_t99vmhfllumbh) == 8 && offsetof(gm_t99vmhfllumbh, f0) == 0 && offsetof(gm_t99vmhfllumbh, f1) == 32 && offsetof(gm_t99vmhfllumbh, f2) == 48 && offsetof(gm_t99vmhfllumbh, f3) == 56, "layout of `Clause`");
_Static_assert(sizeof(gm_t9qmk75lrp9p4) == 96 && _Alignof(gm_t9qmk75lrp9p4) == 8 && offsetof(gm_t9qmk75lrp9p4, f0) == 0 && offsetof(gm_t9qmk75lrp9p4, f1) == 16 && offsetof(gm_t9qmk75lrp9p4, f2) == 40 && offsetof(gm_t9qmk75lrp9p4, f3) == 48 && offsetof(gm_t9qmk75lrp9p4, f4) == 64 && offsetof(gm_t9qmk75lrp9p4, f5) == 80, "layout of `HandlerTemplate`");
_Static_assert(sizeof(gm_tbeui4bbnm52b) == 48 && _Alignof(gm_tbeui4bbnm52b) == 8 && offsetof(gm_tbeui4bbnm52b, f0) == 0 && offsetof(gm_tbeui4bbnm52b, f1) == 8 && offsetof(gm_tbeui4bbnm52b, f2) == 16 && offsetof(gm_tbeui4bbnm52b, f3) == 20 && offsetof(gm_tbeui4bbnm52b, f4) == 24, "layout of `PendingClause`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { uint64_t f0; gm_tvc83pqnepc2e f1; } gm_tcth15utccicj;
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_thdmr9qicg35r) == 12 && _Alignof(gm_thdmr9qicg35r) == 4 && offsetof(gm_thdmr9qicg35r, f0) == 0 && offsetof(gm_thdmr9qicg35r, f1) == 8, "layout of `?(template: u32, ty: Ty)`");
typedef struct { gm_tuanp0jbrlb6j f0; uint8_t f1; uint8_t pe[7]; } gm_tjvtgq0p0sehv;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trj4m8pa99qp7) == 20 && _Alignof(gm_trj4m8pa99qp7) == 4 && offsetof(gm_trj4m8pa99qp7, f0) == 0 && offsetof(gm_trj4m8pa99qp7, f1) == 16, "layout of `?NeedsAt`");
typedef struct { uint64_t f0; gm_tbeui4bbnm52b f1; } gm_tt01bthgiqsjo;
typedef struct { gm_trqfvmjtt4sij f0; gm_b16_8 f1; gm_thdmr9qicg35r f2; uint8_t pe[4]; } gm_tti6qnvf9jg1p;
_Static_assert(sizeof(gm_tuanp0jbrlb6j) == 32 && _Alignof(gm_tuanp0jbrlb6j) == 8 && offsetof(gm_tuanp0jbrlb6j, f0) == 0 && offsetof(gm_tuanp0jbrlb6j, f1) == 24, "layout of `ForLabel`");
_Static_assert(sizeof(gm_tvc83pqnepc2e) == 64 && _Alignof(gm_tvc83pqnepc2e) == 8 && offsetof(gm_tvc83pqnepc2e, f0) == 0 && offsetof(gm_tvc83pqnepc2e, f1) == 24 && offsetof(gm_tvc83pqnepc2e, f2) == 32 && offsetof(gm_tvc83pqnepc2e, f3) == 48, "layout of `HandlerDeclInfo`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tcth15utccicj) == 72 && _Alignof(gm_tcth15utccicj) == 8 && offsetof(gm_tcth15utccicj, f0) == 0 && offsetof(gm_tcth15utccicj, f1) == 8, "layout of `(i: usize, x: HandlerDeclInfo)`");
typedef struct { uint64_t f0; gm_toa2vhe3l4hfr f1; } gm_tdr8bh2bm3fc8;
_Static_assert(sizeof(gm_tjvtgq0p0sehv) == 40 && _Alignof(gm_tjvtgq0p0sehv) == 8 && offsetof(gm_tjvtgq0p0sehv, f0) == 0 && offsetof(gm_tjvtgq0p0sehv, f1) == 32, "layout of `?ForLabel`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_tkn42u7na3t81 f3; uint8_t p4[4]; gm_tjvtgq0p0sehv f4; gm_tkn42u7na3t81 f5; gm_tqu3m2l3n8mnc f6; } gm_tnqmlaad6uchl;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tt01bthgiqsjo) == 56 && _Alignof(gm_tt01bthgiqsjo) == 8 && offsetof(gm_tt01bthgiqsjo, f0) == 0 && offsetof(gm_tt01bthgiqsjo, f1) == 8, "layout of `(i: usize, x: PendingClause)`");
_Static_assert(sizeof(gm_tti6qnvf9jg1p) == 72 && _Alignof(gm_tti6qnvf9jg1p) == 8 && offsetof(gm_tti6qnvf9jg1p, f0) == 0 && offsetof(gm_tti6qnvf9jg1p, f1) == 40 && offsetof(gm_tti6qnvf9jg1p, f2) == 56, "layout of `InferTable`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
typedef struct { gm_tnqmlaad6uchl f0; uint8_t f1; uint8_t pe[7]; } gm_t403eokj7ifg9;
_Static_assert(sizeof(gm_tdr8bh2bm3fc8) == 48 && _Alignof(gm_tdr8bh2bm3fc8) == 8 && offsetof(gm_tdr8bh2bm3fc8, f0) == 0 && offsetof(gm_tdr8bh2bm3fc8, f1) == 8, "layout of `(i: usize, x: GenericArg)`");
_Static_assert(sizeof(gm_tnqmlaad6uchl) == 112 && _Alignof(gm_tnqmlaad6uchl) == 8 && offsetof(gm_tnqmlaad6uchl, f0) == 0 && offsetof(gm_tnqmlaad6uchl, f1) == 4 && offsetof(gm_tnqmlaad6uchl, f2) == 12 && offsetof(gm_tnqmlaad6uchl, f3) == 20 && offsetof(gm_tnqmlaad6uchl, f4) == 32 && offsetof(gm_tnqmlaad6uchl, f5) == 72 && offsetof(gm_tnqmlaad6uchl, f6) == 80, "layout of `ClauseInfo`");
typedef struct { uint64_t f0; gm_tvpnktv5fo8nl f1; } gm_tp8h583g7s5hd;
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t403eokj7ifg9) == 120 && _Alignof(gm_t403eokj7ifg9) == 8 && offsetof(gm_t403eokj7ifg9, f0) == 0 && offsetof(gm_t403eokj7ifg9, f1) == 112, "layout of `?ClauseInfo`");
_Static_assert(sizeof(gm_tp8h583g7s5hd) == 168 && _Alignof(gm_tp8h583g7s5hd) == 8 && offsetof(gm_tp8h583g7s5hd, f0) == 0 && offsetof(gm_tp8h583g7s5hd, f1) == 8, "layout of `OtherItem`");
typedef struct { void * f0; uint64_t f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_tti6qnvf9jg1p f4; gm_t88rcucg1b7uk f5; uint8_t p6[4]; gm_tnpifv20irnph f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; uint32_t f9; uint8_t p10[4]; gm_trqfvmjtt4sij f10; gm_trqfvmjtt4sij f11; gm_trqfvmjtt4sij f12; gm_t9805qtonbsag f13; gm_trj4m8pa99qp7 f14; uint8_t p15[4]; gm_trqfvmjtt4sij f15; uint8_t f16; uint8_t p17[7]; gm_trqfvmjtt4sij f17; uint8_t f18; uint8_t p19[7]; uint64_t f19; gm_trqfvmjtt4sij f20; gm_trqfvmjtt4sij f21; gm_trqfvmjtt4sij f22; gm_trqfvmjtt4sij f23; uint8_t f24; uint8_t p25[7]; gm_trqfvmjtt4sij f25; uint8_t f26; uint8_t p27[3]; gm_tkn42u7na3t81 f27; uint8_t p28[4]; gm_trqfvmjtt4sij f28; gm_t403eokj7ifg9 f29; gm_trqfvmjtt4sij f30; gm_trqfvmjtt4sij f31; gm_trqfvmjtt4sij f32; gm_trqfvmjtt4sij f33; gm_trqfvmjtt4sij f34; gm_trqfvmjtt4sij f35; gm_trqfvmjtt4sij f36; gm_trqfvmjtt4sij f37; uint8_t f38; uint8_t p39[7]; gm_trqfvmjtt4sij f39; uint8_t f40; uint8_t p41[7]; gm_t4c7ipjj9s3fp f41; void * f42; } gm_tta5be0c75tfq;
_Static_assert(sizeof(gm_tta5be0c75tfq) == 1360 && _Alignof(gm_tta5be0c75tfq) == 8 && offsetof(gm_tta5be0c75tfq, f0) == 0 && offsetof(gm_tta5be0c75tfq, f1) == 8 && offsetof(gm_tta5be0c75tfq, f2) == 16 && offsetof(gm_tta5be0c75tfq, f3) == 56 && offsetof(gm_tta5be0c75tfq, f4) == 96 && offsetof(gm_tta5be0c75tfq, f5) == 168 && offsetof(gm_tta5be0c75tfq, f6) == 176 && offsetof(gm_tta5be0c75tfq, f7) == 192 && offsetof(gm_tta5be0c75tfq, f8) == 232 && offsetof(gm_tta5be0c75tfq, f9) == 272 && offsetof(gm_tta5be0c75tfq, f10) == 280 && offsetof(gm_tta5be0c75tfq, f11) == 320 && offsetof(gm_tta5be0c75tfq, f12) == 360 && offsetof(gm_tta5be0c75tfq, f13) == 400 && offsetof(gm_tta5be0c75tfq, f14) == 408 && offsetof(gm_tta5be0c75tfq, f15) == 432 && offsetof(gm_tta5be0c75tfq, f16) == 472 && offsetof(gm_tta5be0c75tfq, f17) == 480 && offsetof(gm_tta5be0c75tfq, f18) == 520 && offsetof(gm_tta5be0c75tfq, f19) == 528 && offsetof(gm_tta5be0c75tfq, f20) == 536 && offsetof(gm_tta5be0c75tfq, f21) == 576 && offsetof(gm_tta5be0c75tfq, f22) == 616 && offsetof(gm_tta5be0c75tfq, f23) == 656 && offsetof(gm_tta5be0c75tfq, f24) == 696 && offsetof(gm_tta5be0c75tfq, f25) == 704 && offsetof(gm_tta5be0c75tfq, f26) == 744 && offsetof(gm_tta5be0c75tfq, f27) == 748 && offsetof(gm_tta5be0c75tfq, f28) == 760 && offsetof(gm_tta5be0c75tfq, f29) == 800 && offsetof(gm_tta5be0c75tfq, f30) == 920 && offsetof(gm_tta5be0c75tfq, f31) == 960 && offsetof(gm_tta5be0c75tfq, f32) == 1000 && offsetof(gm_tta5be0c75tfq, f33) == 1040 && offsetof(gm_tta5be0c75tfq, f34) == 1080 && offsetof(gm_tta5be0c75tfq, f35) == 1120 && offsetof(gm_tta5be0c75tfq, f36) == 1160 && offsetof(gm_tta5be0c75tfq, f37) == 1200 && offsetof(gm_tta5be0c75tfq, f38) == 1240 && offsetof(gm_tta5be0c75tfq, f39) == 1248 && offsetof(gm_tta5be0c75tfq, f40) == 1288 && offsetof(gm_tta5be0c75tfq, f41) == 1296 && offsetof(gm_tta5be0c75tfq, f42) == 1352, "layout of `FnCx`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_arg_may_unify_qmi580g {
    gm_frame h;
    gm_toa2vhe3l4hfr l1; /* a */
    gm_toa2vhe3l4hfr l2; /* b */
    uint8_t ret;
};
struct gm_fr_array_kind_same_rl3epej {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_check_clause_1b0dqf0 {
    gm_frame h;
    gm_tbeui4bbnm52b l1; /* p */
    gm_tqqhoivahirr0 l2; /* sig */
    gm_td7pkamr64l0c l4; /* outer */
    gm_trqfvmjtt4sij l6; /* runtime */
    gm_t5v0c8ia6a5mo l9; /* q */
    gm_tta5be0c75tfq l10; /* cx */
    gm_b0_1 ret;
};
struct gm_fr_check_decl_breaks_pk9cnmn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* reported */
    gm_t4gut2c9fj1kt l14; /* e */
    gm_b0_1 ret;
};
struct gm_fr_check_handler_clauses_n3lq2s1 {
    gm_frame h;
    gm_tbeui4bbnm52b l4; /* p */
    gm_b0_1 ret;
};
struct gm_fr_check_pending_clause_33df87a {
    gm_frame h;
    gm_tbeui4bbnm52b l5; /* p */
    uint8_t ret;
};
struct gm_fr_decl_clause_0i5kmeb {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_declare_handler_gd3gpcg {
    gm_frame h;
    void * l0; /* s */
    gm_trqfvmjtt4sij l3; /* params */
    gm_tf2orqcrphmek l7; /* p */
    gm_trqfvmjtt4sij l21; /* generics */
    gm_t5v0c8ia6a5mo l24; /* p */
    gm_trqfvmjtt4sij l26; /* seen */
    gm_t5v0c8ia6a5mo l29; /* p */
    gm_tammvh1iespi3 l31; /* info */
    gm_tvc83pqnepc2e l35; /* d */
    gm_t9qmk75lrp9p4 l39; /* probe */
    gm_tvc83pqnepc2e l42; /* d */
    gm_t9qmk75lrp9p4 l45; /* t */
    gm_toa2vhe3l4hfr l50; /* x */
    gm_t99vmhfllumbh l53; /* c */
    gm_tv0k2vg3054a6 l57; /* o */
    gm_trqfvmjtt4sij l58; /* clauses */
    gm_tv0k2vg3054a6 l61; /* o */
    gm_trqfvmjtt4sij l64; /* cparams */
    gm_b0_1 ret;
};
struct gm_fr_declare_handlers_c3ac89r {
    gm_frame h;
    gm_tp8h583g7s5hd l4; /* o */
    gm_b0_1 ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_7od16il {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8fnsr5h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f6g91fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_he774nl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hii1vte {
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
struct gm_fr_empty_slice_qmcc4fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s6l2ctu {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tslvjbf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_enter_module_scope_011vo5a {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_td7pkamr64l0c ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_extend_vi643oe {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_51177of {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_bfunt71 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_74vh99o {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8h6eueo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_klj2nqm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lveqqdp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mgpfvh0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tv3i6qj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_handler_template_args_dlm07kb {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* binds */
    gm_trqfvmjtt4sij l4; /* args */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_handlers_of_type_5gareb4 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_tvc83pqnepc2e l6; /* d */
    gm_t9qmk75lrp9p4 l10; /* tpl */
    gm_trqfvmjtt4sij l27; /* clauses */
    gm_b16_8 ret;
};
struct gm_fr_ident_is_uo88ron {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_effects_kind_uutbf9u {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_leave_module_scope_4hp5mc4 {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_b0_1 ret;
};
struct gm_fr_list_0bj26n7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1jabh22 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_4kh12ib {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_oalq03d {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_may_unify_0noeh56 {
    gm_frame h;
    gm_t53at80pfdus3 l3; /* ka */
    gm_t53at80pfdus3 l4; /* kb */
    gm_toa2vhe3l4hfr l54; /* p */
    uint8_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_0mk7udh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_9gv7k4m {
    gm_frame h;
    gm_t9qmk75lrp9p4 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_b9c0fdf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_h69n5fi {
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
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_mi6l2kj {
    gm_frame h;
    gm_tvc83pqnepc2e l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o8u01sh {
    gm_frame h;
    gm_t5v0c8ia6a5mo l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_qkkb0em {
    gm_frame h;
    gm_tbeui4bbnm52b l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_1sqb3ir {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5mao3rk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_7sj41hq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8d45hjk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_a304ri6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ub69oip {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_v0p8ic9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_template_instance_is_ovv1pcj {
    gm_frame h;
    gm_t9qmk75lrp9p4 l4; /* tpl */
    uint8_t ret;
};
struct gm_fr_template_map_lfm02i0 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* map */
    gm_b16_8 ret;
};
struct gm_fr_template_probe_is_d1ep4c6 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_truncate_7r782uc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_daelje0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_dsb08pk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_type_ty_9ph2gbr {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unscope_type_params_bofhhep {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_walk_v858co7 {
    gm_frame h;
    void * l0; /* s */
    gm_closure l2; /* f */
    gm_t53at80pfdus3 l3; /* k */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_arg_may_unify_qmi580g(void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_arg_may_unify_qmi580g_c(void * a0, gm_toa2vhe3l4hfr a1, gm_toa2vhe3l4hfr a2);
__attribute__((aligned(4))) uint8_t gm_f_array_kind_same_rl3epej(gm_tnnjeerolos0a a0, gm_tnnjeerolos0a a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bound_to_5c11t0h(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_clause_1b0dqf0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_decl_breaks_pk9cnmn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_handler_clauses_n3lq2s1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_check_pending_clause_33df87a(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_clause_signature_9v4rkv8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_clause_value_tpeb80m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) int32_t gm_f_cmp_key_c6mirrv(void * a0, void * a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_decl_clause_0i5kmeb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_declare_clause_params_7bqbscg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_handler_gd3gpcg(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_handlers_c3ac89r(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7od16il(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hii1vte(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s6l2ctu(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tslvjbf(void);
inline __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vi643oe(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_finish_m8t0599(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_51177of(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfunt71(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_fn_cx_hgvq15e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
__attribute__((aligned(4))) void gm_f_for_each_component_o5cuh7c(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_2kt92vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_74vh99o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8h6eueo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_klj2nqm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lveqqdp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_mgpfvh0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_tv3i6qj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_handler_template_args_dlm07kb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_handlers_of_type_5gareb4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_ident_is_uo88ron(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_instantiate_35r5pb2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_4kh12ib(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_match_params_6vndkud(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, void * a3);
__attribute__((aligned(4))) uint8_t gm_f_may_unify_0noeh56(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) uint32_t gm_f_new_param_7010dt4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tnnjeerolos0a a2);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9gv7k4m(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_b9c0fdf(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_mi6l2kj(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_qkkb0em(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5mao3rk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8d45hjk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_a304ri6(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ub69oip(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_remove_76216q8(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_list_uhjlh6o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_effect_ref_bggirs2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_scope_type_params_2di4dm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_subst_eref_kf9o1q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_template_instance_is_ovv1pcj(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_template_map_lfm02i0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_template_probe_is_d1ep4c6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2);
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s8075t766i1f0[50];
extern const uint8_t gm_s9ap8gk0oj12e[73];
extern const uint8_t gm_sagtv6bs2sviu[111];
extern const uint8_t gm_sal83f6sn6m3n[78];
extern const uint8_t gm_sg0bl57kiha8j[27];
extern const uint8_t gm_sgbumttiou4de[8];
extern const uint8_t gm_sgjkgsqdq34nu[37];
extern const uint8_t gm_sho5nuaja1vvf[5];
extern const uint8_t gm_shqja2jbdo2o0[58];
extern const uint8_t gm_sm8t3g6c72ol4[62];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcgr2mvgis129a[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_tmpl gm_tpl5m2t8us2v800;
extern const gm_tmpl gm_tpl9ag92r71r0vm;
extern const gm_tmpl gm_tpldtaonf5laaam;
extern const gm_tmpl gm_tplfc08pcq9dhq6;
extern const gm_tmpl gm_tplg7bh9jeid72i;
extern const gm_tmpl gm_tplggnuionimjk9;
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
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_effects_r0iv3pv F;
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7od16il(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7od16il F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 96)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8fnsr5h F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f6g91fe F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 224)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hii1vte(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hii1vte F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qmcc4fe F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s6l2ctu(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s6l2ctu F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tslvjbf(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tslvjbf F;
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
inline __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_enter_module_scope_011vo5a F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    gm_trqfvmjtt4sij t4 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 1688));
    gm_trqfvmjtt4sij t5 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t3) + 1728));
    gm_td7pkamr64l0c t6;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t6 + 40), t5);
    F.l1 = t6;
    void * t7 = gm_l0;
    gm_trqfvmjtt4sij t8;
    gm_f_list_1jabh22((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t7) + 1688), t8);
    void * t9 = gm_l0;
    gm_trqfvmjtt4sij t10;
    gm_f_list_0bj26n7((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t9) + 1728), t10);
    memmove(gm_out, ((uint8_t *)&F.l1), 80);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vi643oe(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_vi643oe F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_7sj41hq(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 56)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 56);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 56)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_51177of(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_51177of F;
    gm_t88rcucg1b7uk gm_l0; /* t */
    uint32_t gm_l1; /* p */
    GM_STACK_CHECK(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t53at80pfdus3 gm_l2;
    gm_tftb1odc739t5 gm_l3;
    {
        {
            void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_declare_handler_gd3gpcg *)((&F.h)->up))->l0));
            void * t5 = (void *)(((uint8_t *)t4) + 8);
            gm_t88rcucg1b7uk t6 = gm_l0;
            gm_f_kind_jcnt4hm((void *)&gm_l2, t5, GM_CONV(gm_csguankko4lmhd, t6));
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)19ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
            gm_l1 = t9;
            void * t10 = (void *)((uint8_t *)&((struct gm_fr_declare_handler_gd3gpcg *)((&F.h)->up))->l26);
            uint32_t t11 = gm_l1;
            gm_tftb1odc739t5 t12 = gm_f_push_im9d4ro(t10, t11);
            gm_l3 = t12;
            uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcgr2mvgis129a, 57, 45);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfunt71(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_bfunt71 F;
    gm_t88rcucg1b7uk gm_l0; /* c */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_walk_v858co7 *)((&F.h)->up))->l0));
    gm_t88rcucg1b7uk t3 = gm_l0;
    gm_closure t4 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_walk_v858co7 *)((&F.h)->up))->l2));
    gm_f_walk_v858co7(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
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
inline __attribute__((aligned(4))) void gm_f_format_74vh99o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_74vh99o F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sg0bl57kiha8j));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5m2t8us2v800), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_8h6eueo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_8h6eueo F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sagtv6bs2sviu));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(110));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfc08pcq9dhq6), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_klj2nqm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_klj2nqm F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sal83f6sn6m3n));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(77));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9ag92r71r0vm), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lveqqdp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lveqqdp F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s9ap8gk0oj12e));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(72));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplggnuionimjk9), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_mgpfvh0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_mgpfvh0 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sm8t3g6c72ol4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(61));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldtaonf5laaam), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_tv3i6qj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tv3i6qj F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8075t766i1f0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplg7bh9jeid72i), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_ident_is_uo88ron(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ident_is_uo88ron F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l1; /* name */
    gm_t08olq97rscn3 gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)6ULL));
            if (!t6) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l2), ((((uint8_t *)t4) + 8) + 8), 32);
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_b16_8 t8 = gm_l1;
            uint8_t t9 = (gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
            F.ret = t9;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_effects_kind_uutbf9u F;
    void * gm_l0; /* t */
    gm_t08olq97rscn3 gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)6ULL));
            if (!t6) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l1), ((((uint8_t *)t4) + 8) + 8), 32);
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sgbumttiou4de));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(7));
            uint8_t t9 = (gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
            F.ret = t9;
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
inline __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_leave_module_scope_4hp5mc4 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    memmove((((uint8_t *)t2) + 1688), ((uint8_t *)a1), 40);
    void * t3 = gm_l0;
    memmove((((uint8_t *)t3) + 1728), (((uint8_t *)a1) + 40), 40);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0bj26n7 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_qmcc4fe();
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
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1jabh22 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_f6g91fe();
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
inline __attribute__((aligned(4))) void gm_f_list_4kh12ib(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_4kh12ib F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_s6l2ctu();
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
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_af74s0q F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_he774nl();
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
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_oalq03d F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8fnsr5h();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0mk7udh F;
    void * gm_l0; /* l */
    gm_tl874t2atnlh3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_r81t436();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_v0p8ic9(t8, ((uint64_t)1ULL), t10);
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
    gm_tl874t2atnlh3 t18 = gm_l1;
    GM_ST(gm_tl874t2atnlh3, (((uint8_t *)t16) + (uintptr_t)t17 * 12), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 12)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9gv7k4m(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_9gv7k4m F;
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
        gm_b16_8 t9 = gm_f_empty_slice_7od16il();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_a304ri6(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 96), ((uint8_t *)a1), 96);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 96)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_b9c0fdf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_b9c0fdf F;
    void * gm_l0; /* l */
    gm_tm9oc6v1he136 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_tslvjbf();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ub69oip(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_h69n5fi F;
    void * gm_l0; /* l */
    gm_tplpbqf3duvn3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tplpbqf3duvn3, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_8fnsr5h();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_1sqb3ir(t8, ((uint64_t)1ULL), t10);
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
    gm_tplpbqf3duvn3 t18 = gm_l1;
    GM_ST(gm_tplpbqf3duvn3, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_mi6l2kj(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_mi6l2kj F;
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
        gm_b16_8 t9 = gm_f_empty_slice_hii1vte();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8d45hjk(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 64), ((uint8_t *)a1), 64);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o8u01sh F;
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
        gm_b16_8 t9 = gm_f_empty_slice_he774nl();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_7sj41hq(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 56), ((uint8_t *)&F.l1), 56);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_qkkb0em(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_qkkb0em F;
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
        gm_b16_8 t9 = gm_f_empty_slice_s6l2ctu();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_5mao3rk(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_1sqb3ir F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5mao3rk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5mao3rk F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_7sj41hq F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8d45hjk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8d45hjk F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_a304ri6(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_a304ri6 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)96ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ub69oip(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ub69oip F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_v0p8ic9 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)12ULL), ((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_scope_edqq1v8 F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* mark */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 1608);
    uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)a1));
    gm_f_truncate_7r782uc(t3, t4);
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 1648);
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
    gm_f_truncate_daelje0(t6, t7);
    void * t8 = gm_l0;
    void * t9 = (void *)(((uint8_t *)t8) + 1728);
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
    gm_f_truncate_dsb08pk(t9, t10);
    void * t11 = gm_l0;
    void * t12 = (void *)(((uint8_t *)t11) + 1688);
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
    gm_f_truncate_upeumbt(t12, t13);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) uint8_t gm_f_template_instance_is_ovv1pcj(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_template_instance_is_ovv1pcj F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* k */
    void * gm_l2; /* eref */
    gm_t88rcucg1b7uk gm_l3; /* t */
    GM_STACK_CHECK(&F, 91u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 592));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 592) + 8));
    uint64_t t5 = gm_l1;
    if ((uintptr_t)t5 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t5, t4, gm_srcgr2mvgis129a, 154, 12);
    memmove(((uint8_t *)&F.l4), (((uint8_t *)t3) + (uintptr_t)t5 * 96), 96);
    void * t6 = gm_l0;
    void * t7 = (void *)((uint8_t *)&F.l4);
    void * t8 = gm_l2;
    gm_t88rcucg1b7uk t9 = gm_l3;
    uint8_t t10 = gm_f_template_probe_is_d1ep4c6((void *)NULL, cx, NULL, t6, t7, t8, t9);
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
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_7r782uc F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 24)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_daelje0 F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 24)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_dsb08pk F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 24)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_upeumbt F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 224)));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_type_ty_9ph2gbr F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)6ULL));
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
inline __attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unscope_type_params_bofhhep F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* before */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_f_restore_scope_edqq1v8(t2, &(*(gm_t66j1im9ip128 *)((uint8_t *)a1)));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_walk_v858co7 F;
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    F.l2 = a2;
    gm_closure t2 = F.l2;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_closure t4 = t2;
    gm_node *ccx5 = t4.cx;
    void * t6 = t4.code;
    gm_b0_1 t7;
    if (GM_PTR_TAGGED(t6)) {
        ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t6))(GM_CONV(gm_csguankko4lmhd, t3));
    } else {
        t7 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t6)(t4.env, ccx5, NULL, t3);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t8 = F.l0;
    void * t9 = (void *)(((uint8_t *)t8) + 8);
    gm_t88rcucg1b7uk t10 = gm_l1;
    gm_t53at80pfdus3 t11;
    gm_f_kind_jcnt4hm((void *)&t11, t9, GM_CONV(gm_csguankko4lmhd, t10));
    F.l3 = t11;
    void * t12 = (void *)((uint8_t *)&F.l3);
    gm_closure t13 = ((gm_closure){(void *)gm_f_fn_bfunt71, (void *)&F.h, cx});
    gm_f_for_each_component_o5cuh7c(t12, t13);
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
inline __attribute__((aligned(4))) uint8_t gm_f_arg_may_unify_qmi580g(void * a0, const void * a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_arg_may_unify_qmi580g F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l3; /* x */
    gm_t88rcucg1b7uk gm_l4; /* y */
    gm_t428ue2u3hf5g gm_l5; /* x */
    gm_t428ue2u3hf5g gm_l6; /* y */
    gm_t88rcucg1b7uk gm_l7; /* t */
    GM_STACK_CHECK(&F, 308u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l2, a2, sizeof F.l2);
    uint8_t gm_l8;
    gm_t53at80pfdus3 gm_l9;
    uint8_t gm_l10;
    gm_t53at80pfdus3 gm_l11;
    {
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
            if (!t18) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 8));
            gm_l3 = t19;
            {
                {
                    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)0ULL));
                    if (!t21) {
                        goto gm_lb4;
                    }
                    gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l2) + 8));
                    gm_l4 = t22;
                    void * t23 = gm_l0;
                    gm_t88rcucg1b7uk t24 = gm_l3;
                    gm_t88rcucg1b7uk t25 = gm_l4;
                    uint8_t t26 = gm_f_may_unify_0noeh56(t23, GM_CONV(gm_csguankko4lmhd, t24), GM_CONV(gm_csguankko4lmhd, t25));
                    F.ret = t26;
                    goto gm_fret;
                }
gm_lb4:;
                {
                    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)1ULL));
                    if (!t28) {
                        goto gm_lb5;
                    }
                    void * t29 = gm_l0;
                    void * t30 = (void *)(((uint8_t *)t29) + 8);
                    gm_t88rcucg1b7uk t31 = gm_l3;
                    gm_f_kind_jcnt4hm((void *)&gm_l9, t30, GM_CONV(gm_csguankko4lmhd, t31));
                    {
                        {
                            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)19ULL));
                            if (!t33) {
                                goto gm_lb7;
                            }
                            gm_l8 = ((uint8_t)1);
                            goto gm_lb6;
                        }
gm_lb7:;
                        {
                            gm_l8 = ((uint8_t)0);
                            goto gm_lb6;
                        }
gm_lb8:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 227, 33);
                    }
gm_lb6:;
                    uint8_t t34 = gm_l8;
                    F.ret = t34;
                    goto gm_fret;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 225, 21);
            }
gm_lb3:;
        }
gm_lb2:;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)1ULL));
            if (!t36) {
                goto gm_lb9;
            }
            memmove(((uint8_t *)&gm_l5), (((uint8_t *)a1) + 8), 32);
            {
                {
                    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t38 = (uint8_t)(t37 == ((uint8_t)1ULL));
                    if (!t38) {
                        goto gm_lb11;
                    }
                    memmove(((uint8_t *)&gm_l6), (((uint8_t *)&F.l2) + 8), 32);
                    void * t39 = (void *)((uint8_t *)&gm_l5);
                    void * t40 = (void *)((uint8_t *)&gm_l6);
                    int32_t t41 = gm_f_cmp_key_c6mirrv(t39, t40);
                    uint8_t t42 = ((t41 == ((int32_t)0LL)));
                    F.ret = t42;
                    goto gm_fret;
                }
gm_lb11:;
                {
                    uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t44 = (uint8_t)(t43 == ((uint8_t)0ULL));
                    if (!t44) {
                        goto gm_lb12;
                    }
                    gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l2) + 8));
                    gm_l7 = t45;
                    void * t46 = gm_l0;
                    void * t47 = (void *)(((uint8_t *)t46) + 8);
                    gm_t88rcucg1b7uk t48 = gm_l7;
                    gm_f_kind_jcnt4hm((void *)&gm_l11, t47, GM_CONV(gm_csguankko4lmhd, t48));
                    {
                        {
                            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)19ULL));
                            if (!t50) {
                                goto gm_lb14;
                            }
                            gm_l10 = ((uint8_t)1);
                            goto gm_lb13;
                        }
gm_lb14:;
                        {
                            gm_l10 = ((uint8_t)0);
                            goto gm_lb13;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 231, 32);
                    }
gm_lb13:;
                    uint8_t t51 = gm_l10;
                    F.ret = t51;
                    goto gm_fret;
                }
gm_lb12:;
                gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 229, 22);
            }
gm_lb10:;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 224, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_arg_may_unify_qmi580g_c(void * a0, gm_toa2vhe3l4hfr a1, gm_toa2vhe3l4hfr a2) { return gm_f_arg_may_unify_qmi580g(a0, &a1, &a2); }
inline __attribute__((aligned(4))) uint8_t gm_f_array_kind_same_rl3epej(gm_tnnjeerolos0a a0, gm_tnnjeerolos0a a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_array_kind_same_rl3epej F;
    gm_tnnjeerolos0a gm_l0; /* a */
    gm_tnnjeerolos0a gm_l1; /* b */
    gm_t88rcucg1b7uk gm_l2; /* x */
    gm_t88rcucg1b7uk gm_l3; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 111u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l4;
    uint8_t gm_l5;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)0ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_tnnjeerolos0a t11 = gm_l1;
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
            uint8_t t13 = (uint8_t)((t12 == ((uint8_t)0ULL)));
            gm_tnnjeerolos0a t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_l4 = t13;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)1ULL));
            if (!t16) {
                goto gm_lb3;
            }
            gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l0) + 4));
            gm_l2 = t17;
            {
                {
                    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    uint8_t t19 = (uint8_t)(t18 == ((uint8_t)1ULL));
                    if (!t19) {
                        goto gm_lb5;
                    }
                    gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l1) + 4));
                    gm_l3 = t20;
                    gm_t88rcucg1b7uk t21 = gm_l2;
                    gm_t88rcucg1b7uk t22 = gm_l3;
                    uint8_t t23 = (gm_eq9m6mvtmgde(((uint8_t *)&t21), ((uint8_t *)&t22)));
                    gm_l5 = t23;
                    goto gm_lb4;
                }
gm_lb5:;
                gm_l5 = ((uint8_t)0);
            }
gm_lb4:;
            uint8_t t24 = gm_l5;
            gm_l4 = t24;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)2ULL));
            if (!t26) {
                goto gm_lb6;
            }
            gm_tnnjeerolos0a t27 = gm_l1;
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
            uint8_t t29 = (uint8_t)((t28 == ((uint8_t)2ULL)));
            gm_tnnjeerolos0a t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)2ULL));
            gm_l4 = t29;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)3ULL));
            if (!t32) {
                goto gm_lb7;
            }
            gm_tnnjeerolos0a t33 = gm_l1;
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t33) + 0));
            uint8_t t35 = (uint8_t)((t34 == ((uint8_t)3ULL)));
            gm_tnnjeerolos0a t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)3ULL));
            gm_l4 = t35;
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 216, 57);
    }
gm_lb1:;
    uint8_t t37 = gm_l4;
    F.ret = t37;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_check_clause_1b0dqf0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_check_clause_1b0dqf0 F;
    void * gm_l0; /* s */
    void * gm_l3; /* c */
    gm_t66j1im9ip128 gm_l5; /* scope */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l11; /* ids */
    void * gm_l12; /* body */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* j */
    void * gm_l16; /* b */
    void * gm_l17; /* def */
    GM_STACK_CHECK(&F, 2695u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tabb710d9mq2e gm_l20;
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, (((uint8_t *)t15) + 344));
    uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t15) + 344) + 8));
    uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
    uint64_t t19 = (uint64_t)t18;
    if ((uintptr_t)t19 >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)t19, t17, gm_srcgr2mvgis129a, 271, 12);
    memmove(((uint8_t *)&F.l2), (((uint8_t *)t16) + (uintptr_t)t19 * 136), 136);
    void * t20 = GM_LD(void *, (((uint8_t *)a1) + 8));
    gm_l3 = t20;
    void * t21 = gm_l0;
    gm_f_enter_module_scope_011vo5a((void *)&F.l4, (void *)NULL, cx, NULL, t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t22 = gm_l0;
    gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 40));
    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
    gm_f_scope_type_params_2di4dm5((void *)&gm_l5, (void *)NULL, cx, NULL, t22, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t25;
    gm_f_list_af74s0q((void *)&t25, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t25;
    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 40));
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
                void * t30 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t32 = gm_l8;
                {
                    {
                        memmove(((uint8_t *)&F.l9), (((uint8_t *)t30) + (uintptr_t)t32 * 56), 56);
                        {
                            {
                                uint8_t t33 = (uint8_t)(((((uint8_t *)&F.l9) + 40))[4] != 0);
                                if (t33) {
                                    goto gm_lb7;
                                }
                                gm_l18 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l18 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 278, 12);
                        }
gm_lb6:;
                        uint8_t t34 = gm_l18;
                        if (t34) {
                            void * t35 = (void *)((uint8_t *)&F.l6);
                            gm_tftb1odc739t5 t36 = gm_f_push_o8u01sh(t35, &(*(gm_t5v0c8ia6a5mo *)((uint8_t *)&F.l9)));
                            gm_l19 = t36;
                            uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                            if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcgr2mvgis129a, 278, 32);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 277, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t38 = gm_l8;
            uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
            uint64_t t41 = r39;
            gm_l8 = t41;
        }
    }
gm_lb1:;
    void * t42 = gm_l0;
    uint64_t t43 = GM_LD(uint64_t, ((uint8_t *)a1));
    gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l2) + 56));
    gm_tnpifv20irnph t45 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l2) + 64));
    gm_tta5be0c75tfq t46;
    gm_f_fn_cx_hgvq15e((void *)&t46, (void *)NULL, cx, NULL, t42, t43, t44, t45);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l10 = t46;
    uint32_t t47 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
    gm_t9805qtonbsag t48;
    GM_ST(uint8_t, (uint8_t *)&t48 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t48 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t48), t47);
    ((uint8_t *)&t48)[4] = 1;
    GM_ST(gm_t9805qtonbsag, (((uint8_t *)&F.l10) + 400), t48);
    gm_t9qkh0rsooc8d t49 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a1) + 20));
    gm_t9805qtonbsag t50;
    memset(&t50, 0, sizeof t50);
    uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
    gm_t9805qtonbsag t52;
    GM_ST(uint8_t, (uint8_t *)&t52 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t52 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t52), t51);
    ((uint8_t *)&t52)[4] = 1;
    gm_tkn42u7na3t81 t53;
    memset(&t53, 0, sizeof t53);
    gm_tjvtgq0p0sehv t54;
    memset(&t54, 0, sizeof t54);
    gm_tkn42u7na3t81 t55;
    memset(&t55, 0, sizeof t55);
    gm_tiilmfd16v3fc t56 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)a1) + 24));
    gm_tqu3m2l3n8mnc t57;
    GM_ST(uint8_t, (uint8_t *)&t57 + 25, 0);
    GM_ST(uint16_t, (uint8_t *)&t57 + 26, 0);
    GM_ST(uint32_t, (uint8_t *)&t57 + 28, 0);
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t57), t56);
    ((uint8_t *)&t57)[24] = 1;
    gm_tnqmlaad6uchl t58;
    GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t58 + 28, 0);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t58 + 0), t49);
    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t58 + 4), t50);
    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t58 + 12), t52);
    GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t58 + 20), t53);
    GM_ST(gm_tjvtgq0p0sehv, ((uint8_t *)&t58 + 32), t54);
    GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t58 + 72), t55);
    GM_ST(gm_tqu3m2l3n8mnc, ((uint8_t *)&t58 + 80), t57);
    gm_t403eokj7ifg9 t59;
    GM_ST(uint8_t, (uint8_t *)&t59 + 113, 0);
    GM_ST(uint16_t, (uint8_t *)&t59 + 114, 0);
    GM_ST(uint32_t, (uint8_t *)&t59 + 116, 0);
    GM_ST(gm_tnqmlaad6uchl, ((uint8_t *)&t59), t58);
    ((uint8_t *)&t59)[112] = 1;
    GM_ST(gm_t403eokj7ifg9, (((uint8_t *)&F.l10) + 800), t59);
    void * t60 = gm_l3;
    uint64_t t61 = GM_LD(uint64_t, ((((uint8_t *)t60) + 32) + 8));
    uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
    uint8_t t63 = (!(t61 == t62));
    if (t63) {
        void * t64 = (void *)((uint8_t *)&F.l10);
        void * t65 = gm_l3;
        gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 56));
        void * t67 = gm_l3;
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)t67));
        uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
        gm_t3877pk7lv748 t70;
        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t68);
        GM_ST(uint64_t, ((uint8_t *)&t70 + 16), t69);
        gm_f_format_lveqqdp((void *)&gm_l20, (void *)NULL, cx, NULL, &t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
        if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srcgr2mvgis129a, 284, 26);
        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
        gm_b0_1 t74 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t64, t66, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t75 = (void *)((uint8_t *)&F.l10);
    void * t76 = gm_l3;
    gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
    gm_b16_8 t79 = gm_f_declare_clause_params_7bqbscg((void *)NULL, cx, NULL, t75, t76, t78);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t79;
    void * t80 = (void *)((uint8_t *)&F.l10);
    void * t81 = gm_l3;
    gm_t9qkh0rsooc8d t82 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a1) + 20));
    gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l2) + 56));
    void * t84 = gm_f_clause_value_tpeb80m((void *)NULL, cx, NULL, t80, t81, t82, t83);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l12 = t84;
    gm_l14 = ((uint64_t)0ULL);
    void * t85 = gm_l0;
    uint64_t t86 = GM_LD(uint64_t, ((((uint8_t *)t85) + 720) + 8));
    gm_l13 = t86;
    {
        for (;;) {
            uint64_t t87 = gm_l14;
            uint64_t t88 = gm_l13;
            uint8_t t89 = (t87 < t88);
            if (!t89) {
                goto gm_lb9;
            }
            {
                {
                    {
                        uint64_t t90 = gm_l14;
                        gm_l15 = t90;
                        void * t91 = gm_l0;
                        void * t92 = GM_LD(void *, (((uint8_t *)t91) + 720));
                        uint64_t t93 = GM_LD(uint64_t, ((((uint8_t *)t91) + 720) + 8));
                        uint64_t t94 = gm_l15;
                        if ((uintptr_t)t94 >= (uintptr_t)(t93)) gm_trap_index((uintptr_t)t94, t93, gm_srcgr2mvgis129a, 290, 15);
                        void * t95 = (void *)(((uint8_t *)t92) + (uintptr_t)t94 * 20);
                        gm_l16 = t95;
                        void * t96 = gm_l16;
                        uint32_t t97 = GM_LD(uint32_t, ((uint8_t *)t96));
                        uint32_t t98 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
                        uint8_t t99 = ((t97 == t98));
                        if (t99) {
                            void * t100 = gm_l16;
                            void * t101 = (void *)((uint8_t *)&F.l10);
                            void * t102 = gm_l16;
                            gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t102) + 4));
                            void * t104 = gm_l16;
                            gm_tl874t2atnlh3 t105 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t104) + 8));
                            gm_t88rcucg1b7uk t106 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t101, t103, t105);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)t100) + 4), t106);
                        }
                        goto gm_lb12;
                    }
gm_lb13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 289, 5);
                }
gm_lb12:;
            }
gm_lb11:;
            uint64_t t107 = gm_l14;
            uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
            uint64_t t110 = r108;
            gm_l14 = t110;
        }
    }
gm_lb9:;
    void * t111 = (void *)((uint8_t *)&F.l10);
    void * t112 = gm_l12;
    gm_b0_1 t113 = gm_f_finish_m8t0599((void *)NULL, cx, NULL, t111, t112);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t114 = gm_l0;
    void * t115 = GM_LD(void *, (((uint8_t *)t114) + 384));
    uint64_t t116 = GM_LD(uint64_t, ((((uint8_t *)t114) + 384) + 8));
    uint32_t t117 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
    uint64_t t118 = (uint64_t)t117;
    if ((uintptr_t)t118 >= (uintptr_t)(t116)) gm_trap_index((uintptr_t)t118, t116, gm_srcgr2mvgis129a, 294, 13);
    void * t119 = (void *)(((uint8_t *)t115) + (uintptr_t)t118 * 64);
    gm_l17 = t119;
    void * t120 = gm_l17;
    gm_b16_8 t121 = gm_l11;
    GM_ST(gm_b16_8, (((uint8_t *)t120) + 8), t121);
    void * t122 = gm_l17;
    gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 16));
    GM_ST(gm_b16_8, (((uint8_t *)t122) + 24), t123);
    void * t124 = gm_l17;
    void * t125 = gm_l12;
    GM_ST(void *, (((uint8_t *)t124) + 40), t125);
    void * t126 = gm_l0;
    gm_f_unscope_type_params_bofhhep(t126, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l5)));
    void * t127 = gm_l0;
    gm_f_leave_module_scope_4hp5mc4(t127, &(*(gm_td7pkamr64l0c *)((uint8_t *)&F.l4)));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_decl_breaks_pk9cnmn(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_decl_breaks_pk9cnmn F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tekl8c9g34a9k gm_l4; /* w */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l7; /* d */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint32_t gm_l10; /* f */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_taqu3436kubri gm_l13; /* b */
    GM_STACK_CHECK(&F, 1175u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_trqfvmjtt4sij t22;
    gm_f_list_7s4om10((void *)&t22, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t22;
    void * t23 = gm_l0;
    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t23) + 760));
    gm_l2 = t24;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t25 = gm_l3;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t27 = (t25 < t26);
            if (!t27) {
                goto gm_lb1;
            }
            {
                void * t28 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t30 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t28) + (uintptr_t)t30 * 32), 32);
                        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                        gm_l5 = t31;
                        gm_l6 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t32 = gm_l6;
                                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                uint8_t t34 = (t32 < t33);
                                if (!t34) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t35 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                    uint64_t t37 = gm_l6;
                                    {
                                        {
                                            uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)t35) + (uintptr_t)t37 * 4));
                                            gm_l7 = t38;
                                            void * t39 = gm_l0;
                                            void * t40 = GM_LD(void *, (((uint8_t *)t39) + 544));
                                            uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t39) + 544) + 8));
                                            uint32_t t42 = gm_l7;
                                            uint64_t t43 = (uint64_t)t42;
                                            if ((uintptr_t)t43 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t43, t41, gm_srcgr2mvgis129a, 308, 22);
                                            gm_b16_8 t44 = GM_LD(gm_b16_8, ((((uint8_t *)t40) + (uintptr_t)t43 * 64) + 48));
                                            gm_l8 = t44;
                                            gm_l9 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t45 = gm_l9;
                                                    uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                                    uint8_t t47 = (t45 < t46);
                                                    if (!t47) {
                                                        goto gm_lb11;
                                                    }
                                                    {
                                                        void * t48 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                                        uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                                        uint64_t t50 = gm_l9;
                                                        {
                                                            {
                                                                uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)t48) + (uintptr_t)t50 * 4));
                                                                gm_l10 = t51;
                                                                void * t52 = gm_l0;
                                                                gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)t52) + 720));
                                                                gm_l11 = t53;
                                                                gm_l12 = ((uint64_t)0ULL);
                                                                {
                                                                    for (;;) {
                                                                        uint64_t t54 = gm_l12;
                                                                        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                                                        uint8_t t56 = (t54 < t55);
                                                                        if (!t56) {
                                                                            goto gm_lb16;
                                                                        }
                                                                        {
                                                                            void * t57 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                                                            uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                                                            uint64_t t59 = gm_l12;
                                                                            {
                                                                                {
                                                                                    memmove(((uint8_t *)&gm_l13), (((uint8_t *)t57) + (uintptr_t)t59 * 20), 20);
                                                                                    uint32_t t60 = GM_LD(uint32_t, ((uint8_t *)&gm_l13));
                                                                                    uint32_t t61 = gm_l10;
                                                                                    uint8_t t62 = (!(t60 == t61));
                                                                                    if (t62) {
                                                                                        gm_l15 = ((uint8_t)1);
                                                                                    } else {
                                                                                        gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 4));
                                                                                        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 16));
                                                                                        uint8_t t65 = (gm_eq9m6mvtmgde(((uint8_t *)&t63), ((uint8_t *)&t64)));
                                                                                        gm_l15 = t65;
                                                                                    }
                                                                                    uint8_t t66 = gm_l15;
                                                                                    if (t66) {
                                                                                        goto gm_lb18;
                                                                                    }
                                                                                    void * t67 = gm_l0;
                                                                                    void * t68 = (void *)(((uint8_t *)t67) + 8);
                                                                                    gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 4));
                                                                                    gm_t53at80pfdus3 t70;
                                                                                    gm_f_kind_jcnt4hm((void *)&t70, t68, GM_CONV(gm_csguankko4lmhd, t69));
                                                                                    uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&t70) + 0));
                                                                                    uint8_t t72 = (uint8_t)((t71 == ((uint8_t)5ULL)));
                                                                                    gm_t53at80pfdus3 t73;
                                                                                    memset(&t73, 0, sizeof t73);
                                                                                    GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)5ULL));
                                                                                    if (t72) {
                                                                                        gm_l17 = ((uint8_t)1);
                                                                                    } else {
                                                                                        void * t74 = gm_l0;
                                                                                        void * t75 = (void *)(((uint8_t *)t74) + 8);
                                                                                        gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 4));
                                                                                        uint8_t t77 = gm_f_is_error_8k27gd1(t75, GM_CONV(gm_csguankko4lmhd, t76));
                                                                                        gm_l17 = t77;
                                                                                    }
                                                                                    uint8_t t78 = gm_l17;
                                                                                    if (t78) {
                                                                                        gm_l16 = ((uint8_t)1);
                                                                                    } else {
                                                                                        gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 8));
                                                                                        gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
                                                                                        uint8_t t81 = 0;
                                                                                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t80) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t80))) + i * 12, ((uint8_t *)&t79))) { t81 = 1; break; }
                                                                                        uint8_t t82 = (uint8_t)(t81);
                                                                                        gm_l16 = t82;
                                                                                    }
                                                                                    uint8_t t83 = gm_l16;
                                                                                    if (t83) {
                                                                                        goto gm_lb18;
                                                                                    }
                                                                                    void * t84 = (void *)((uint8_t *)&F.l1);
                                                                                    gm_tl874t2atnlh3 t85 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 8));
                                                                                    gm_tftb1odc739t5 t86 = gm_f_push_0mk7udh(t84, GM_CONV(gm_cseahvg8v7p0vs, t85));
                                                                                    gm_l18 = t86;
                                                                                    uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                                                                    if (!t87) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcgr2mvgis129a, 312, 21);
                                                                                    gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 8));
                                                                                    void * t89 = gm_l0;
                                                                                    gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 4));
                                                                                    gm_b16_8 t91 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t89, t90);
                                                                                    if (gm_unw.kind) {
                                                                                        goto gm_lad;
                                                                                    }
                                                                                    void * t92 = gm_l0;
                                                                                    gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 16));
                                                                                    gm_b16_8 t94 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t92, t93);
                                                                                    if (gm_unw.kind) {
                                                                                        goto gm_lad;
                                                                                    }
                                                                                    gm_t6j6smj7qoctu t95;
                                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t95 + 0), t91);
                                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t95 + 16), t94);
                                                                                    gm_f_format_klj2nqm((void *)&gm_l19, (void *)NULL, cx, NULL, &t95);
                                                                                    if (gm_unw.kind) {
                                                                                        goto gm_lad;
                                                                                    }
                                                                                    uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                                                                    if (!t96) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srcgr2mvgis129a, 313, 45);
                                                                                    gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                                                                                    gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
                                                                                    gm_t4gut2c9fj1kt t99;
                                                                                    gm_f_error_s6hn871((void *)&t99, (void *)NULL, cx, NULL, t88, t98);
                                                                                    if (gm_unw.kind) {
                                                                                        goto gm_lad;
                                                                                    }
                                                                                    F.l14 = t99;
                                                                                    void * t100 = (void *)((uint8_t *)&F.l14);
                                                                                    gm_tl874t2atnlh3 t101 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 20));
                                                                                    gm_b16_8 t102;
                                                                                    GM_ST(void *, (uint8_t *)&t102, (void *)(gm_sgjkgsqdq34nu));
                                                                                    GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(36));
                                                                                    gm_f_add_label_50fj7vv(t100, GM_CONV(gm_cseahvg8v7p0vs, t101), t102);
                                                                                    void * t103 = gm_l0;
                                                                                    gm_b0_1 t104 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t103, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l14)));
                                                                                    if (gm_unw.kind) {
                                                                                        goto gm_lad;
                                                                                    }
                                                                                    goto gm_lb19;
                                                                                }
gm_lb20:;
                                                                                gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 309, 17);
                                                                            }
gm_lb19:;
                                                                        }
gm_lb18:;
                                                                        uint64_t t105 = gm_l12;
                                                                        uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                                                                        uint64_t t108 = r106;
                                                                        gm_l12 = t108;
                                                                    }
                                                                }
gm_lb16:;
                                                                goto gm_lb14;
                                                            }
gm_lb15:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 308, 13);
                                                        }
gm_lb14:;
                                                    }
gm_lb13:;
                                                    uint64_t t109 = gm_l9;
                                                    uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
                                                    uint64_t t112 = r110;
                                                    gm_l9 = t112;
                                                }
                                            }
gm_lb11:;
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 307, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t113 = gm_l6;
                                uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                                uint64_t t116 = r114;
                                gm_l6 = t116;
                            }
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 306, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t117 = gm_l3;
            uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
            uint64_t t120 = r118;
            gm_l3 = t120;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_handler_clauses_n3lq2s1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_handler_clauses_n3lq2s1 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* taken */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 263u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l5;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 680));
    gm_l1 = t8;
    void * t9 = gm_l0;
    gm_trqfvmjtt4sij t10;
    gm_f_list_4kh12ib((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t9) + 680), t10);
    gm_b16_8 t11 = gm_l1;
    gm_l2 = t11;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l3;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t17 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t15) + (uintptr_t)t17 * 48), 48);
                        void * t18 = gm_l0;
                        void * t19 = GM_LD(void *, ((uint8_t *)t18));
                        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 200));
                        uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)t19) + 200) + 8));
                        uint64_t t22 = GM_LD(uint64_t, ((uint8_t *)&F.l4));
                        if ((uintptr_t)t22 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t22, t21, gm_srcgr2mvgis129a, 250, 12);
                        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t20) + (uintptr_t)t22 * 184) + 112));
                        if (t23) {
                            void * t24 = gm_l0;
                            void * t25 = (void *)(((uint8_t *)t24) + 680);
                            gm_tftb1odc739t5 t26 = gm_f_push_qkkb0em(t25, &(*(gm_tbeui4bbnm52b *)((uint8_t *)&F.l4)));
                            gm_l5 = t26;
                            uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                            if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcgr2mvgis129a, 251, 13);
                        } else {
                            void * t28 = gm_l0;
                            gm_b0_1 t29 = gm_f_check_clause_1b0dqf0((void *)NULL, cx, NULL, t28, &(*(gm_tbeui4bbnm52b *)((uint8_t *)&F.l4)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 247, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t30 = gm_l3;
            uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
            uint64_t t33 = r31;
            gm_l3 = t33;
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
inline __attribute__((aligned(4))) uint8_t gm_f_check_pending_clause_33df87a(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_check_pending_clause_33df87a F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 408u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tt01bthgiqsjo gm_l6;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 680));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_l2 = t9;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l3;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                uint64_t t13 = gm_l3;
                void * t14 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t16 = gm_l3;
                gm_tbeui4bbnm52b t17 = GM_LD(gm_tbeui4bbnm52b, (((uint8_t *)t14) + (uintptr_t)t16 * 48));
                gm_tt01bthgiqsjo t18;
                GM_ST(uint64_t, ((uint8_t *)&t18 + 0), t13);
                GM_ST(gm_tbeui4bbnm52b, ((uint8_t *)&t18 + 8), t17);
                gm_l6 = t18;
                {
                    {
                        uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
                        gm_l4 = t19;
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l6) + 8), 48);
                        uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 16));
                        uint32_t t21 = gm_l1;
                        uint8_t t22 = (!(t20 == t21));
                        if (t22) {
                            goto gm_lb3;
                        }
                        void * t23 = gm_l0;
                        void * t24 = gm_l0;
                        void * t25 = (void *)(((uint8_t *)t24) + 680);
                        uint64_t t26 = gm_l4;
                        gm_tbeui4bbnm52b t27;
                        gm_f_remove_76216q8((void *)&t27, t25, t26);
                        gm_b0_1 t28 = gm_f_check_clause_1b0dqf0((void *)NULL, cx, NULL, t23, &t27);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = ((uint8_t)1);
                        goto gm_fret;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 261, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t29 = gm_l3;
            uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
            uint64_t t32 = r30;
            gm_l3 = t32;
        }
    }
gm_lb1:;
    F.ret = ((uint8_t)0);
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_decl_clause_0i5kmeb(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_clause_0i5kmeb F;
    void * gm_l0; /* h */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 242u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = ((uint64_t)0ULL);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 64) + 8));
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
                        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 64));
                        uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 64) + 8));
                        uint64_t t16 = gm_l4;
                        if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcgr2mvgis129a, 238, 12);
                        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t14) + (uintptr_t)t16 * 72));
                        gm_b16_8 t18 = gm_l1;
                        uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
                        if (t19) {
                            uint64_t t20 = gm_l4;
                            gm_tf5srh4mp4b3i t21;
                            GM_ST(uint8_t, (uint8_t *)&t21 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t21 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t21 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t21), t20);
                            ((uint8_t *)&t21)[8] = 1;
                            F.ret = t21;
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 237, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t22 = gm_l3;
            uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
            uint64_t t25 = r23;
            gm_l3 = t25;
        }
    }
gm_lb1:;
    gm_tf5srh4mp4b3i t26;
    memset(&t26, 0, sizeof t26);
    F.ret = t26;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_handler_gd3gpcg(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_declare_handler_gd3gpcg F;
    uint64_t gm_l1; /* module */
    void * gm_l2; /* h */
    gm_b16_8 gm_l4; /* ps */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tnnjeerolos0a gm_l8; /* kind */
    gm_t88rcucg1b7uk gm_l9; /* ty */
    void * gm_l10; /* t */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t08olq97rscn3 gm_l13; /* n */
    uint32_t gm_l14; /* pid */
    gm_t66j1im9ip128 gm_l15; /* saved */
    gm_tqu3m2l3n8mnc gm_l16; /* r */
    gm_t88rcucg1b7uk gm_l17; /* for_ty */
    gm_tnpifv20irnph gm_l18; /* effects */
    gm_b16_8 gm_l19; /* list_ */
    gm_tiilmfd16v3fc gm_l20; /* eref */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint32_t gm_l25; /* g */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    uint32_t gm_l30; /* g */
    uint8_t gm_l32; /* duplicate */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* k */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    gm_b16_8 gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    uint8_t gm_l46; /* all */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    uint64_t gm_l49; /* i */
    gm_b16_8 gm_l51; /* <tmp> */
    uint64_t gm_l52; /* <tmp> */
    uint8_t gm_l54; /* known */
    gm_b16_8 gm_l55; /* <tmp> */
    uint64_t gm_l56; /* <tmp> */
    gm_b16_8 gm_l59; /* <tmp> */
    uint64_t gm_l60; /* <tmp> */
    uint64_t gm_l62; /* ci */
    gm_tep2drsl9408b gm_l63; /* sig */
    uint32_t gm_l65; /* fn_id */
    GM_STACK_CHECK(&F, 4516u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l66;
    uint8_t gm_l67;
    gm_tftb1odc739t5 gm_l68;
    gm_tiilmfd16v3fc gm_l69;
    gm_tftb1odc739t5 gm_l70;
    uint32_t gm_l71;
    gm_tabb710d9mq2e gm_l72;
    gm_tdr8bh2bm3fc8 gm_l73;
    gm_tabb710d9mq2e gm_l74;
    gm_tabb710d9mq2e gm_l75;
    gm_tf5srh4mp4b3i gm_l76;
    uint64_t gm_l77;
    gm_tabb710d9mq2e gm_l78;
    gm_tftb1odc739t5 gm_l79;
    gm_tftb1odc739t5 gm_l80;
    gm_tftb1odc739t5 gm_l81;
    gm_tftb1odc739t5 gm_l82;
    gm_tftb1odc739t5 gm_l83;
    gm_tftb1odc739t5 gm_l84;
    gm_tftb1odc739t5 gm_l85;
    {
        gm_trqfvmjtt4sij t77;
        gm_f_list_af74s0q((void *)&t77, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t77;
        {
            {
                void * t78 = gm_l2;
                uint8_t t79 = (uint8_t)((((uint8_t *)t78))[16] != 0);
                if (!t79) {
                    goto gm_lb2;
                }
                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)t78));
                gm_l4 = t80;
                gm_b16_8 t81 = gm_l4;
                gm_l5 = t81;
                gm_l6 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t83 = gm_l6;
                        uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint8_t t85 = (t83 < t84);
                        if (!t85) {
                            goto gm_lb3;
                        }
                        {
                            void * t86 = GM_LD(void *, ((uint8_t *)&gm_l5));
                            uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                            uint64_t t88 = gm_l6;
                            {
                                {
                                    memmove(((uint8_t *)&F.l7), (((uint8_t *)t86) + (uintptr_t)t88 * 56), 56);
                                    gm_tnnjeerolos0a t89;
                                    GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)0ULL));
                                    gm_l8 = t89;
                                    void * t90 = F.l0;
                                    void * t91 = (void *)(((uint8_t *)t90) + 8);
                                    gm_t88rcucg1b7uk t92 = gm_f_type_ty_9ph2gbr((void *)NULL, cx, NULL, t91);
                                    if (gm_unw.kind) {
                                        goto gm_ll82;
                                    }
                                    gm_l9 = t92;
                                    {
                                        {
                                            uint8_t t93 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l7) + 24)) != 0);
                                            if (!t93) {
                                                goto gm_lb9;
                                            }
                                            void * t94 = GM_LD(void *, (((uint8_t *)&F.l7) + 24));
                                            gm_l10 = t94;
                                            void * t95 = gm_l10;
                                            uint8_t t96 = gm_f_is_effects_kind_uutbf9u(t95);
                                            if (t96) {
                                                gm_tnnjeerolos0a t97;
                                                GM_ST(uint8_t, (uint8_t *)&t97 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t97 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t97 + 4, 0);
                                                GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)1ULL));
                                                gm_l8 = t97;
                                            } else {
                                                void * t98 = gm_l10;
                                                {
                                                    {
                                                        uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t98) + 8) + 0));
                                                        uint8_t t100 = (uint8_t)(t99 == ((uint8_t)6ULL));
                                                        if (!t100) {
                                                            goto gm_lb11;
                                                        }
                                                        gm_l67 = ((uint8_t)1);
                                                        goto gm_lb10;
                                                    }
gm_lb11:;
                                                    {
                                                        gm_l67 = ((uint8_t)0);
                                                        goto gm_lb10;
                                                    }
gm_lb12:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 28, 32);
                                                }
gm_lb10:;
                                                uint8_t t101 = gm_l67;
                                                if (t101) {
                                                    void * t102 = gm_l10;
                                                    gm_b16_8 t103;
                                                    GM_ST(void *, (uint8_t *)&t103, (void *)(gm_s2737i1iq57nf));
                                                    GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(4));
                                                    uint8_t t104 = gm_f_ident_is_uo88ron(t102, t103);
                                                    gm_l66 = t104;
                                                } else {
                                                    gm_l66 = ((uint8_t)0);
                                                }
                                                uint8_t t105 = gm_l66;
                                                uint8_t t106 = (uint8_t)!t105;
                                                if (t106) {
                                                    void * t107 = F.l0;
                                                    uint64_t t108 = gm_l1;
                                                    void * t109 = gm_l10;
                                                    gm_t88rcucg1b7uk t110 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t107, t108, t109);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll82;
                                                    }
                                                    gm_l9 = t110;
                                                    gm_t88rcucg1b7uk t111 = gm_l9;
                                                    gm_tnnjeerolos0a t112;
                                                    GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)2ULL));
                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t112 + 4), t111);
                                                    gm_l8 = t112;
                                                }
                                            }
                                            goto gm_lb8;
                                        }
gm_lb9:;
                                        void * t113 = F.l0;
                                        gm_tl874t2atnlh3 t114 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l7) + 40));
                                        gm_b16_8 t115;
                                        GM_ST(void *, (uint8_t *)&t115, (void *)(gm_shqja2jbdo2o0));
                                        GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(57));
                                        gm_b0_1 t116 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t113, t114, t115);
                                        if (gm_unw.kind) {
                                            goto gm_ll82;
                                        }
                                        goto gm_lb5;
                                    }
gm_lb8:;
                                    gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                                    gm_l11 = t117;
                                    gm_l12 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t119 = gm_l12;
                                            uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                            uint8_t t121 = (t119 < t120);
                                            if (!t121) {
                                                goto gm_lb13;
                                            }
                                            {
                                                void * t122 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                                uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                                uint64_t t124 = gm_l12;
                                                {
                                                    {
                                                        memmove(((uint8_t *)&gm_l13), (((uint8_t *)t122) + (uintptr_t)t124 * 32), 32);
                                                        void * t125 = F.l0;
                                                        gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
                                                        gm_tnnjeerolos0a t127 = gm_l8;
                                                        uint32_t t128 = gm_f_new_param_7010dt4((void *)NULL, cx, NULL, t125, t126, t127);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll118;
                                                        }
                                                        gm_l14 = t128;
                                                        void * t129 = (void *)((uint8_t *)&F.l3);
                                                        gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
                                                        gm_t88rcucg1b7uk t131 = gm_l9;
                                                        void * t132 = gm_l2;
                                                        gm_tl874t2atnlh3 t133 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t132) + 80));
                                                        uint32_t t134 = gm_l14;
                                                        gm_t9805qtonbsag t135;
                                                        GM_ST(uint8_t, (uint8_t *)&t135 + 5, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t135 + 6, 0);
                                                        GM_ST(uint32_t, ((uint8_t *)&t135), t134);
                                                        ((uint8_t *)&t135)[4] = 1;
                                                        gm_t5v0c8ia6a5mo t136;
                                                        GM_ST(uint8_t, (uint8_t *)&t136 + 49, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t136 + 50, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t136 + 52, 0);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t136 + 0), t130);
                                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t136 + 16), t131);
                                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t136 + 20), t133);
                                                        GM_ST(void *, ((uint8_t *)&t136 + 32), ((void *)0));
                                                        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t136 + 40), t135);
                                                        GM_ST(uint8_t, ((uint8_t *)&t136 + 48), ((uint8_t)0));
                                                        gm_tftb1odc739t5 t137 = gm_f_push_o8u01sh(t129, &t136);
                                                        gm_l68 = t137;
                                                        uint8_t t138 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                                                        if (!t138) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 1, gm_srcgr2mvgis129a, 38, 17);
                                                        goto gm_lb16;
                                                    }
gm_lb17:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 36, 13);
                                                }
gm_lb16:;
                                            }
gm_lb15:;
                                            uint64_t t139 = gm_l12;
                                            uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                                            uint64_t t142 = r140;
                                            gm_l12 = t142;
                                        }
                                    }
                                    goto gm_ls143;
gm_ll118:;
                                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb13; }
                                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb15; }
                                    }
                                    goto gm_ll82;
gm_ls143:;
gm_lb13:;
                                    goto gm_lb6;
                                }
gm_lb7:;
                                gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 22, 9);
                            }
gm_lb6:;
                        }
gm_lb5:;
                        uint64_t t144 = gm_l6;
                        uint64_t r145; bool o146 = __builtin_add_overflow(t144, ((uint64_t)1ULL), &r145);
                        uint64_t t147 = r145;
                        gm_l6 = t147;
                    }
                }
                goto gm_ls148;
gm_ll82:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb3; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb5; }
                }
                goto gm_lad;
gm_ls148:;
gm_lb3:;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        void * t149 = F.l0;
        gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
        gm_f_scope_type_params_2di4dm5((void *)&gm_l15, (void *)NULL, cx, NULL, t149, t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t152 = F.l0;
        uint64_t t153 = gm_l1;
        void * t154 = gm_l2;
        void * t155 = GM_LD(void *, (((uint8_t *)t154) + 24));
        gm_f_resolve_effect_ref_bggirs2((void *)&gm_l16, (void *)NULL, cx, NULL, t152, t153, t155);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t156 = F.l0;
        uint64_t t157 = gm_l1;
        void * t158 = gm_l2;
        void * t159 = GM_LD(void *, (((uint8_t *)t158) + 32));
        gm_t88rcucg1b7uk t160 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t156, t157, t159);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l17 = t160;
        gm_tnpifv20irnph t161 = gm_f_empty_effects_r0iv3pv();
        gm_l18 = t161;
        {
            {
                void * t162 = gm_l2;
                uint8_t t163 = (uint8_t)(((((uint8_t *)t162) + 40))[16] != 0);
                if (!t163) {
                    goto gm_lb19;
                }
                gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)t162) + 40));
                gm_l19 = t164;
                void * t165 = F.l0;
                uint64_t t166 = gm_l1;
                gm_b16_8 t167 = gm_l19;
                gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)&t167));
                gm_tnpifv20irnph t169 = gm_f_resolve_effect_list_uhjlh6o((void *)NULL, cx, NULL, t165, t166, t168);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t169;
                goto gm_lb18;
            }
gm_lb19:;
        }
gm_lb18:;
        void * t170 = F.l0;
        gm_f_unscope_type_params_bofhhep(t170, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l15)));
        uint8_t t171 = (uint8_t)((((uint8_t *)&gm_l16))[24] != 0);
        if (t171) {
            memmove(((uint8_t *)&gm_l69), ((uint8_t *)&gm_l16), 24);
        } else {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        memmove(((uint8_t *)&gm_l20), ((uint8_t *)&gm_l69), 24);
        void * t172 = F.l0;
        void * t173 = (void *)(((uint8_t *)t172) + 8);
        gm_t88rcucg1b7uk t174 = gm_l17;
        uint8_t t175 = gm_f_is_error_8k27gd1(t173, GM_CONV(gm_csguankko4lmhd, t174));
        if (t175) {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        gm_trqfvmjtt4sij t176;
        gm_f_list_go2qg0b((void *)&t176, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l21 = t176;
        gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_l22 = t177;
        gm_l23 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t179 = gm_l23;
                uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                uint8_t t181 = (t179 < t180);
                if (!t181) {
                    goto gm_lb20;
                }
                {
                    void * t182 = GM_LD(void *, ((uint8_t *)&gm_l22));
                    uint64_t t183 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                    uint64_t t184 = gm_l23;
                    {
                        {
                            memmove(((uint8_t *)&F.l24), (((uint8_t *)t182) + (uintptr_t)t184 * 56), 56);
                            {
                                {
                                    uint8_t t185 = (uint8_t)(((((uint8_t *)&F.l24) + 40))[4] != 0);
                                    if (!t185) {
                                        goto gm_lb26;
                                    }
                                    uint32_t t186 = GM_LD(uint32_t, (((uint8_t *)&F.l24) + 40));
                                    gm_l25 = t186;
                                    void * t187 = (void *)((uint8_t *)&F.l21);
                                    uint32_t t188 = gm_l25;
                                    gm_tftb1odc739t5 t189 = gm_f_push_im9d4ro(t187, t188);
                                    gm_l70 = t189;
                                    uint8_t t190 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                                    if (!t190) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 1, gm_srcgr2mvgis129a, 52, 29);
                                    goto gm_lb25;
                                }
gm_lb26:;
                            }
gm_lb25:;
                            goto gm_lb23;
                        }
gm_lb24:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 51, 5);
                    }
gm_lb23:;
                }
gm_lb22:;
                uint64_t t191 = gm_l23;
                uint64_t r192; bool o193 = __builtin_add_overflow(t191, ((uint64_t)1ULL), &r192);
                uint64_t t194 = r192;
                gm_l23 = t194;
            }
        }
gm_lb20:;
    }
    {
        uint64_t t195 = GM_LD(uint64_t, (((uint8_t *)&F.l21) + 8));
        uint8_t t196 = (t195 > ((uint64_t)0ULL));
        if (t196) {
            gm_trqfvmjtt4sij t197;
            gm_f_list_go2qg0b((void *)&t197, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l26 = t197;
            void * t198 = F.l0;
            gm_t88rcucg1b7uk t199 = gm_l17;
            gm_closure t200 = ((gm_closure){(void *)gm_f_fn_51177of, (void *)&F.h, cx});
            gm_f_walk_v858co7(t198, GM_CONV(gm_csguankko4lmhd, t199), t200);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
            gm_l27 = t201;
            gm_l28 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t203 = gm_l28;
                    uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                    uint8_t t205 = (t203 < t204);
                    if (!t205) {
                        goto gm_lb27;
                    }
                    {
                        void * t206 = GM_LD(void *, ((uint8_t *)&gm_l27));
                        uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                        uint64_t t208 = gm_l28;
                        {
                            {
                                memmove(((uint8_t *)&F.l29), (((uint8_t *)t206) + (uintptr_t)t208 * 56), 56);
                                uint8_t t209 = (uint8_t)(((((uint8_t *)&F.l29) + 40))[4] != 0);
                                if (t209) {
                                    uint32_t t210 = GM_LD(uint32_t, (((uint8_t *)&F.l29) + 40));
                                    gm_l71 = t210;
                                } else {
                                    goto gm_lb29;
                                }
                                uint32_t t211 = gm_l71;
                                gm_l30 = t211;
                                uint32_t t212 = gm_l30;
                                gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
                                uint8_t t214 = 0;
                                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t213) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t213))) + i * 4, ((uint8_t *)&t212))) { t214 = 1; break; }
                                uint8_t t215 = (uint8_t)(!t214);
                                if (t215) {
                                    void * t216 = F.l0;
                                    void * t217 = gm_l2;
                                    gm_tl874t2atnlh3 t218 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t217) + 80));
                                    gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&F.l29));
                                    gm_tnpifv20irnph t220;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t220 + 0), t219);
                                    gm_f_format_mgpfvh0((void *)&gm_l72, (void *)NULL, cx, NULL, t220);
                                    if (gm_unw.kind) {
                                        goto gm_ll202;
                                    }
                                    uint8_t t221 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                                    if (!t221) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 8, gm_srcgr2mvgis129a, 62, 33);
                                    gm_b16_8 t222 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l72) + 8));
                                    gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&t222));
                                    gm_b0_1 t224 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t216, t218, t223);
                                    if (gm_unw.kind) {
                                        goto gm_ll202;
                                    }
                                    F.ret = GM_ZERO(gm_b0_1);
                                    goto gm_fret;
                                }
                                goto gm_lb30;
                            }
gm_lb31:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 59, 9);
                        }
gm_lb30:;
                    }
gm_lb29:;
                    uint64_t t225 = gm_l28;
                    uint64_t r226; bool o227 = __builtin_add_overflow(t225, ((uint64_t)1ULL), &r226);
                    uint64_t t228 = r226;
                    gm_l28 = t228;
                }
            }
            goto gm_ls229;
gm_ll202:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb27; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb29; }
            }
            goto gm_lad;
gm_ls229:;
gm_lb27:;
        }
        void * t230 = F.l0;
        void * t231 = GM_LD(void *, (((uint8_t *)t230) + 1048));
        uint64_t t232 = GM_LD(uint64_t, ((((uint8_t *)t230) + 1048) + 8));
        uint32_t t233 = GM_LD(uint32_t, ((uint8_t *)&gm_l20));
        uint64_t t234 = (uint64_t)t233;
        if ((uintptr_t)t234 >= (uintptr_t)(t232)) gm_trap_index((uintptr_t)t234, t232, gm_srcgr2mvgis129a, 67, 13);
        memmove(((uint8_t *)&F.l31), (((uint8_t *)t231) + (uintptr_t)t234 * 56), 56);
    }
    {
        gm_l32 = ((uint8_t)0);
        uint64_t t235 = GM_LD(uint64_t, (((uint8_t *)&F.l21) + 8));
        uint8_t t236 = ((t235 == ((uint64_t)0ULL)));
        if (t236) {
            void * t237 = F.l0;
            gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)t237) + 544));
            gm_l33 = t238;
            gm_l34 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t240 = gm_l34;
                    uint64_t t241 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                    uint8_t t242 = (t240 < t241);
                    if (!t242) {
                        goto gm_lb32;
                    }
                    {
                        void * t243 = GM_LD(void *, ((uint8_t *)&gm_l33));
                        uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                        uint64_t t245 = gm_l34;
                        {
                            {
                                memmove(((uint8_t *)&F.l35), (((uint8_t *)t243) + (uintptr_t)t245 * 64), 64);
                                {
                                    {
                                        void * t246 = (void *)((uint8_t *)&F.l35);
                                        void * t247 = (void *)((uint8_t *)&gm_l20);
                                        int32_t t248 = gm_f_cmp_ref_8ntrqll(t246, t247);
                                        uint8_t t249 = ((t248 == ((int32_t)0LL)));
                                        if (!t249) {
                                            goto gm_lb38;
                                        }
                                        gm_t88rcucg1b7uk t250 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l35) + 24));
                                        gm_t88rcucg1b7uk t251 = gm_l17;
                                        uint8_t t252 = (gm_eq9m6mvtmgde(((uint8_t *)&t250), ((uint8_t *)&t251)));
                                        if (!t252) {
                                            goto gm_lb38;
                                        }
                                        gm_l32 = ((uint8_t)1);
                                        goto gm_lb37;
                                    }
gm_lb38:;
                                }
gm_lb37:;
                                goto gm_lb35;
                            }
gm_lb36:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 72, 9);
                        }
gm_lb35:;
                    }
gm_lb34:;
                    uint64_t t253 = gm_l34;
                    uint64_t r254; bool o255 = __builtin_add_overflow(t253, ((uint64_t)1ULL), &r254);
                    uint64_t t256 = r254;
                    gm_l34 = t256;
                }
            }
gm_lb32:;
            uint8_t t257 = gm_l32;
            uint8_t t258 = (uint8_t)!t257;
            if (t258) {
                gm_l37 = ((uint64_t)0ULL);
                void * t259 = F.l0;
                uint64_t t260 = GM_LD(uint64_t, ((((uint8_t *)t259) + 592) + 8));
                gm_l36 = t260;
                {
                    for (;;) {
                        uint64_t t262 = gm_l37;
                        uint64_t t263 = gm_l36;
                        uint8_t t264 = (t262 < t263);
                        if (!t264) {
                            goto gm_lb39;
                        }
                        {
                            {
                                {
                                    uint64_t t265 = gm_l37;
                                    gm_l38 = t265;
                                    void * t266 = F.l0;
                                    uint64_t t267 = gm_l38;
                                    void * t268 = (void *)((uint8_t *)&gm_l20);
                                    gm_t88rcucg1b7uk t269 = gm_l17;
                                    uint8_t t270 = gm_f_template_instance_is_ovv1pcj((void *)NULL, cx, NULL, t266, t267, t268, t269);
                                    if (gm_unw.kind) {
                                        goto gm_ll261;
                                    }
                                    if (t270) {
                                        gm_l32 = ((uint8_t)1);
                                    }
                                    goto gm_lb42;
                                }
gm_lb43:;
                                gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 76, 13);
                            }
gm_lb42:;
                        }
gm_lb41:;
                        uint64_t t271 = gm_l37;
                        uint64_t r272; bool o273 = __builtin_add_overflow(t271, ((uint64_t)1ULL), &r272);
                        uint64_t t274 = r272;
                        gm_l37 = t274;
                    }
                }
                goto gm_ls275;
gm_ll261:;
                if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb39; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb41; }
                }
                goto gm_lad;
gm_ls275:;
gm_lb39:;
            }
        } else {
            gm_b16_8 t276 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
            gm_t88rcucg1b7uk t277 = gm_l17;
            gm_tnpifv20irnph t278 = gm_l18;
            gm_tiilmfd16v3fc t279 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l20));
            gm_b16_8 t280 = gm_f_empty_slice_d914i5u();
            void * t281 = gm_l2;
            gm_tl874t2atnlh3 t282 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t281) + 80));
            gm_t9qmk75lrp9p4 t283;
            GM_ST(uint32_t, (uint8_t *)&t283 + 44, 0);
            GM_ST(uint32_t, (uint8_t *)&t283 + 92, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t283 + 0), t276);
            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t283 + 16), t279);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t283 + 40), t277);
            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t283 + 48), t278);
            GM_ST(gm_b16_8, ((uint8_t *)&t283 + 64), t280);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t283 + 80), t282);
            F.l39 = t283;
            void * t284 = F.l0;
            gm_b16_8 t285 = GM_LD(gm_b16_8, (((uint8_t *)t284) + 544));
            gm_l40 = t285;
            gm_l41 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t287 = gm_l41;
                    uint64_t t288 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                    uint8_t t289 = (t287 < t288);
                    if (!t289) {
                        goto gm_lb44;
                    }
                    {
                        void * t290 = GM_LD(void *, ((uint8_t *)&gm_l40));
                        uint64_t t291 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                        uint64_t t292 = gm_l41;
                        {
                            {
                                memmove(((uint8_t *)&F.l42), (((uint8_t *)t290) + (uintptr_t)t292 * 64), 64);
                                void * t293 = F.l0;
                                void * t294 = (void *)((uint8_t *)&F.l39);
                                void * t295 = (void *)((uint8_t *)&F.l42);
                                gm_t88rcucg1b7uk t296 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l42) + 24));
                                uint8_t t297 = gm_f_template_probe_is_d1ep4c6((void *)NULL, cx, NULL, t293, t294, t295, t296);
                                if (gm_unw.kind) {
                                    goto gm_ll286;
                                }
                                if (t297) {
                                    gm_l32 = ((uint8_t)1);
                                }
                                goto gm_lb47;
                            }
gm_lb48:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 82, 9);
                        }
gm_lb47:;
                    }
gm_lb46:;
                    uint64_t t298 = gm_l41;
                    uint64_t r299; bool o300 = __builtin_add_overflow(t298, ((uint64_t)1ULL), &r299);
                    uint64_t t301 = r299;
                    gm_l41 = t301;
                }
            }
            goto gm_ls302;
gm_ll286:;
            if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb44; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb46; }
            }
            goto gm_lad;
gm_ls302:;
gm_lb44:;
            void * t303 = F.l0;
            gm_b16_8 t304 = GM_LD(gm_b16_8, (((uint8_t *)t303) + 592));
            gm_l43 = t304;
            gm_l44 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t306 = gm_l44;
                    uint64_t t307 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                    uint8_t t308 = (t306 < t307);
                    if (!t308) {
                        goto gm_lb49;
                    }
                    {
                        void * t309 = GM_LD(void *, ((uint8_t *)&gm_l43));
                        uint64_t t310 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                        uint64_t t311 = gm_l44;
                        {
                            {
                                memmove(((uint8_t *)&F.l45), (((uint8_t *)t309) + (uintptr_t)t311 * 96), 96);
                                {
                                    {
                                        uint32_t t312 = GM_LD(uint32_t, (((uint8_t *)&F.l45) + 16));
                                        uint32_t t313 = GM_LD(uint32_t, ((uint8_t *)&gm_l20));
                                        uint8_t t314 = ((t312 == t313));
                                        if (!t314) {
                                            goto gm_lb55;
                                        }
                                        void * t315 = F.l0;
                                        gm_t88rcucg1b7uk t316 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l45) + 40));
                                        gm_t88rcucg1b7uk t317 = gm_l17;
                                        uint8_t t318 = gm_f_may_unify_0noeh56(t315, GM_CONV(gm_csguankko4lmhd, t316), GM_CONV(gm_csguankko4lmhd, t317));
                                        if (!t318) {
                                            goto gm_lb55;
                                        }
                                        uint64_t t319 = GM_LD(uint64_t, (((((uint8_t *)&F.l45) + 16) + 8) + 8));
                                        uint64_t t320 = GM_LD(uint64_t, ((((uint8_t *)&gm_l20) + 8) + 8));
                                        uint8_t t321 = ((t319 == t320));
                                        if (!t321) {
                                            goto gm_lb55;
                                        }
                                        gm_l46 = ((uint8_t)1);
                                        gm_b16_8 t322 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l45) + 16) + 8));
                                        gm_l47 = t322;
                                        gm_l48 = ((uint64_t)0ULL);
                                        {
                                            for (;;) {
                                                uint64_t t324 = gm_l48;
                                                uint64_t t325 = GM_LD(uint64_t, (((uint8_t *)&gm_l47) + 8));
                                                uint8_t t326 = (t324 < t325);
                                                if (!t326) {
                                                    goto gm_lb56;
                                                }
                                                {
                                                    uint64_t t327 = gm_l48;
                                                    void * t328 = GM_LD(void *, ((uint8_t *)&gm_l47));
                                                    uint64_t t329 = GM_LD(uint64_t, (((uint8_t *)&gm_l47) + 8));
                                                    uint64_t t330 = gm_l48;
                                                    gm_toa2vhe3l4hfr t331 = GM_LD(gm_toa2vhe3l4hfr, (((uint8_t *)t328) + (uintptr_t)t330 * 40));
                                                    gm_tdr8bh2bm3fc8 t332;
                                                    GM_ST(uint64_t, ((uint8_t *)&t332 + 0), t327);
                                                    GM_ST(gm_toa2vhe3l4hfr, ((uint8_t *)&t332 + 8), t331);
                                                    gm_l73 = t332;
                                                    {
                                                        {
                                                            uint64_t t333 = GM_LD(uint64_t, ((uint8_t *)&gm_l73));
                                                            gm_l49 = t333;
                                                            memmove(((uint8_t *)&F.l50), (((uint8_t *)&gm_l73) + 8), 40);
                                                            void * t334 = F.l0;
                                                            void * t335 = GM_LD(void *, (((uint8_t *)&gm_l20) + 8));
                                                            uint64_t t336 = GM_LD(uint64_t, ((((uint8_t *)&gm_l20) + 8) + 8));
                                                            uint64_t t337 = gm_l49;
                                                            if ((uintptr_t)t337 >= (uintptr_t)(t336)) gm_trap_index((uintptr_t)t337, t336, gm_srcgr2mvgis129a, 89, 48);
                                                            uint8_t t338 = gm_f_arg_may_unify_qmi580g(t334, &(*(gm_toa2vhe3l4hfr *)((uint8_t *)&F.l50)), &(*(gm_toa2vhe3l4hfr *)(((uint8_t *)t335) + (uintptr_t)t337 * 40)));
                                                            uint8_t t339 = (uint8_t)!t338;
                                                            if (t339) {
                                                                gm_l46 = ((uint8_t)0);
                                                            }
                                                            goto gm_lb59;
                                                        }
gm_lb60:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 88, 17);
                                                    }
gm_lb59:;
                                                }
gm_lb58:;
                                                uint64_t t340 = gm_l48;
                                                uint64_t r341; bool o342 = __builtin_add_overflow(t340, ((uint64_t)1ULL), &r341);
                                                uint64_t t343 = r341;
                                                gm_l48 = t343;
                                            }
                                        }
gm_lb56:;
                                        uint8_t t344 = gm_l46;
                                        if (t344) {
                                            gm_l32 = ((uint8_t)1);
                                        }
                                        goto gm_lb54;
                                    }
gm_lb55:;
                                }
gm_lb54:;
                                goto gm_lb52;
                            }
gm_lb53:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 85, 9);
                        }
gm_lb52:;
                    }
gm_lb51:;
                    uint64_t t345 = gm_l44;
                    uint64_t r346; bool o347 = __builtin_add_overflow(t345, ((uint64_t)1ULL), &r346);
                    uint64_t t348 = r346;
                    gm_l44 = t348;
                }
            }
gm_lb49:;
        }
    }
    {
        uint8_t t349 = gm_l32;
        if (t349) {
            void * t350 = F.l0;
            void * t351 = gm_l2;
            gm_tl874t2atnlh3 t352 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t351) + 80));
            gm_b16_8 t353 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
            void * t354 = F.l0;
            gm_t88rcucg1b7uk t355 = gm_l17;
            gm_b16_8 t356 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t354, t355);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t357;
            GM_ST(gm_b16_8, ((uint8_t *)&t357 + 0), t353);
            GM_ST(gm_b16_8, ((uint8_t *)&t357 + 16), t356);
            gm_f_format_8h6eueo((void *)&gm_l74, (void *)NULL, cx, NULL, &t357);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t358 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
            if (!t358) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 8, gm_srcgr2mvgis129a, 96, 25);
            gm_b16_8 t359 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l74) + 8));
            gm_b16_8 t360 = GM_LD(gm_b16_8, ((uint8_t *)&t359));
            gm_b0_1 t361 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t350, t352, t360);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        void * t362 = gm_l2;
        gm_b16_8 t363 = GM_LD(gm_b16_8, (((uint8_t *)t362) + 64));
        gm_l51 = t363;
        gm_l52 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t365 = gm_l52;
                uint64_t t366 = GM_LD(uint64_t, (((uint8_t *)&gm_l51) + 8));
                uint8_t t367 = (t365 < t366);
                if (!t367) {
                    goto gm_lb61;
                }
                {
                    void * t368 = GM_LD(void *, ((uint8_t *)&gm_l51));
                    uint64_t t369 = GM_LD(uint64_t, (((uint8_t *)&gm_l51) + 8));
                    uint64_t t370 = gm_l52;
                    {
                        {
                            memmove(((uint8_t *)&F.l53), (((uint8_t *)t368) + (uintptr_t)t370 * 72), 72);
                            gm_l54 = ((uint8_t)0);
                            gm_b16_8 t371 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 40));
                            gm_l55 = t371;
                            gm_l56 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t373 = gm_l56;
                                    uint64_t t374 = GM_LD(uint64_t, (((uint8_t *)&gm_l55) + 8));
                                    uint8_t t375 = (t373 < t374);
                                    if (!t375) {
                                        goto gm_lb66;
                                    }
                                    {
                                        void * t376 = GM_LD(void *, ((uint8_t *)&gm_l55));
                                        uint64_t t377 = GM_LD(uint64_t, (((uint8_t *)&gm_l55) + 8));
                                        uint64_t t378 = gm_l56;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l57), (((uint8_t *)t376) + (uintptr_t)t378 * 48), 48);
                                                gm_b16_8 t379 = GM_LD(gm_b16_8, ((uint8_t *)&F.l57));
                                                gm_b16_8 t380 = GM_LD(gm_b16_8, ((uint8_t *)&F.l53));
                                                uint8_t t381 = (gm_eq0iqiubfm7e(((uint8_t *)&t379), ((uint8_t *)&t380)));
                                                if (t381) {
                                                    gm_l54 = ((uint8_t)1);
                                                }
                                                goto gm_lb69;
                                            }
gm_lb70:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 101, 9);
                                        }
gm_lb69:;
                                    }
gm_lb68:;
                                    uint64_t t382 = gm_l56;
                                    uint64_t r383; bool o384 = __builtin_add_overflow(t382, ((uint64_t)1ULL), &r383);
                                    uint64_t t385 = r383;
                                    gm_l56 = t385;
                                }
                            }
gm_lb66:;
                            uint8_t t386 = gm_l54;
                            uint8_t t387 = (uint8_t)!t386;
                            if (t387) {
                                void * t388 = F.l0;
                                gm_tl874t2atnlh3 t389 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l53) + 16));
                                gm_b16_8 t390 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
                                gm_b16_8 t391 = GM_LD(gm_b16_8, ((uint8_t *)&F.l53));
                                gm_t6j6smj7qoctu t392;
                                GM_ST(gm_b16_8, ((uint8_t *)&t392 + 0), t390);
                                GM_ST(gm_b16_8, ((uint8_t *)&t392 + 16), t391);
                                gm_f_format_74vh99o((void *)&gm_l75, (void *)NULL, cx, NULL, &t392);
                                if (gm_unw.kind) {
                                    goto gm_ll364;
                                }
                                uint8_t t393 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                                if (!t393) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 8, gm_srcgr2mvgis129a, 104, 45);
                                gm_b16_8 t394 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l75) + 8));
                                gm_b16_8 t395 = GM_LD(gm_b16_8, ((uint8_t *)&t394));
                                gm_b0_1 t396 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t388, t389, t395);
                                if (gm_unw.kind) {
                                    goto gm_ll364;
                                }
                            }
                            goto gm_lb64;
                        }
gm_lb65:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 99, 5);
                    }
gm_lb64:;
                }
gm_lb63:;
                uint64_t t397 = gm_l52;
                uint64_t r398; bool o399 = __builtin_add_overflow(t397, ((uint64_t)1ULL), &r398);
                uint64_t t400 = r398;
                gm_l52 = t400;
            }
        }
        goto gm_ls401;
gm_ll364:;
        if (gm_unw.frame == &F.h && gm_unw.label == 9) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb61; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb63; }
        }
        goto gm_lad;
gm_ls401:;
gm_lb61:;
    }
    {
        gm_trqfvmjtt4sij t402;
        gm_f_list_go2qg0b((void *)&t402, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l58 = t402;
        gm_b16_8 t403 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 40));
        gm_l59 = t403;
        gm_l60 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t405 = gm_l60;
                uint64_t t406 = GM_LD(uint64_t, (((uint8_t *)&gm_l59) + 8));
                uint8_t t407 = (t405 < t406);
                if (!t407) {
                    goto gm_lb71;
                }
                {
                    void * t408 = GM_LD(void *, ((uint8_t *)&gm_l59));
                    uint64_t t409 = GM_LD(uint64_t, (((uint8_t *)&gm_l59) + 8));
                    uint64_t t410 = gm_l60;
                    {
                        {
                            memmove(((uint8_t *)&F.l61), (((uint8_t *)t408) + (uintptr_t)t410 * 48), 48);
                            void * t411 = gm_l2;
                            gm_b16_8 t412 = GM_LD(gm_b16_8, ((uint8_t *)&F.l61));
                            gm_tf5srh4mp4b3i t413 = gm_f_decl_clause_0i5kmeb(t411, t412);
                            gm_l76 = t413;
                            uint8_t t414 = (uint8_t)((((uint8_t *)&gm_l76))[8] != 0);
                            if (t414) {
                                uint64_t t415 = GM_LD(uint64_t, ((uint8_t *)&gm_l76));
                                gm_l77 = t415;
                            } else {
                                void * t416 = F.l0;
                                void * t417 = gm_l2;
                                gm_tl874t2atnlh3 t418 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t417) + 80));
                                gm_b16_8 t419 = GM_LD(gm_b16_8, ((uint8_t *)&F.l61));
                                gm_tnpifv20irnph t420;
                                GM_ST(gm_b16_8, ((uint8_t *)&t420 + 0), t419);
                                gm_f_format_tv3i6qj((void *)&gm_l78, (void *)NULL, cx, NULL, t420);
                                if (gm_unw.kind) {
                                    goto gm_ll404;
                                }
                                uint8_t t421 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                                if (!t421) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 8, gm_srcgr2mvgis129a, 109, 29);
                                gm_b16_8 t422 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l78) + 8));
                                gm_b16_8 t423 = GM_LD(gm_b16_8, ((uint8_t *)&t422));
                                gm_b0_1 t424 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t416, t418, t423);
                                if (gm_unw.kind) {
                                    goto gm_ll404;
                                }
                                goto gm_lb73;
                            }
                            uint64_t t425 = gm_l77;
                            gm_l62 = t425;
                            void * t426 = F.l0;
                            void * t427 = (void *)((uint8_t *)&gm_l20);
                            void * t428 = (void *)((uint8_t *)&F.l61);
                            gm_f_clause_signature_9v4rkv8((void *)&gm_l63, (void *)NULL, cx, NULL, t426, t427, t428);
                            if (gm_unw.kind) {
                                goto gm_ll404;
                            }
                            gm_trqfvmjtt4sij t429;
                            gm_f_list_af74s0q((void *)&t429, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll404;
                            }
                            F.l64 = t429;
                            void * t430 = (void *)((uint8_t *)&F.l64);
                            gm_b16_8 t431 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                            gm_b16_8 t432 = GM_LD(gm_b16_8, ((uint8_t *)&t431));
                            gm_tftb1odc739t5 t433 = gm_f_extend_vi643oe(t430, t432);
                            gm_l79 = t433;
                            uint8_t t434 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                            if (!t434) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l79) + 1, gm_srcgr2mvgis129a, 115, 9);
                            void * t435 = (void *)((uint8_t *)&F.l64);
                            gm_b16_8 t436;
                            GM_ST(void *, (uint8_t *)&t436, (void *)(gm_sho5nuaja1vvf));
                            GM_ST(uintptr_t, (uint8_t *)&t436 + 8, (uintptr_t)(4));
                            gm_t88rcucg1b7uk t437 = gm_l17;
                            void * t438 = gm_l2;
                            void * t439 = GM_LD(void *, (((uint8_t *)t438) + 32));
                            gm_tl874t2atnlh3 t440 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t439) + 72));
                            gm_t9805qtonbsag t441;
                            memset(&t441, 0, sizeof t441);
                            gm_t5v0c8ia6a5mo t442;
                            GM_ST(uint8_t, (uint8_t *)&t442 + 49, 0);
                            GM_ST(uint16_t, (uint8_t *)&t442 + 50, 0);
                            GM_ST(uint32_t, (uint8_t *)&t442 + 52, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t442 + 0), t436);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t442 + 16), t437);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t442 + 20), t440);
                            GM_ST(void *, ((uint8_t *)&t442 + 32), ((void *)0));
                            GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t442 + 40), t441);
                            GM_ST(uint8_t, ((uint8_t *)&t442 + 48), ((uint8_t)0));
                            gm_tftb1odc739t5 t443 = gm_f_push_o8u01sh(t435, &t442);
                            gm_l80 = t443;
                            uint8_t t444 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                            if (!t444) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 1, gm_srcgr2mvgis129a, 116, 9);
                            void * t445 = (void *)((uint8_t *)&F.l64);
                            gm_b16_8 t446 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l63));
                            gm_b16_8 t447 = GM_LD(gm_b16_8, ((uint8_t *)&t446));
                            gm_tftb1odc739t5 t448 = gm_f_extend_vi643oe(t445, t447);
                            gm_l81 = t448;
                            uint8_t t449 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                            if (!t449) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l81) + 1, gm_srcgr2mvgis129a, 117, 9);
                            void * t450 = F.l0;
                            gm_b16_8 t451 = GM_LD(gm_b16_8, ((uint8_t *)&F.l61));
                            void * t452 = gm_l2;
                            void * t453 = GM_LD(void *, (((uint8_t *)t452) + 64));
                            uint64_t t454 = GM_LD(uint64_t, ((((uint8_t *)t452) + 64) + 8));
                            uint64_t t455 = gm_l62;
                            if ((uintptr_t)t455 >= (uintptr_t)(t454)) gm_trap_index((uintptr_t)t455, t454, gm_srcgr2mvgis129a, 118, 51);
                            gm_tl874t2atnlh3 t456 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t453) + (uintptr_t)t455 * 72) + 56));
                            uint64_t t457 = gm_l1;
                            gm_b16_8 t458 = GM_LD(gm_b16_8, ((uint8_t *)&F.l64));
                            gm_t88rcucg1b7uk t459 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l63) + 16));
                            gm_tnpifv20irnph t460 = gm_l18;
                            gm_t9qkh0rsooc8d t461;
                            GM_ST(uint8_t, ((uint8_t *)&t461 + 0), ((uint8_t)2ULL));
                            uint32_t t462 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t450, t461, t451, t456, t457, t458, t459, t460);
                            if (gm_unw.kind) {
                                goto gm_ll404;
                            }
                            gm_l65 = t462;
                            void * t463 = F.l0;
                            void * t464 = GM_LD(void *, (((uint8_t *)t463) + 344));
                            uint64_t t465 = GM_LD(uint64_t, ((((uint8_t *)t463) + 344) + 8));
                            uint32_t t466 = gm_l65;
                            uint64_t t467 = (uint64_t)t466;
                            if ((uintptr_t)t467 >= (uintptr_t)(t465)) gm_trap_index((uintptr_t)t467, t465, gm_srcgr2mvgis129a, 120, 9);
                            gm_b16_8 t468 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
                            GM_ST(gm_b16_8, ((((uint8_t *)t464) + (uintptr_t)t467 * 136) + 80), t468);
                            void * t469 = (void *)((uint8_t *)&F.l58);
                            uint32_t t470 = gm_l65;
                            gm_tftb1odc739t5 t471 = gm_f_push_im9d4ro(t469, t470);
                            gm_l82 = t471;
                            uint8_t t472 = (uint8_t)!((((uint8_t *)&gm_l82))[0] != 0);
                            if (!t472) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l82) + 1, gm_srcgr2mvgis129a, 121, 9);
                            void * t473 = F.l0;
                            void * t474 = (void *)(((uint8_t *)t473) + 680);
                            uint64_t t475 = gm_l1;
                            void * t476 = gm_l2;
                            void * t477 = GM_LD(void *, (((uint8_t *)t476) + 64));
                            uint64_t t478 = GM_LD(uint64_t, ((((uint8_t *)t476) + 64) + 8));
                            uint64_t t479 = gm_l62;
                            if ((uintptr_t)t479 >= (uintptr_t)(t478)) gm_trap_index((uintptr_t)t479, t478, gm_srcgr2mvgis129a, 122, 76);
                            void * t480 = (void *)(((uint8_t *)t477) + (uintptr_t)t479 * 72);
                            uint32_t t481 = gm_l65;
                            gm_t9qkh0rsooc8d t482 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l61) + 16));
                            gm_tiilmfd16v3fc t483 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l20));
                            gm_tbeui4bbnm52b t484;
                            GM_ST(uint8_t, (uint8_t *)&t484 + 21, 0);
                            GM_ST(uint16_t, (uint8_t *)&t484 + 22, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t484 + 0), t475);
                            GM_ST(void *, ((uint8_t *)&t484 + 8), t480);
                            GM_ST(uint32_t, ((uint8_t *)&t484 + 16), t481);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t484 + 20), t482);
                            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t484 + 24), t483);
                            gm_tftb1odc739t5 t485 = gm_f_push_qkkb0em(t474, &t484);
                            gm_l83 = t485;
                            uint8_t t486 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
                            if (!t486) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l83) + 1, gm_srcgr2mvgis129a, 122, 9);
                            goto gm_lb74;
                        }
gm_lb75:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 107, 5);
                    }
gm_lb74:;
                }
gm_lb73:;
                uint64_t t487 = gm_l60;
                uint64_t r488; bool o489 = __builtin_add_overflow(t487, ((uint64_t)1ULL), &r488);
                uint64_t t490 = r488;
                gm_l60 = t490;
            }
        }
        goto gm_ls491;
gm_ll404:;
        if (gm_unw.frame == &F.h && gm_unw.label == 11) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb71; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb73; }
        }
        goto gm_lad;
gm_ls491:;
gm_lb71:;
    }
    {
        uint64_t t492 = GM_LD(uint64_t, (((uint8_t *)&F.l21) + 8));
        uint8_t t493 = ((t492 == ((uint64_t)0ULL)));
        if (t493) {
            void * t494 = F.l0;
            void * t495 = (void *)(((uint8_t *)t494) + 544);
            gm_t88rcucg1b7uk t496 = gm_l17;
            gm_tnpifv20irnph t497 = gm_l18;
            gm_b16_8 t498 = GM_LD(gm_b16_8, ((uint8_t *)&F.l58));
            gm_tiilmfd16v3fc t499 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l20));
            gm_tvc83pqnepc2e t500;
            GM_ST(uint32_t, (uint8_t *)&t500 + 28, 0);
            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t500 + 0), t499);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t500 + 24), t496);
            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t500 + 32), t497);
            GM_ST(gm_b16_8, ((uint8_t *)&t500 + 48), t498);
            gm_tftb1odc739t5 t501 = gm_f_push_mi6l2kj(t495, &t500);
            gm_l84 = t501;
            uint8_t t502 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
            if (!t502) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l84) + 1, gm_srcgr2mvgis129a, 125, 9);
        } else {
            void * t503 = F.l0;
            void * t504 = (void *)(((uint8_t *)t503) + 592);
            gm_b16_8 t505 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
            gm_t88rcucg1b7uk t506 = gm_l17;
            gm_tnpifv20irnph t507 = gm_l18;
            gm_b16_8 t508 = GM_LD(gm_b16_8, ((uint8_t *)&F.l58));
            void * t509 = gm_l2;
            gm_tl874t2atnlh3 t510 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t509) + 80));
            gm_tiilmfd16v3fc t511 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l20));
            gm_t9qmk75lrp9p4 t512;
            GM_ST(uint32_t, (uint8_t *)&t512 + 44, 0);
            GM_ST(uint32_t, (uint8_t *)&t512 + 92, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t512 + 0), t505);
            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t512 + 16), t511);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t512 + 40), t506);
            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t512 + 48), t507);
            GM_ST(gm_b16_8, ((uint8_t *)&t512 + 64), t508);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t512 + 80), t510);
            gm_tftb1odc739t5 t513 = gm_f_push_9gv7k4m(t504, &t512);
            gm_l85 = t513;
            uint8_t t514 = (uint8_t)!((((uint8_t *)&gm_l85))[0] != 0);
            if (!t514) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l85) + 1, gm_srcgr2mvgis129a, 127, 9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_declare_handlers_c3ac89r(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_declare_handlers_c3ac89r F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    void * gm_l5; /* h */
    GM_STACK_CHECK(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t9 = gm_l0;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t9) + 584));
    if (t10) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t11 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t11) + 584), ((uint8_t)1));
    gm_l2 = ((uint64_t)0ULL);
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t13) + 280) + 8));
    gm_l1 = t14;
    {
        for (;;) {
            uint64_t t15 = gm_l2;
            uint64_t t16 = gm_l1;
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t18 = gm_l2;
                        gm_l3 = t18;
                        void * t19 = gm_l0;
                        void * t20 = GM_LD(void *, ((uint8_t *)t19));
                        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 280));
                        uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 280) + 8));
                        uint64_t t23 = gm_l3;
                        if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcgr2mvgis129a, 12, 14);
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t21) + (uintptr_t)t23 * 168), 168);
                        {
                            {
                                uint8_t t24 = GM_LD(uint8_t, (((((uint8_t *)&F.l4) + 8) + 64) + 0));
                                uint8_t t25 = (uint8_t)(t24 == ((uint8_t)2ULL));
                                if (!t25) {
                                    goto gm_lb7;
                                }
                                void * t26 = GM_LD(void *, (((((uint8_t *)&F.l4) + 8) + 64) + 8));
                                gm_l5 = t26;
                                void * t27 = gm_l0;
                                uint64_t t28 = GM_LD(uint64_t, ((uint8_t *)&F.l4));
                                void * t29 = gm_l5;
                                gm_b0_1 t30 = gm_f_declare_handler_gd3gpcg((void *)NULL, cx, NULL, t27, t28, t29);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 11, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t31 = gm_l2;
            uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
            uint64_t t34 = r32;
            gm_l2 = t34;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_51177of(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_handler_template_args_dlm07kb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_handler_template_args_dlm07kb F;
    void * gm_l0; /* s */
    void * gm_l1; /* tpl */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l7; /* p */
    GM_STACK_CHECK(&F, 387u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tkn42u7na3t81 gm_l8;
    gm_t88rcucg1b7uk gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_trqfvmjtt4sij t7;
    gm_f_list_oalq03d((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t7;
    void * t8 = gm_l0;
    void * t9 = gm_l1;
    gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 40));
    gm_t88rcucg1b7uk t11 = gm_l2;
    void * t12 = (void *)((uint8_t *)&F.l3);
    gm_b0_1 t13 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t8, t10, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t14;
    gm_f_list_nof7vfj((void *)&t14, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t14;
    void * t15 = gm_l1;
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)t15));
    gm_l5 = t16;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t17 = gm_l6;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t19 = (t17 < t18);
            if (!t19) {
                goto gm_lb1;
            }
            {
                void * t20 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t22 = gm_l6;
                {
                    {
                        uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)t20) + (uintptr_t)t22 * 4));
                        gm_l7 = t23;
                        void * t24 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
                        uint32_t t27 = gm_l7;
                        gm_tkn42u7na3t81 t28 = gm_f_bound_to_5c11t0h(t26, t27);
                        gm_l8 = t28;
                        uint8_t t29 = (uint8_t)((((uint8_t *)&gm_l8))[4] != 0);
                        if (t29) {
                            gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l8));
                            gm_l9 = t30;
                        } else {
                            gm_th2lqbo77pvsc t31;
                            memset(&t31, 0, sizeof t31);
                            memmove(gm_out, ((uint8_t *)&t31), 24);
                            goto gm_fdone;
                        }
                        gm_t88rcucg1b7uk t32 = gm_l9;
                        gm_tftb1odc739t5 t33 = gm_f_push_k3l9nic(t24, GM_CONV(gm_csguankko4lmhd, t32));
                        gm_l10 = t33;
                        uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcgr2mvgis129a, 142, 29);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 142, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t35 = gm_l6;
            uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
            uint64_t t38 = r36;
            gm_l6 = t38;
        }
    }
gm_lb1:;
    gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
    gm_th2lqbo77pvsc t40;
    GM_ST(uint8_t, (uint8_t *)&t40 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t40 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t40 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t40), t39);
    ((uint8_t *)&t40)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t40), 24);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_handlers_of_type_5gareb4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_handlers_of_type_5gareb4 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* k */
    gm_b16_8 gm_l11; /* args */
    gm_b16_8 gm_l12; /* map */
    uint8_t gm_l13; /* repeated */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint32_t gm_l16; /* d */
    gm_t9805qtonbsag gm_l17; /* found */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_tm9oc6v1he136 gm_l20; /* inst */
    uint8_t gm_l21; /* same */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    gm_t88rcucg1b7uk gm_l25; /* a */
    uint32_t gm_l26; /* d */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    uint32_t gm_l30; /* c */
    uint32_t gm_l31; /* id */
    GM_STACK_CHECK(&F, 2032u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tcth15utccicj gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_th2lqbo77pvsc gm_l34;
    gm_b16_8 gm_l35;
    gm_tlcm4lvu3g1a8 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tftb1odc739t5 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    {
        void * t36 = gm_l0;
        gm_b0_1 t37 = gm_f_declare_handlers_c3ac89r((void *)NULL, cx, NULL, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t38;
        gm_f_list_go2qg0b((void *)&t38, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t38;
        void * t39 = gm_l0;
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)t39) + 544));
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
        gm_l3 = t41;
        gm_l4 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t42 = gm_l4;
                uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint8_t t44 = (t42 < t43);
                if (!t44) {
                    goto gm_lb1;
                }
                {
                    uint64_t t45 = gm_l4;
                    void * t46 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint64_t t48 = gm_l4;
                    gm_tvc83pqnepc2e t49 = GM_LD(gm_tvc83pqnepc2e, (((uint8_t *)t46) + (uintptr_t)t48 * 64));
                    gm_tcth15utccicj t50;
                    GM_ST(uint64_t, ((uint8_t *)&t50 + 0), t45);
                    GM_ST(gm_tvc83pqnepc2e, ((uint8_t *)&t50 + 8), t49);
                    gm_l32 = t50;
                    {
                        {
                            uint64_t t51 = GM_LD(uint64_t, ((uint8_t *)&gm_l32));
                            gm_l5 = t51;
                            memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l32) + 8), 64);
                            gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 24));
                            gm_t88rcucg1b7uk t53 = gm_l1;
                            uint8_t t54 = (gm_eq9m6mvtmgde(((uint8_t *)&t52), ((uint8_t *)&t53)));
                            if (t54) {
                                void * t55 = (void *)((uint8_t *)&F.l2);
                                uint64_t t56 = gm_l5;
                                uint32_t t57 = (uint32_t)t56;
                                gm_tftb1odc739t5 t58 = gm_f_push_im9d4ro(t55, t57);
                                gm_l33 = t58;
                                uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcgr2mvgis129a, 328, 28);
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 327, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t60 = gm_l4;
                uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                uint64_t t63 = r61;
                gm_l4 = t63;
            }
        }
gm_lb1:;
        gm_l8 = ((uint64_t)0ULL);
        void * t64 = gm_l0;
        uint64_t t65 = GM_LD(uint64_t, ((((uint8_t *)t64) + 592) + 8));
        gm_l7 = t65;
        {
            for (;;) {
                uint64_t t66 = gm_l8;
                uint64_t t67 = gm_l7;
                uint8_t t68 = (t66 < t67);
                if (!t68) {
                    goto gm_lb6;
                }
                {
                    {
                        {
                            uint64_t t69 = gm_l8;
                            gm_l9 = t69;
                            void * t70 = gm_l0;
                            void * t71 = GM_LD(void *, (((uint8_t *)t70) + 592));
                            uint64_t t72 = GM_LD(uint64_t, ((((uint8_t *)t70) + 592) + 8));
                            uint64_t t73 = gm_l9;
                            if ((uintptr_t)t73 >= (uintptr_t)(t72)) gm_trap_index((uintptr_t)t73, t72, gm_srcgr2mvgis129a, 331, 16);
                            memmove(((uint8_t *)&F.l10), (((uint8_t *)t71) + (uintptr_t)t73 * 96), 96);
                            void * t74 = gm_l0;
                            void * t75 = (void *)((uint8_t *)&F.l10);
                            gm_t88rcucg1b7uk t76 = gm_l1;
                            gm_f_handler_template_args_dlm07kb((void *)&gm_l34, (void *)NULL, cx, NULL, t74, t75, t76);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t77 = (uint8_t)((((uint8_t *)&gm_l34))[16] != 0);
                            if (t77) {
                                gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l34));
                                gm_l35 = t78;
                            } else {
                                goto gm_lb8;
                            }
                            gm_b16_8 t79 = gm_l35;
                            gm_l11 = t79;
                            void * t80 = (void *)((uint8_t *)&F.l10);
                            gm_b16_8 t81 = gm_l11;
                            gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                            gm_b16_8 t83 = gm_f_template_map_lfm02i0((void *)NULL, cx, NULL, t80, t82);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l12 = t83;
                            void * t84 = gm_l0;
                            gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 40));
                            gm_b16_8 t86 = gm_l12;
                            gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                            gm_t88rcucg1b7uk t88 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t84, t85, t87);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t88rcucg1b7uk t89 = gm_l1;
                            uint8_t t90 = (!gm_eq9m6mvtmgde(((uint8_t *)&t88), ((uint8_t *)&t89)));
                            if (t90) {
                                goto gm_lb8;
                            }
                            gm_l13 = ((uint8_t)0);
                            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
                            gm_l14 = t91;
                            gm_l15 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t92 = gm_l15;
                                    uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                    uint8_t t94 = (t92 < t93);
                                    if (!t94) {
                                        goto gm_lb11;
                                    }
                                    {
                                        void * t95 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                        uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                        uint64_t t97 = gm_l15;
                                        {
                                            {
                                                uint32_t t98 = GM_LD(uint32_t, (((uint8_t *)t95) + (uintptr_t)t97 * 4));
                                                gm_l16 = t98;
                                                void * t99 = gm_l0;
                                                void * t100 = GM_LD(void *, (((uint8_t *)t99) + 544));
                                                uint64_t t101 = GM_LD(uint64_t, ((((uint8_t *)t99) + 544) + 8));
                                                uint32_t t102 = gm_l16;
                                                uint64_t t103 = (uint64_t)t102;
                                                if ((uintptr_t)t103 >= (uintptr_t)(t101)) gm_trap_index((uintptr_t)t103, t101, gm_srcgr2mvgis129a, 338, 16);
                                                uint32_t t104 = GM_LD(uint32_t, (((uint8_t *)t100) + (uintptr_t)t103 * 64));
                                                uint32_t t105 = GM_LD(uint32_t, (((uint8_t *)&F.l10) + 16));
                                                uint8_t t106 = ((t104 == t105));
                                                if (t106) {
                                                    gm_l13 = ((uint8_t)1);
                                                }
                                                goto gm_lb14;
                                            }
gm_lb15:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 337, 9);
                                        }
gm_lb14:;
                                    }
gm_lb13:;
                                    uint64_t t107 = gm_l15;
                                    uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                                    uint64_t t110 = r108;
                                    gm_l15 = t110;
                                }
                            }
gm_lb11:;
                            uint8_t t111 = gm_l13;
                            if (t111) {
                                goto gm_lb8;
                            }
                            gm_t9805qtonbsag t112;
                            memset(&t112, 0, sizeof t112);
                            gm_l17 = t112;
                            void * t113 = gm_l0;
                            gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)t113) + 632));
                            gm_l18 = t114;
                            gm_l19 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t115 = gm_l19;
                                    uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                    uint8_t t117 = (t115 < t116);
                                    if (!t117) {
                                        goto gm_lb16;
                                    }
                                    {
                                        void * t118 = GM_LD(void *, ((uint8_t *)&gm_l18));
                                        uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                        uint64_t t120 = gm_l19;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l20), (((uint8_t *)t118) + (uintptr_t)t120 * 32), 32);
                                                {
                                                    {
                                                        uint64_t t121 = GM_LD(uint64_t, ((uint8_t *)&gm_l20));
                                                        uint64_t t122 = gm_l9;
                                                        uint8_t t123 = ((t121 == t122));
                                                        if (!t123) {
                                                            goto gm_lb22;
                                                        }
                                                        uint64_t t124 = GM_LD(uint64_t, ((((uint8_t *)&gm_l20) + 8) + 8));
                                                        uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                                        uint8_t t126 = ((t124 == t125));
                                                        if (!t126) {
                                                            goto gm_lb22;
                                                        }
                                                        gm_l21 = ((uint8_t)1);
                                                        gm_b16_8 t127 = gm_l11;
                                                        gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
                                                        gm_l22 = t128;
                                                        gm_l23 = ((uint64_t)0ULL);
                                                        {
                                                            for (;;) {
                                                                uint64_t t129 = gm_l23;
                                                                uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                                                uint8_t t131 = (t129 < t130);
                                                                if (!t131) {
                                                                    goto gm_lb23;
                                                                }
                                                                {
                                                                    uint64_t t132 = gm_l23;
                                                                    void * t133 = GM_LD(void *, ((uint8_t *)&gm_l22));
                                                                    uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                                                    uint64_t t135 = gm_l23;
                                                                    gm_t88rcucg1b7uk t136 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t133) + (uintptr_t)t135 * 4));
                                                                    gm_tlcm4lvu3g1a8 t137;
                                                                    GM_ST(uint32_t, (uint8_t *)&t137 + 12, 0);
                                                                    GM_ST(uint64_t, ((uint8_t *)&t137 + 0), t132);
                                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t137 + 8), t136);
                                                                    gm_l36 = t137;
                                                                    {
                                                                        {
                                                                            uint64_t t138 = GM_LD(uint64_t, ((uint8_t *)&gm_l36));
                                                                            gm_l24 = t138;
                                                                            gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l36) + 8));
                                                                            gm_l25 = t139;
                                                                            void * t140 = GM_LD(void *, (((uint8_t *)&gm_l20) + 8));
                                                                            uint64_t t141 = GM_LD(uint64_t, ((((uint8_t *)&gm_l20) + 8) + 8));
                                                                            uint64_t t142 = gm_l24;
                                                                            if ((uintptr_t)t142 >= (uintptr_t)(t141)) gm_trap_index((uintptr_t)t142, t141, gm_srcgr2mvgis129a, 346, 24);
                                                                            gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t140) + (uintptr_t)t142 * 4));
                                                                            gm_t88rcucg1b7uk t144 = gm_l25;
                                                                            uint8_t t145 = (!gm_eq9m6mvtmgde(((uint8_t *)&t143), ((uint8_t *)&t144)));
                                                                            if (t145) {
                                                                                gm_l21 = ((uint8_t)0);
                                                                            }
                                                                            goto gm_lb26;
                                                                        }
gm_lb27:;
                                                                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 345, 17);
                                                                    }
gm_lb26:;
                                                                }
gm_lb25:;
                                                                uint64_t t146 = gm_l23;
                                                                uint64_t r147; bool o148 = __builtin_add_overflow(t146, ((uint64_t)1ULL), &r147);
                                                                uint64_t t149 = r147;
                                                                gm_l23 = t149;
                                                            }
                                                        }
gm_lb23:;
                                                        uint8_t t150 = gm_l21;
                                                        if (t150) {
                                                            uint32_t t151 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 24));
                                                            gm_t9805qtonbsag t152;
                                                            GM_ST(uint8_t, (uint8_t *)&t152 + 5, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t152 + 6, 0);
                                                            GM_ST(uint32_t, ((uint8_t *)&t152), t151);
                                                            ((uint8_t *)&t152)[4] = 1;
                                                            gm_l17 = t152;
                                                        }
                                                        goto gm_lb21;
                                                    }
gm_lb22:;
                                                }
gm_lb21:;
                                                goto gm_lb19;
                                            }
gm_lb20:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 342, 9);
                                        }
gm_lb19:;
                                    }
gm_lb18:;
                                    uint64_t t153 = gm_l19;
                                    uint64_t r154; bool o155 = __builtin_add_overflow(t153, ((uint64_t)1ULL), &r154);
                                    uint64_t t156 = r154;
                                    gm_l19 = t156;
                                }
                            }
gm_lb16:;
                            {
                                {
                                    uint8_t t157 = (uint8_t)((((uint8_t *)&gm_l17))[4] != 0);
                                    if (!t157) {
                                        goto gm_lb29;
                                    }
                                    uint32_t t158 = GM_LD(uint32_t, ((uint8_t *)&gm_l17));
                                    gm_l26 = t158;
                                    void * t159 = (void *)((uint8_t *)&F.l2);
                                    uint32_t t160 = gm_l26;
                                    gm_tftb1odc739t5 t161 = gm_f_push_im9d4ro(t159, t160);
                                    gm_l37 = t161;
                                    uint8_t t162 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                    if (!t162) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_srcgr2mvgis129a, 352, 13);
                                    goto gm_lb8;
                                }
gm_lb29:;
                            }
gm_lb28:;
                            gm_trqfvmjtt4sij t163;
                            gm_f_list_go2qg0b((void *)&t163, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l27 = t163;
                            gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 64));
                            gm_l28 = t164;
                            gm_l29 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t165 = gm_l29;
                                    uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                    uint8_t t167 = (t165 < t166);
                                    if (!t167) {
                                        goto gm_lb30;
                                    }
                                    {
                                        void * t168 = GM_LD(void *, ((uint8_t *)&gm_l28));
                                        uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                        uint64_t t170 = gm_l29;
                                        {
                                            {
                                                uint32_t t171 = GM_LD(uint32_t, (((uint8_t *)t168) + (uintptr_t)t170 * 4));
                                                gm_l30 = t171;
                                                void * t172 = (void *)((uint8_t *)&F.l27);
                                                void * t173 = gm_l0;
                                                uint32_t t174 = gm_l30;
                                                gm_b16_8 t175 = gm_l11;
                                                gm_b16_8 t176 = GM_LD(gm_b16_8, ((uint8_t *)&t175));
                                                gm_tl874t2atnlh3 t177 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l10) + 80));
                                                uint32_t t178 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t173, t174, t176, t177);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_tftb1odc739t5 t179 = gm_f_push_im9d4ro(t172, t178);
                                                gm_l38 = t179;
                                                uint8_t t180 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                                if (!t180) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srcgr2mvgis129a, 356, 32);
                                                goto gm_lb33;
                                            }
gm_lb34:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 356, 9);
                                        }
gm_lb33:;
                                    }
gm_lb32:;
                                    uint64_t t181 = gm_l29;
                                    uint64_t r182; bool o183 = __builtin_add_overflow(t181, ((uint64_t)1ULL), &r182);
                                    uint64_t t184 = r182;
                                    gm_l29 = t184;
                                }
                            }
gm_lb30:;
                            void * t185 = gm_l0;
                            uint64_t t186 = GM_LD(uint64_t, ((((uint8_t *)t185) + 544) + 8));
                            uint32_t t187 = (uint32_t)t186;
                            gm_l31 = t187;
                            void * t188 = gm_l0;
                            void * t189 = (void *)(((uint8_t *)t188) + 544);
                            void * t190 = gm_l0;
                            void * t191 = (void *)(((uint8_t *)&F.l10) + 16);
                            gm_b16_8 t192 = gm_l12;
                            gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
                            gm_tiilmfd16v3fc t194;
                            gm_f_subst_eref_kf9o1q5((void *)&t194, (void *)NULL, cx, NULL, t190, t191, t193);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t88rcucg1b7uk t195 = gm_l1;
                            void * t196 = gm_l0;
                            void * t197 = (void *)(((uint8_t *)&F.l10) + 48);
                            gm_b16_8 t198 = gm_l12;
                            gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)&t198));
                            gm_tnpifv20irnph t200 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t196, t197, t199);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&F.l27));
                            gm_tvc83pqnepc2e t202;
                            GM_ST(uint32_t, (uint8_t *)&t202 + 28, 0);
                            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t202 + 0), t194);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t202 + 24), t195);
                            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t202 + 32), t200);
                            GM_ST(gm_b16_8, ((uint8_t *)&t202 + 48), t201);
                            gm_tftb1odc739t5 t203 = gm_f_push_mi6l2kj(t189, &t202);
                            gm_l39 = t203;
                            uint8_t t204 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                            if (!t204) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_srcgr2mvgis129a, 358, 9);
                            void * t205 = gm_l0;
                            void * t206 = (void *)(((uint8_t *)t205) + 632);
                            uint64_t t207 = gm_l9;
                            gm_b16_8 t208 = gm_l11;
                            gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                            uint32_t t210 = gm_l31;
                            gm_tm9oc6v1he136 t211;
                            GM_ST(uint32_t, (uint8_t *)&t211 + 28, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t211 + 0), t207);
                            GM_ST(gm_b16_8, ((uint8_t *)&t211 + 8), t209);
                            GM_ST(uint32_t, ((uint8_t *)&t211 + 24), t210);
                            gm_tftb1odc739t5 t212 = gm_f_push_b9c0fdf(t206, &t211);
                            gm_l40 = t212;
                            uint8_t t213 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                            if (!t213) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_srcgr2mvgis129a, 359, 9);
                            void * t214 = (void *)((uint8_t *)&F.l2);
                            uint32_t t215 = gm_l31;
                            gm_tftb1odc739t5 t216 = gm_f_push_im9d4ro(t214, t215);
                            gm_l41 = t216;
                            uint8_t t217 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                            if (!t217) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srcgr2mvgis129a, 360, 9);
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 330, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t218 = gm_l8;
                uint64_t r219; bool o220 = __builtin_add_overflow(t218, ((uint64_t)1ULL), &r219);
                uint64_t t221 = r219;
                gm_l8 = t221;
            }
        }
gm_lb6:;
    }
    {
        gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        F.ret = t222;
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
extern __attribute__((aligned(4))) uint8_t gm_f_ident_is_uo88ron(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_may_unify_0noeh56(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_may_unify_0noeh56 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* a */
    gm_t88rcucg1b7uk gm_l2; /* b */
    gm_t88rcucg1b7uk gm_l5; /* x */
    gm_t88rcucg1b7uk gm_l6; /* y */
    gm_t88rcucg1b7uk gm_l7; /* y */
    uint64_t gm_l8; /* m */
    gm_t88rcucg1b7uk gm_l9; /* x */
    gm_tnnjeerolos0a gm_l10; /* i */
    gm_t88rcucg1b7uk gm_l11; /* y */
    uint64_t gm_l12; /* n */
    gm_t88rcucg1b7uk gm_l13; /* y */
    gm_tnnjeerolos0a gm_l14; /* j */
    uint8_t gm_l15; /* c */
    gm_t88rcucg1b7uk gm_l16; /* x */
    uint8_t gm_l17; /* d */
    gm_t88rcucg1b7uk gm_l18; /* y */
    uint8_t gm_l19; /* c */
    gm_t88rcucg1b7uk gm_l20; /* x */
    uint8_t gm_l21; /* d */
    gm_t88rcucg1b7uk gm_l22; /* y */
    uint8_t gm_l23; /* c */
    gm_t88rcucg1b7uk gm_l24; /* x */
    uint8_t gm_l25; /* d */
    gm_t88rcucg1b7uk gm_l26; /* y */
    gm_t88rcucg1b7uk gm_l27; /* x */
    gm_t88rcucg1b7uk gm_l28; /* y */
    gm_t88rcucg1b7uk gm_l29; /* x */
    gm_t88rcucg1b7uk gm_l30; /* y */
    gm_t88rcucg1b7uk gm_l31; /* e1 */
    gm_t88rcucg1b7uk gm_l32; /* o1 */
    gm_t88rcucg1b7uk gm_l33; /* e2 */
    gm_t88rcucg1b7uk gm_l34; /* o2 */
    gm_b16_8 gm_l35; /* x */
    gm_b16_8 gm_l36; /* n */
    gm_b16_8 gm_l37; /* y */
    gm_b16_8 gm_l38; /* m */
    gm_b16_8 gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* i */
    gm_b16_8 gm_l42; /* nm */
    gm_b16_8 gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    uint64_t gm_l45; /* i */
    gm_t88rcucg1b7uk gm_l46; /* p */
    uint32_t gm_l47; /* x */
    gm_b16_8 gm_l48; /* xs */
    uint32_t gm_l49; /* y */
    gm_b16_8 gm_l50; /* ys */
    gm_b16_8 gm_l51; /* <tmp> */
    uint64_t gm_l52; /* <tmp> */
    uint64_t gm_l53; /* i */
    GM_STACK_CHECK(&F, 1678u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    uint8_t gm_l55;
    uint8_t gm_l56;
    uint8_t gm_l57;
    uint8_t gm_l58;
    uint8_t gm_l59;
    uint8_t gm_l60;
    uint8_t gm_l61;
    uint8_t gm_l62;
    uint8_t gm_l63;
    uint8_t gm_l64;
    gm_tf1bjg81u0glv gm_l65;
    gm_tlcm4lvu3g1a8 gm_l66;
    uint8_t gm_l67;
    gm_tdr8bh2bm3fc8 gm_l68;
    uint8_t gm_l69;
    uint8_t gm_l70;
    {
        gm_t88rcucg1b7uk t69 = gm_l1;
        gm_t88rcucg1b7uk t70 = gm_l2;
        uint8_t t71 = (gm_eq9m6mvtmgde(((uint8_t *)&t69), ((uint8_t *)&t70)));
        if (t71) {
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        void * t72 = gm_l0;
        void * t73 = (void *)(((uint8_t *)t72) + 8);
        gm_t88rcucg1b7uk t74 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&F.l3, t73, GM_CONV(gm_csguankko4lmhd, t74));
        void * t75 = gm_l0;
        void * t76 = (void *)(((uint8_t *)t75) + 8);
        gm_t88rcucg1b7uk t77 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&F.l4, t76, GM_CONV(gm_csguankko4lmhd, t77));
        {
            {
                uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t79 = (uint8_t)(t78 == ((uint8_t)19ULL));
                if (!t79) {
                    goto gm_lb2;
                }
                gm_l56 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l56 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 174, 8);
        }
gm_lb1:;
        uint8_t t80 = gm_l56;
        if (t80) {
            gm_l55 = ((uint8_t)1);
        } else {
            {
                {
                    uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t82 = (uint8_t)(t81 == ((uint8_t)19ULL));
                    if (!t82) {
                        goto gm_lb5;
                    }
                    gm_l57 = ((uint8_t)1);
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    gm_l57 = ((uint8_t)0);
                    goto gm_lb4;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 174, 27);
            }
gm_lb4:;
            uint8_t t83 = gm_l57;
            gm_l55 = t83;
        }
        uint8_t t84 = gm_l55;
        if (t84) {
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        {
            {
                {
                    uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t86 = (uint8_t)(t85 == ((uint8_t)20ULL));
                    if (!t86) {
                        goto gm_lb8;
                    }
                    gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
                    gm_l5 = t87;
                    {
                        {
                            {
                                {
                                    uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                    uint8_t t89 = (uint8_t)(t88 == ((uint8_t)20ULL));
                                    if (!t89) {
                                        goto gm_lb12;
                                    }
                                    gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
                                    gm_l6 = t90;
                                    goto gm_lb11;
                                }
gm_lb12:;
                                {
                                    uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                    uint8_t t92 = (uint8_t)(t91 == ((uint8_t)11ULL));
                                    if (!t92) {
                                        goto gm_lb13;
                                    }
                                    gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 8));
                                    gm_l6 = t93;
                                    goto gm_lb11;
                                }
gm_lb13:;
                                goto gm_lb10;
                            }
gm_lb11:;
                            void * t94 = gm_l0;
                            gm_t88rcucg1b7uk t95 = gm_l5;
                            gm_t88rcucg1b7uk t96 = gm_l6;
                            uint8_t t97 = gm_f_may_unify_0noeh56(t94, GM_CONV(gm_csguankko4lmhd, t95), GM_CONV(gm_csguankko4lmhd, t96));
                            F.ret = t97;
                            goto gm_fret;
                        }
gm_lb10:;
                        {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
gm_lb14:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 176, 30);
                    }
gm_lb9:;
                }
gm_lb8:;
                {
                    uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t99 = (uint8_t)(t98 == ((uint8_t)11ULL));
                    if (!t99) {
                        goto gm_lb15;
                    }
                    uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                    gm_l8 = t100;
                    gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 8));
                    gm_l9 = t101;
                    gm_tnnjeerolos0a t102 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&F.l3) + 8) + 12));
                    gm_l10 = t102;
                    {
                        {
                            uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                            uint8_t t104 = (uint8_t)(t103 == ((uint8_t)20ULL));
                            if (!t104) {
                                goto gm_lb17;
                            }
                            gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
                            gm_l11 = t105;
                            void * t106 = gm_l0;
                            gm_t88rcucg1b7uk t107 = gm_l9;
                            gm_t88rcucg1b7uk t108 = gm_l11;
                            uint8_t t109 = gm_f_may_unify_0noeh56(t106, GM_CONV(gm_csguankko4lmhd, t107), GM_CONV(gm_csguankko4lmhd, t108));
                            F.ret = t109;
                            goto gm_fret;
                        }
gm_lb17:;
                        {
                            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                            uint8_t t111 = (uint8_t)(t110 == ((uint8_t)11ULL));
                            if (!t111) {
                                goto gm_lb18;
                            }
                            uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                            gm_l12 = t112;
                            gm_t88rcucg1b7uk t113 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 8));
                            gm_l13 = t113;
                            gm_tnnjeerolos0a t114 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&F.l4) + 8) + 12));
                            gm_l14 = t114;
                            uint64_t t115 = gm_l8;
                            uint64_t t116 = gm_l12;
                            uint8_t t117 = ((t115 == t116));
                            if (t117) {
                                gm_tnnjeerolos0a t118 = gm_l10;
                                gm_tnnjeerolos0a t119 = gm_l14;
                                uint8_t t120 = gm_f_array_kind_same_rl3epej(t118, t119);
                                gm_l59 = t120;
                            } else {
                                gm_l59 = ((uint8_t)0);
                            }
                            uint8_t t121 = gm_l59;
                            if (t121) {
                                void * t122 = gm_l0;
                                gm_t88rcucg1b7uk t123 = gm_l9;
                                gm_t88rcucg1b7uk t124 = gm_l13;
                                uint8_t t125 = gm_f_may_unify_0noeh56(t122, GM_CONV(gm_csguankko4lmhd, t123), GM_CONV(gm_csguankko4lmhd, t124));
                                gm_l58 = t125;
                            } else {
                                gm_l58 = ((uint8_t)0);
                            }
                            uint8_t t126 = gm_l58;
                            F.ret = t126;
                            goto gm_fret;
                        }
gm_lb18:;
                        {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
gm_lb19:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 180, 28);
                    }
gm_lb16:;
                }
gm_lb15:;
            }
            {
                {
                    uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t128 = (uint8_t)(t127 == ((uint8_t)8ULL));
                    if (!t128) {
                        goto gm_lb20;
                    }
                    uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 8));
                    gm_l15 = t129;
                    gm_t88rcucg1b7uk t130 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
                    gm_l16 = t130;
                    {
                        {
                            uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                            uint8_t t132 = (uint8_t)(t131 == ((uint8_t)8ULL));
                            if (!t132) {
                                goto gm_lb22;
                            }
                            uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 8));
                            gm_l17 = t133;
                            gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
                            gm_l18 = t134;
                            uint8_t t135 = gm_l15;
                            uint8_t t136 = gm_l17;
                            uint8_t t137 = ((t135 == t136));
                            if (t137) {
                                void * t138 = gm_l0;
                                gm_t88rcucg1b7uk t139 = gm_l16;
                                gm_t88rcucg1b7uk t140 = gm_l18;
                                uint8_t t141 = gm_f_may_unify_0noeh56(t138, GM_CONV(gm_csguankko4lmhd, t139), GM_CONV(gm_csguankko4lmhd, t140));
                                gm_l60 = t141;
                            } else {
                                gm_l60 = ((uint8_t)0);
                            }
                            uint8_t t142 = gm_l60;
                            F.ret = t142;
                            goto gm_fret;
                        }
gm_lb22:;
                    }
gm_lb21:;
                    goto gm_lb7;
                }
gm_lb20:;
                {
                    uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t144 = (uint8_t)(t143 == ((uint8_t)10ULL));
                    if (!t144) {
                        goto gm_lb23;
                    }
                    uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 8));
                    gm_l19 = t145;
                    gm_t88rcucg1b7uk t146 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
                    gm_l20 = t146;
                    {
                        {
                            uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                            uint8_t t148 = (uint8_t)(t147 == ((uint8_t)10ULL));
                            if (!t148) {
                                goto gm_lb25;
                            }
                            uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 8));
                            gm_l21 = t149;
                            gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
                            gm_l22 = t150;
                            uint8_t t151 = gm_l19;
                            uint8_t t152 = gm_l21;
                            uint8_t t153 = ((t151 == t152));
                            if (t153) {
                                void * t154 = gm_l0;
                                gm_t88rcucg1b7uk t155 = gm_l20;
                                gm_t88rcucg1b7uk t156 = gm_l22;
                                uint8_t t157 = gm_f_may_unify_0noeh56(t154, GM_CONV(gm_csguankko4lmhd, t155), GM_CONV(gm_csguankko4lmhd, t156));
                                gm_l61 = t157;
                            } else {
                                gm_l61 = ((uint8_t)0);
                            }
                            uint8_t t158 = gm_l61;
                            F.ret = t158;
                            goto gm_fret;
                        }
gm_lb25:;
                    }
gm_lb24:;
                    goto gm_lb7;
                }
gm_lb23:;
                {
                    uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t160 = (uint8_t)(t159 == ((uint8_t)9ULL));
                    if (!t160) {
                        goto gm_lb26;
                    }
                    uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 8));
                    gm_l23 = t161;
                    gm_t88rcucg1b7uk t162 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
                    gm_l24 = t162;
                    {
                        {
                            uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                            uint8_t t164 = (uint8_t)(t163 == ((uint8_t)9ULL));
                            if (!t164) {
                                goto gm_lb28;
                            }
                            uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 8));
                            gm_l25 = t165;
                            gm_t88rcucg1b7uk t166 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
                            gm_l26 = t166;
                            uint8_t t167 = gm_l23;
                            uint8_t t168 = gm_l25;
                            uint8_t t169 = ((t167 == t168));
                            if (t169) {
                                void * t170 = gm_l0;
                                gm_t88rcucg1b7uk t171 = gm_l24;
                                gm_t88rcucg1b7uk t172 = gm_l26;
                                uint8_t t173 = gm_f_may_unify_0noeh56(t170, GM_CONV(gm_csguankko4lmhd, t171), GM_CONV(gm_csguankko4lmhd, t172));
                                gm_l62 = t173;
                            } else {
                                gm_l62 = ((uint8_t)0);
                            }
                            uint8_t t174 = gm_l62;
                            F.ret = t174;
                            goto gm_fret;
                        }
gm_lb28:;
                    }
gm_lb27:;
                    goto gm_lb7;
                }
gm_lb26:;
            }
            {
                {
                    uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t176 = (uint8_t)(t175 == ((uint8_t)13ULL));
                    if (!t176) {
                        goto gm_lb29;
                    }
                    gm_t88rcucg1b7uk t177 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l3) + 8));
                    gm_l27 = t177;
                    {
                        {
                            uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                            uint8_t t179 = (uint8_t)(t178 == ((uint8_t)13ULL));
                            if (!t179) {
                                goto gm_lb31;
                            }
                            gm_t88rcucg1b7uk t180 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
                            gm_l28 = t180;
                            void * t181 = gm_l0;
                            gm_t88rcucg1b7uk t182 = gm_l27;
                            gm_t88rcucg1b7uk t183 = gm_l28;
                            uint8_t t184 = gm_f_may_unify_0noeh56(t181, GM_CONV(gm_csguankko4lmhd, t182), GM_CONV(gm_csguankko4lmhd, t183));
                            F.ret = t184;
                            goto gm_fret;
                        }
gm_lb31:;
                    }
gm_lb30:;
                    goto gm_lb7;
                }
gm_lb29:;
                {
                    uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t186 = (uint8_t)(t185 == ((uint8_t)18ULL));
                    if (!t186) {
                        goto gm_lb32;
                    }
                    gm_t88rcucg1b7uk t187 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l3) + 8));
                    gm_l29 = t187;
                    {
                        {
                            uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                            uint8_t t189 = (uint8_t)(t188 == ((uint8_t)18ULL));
                            if (!t189) {
                                goto gm_lb34;
                            }
                            gm_t88rcucg1b7uk t190 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
                            gm_l30 = t190;
                            void * t191 = gm_l0;
                            gm_t88rcucg1b7uk t192 = gm_l29;
                            gm_t88rcucg1b7uk t193 = gm_l30;
                            uint8_t t194 = gm_f_may_unify_0noeh56(t191, GM_CONV(gm_csguankko4lmhd, t192), GM_CONV(gm_csguankko4lmhd, t193));
                            F.ret = t194;
                            goto gm_fret;
                        }
gm_lb34:;
                    }
gm_lb33:;
                    goto gm_lb7;
                }
gm_lb32:;
                {
                    uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t196 = (uint8_t)(t195 == ((uint8_t)14ULL));
                    if (!t196) {
                        goto gm_lb35;
                    }
                    gm_t88rcucg1b7uk t197 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l3) + 8));
                    gm_l31 = t197;
                    gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
                    gm_l32 = t198;
                    {
                        {
                            uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                            uint8_t t200 = (uint8_t)(t199 == ((uint8_t)14ULL));
                            if (!t200) {
                                goto gm_lb37;
                            }
                            gm_t88rcucg1b7uk t201 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
                            gm_l33 = t201;
                            gm_t88rcucg1b7uk t202 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
                            gm_l34 = t202;
                            void * t203 = gm_l0;
                            gm_t88rcucg1b7uk t204 = gm_l31;
                            gm_t88rcucg1b7uk t205 = gm_l33;
                            uint8_t t206 = gm_f_may_unify_0noeh56(t203, GM_CONV(gm_csguankko4lmhd, t204), GM_CONV(gm_csguankko4lmhd, t205));
                            if (t206) {
                                void * t207 = gm_l0;
                                gm_t88rcucg1b7uk t208 = gm_l32;
                                gm_t88rcucg1b7uk t209 = gm_l34;
                                uint8_t t210 = gm_f_may_unify_0noeh56(t207, GM_CONV(gm_csguankko4lmhd, t208), GM_CONV(gm_csguankko4lmhd, t209));
                                gm_l63 = t210;
                            } else {
                                gm_l63 = ((uint8_t)0);
                            }
                            uint8_t t211 = gm_l63;
                            F.ret = t211;
                            goto gm_fret;
                        }
gm_lb37:;
                    }
gm_lb36:;
                    goto gm_lb7;
                }
gm_lb35:;
                {
                    uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t213 = (uint8_t)(t212 == ((uint8_t)12ULL));
                    if (!t213) {
                        goto gm_lb38;
                    }
                    gm_b16_8 t214 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
                    gm_l35 = t214;
                    gm_b16_8 t215 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 16));
                    gm_l36 = t215;
                    {
                        {
                            uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                            uint8_t t217 = (uint8_t)(t216 == ((uint8_t)12ULL));
                            if (!t217) {
                                goto gm_lb40;
                            }
                            gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                            gm_l37 = t218;
                            gm_b16_8 t219 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 16));
                            gm_l38 = t219;
                            uint64_t t220 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                            uint64_t t221 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                            uint8_t t222 = (!(t220 == t221));
                            if (t222) {
                                gm_l64 = ((uint8_t)1);
                            } else {
                                uint64_t t223 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                                uint64_t t224 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                uint8_t t225 = (!(t223 == t224));
                                gm_l64 = t225;
                            }
                            uint8_t t226 = gm_l64;
                            if (t226) {
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            gm_b16_8 t227 = gm_l36;
                            gm_l39 = t227;
                            gm_l40 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t228 = gm_l40;
                                    uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                                    uint8_t t230 = (t228 < t229);
                                    if (!t230) {
                                        goto gm_lb41;
                                    }
                                    {
                                        uint64_t t231 = gm_l40;
                                        void * t232 = GM_LD(void *, ((uint8_t *)&gm_l39));
                                        uint64_t t233 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                                        uint64_t t234 = gm_l40;
                                        gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)t232) + (uintptr_t)t234 * 16));
                                        gm_tf1bjg81u0glv t236;
                                        GM_ST(uint64_t, ((uint8_t *)&t236 + 0), t231);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t236 + 8), t235);
                                        gm_l65 = t236;
                                        {
                                            {
                                                uint64_t t237 = GM_LD(uint64_t, ((uint8_t *)&gm_l65));
                                                gm_l41 = t237;
                                                gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
                                                gm_l42 = t238;
                                                gm_b16_8 t239 = gm_l42;
                                                void * t240 = GM_LD(void *, ((uint8_t *)&gm_l38));
                                                uint64_t t241 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                                uint64_t t242 = gm_l41;
                                                if ((uintptr_t)t242 >= (uintptr_t)(t241)) gm_trap_index((uintptr_t)t242, t241, gm_srcgr2mvgis129a, 194, 30);
                                                gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)t240) + (uintptr_t)t242 * 16));
                                                uint8_t t244 = (!gm_eq0iqiubfm7e(((uint8_t *)&t239), ((uint8_t *)&t243)));
                                                if (t244) {
                                                    F.ret = ((uint8_t)0);
                                                    goto gm_fret;
                                                }
                                                goto gm_lb44;
                                            }
gm_lb45:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 193, 13);
                                        }
gm_lb44:;
                                    }
gm_lb43:;
                                    uint64_t t245 = gm_l40;
                                    uint64_t r246; bool o247 = __builtin_add_overflow(t245, ((uint64_t)1ULL), &r246);
                                    uint64_t t248 = r246;
                                    gm_l40 = t248;
                                }
                            }
gm_lb41:;
                            gm_b16_8 t249 = gm_l35;
                            gm_l43 = t249;
                            gm_l44 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t250 = gm_l44;
                                    uint64_t t251 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                                    uint8_t t252 = (t250 < t251);
                                    if (!t252) {
                                        goto gm_lb46;
                                    }
                                    {
                                        uint64_t t253 = gm_l44;
                                        void * t254 = GM_LD(void *, ((uint8_t *)&gm_l43));
                                        uint64_t t255 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                                        uint64_t t256 = gm_l44;
                                        gm_t88rcucg1b7uk t257 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t254) + (uintptr_t)t256 * 4));
                                        gm_tlcm4lvu3g1a8 t258;
                                        GM_ST(uint32_t, (uint8_t *)&t258 + 12, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t258 + 0), t253);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t258 + 8), t257);
                                        gm_l66 = t258;
                                        {
                                            {
                                                uint64_t t259 = GM_LD(uint64_t, ((uint8_t *)&gm_l66));
                                                gm_l45 = t259;
                                                gm_t88rcucg1b7uk t260 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l66) + 8));
                                                gm_l46 = t260;
                                                void * t261 = gm_l0;
                                                gm_t88rcucg1b7uk t262 = gm_l46;
                                                void * t263 = GM_LD(void *, ((uint8_t *)&gm_l37));
                                                uint64_t t264 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                                                uint64_t t265 = gm_l45;
                                                if ((uintptr_t)t265 >= (uintptr_t)(t264)) gm_trap_index((uintptr_t)t265, t264, gm_srcgr2mvgis129a, 197, 40);
                                                gm_t88rcucg1b7uk t266 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t263) + (uintptr_t)t265 * 4));
                                                uint8_t t267 = gm_f_may_unify_0noeh56(t261, GM_CONV(gm_csguankko4lmhd, t262), GM_CONV(gm_csguankko4lmhd, t266));
                                                uint8_t t268 = (uint8_t)!t267;
                                                if (t268) {
                                                    F.ret = ((uint8_t)0);
                                                    goto gm_fret;
                                                }
                                                goto gm_lb49;
                                            }
gm_lb50:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 196, 13);
                                        }
gm_lb49:;
                                    }
gm_lb48:;
                                    uint64_t t269 = gm_l44;
                                    uint64_t r270; bool o271 = __builtin_add_overflow(t269, ((uint64_t)1ULL), &r270);
                                    uint64_t t272 = r270;
                                    gm_l44 = t272;
                                }
                            }
gm_lb46:;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
gm_lb40:;
                    }
gm_lb39:;
                    goto gm_lb7;
                }
gm_lb38:;
            }
            {
                {
                    uint8_t t273 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t274 = (uint8_t)(t273 == ((uint8_t)17ULL));
                    if (!t274) {
                        goto gm_lb51;
                    }
                    uint32_t t275 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 8));
                    gm_l47 = t275;
                    gm_b16_8 t276 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 8));
                    gm_l48 = t276;
                    {
                        {
                            uint8_t t277 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                            uint8_t t278 = (uint8_t)(t277 == ((uint8_t)17ULL));
                            if (!t278) {
                                goto gm_lb53;
                            }
                            uint32_t t279 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 8));
                            gm_l49 = t279;
                            gm_b16_8 t280 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 8));
                            gm_l50 = t280;
                            void * t281 = gm_l0;
                            void * t282 = (void *)(((uint8_t *)t281) + 8);
                            uint32_t t283 = gm_l47;
                            uint32_t t284 = gm_f_template_9o92cvv(t282, t283);
                            void * t285 = gm_l0;
                            void * t286 = (void *)(((uint8_t *)t285) + 8);
                            uint32_t t287 = gm_l49;
                            uint32_t t288 = gm_f_template_9o92cvv(t286, t287);
                            uint8_t t289 = (!(t284 == t288));
                            if (t289) {
                                gm_l67 = ((uint8_t)1);
                            } else {
                                uint64_t t290 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                                uint64_t t291 = GM_LD(uint64_t, (((uint8_t *)&gm_l50) + 8));
                                uint8_t t292 = (!(t290 == t291));
                                gm_l67 = t292;
                            }
                            uint8_t t293 = gm_l67;
                            if (t293) {
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            gm_b16_8 t294 = gm_l48;
                            gm_l51 = t294;
                            gm_l52 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t295 = gm_l52;
                                    uint64_t t296 = GM_LD(uint64_t, (((uint8_t *)&gm_l51) + 8));
                                    uint8_t t297 = (t295 < t296);
                                    if (!t297) {
                                        goto gm_lb54;
                                    }
                                    {
                                        uint64_t t298 = gm_l52;
                                        void * t299 = GM_LD(void *, ((uint8_t *)&gm_l51));
                                        uint64_t t300 = GM_LD(uint64_t, (((uint8_t *)&gm_l51) + 8));
                                        uint64_t t301 = gm_l52;
                                        gm_toa2vhe3l4hfr t302 = GM_LD(gm_toa2vhe3l4hfr, (((uint8_t *)t299) + (uintptr_t)t301 * 40));
                                        gm_tdr8bh2bm3fc8 t303;
                                        GM_ST(uint64_t, ((uint8_t *)&t303 + 0), t298);
                                        GM_ST(gm_toa2vhe3l4hfr, ((uint8_t *)&t303 + 8), t302);
                                        gm_l68 = t303;
                                        {
                                            {
                                                uint64_t t304 = GM_LD(uint64_t, ((uint8_t *)&gm_l68));
                                                gm_l53 = t304;
                                                memmove(((uint8_t *)&F.l54), (((uint8_t *)&gm_l68) + 8), 40);
                                                void * t305 = gm_l0;
                                                void * t306 = GM_LD(void *, ((uint8_t *)&gm_l50));
                                                uint64_t t307 = GM_LD(uint64_t, (((uint8_t *)&gm_l50) + 8));
                                                uint64_t t308 = gm_l53;
                                                if ((uintptr_t)t308 >= (uintptr_t)(t307)) gm_trap_index((uintptr_t)t308, t307, gm_srcgr2mvgis129a, 204, 44);
                                                uint8_t t309 = gm_f_arg_may_unify_qmi580g(t305, &(*(gm_toa2vhe3l4hfr *)((uint8_t *)&F.l54)), &(*(gm_toa2vhe3l4hfr *)(((uint8_t *)t306) + (uintptr_t)t308 * 40)));
                                                uint8_t t310 = (uint8_t)!t309;
                                                if (t310) {
                                                    F.ret = ((uint8_t)0);
                                                    goto gm_fret;
                                                }
                                                goto gm_lb57;
                                            }
gm_lb58:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 203, 13);
                                        }
gm_lb57:;
                                    }
gm_lb56:;
                                    uint64_t t311 = gm_l52;
                                    uint64_t r312; bool o313 = __builtin_add_overflow(t311, ((uint64_t)1ULL), &r312);
                                    uint64_t t314 = r312;
                                    gm_l52 = t314;
                                }
                            }
gm_lb54:;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
gm_lb53:;
                    }
gm_lb52:;
                    goto gm_lb7;
                }
gm_lb51:;
            }
            {
                {
                    uint8_t t315 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t316 = (uint8_t)(t315 == ((uint8_t)15ULL));
                    if (!t316) {
                        goto gm_lb59;
                    }
                    {
                        {
                            uint8_t t317 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                            uint8_t t318 = (uint8_t)(t317 == ((uint8_t)15ULL));
                            if (!t318) {
                                goto gm_lb61;
                            }
                            gm_l69 = ((uint8_t)1);
                            goto gm_lb60;
                        }
gm_lb61:;
                        {
                            gm_l69 = ((uint8_t)0);
                            goto gm_lb60;
                        }
gm_lb62:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 209, 26);
                    }
gm_lb60:;
                    uint8_t t319 = gm_l69;
                    F.ret = t319;
                    goto gm_fret;
                }
gm_lb59:;
                {
                    uint8_t t320 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t321 = (uint8_t)(t320 == ((uint8_t)16ULL));
                    if (!t321) {
                        goto gm_lb63;
                    }
                    {
                        {
                            uint8_t t322 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                            uint8_t t323 = (uint8_t)(t322 == ((uint8_t)16ULL));
                            if (!t323) {
                                goto gm_lb65;
                            }
                            gm_l70 = ((uint8_t)1);
                            goto gm_lb64;
                        }
gm_lb65:;
                        {
                            gm_l70 = ((uint8_t)0);
                            goto gm_lb64;
                        }
gm_lb66:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 210, 29);
                    }
gm_lb64:;
                    uint8_t t324 = gm_l70;
                    F.ret = t324;
                    goto gm_fret;
                }
gm_lb63:;
                {
                    goto gm_lb7;
                }
gm_lb67:;
                gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 175, 5);
            }
        }
gm_lb7:;
    }
    {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_template_instance_is_ovv1pcj(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_template_map_lfm02i0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_template_map_lfm02i0 F;
    void * gm_l0; /* tpl */
    gm_b16_8 gm_l1; /* args */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint32_t gm_l6; /* p */
    GM_STACK_CHECK(&F, 354u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tn68el8d14vku gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_trqfvmjtt4sij t7;
    gm_f_list_oalq03d((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t7;
    void * t8 = gm_l0;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)t8));
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
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
                uint64_t t14 = gm_l4;
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t17 = gm_l4;
                uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)t15) + (uintptr_t)t17 * 4));
                gm_tn68el8d14vku t19;
                GM_ST(uint32_t, (uint8_t *)&t19 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t19 + 0), t14);
                GM_ST(uint32_t, ((uint8_t *)&t19 + 8), t18);
                gm_l7 = t19;
                {
                    {
                        uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                        gm_l5 = t20;
                        uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 8));
                        gm_l6 = t21;
                        void * t22 = (void *)((uint8_t *)&F.l2);
                        uint32_t t23 = gm_l6;
                        void * t24 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t26 = gm_l5;
                        if ((uintptr_t)t26 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t26, t25, gm_srcgr2mvgis129a, 148, 77);
                        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t24) + (uintptr_t)t26 * 4));
                        gm_tplpbqf3duvn3 t28;
                        GM_ST(uint32_t, ((uint8_t *)&t28 + 0), t23);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t28 + 4), t27);
                        gm_tftb1odc739t5 t29 = gm_f_push_h69n5fi(t22, GM_CONV(gm_cs7e9ppsd384bj, t28));
                        gm_l8 = t29;
                        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcgr2mvgis129a, 148, 43);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 148, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t31 = gm_l4;
            uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
            uint64_t t34 = r32;
            gm_l4 = t34;
        }
    }
gm_lb1:;
    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
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
inline __attribute__((aligned(4))) uint8_t gm_f_template_probe_is_d1ep4c6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_template_probe_is_d1ep4c6 F;
    void * gm_l0; /* s */
    void * gm_l1; /* tpl */
    void * gm_l2; /* eref */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* args */
    gm_b16_8 gm_l5; /* map */
    gm_tiilmfd16v3fc gm_l6; /* inst */
    GM_STACK_CHECK(&F, 422u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_th2lqbo77pvsc gm_l7;
    gm_b16_8 gm_l8;
    void * t2 = gm_l1;
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 16));
    void * t4 = gm_l2;
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)t4));
    uint8_t t6 = (!(t3 == t5));
    if (t6) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t7 = gm_l0;
    void * t8 = gm_l1;
    gm_t88rcucg1b7uk t9 = gm_l3;
    gm_f_handler_template_args_dlm07kb((void *)&gm_l7, (void *)NULL, cx, NULL, t7, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l7))[16] != 0);
    if (t10) {
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
        gm_l8 = t11;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t12 = gm_l8;
    gm_l4 = t12;
    void * t13 = gm_l1;
    gm_b16_8 t14 = gm_l4;
    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
    gm_b16_8 t16 = gm_f_template_map_lfm02i0((void *)NULL, cx, NULL, t13, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t16;
    void * t17 = gm_l0;
    void * t18 = gm_l1;
    gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t18) + 40));
    gm_b16_8 t20 = gm_l5;
    gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
    gm_t88rcucg1b7uk t22 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t17, t19, t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t23 = gm_l3;
    uint8_t t24 = (!gm_eq9m6mvtmgde(((uint8_t *)&t22), ((uint8_t *)&t23)));
    if (t24) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t25 = gm_l0;
    void * t26 = gm_l1;
    void * t27 = (void *)(((uint8_t *)t26) + 16);
    gm_b16_8 t28 = gm_l5;
    gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
    gm_tiilmfd16v3fc t30;
    gm_f_subst_eref_kf9o1q5((void *)&t30, (void *)NULL, cx, NULL, t25, t27, t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t30;
    void * t31 = (void *)((uint8_t *)&gm_l6);
    void * t32 = gm_l2;
    int32_t t33 = gm_f_cmp_ref_8ntrqll(t31, t32);
    uint8_t t34 = ((t33 == ((int32_t)0LL)));
    F.ret = t34;
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
