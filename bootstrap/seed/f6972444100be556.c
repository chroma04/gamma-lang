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
typedef struct { uint64_t f0; void * f1; } gm_t22qobfhrnoif;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
_Static_assert(sizeof(gm_t22qobfhrnoif) == 16 && _Alignof(gm_t22qobfhrnoif) == 8 && offsetof(gm_t22qobfhrnoif, f0) == 0 && offsetof(gm_t22qobfhrnoif, f1) == 8, "layout of `(i: usize, x: ^Pat)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_th2lqbo77pvsc f0; gm_b16_8 f1; gm_toceffb697p0u f2; } gm_tqs4ilbobv1qq;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?(variants: []VariantDef, tag: Ty)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tqs4ilbobv1qq) == 56 && _Alignof(gm_tqs4ilbobv1qq) == 8 && offsetof(gm_tqs4ilbobv1qq, f0) == 0 && offsetof(gm_tqs4ilbobv1qq, f1) == 24 && offsetof(gm_tqs4ilbobv1qq, f2) == 40, "layout of `MatchReport`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; uint32_t v1; gm_tngtuiuskn938 v2; gm_b16_8 v3; } f1; } gm_taevtln836oa8;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tngtuiuskn938 f0; uint8_t f1; uint8_t pe[7]; } gm_tvegkb6ouf3m0;
typedef struct { uint64_t f0; gm_taevtln836oa8 f1; } gm_t1nq69orraetc;
_Static_assert(sizeof(gm_taevtln836oa8) == 104 && _Alignof(gm_taevtln836oa8) == 8 && offsetof(gm_taevtln836oa8, f0) == 0 && offsetof(gm_taevtln836oa8, f1) == 8, "layout of `Ctor`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_taevtln836oa8 f1; gm_b16_8 f2; } gm_tbq1t03a53gpo;
_Static_assert(sizeof(gm_tvegkb6ouf3m0) == 104 && _Alignof(gm_tvegkb6ouf3m0) == 8 && offsetof(gm_tvegkb6ouf3m0, f0) == 0 && offsetof(gm_tvegkb6ouf3m0, f1) == 96, "layout of `?Rat`");
_Static_assert(sizeof(gm_t1nq69orraetc) == 112 && _Alignof(gm_t1nq69orraetc) == 8 && offsetof(gm_t1nq69orraetc, f0) == 0 && offsetof(gm_t1nq69orraetc, f1) == 8, "layout of `(i: usize, x: Ctor)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_tbq1t03a53gpo v1; } f1; } gm_t8oufsr65o5ha;
_Static_assert(sizeof(gm_tbq1t03a53gpo) == 128 && _Alignof(gm_tbq1t03a53gpo) == 8 && offsetof(gm_tbq1t03a53gpo, f0) == 0 && offsetof(gm_tbq1t03a53gpo, f1) == 8 && offsetof(gm_tbq1t03a53gpo, f2) == 112, "layout of `(ty: Ty, ctor: Ctor, subs: []DPat)`");
_Static_assert(sizeof(gm_t8oufsr65o5ha) == 136 && _Alignof(gm_t8oufsr65o5ha) == 8 && offsetof(gm_t8oufsr65o5ha, f0) == 0 && offsetof(gm_t8oufsr65o5ha, f1) == 8, "layout of `DPat`");
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_all_ctors_o7aijfe {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_tngtuiuskn938 l9; /* r */
    gm_tbci5ospk5nnn l10; /* min */
    gm_tbci5ospk5nnn l11; /* end */
    gm_trqfvmjtt4sij l12; /* cuts */
    gm_t8oufsr65o5ha l15; /* d */
    gm_taevtln836oa8 l16; /* c */
    gm_tbci5ospk5nnn l17; /* lo */
    gm_tbci5ospk5nnn l18; /* hi */
    gm_trqfvmjtt4sij l19; /* uniq */
    gm_tbci5ospk5nnn l22; /* c */
    gm_tbci5ospk5nnn l26; /* a */
    gm_tbci5ospk5nnn l27; /* b */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_alternatives_r0dto2k {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_b16_8 ret;
};
struct gm_fr_check_match_mj577a4 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* rows */
    gm_trqfvmjtt4sij l5; /* unreachable */
    gm_t8oufsr65o5ha l14; /* alt */
    gm_trqfvmjtt4sij l15; /* one */
    gm_t8oufsr65o5ha l18; /* alt */
    gm_trqfvmjtt4sij l19; /* row */
    gm_trqfvmjtt4sij l20; /* q */
    gm_tqs4ilbobv1qq ret;
};
struct gm_fr_cmp_i1knrgu {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    int32_t ret;
};
struct gm_fr_column_covers_gn61qhr {
    gm_frame h;
    gm_t8oufsr65o5ha l4; /* d */
    gm_taevtln836oa8 l5; /* hc */
    uint8_t ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_covers_59m49eg {
    gm_frame h;
    gm_tbci5ospk5nnn l2; /* a */
    gm_tbci5ospk5nnn l3; /* b */
    gm_tbci5ospk5nnn l4; /* x */
    gm_tbci5ospk5nnn l5; /* y */
    uint8_t ret;
};
struct gm_fr_ctor_eq_fhbfduc {
    gm_frame h;
    gm_tbci5ospk5nnn l4; /* lo */
    gm_tbci5ospk5nnn l5; /* hi */
    gm_tbci5ospk5nnn l6; /* l2 */
    gm_tbci5ospk5nnn l7; /* h2 */
    uint8_t ret;
};
struct gm_fr_deinit_kuh4e9u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_dpat_ty_2ipsk5e {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6rs1n5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_c8h0sd2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kvdmouk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_sv0bumj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_extend_7hs67uj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_eht4gub {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_lfqasgq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_first_uncovered_fefms0c {
    gm_frame h;
    gm_taevtln836oa8 l5; /* c */
    gm_trqfvmjtt4sij l7; /* seen */
    gm_t8oufsr65o5ha l12; /* d */
    gm_taevtln836oa8 l17; /* c */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_fn_j6p61la {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_format_08505jg {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_0gv6kss {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5k1v0hr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_99gc605 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_f6rro2t {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_g0ibdjv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hj7ahc4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_i83sdhh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_word_n4uc7oo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_bounds_7npnb0b {
    gm_frame h;
    gm_tbci5ospk5nnn l7; /* half */
    gm_tvegkb6ouf3m0 ret;
};
struct gm_fr_irrefutable_lpkmeii {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* rows */
    gm_t8oufsr65o5ha l5; /* d */
    gm_trqfvmjtt4sij l6; /* row */
    gm_trqfvmjtt4sij l7; /* q */
    uint8_t ret;
};
struct gm_fr_is_name_char_eanps9t {
    gm_frame h;
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
struct gm_fr_list_99597qp {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ahp090e {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_furncbs {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ghuba9i {
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
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lower_product_5gakcnb {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_tbci5ospk5nnn l11; /* v */
    gm_tbci5ospk5nnn l13; /* lo */
    gm_tbci5ospk5nnn l14; /* hi */
    gm_tbci5ospk5nnn l16; /* h */
    gm_trqfvmjtt4sij l22; /* subs */
    gm_trqfvmjtt4sij l27; /* first */
    gm_trqfvmjtt4sij l31; /* combos */
    gm_trqfvmjtt4sij l36; /* next */
    gm_t8oufsr65o5ha l42; /* o */
    gm_trqfvmjtt4sij l43; /* c2 */
    gm_b16_8 ret;
};
struct gm_fr_make_r4pbu77 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* m */
    gm_trqfvmjtt4sij l2; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_nested_present_binding_r48665i {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_push_33sq608 {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_95uqvlq {
    gm_frame h;
    gm_taevtln836oa8 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_h9qsb4d {
    gm_frame h;
    gm_t8oufsr65o5ha l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_s94euqb {
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
struct gm_fr_rebuild_90secge {
    gm_frame h;
    gm_taevtln836oa8 l1; /* c */
    gm_trqfvmjtt4sij l5; /* out */
    gm_b16_8 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_1dn6qmo {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h5h9b2d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_trq7lbp {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_vf4k680 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_shl_qqecl81 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_show_int_ub6npub {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* v */
    gm_b16_8 ret;
};
struct gm_fr_show_witness_fmsa425 {
    gm_frame h;
    gm_taevtln836oa8 l3; /* c */
    gm_tbci5ospk5nnn l6; /* lo */
    gm_tbci5ospk5nnn l7; /* hi */
    gm_trqfvmjtt4sij l9; /* out */
    gm_trqfvmjtt4sij l13; /* inner */
    gm_trqfvmjtt4sij l18; /* parts */
    gm_t0mglksrkfa5e l23; /* f */
    gm_b16_8 ret;
};
struct gm_fr_sort_by_sbaob8l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_specialize_si0k4v5 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_taevtln836oa8 l7; /* hc */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_sub_pktm5cr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_sub_types_o2rpif3 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_t0mglksrkfa5e l11; /* f */
    gm_b16_8 ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_useful_ge1iad5 {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* column */
    gm_taevtln836oa8 l9; /* c */
    gm_trqfvmjtt4sij l10; /* pieces */
    gm_tbci5ospk5nnn l11; /* lo */
    gm_tbci5ospk5nnn l12; /* hi */
    gm_trqfvmjtt4sij l13; /* with_head */
    gm_taevtln836oa8 l17; /* p */
    gm_tbci5ospk5nnn l18; /* x */
    gm_tbci5ospk5nnn l19; /* y */
    gm_taevtln836oa8 l22; /* piece */
    gm_trqfvmjtt4sij l24; /* spec_rows */
    gm_taevtln836oa8 l36; /* c */
    gm_trqfvmjtt4sij l38; /* spec_rows */
    gm_trqfvmjtt4sij l45; /* default */
    gm_t8oufsr65o5ha l50; /* first */
    gm_taevtln836oa8 l53; /* c */
    gm_trqfvmjtt4sij l54; /* subs */
    gm_trqfvmjtt4sij l58; /* out */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_wrap_each_2nbqohg {
    gm_frame h;
    gm_taevtln836oa8 l1; /* c */
    gm_t8oufsr65o5ha l6; /* d */
    gm_trqfvmjtt4sij l7; /* subs */
    gm_b0_1 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_all_ctors_o7aijfe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_alternatives_r0dto2k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
__attribute__((aligned(4))) void gm_f_check_match_mj577a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_column_covers_gn61qhr(gm_b16_8 a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_covers_59m49eg(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_ctor_eq_fhbfduc(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_deinit_kuh4e9u(void * a0);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_dpat_ty_2ipsk5e(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6rs1n5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c8h0sd2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kvdmouk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_sv0bumj(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_eht4gub(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_lfqasgq(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_first_uncovered_fefms0c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_j6p61la(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_01lon97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_08505jg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_0gv6kss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5k1v0hr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_99gc605(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_g0ibdjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_hj7ahc4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_i83sdhh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) uint8_t gm_f_has_word_n4uc7oo(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_int_bounds_7npnb0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_introsort_jhfnj7n(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_irrefutable_lpkmeii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_name_char_eanps9t(uint8_t a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_99597qp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ahp090e(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_furncbs(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ghuba9i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_lower_product_5gakcnb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_nested_present_binding_r48665i(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_33sq608(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_95uqvlq(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h9qsb4d(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s94euqb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_rebuild_90secge(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, uint64_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1dn6qmo(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h5h9b2d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_trq7lbp(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vf4k680(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_witness_fmsa425(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_sort_by_sbaob8l(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_specialize_si0k4v5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sub_types_o2rpif3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_useful_ge1iad5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_wrap_each_2nbqohg(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_debug_string_pns0400(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern const uint8_t gm_s521on3hdq96i[4];
extern const uint8_t gm_s8gf7bnv27fi9[7];
extern const uint8_t gm_savtbnmf9bjnr[17];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfqk3rro4g7t0[2];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_shq9t6lutffbf[4];
extern const uint8_t gm_sjg4ie5b5ufsm[8];
extern const uint8_t gm_smhvmmegaur89[7];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_squedv51820an[5];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_src7h8dni243k2v[];
extern const char gm_src8lmgjh8bsvgp[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srua7hn34dkbr[8];
extern const uint8_t gm_suftpl3n8736g[5];
extern const gm_tmpl gm_tpl2qrjfa8j0fl5;
extern const gm_tmpl gm_tpl4476c5bksft1;
extern const gm_tmpl gm_tpl4vjhulqo9e1j;
extern const gm_tmpl gm_tpldrlq7uo0cbrm;
extern const gm_tmpl gm_tplehc5f52ftol1;
extern const gm_tmpl gm_tplnv6l3ecgreid;
extern const gm_tmpl gm_tplqfu63ajpo70a;
extern const gm_tmpl gm_tplvle6f1rmlmjh;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_420lj6o F;
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)a1));
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t2, t4, t5, t7);
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
inline __attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_i1knrgu F;
    int32_t gm_l2; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)a1));
    uint8_t t4 = (!(t2 == t3));
    if (t4) {
        uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)a0));
        if (t5) {
            F.ret = ((int32_t)-1LL);
            goto gm_fret;
        }
        F.ret = ((int32_t)1LL);
        goto gm_fret;
    }
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    int32_t t10 = gm_f_mag_cmp_s39veq6(t7, t9);
    gm_l2 = t10;
    uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)a0));
    if (t11) {
        int32_t t12 = gm_l2;
        if (t12 == ((int32_t)-2147483648LL)) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 360, 28);
        int32_t t13 = (int32_t)((uint32_t)0 - (uint32_t)t12);
        F.ret = t13;
        goto gm_fret;
    }
    int32_t t14 = gm_l2;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_deinit_kuh4e9u(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_kuh4e9u F;
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
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)1ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)1ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_s1lbe5s();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6rs1n5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6rs1n5s F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 136)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c8h0sd2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c8h0sd2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kvdmouk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kvdmouk F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 104)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_sv0bumj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_sv0bumj F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_eht4gub(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_eht4gub F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_1dn6qmo(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 136)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 136);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 136)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_lfqasgq(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_lfqasgq F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_j4iu2lk(t12, t13, t14);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_j6p61la(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_j6p61la F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = gm_l1;
    int32_t t4 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)t2)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)t3)));
    uint8_t t5 = (t4 < ((int32_t)0LL));
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_08505jg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_08505jg F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s521on3hdq96i));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4vjhulqo9e1j), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_0gv6kss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_0gv6kss F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suftpl3n8736g));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqfu63ajpo70a), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5k1v0hr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_5k1v0hr F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_savtbnmf9bjnr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvle6f1rmlmjh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_99gc605(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_99gc605 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smhvmmegaur89));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnv6l3ecgreid), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_f6rro2t F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s8gf7bnv27fi9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4476c5bksft1), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_g0ibdjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_g0ibdjv F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shq9t6lutffbf));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldrlq7uo0cbrm), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_hj7ahc4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hj7ahc4 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjg4ie5b5ufsm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(7));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2qrjfa8j0fl5), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_i83sdhh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_i83sdhh F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_srua7hn34dkbr));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(7));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplehc5f52ftol1), t2);
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
inline __attribute__((aligned(4))) void gm_f_list_99597qp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_99597qp F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_6rs1n5s();
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
inline __attribute__((aligned(4))) void gm_f_list_ahp090e(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ahp090e F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_kvdmouk();
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
inline __attribute__((aligned(4))) void gm_f_list_furncbs(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_furncbs F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_sv0bumj();
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
inline __attribute__((aligned(4))) void gm_f_list_ghuba9i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ghuba9i F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_c8h0sd2();
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
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_make_r4pbu77 F;
    uint8_t gm_l0; /* negative */
    GM_STACK_CHECK_SHALLOW(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    uint8_t gm_l3;
    memmove(((uint8_t *)&F.l2), ((uint8_t *)&F.l1), 40);
    void * t2 = (void *)((uint8_t *)&F.l2);
    gm_f_trim_8rdppmk(t2);
    uint8_t t3 = gm_l0;
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        gm_l3 = t5;
    } else {
        gm_l3 = ((uint8_t)0);
    }
    uint8_t t6 = gm_l3;
    gm_trqfvmjtt4sij t7 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
    gm_tbci5ospk5nnn t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), t6);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t8 + 8), t7);
    memmove(gm_out, ((uint8_t *)&t8), 48);
    goto gm_fdone;
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
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bits_glps5i7 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)t2));
    uint64_t r4; bool o5 = __builtin_mul_overflow(t3, ((uint64_t)8ULL), &r4);
    if (o5) gm_trap("integer overflow in `*`", gm_src4o48ksu43djd, 91, 49);
    uint64_t t6 = r4;
    uint32_t t7 = (uint32_t)t6;
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_33sq608(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_33sq608 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_c8h0sd2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_trq7lbp(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_95uqvlq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_95uqvlq F;
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
        gm_b16_8 t9 = gm_f_empty_slice_kvdmouk();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_h5h9b2d(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 104), ((uint8_t *)&F.l1), 104);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 104)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h9qsb4d(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_h9qsb4d F;
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
        gm_b16_8 t9 = gm_f_empty_slice_6rs1n5s();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_1dn6qmo(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 136), ((uint8_t *)&F.l1), 136);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 136)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s94euqb(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_s94euqb F;
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
        gm_b16_8 t9 = gm_f_empty_slice_sv0bumj();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_vf4k680(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1dn6qmo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_1dn6qmo F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)136ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h5h9b2d(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h5h9b2d F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)104ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_trq7lbp(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_trq7lbp F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vf4k680(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_vf4k680 F;
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
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_shl_qqecl81 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint64_t t5 = gm_l1;
    gm_trqfvmjtt4sij t6;
    gm_f_mag_shl_5df643p((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t2, &t6);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_show_int_ub6npub F;
    GM_STACK_CHECK(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tabb710d9mq2e gm_l1;
    uint64_t t2 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
    uint8_t t3 = (t2 <= ((uint64_t)512ULL));
    if (t3) {
        gm_b16_8 t4 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        F.ret = t5;
        goto gm_fret;
    }
    uint64_t t6 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
    gm_t4ce0jd3tdro4 t7;
    GM_ST(uint64_t, ((uint8_t *)&t7 + 0), t6);
    gm_f_format_5k1v0hr((void *)&gm_l1, (void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_src7h8dni243k2v, 68, 12);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
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
inline __attribute__((aligned(4))) void gm_f_sort_by_sbaob8l(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_sbaob8l F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    gm_closure t3 = gm_l1;
    gm_f_introsort_jhfnj7n(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_pktm5cr F;
    GM_STACK_CHECK(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)a1));
    uint8_t t6 = (uint8_t)!t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t2, t4, t6, t8);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tabb710d9mq2e gm_l2;
    gm_trqfvmjtt4sij t2;
    gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = (void *)((uint8_t *)&F.l1);
    gm_b0_1 t4 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t5 = (void *)((uint8_t *)&F.l1);
    gm_f_into_slice_hq0te56((void *)&gm_l2, t5);
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srclv3pj5es0f8o, 624, 12);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
    F.ret = t7;
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
__attribute__((aligned(4))) void gm_f_all_ctors_o7aijfe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_all_ctors_o7aijfe F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* column */
    void * gm_l4; /* tys */
    gm_tep2drsl9408b gm_l5; /* uv */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* k */
    GM_STACK_CHECK(&F, 2967u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_tftb1odc739t5 gm_l35;
    uint8_t gm_l36;
    gm_th2lqbo77pvsc gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tdlpv0q69lov1 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tvegkb6ouf3m0 gm_l41;
    gm_tngtuiuskn938 gm_l42;
    gm_tftb1odc739t5 gm_l43;
    gm_tftb1odc739t5 gm_l44;
    gm_tftb1odc739t5 gm_l45;
    gm_tftb1odc739t5 gm_l46;
    uint8_t gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    {
        gm_trqfvmjtt4sij t39;
        gm_f_list_ahp090e((void *)&t39, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t39;
        void * t40 = gm_l0;
        void * t41 = (void *)(((uint8_t *)t40) + 8);
        gm_l4 = t41;
        void * t42 = gm_l4;
        gm_t88rcucg1b7uk t43 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&gm_l28, t42, GM_CONV(gm_csguankko4lmhd, t43));
        {
            {
                {
                    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t45 = (uint8_t)(t44 == ((uint8_t)4ULL));
                    if (!t45) {
                        goto gm_lb2;
                    }
                    void * t46 = (void *)((uint8_t *)&F.l3);
                    gm_taevtln836oa8 t47;
                    memset(&t47, 0, sizeof t47);
                    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t47 + 8), ((uint8_t)0));
                    gm_tftb1odc739t5 t48 = gm_f_push_95uqvlq(t46, &t47);
                    gm_l29 = t48;
                    uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                    if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_src8lmgjh8bsvgp, 244, 13);
                    void * t50 = (void *)((uint8_t *)&F.l3);
                    gm_taevtln836oa8 t51;
                    memset(&t51, 0, sizeof t51);
                    GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t51 + 8), ((uint8_t)1));
                    gm_tftb1odc739t5 t52 = gm_f_push_95uqvlq(t50, &t51);
                    gm_l30 = t52;
                    uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                    if (!t53) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_src8lmgjh8bsvgp, 245, 13);
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t55 = (uint8_t)(t54 == ((uint8_t)13ULL));
                    if (!t55) {
                        goto gm_lb3;
                    }
                    void * t56 = (void *)((uint8_t *)&F.l3);
                    gm_taevtln836oa8 t57;
                    memset(&t57, 0, sizeof t57);
                    GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)1ULL));
                    gm_tftb1odc739t5 t58 = gm_f_push_95uqvlq(t56, &t57);
                    gm_l31 = t58;
                    uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                    if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_src8lmgjh8bsvgp, 248, 13);
                    void * t60 = (void *)((uint8_t *)&F.l3);
                    gm_taevtln836oa8 t61;
                    memset(&t61, 0, sizeof t61);
                    GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)2ULL));
                    gm_tftb1odc739t5 t62 = gm_f_push_95uqvlq(t60, &t61);
                    gm_l32 = t62;
                    uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                    if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_src8lmgjh8bsvgp, 249, 13);
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t65 = (uint8_t)(t64 == ((uint8_t)14ULL));
                    if (!t65) {
                        goto gm_lb4;
                    }
                    void * t66 = (void *)((uint8_t *)&F.l3);
                    gm_taevtln836oa8 t67;
                    memset(&t67, 0, sizeof t67);
                    GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)3ULL));
                    gm_tftb1odc739t5 t68 = gm_f_push_95uqvlq(t66, &t67);
                    gm_l33 = t68;
                    uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                    if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_src8lmgjh8bsvgp, 252, 13);
                    void * t70 = (void *)((uint8_t *)&F.l3);
                    gm_taevtln836oa8 t71;
                    memset(&t71, 0, sizeof t71);
                    GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)4ULL));
                    gm_tftb1odc739t5 t72 = gm_f_push_95uqvlq(t70, &t71);
                    gm_l34 = t72;
                    uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                    if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_src8lmgjh8bsvgp, 253, 13);
                    goto gm_lb1;
                }
gm_lb4:;
                {
                    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t75 = (uint8_t)(t74 == ((uint8_t)12ULL));
                    if (!t75) {
                        goto gm_lb5;
                    }
                    void * t76 = (void *)((uint8_t *)&F.l3);
                    gm_taevtln836oa8 t77;
                    memset(&t77, 0, sizeof t77);
                    GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)6ULL));
                    gm_tftb1odc739t5 t78 = gm_f_push_95uqvlq(t76, &t77);
                    gm_l35 = t78;
                    uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                    if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_src8lmgjh8bsvgp, 255, 22);
                    goto gm_lb1;
                }
gm_lb5:;
                {
                    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t81 = (uint8_t)(t80 == ((uint8_t)5ULL));
                    if (!t81) {
                        goto gm_lb6;
                    }
                    goto gm_lb1;
                }
gm_lb6:;
                {
                    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)17ULL));
                    if (!t83) {
                        goto gm_lb7;
                    }
                    void * t84 = gm_l0;
                    gm_t88rcucg1b7uk t85 = gm_l1;
                    gm_f_struct_fields_chqu3it((void *)&gm_l37, t84, GM_CONV(gm_csguankko4lmhd, t85));
                    {
                        {
                            uint8_t t86 = (uint8_t)((((uint8_t *)&gm_l37))[16] != 0);
                            if (t86) {
                                goto gm_lb9;
                            }
                            gm_l36 = ((uint8_t)0);
                            goto gm_lb8;
                        }
gm_lb9:;
                        {
                            gm_l36 = ((uint8_t)1);
                            goto gm_lb8;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 258, 16);
                    }
gm_lb8:;
                    uint8_t t87 = gm_l36;
                    if (t87) {
                        void * t88 = (void *)((uint8_t *)&F.l3);
                        gm_taevtln836oa8 t89;
                        memset(&t89, 0, sizeof t89);
                        GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)6ULL));
                        gm_tftb1odc739t5 t90 = gm_f_push_95uqvlq(t88, &t89);
                        gm_l38 = t90;
                        uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                        if (!t91) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_src8lmgjh8bsvgp, 259, 17);
                    } else {
                        {
                            {
                                void * t92 = gm_l0;
                                gm_t88rcucg1b7uk t93 = gm_l1;
                                gm_f_union_variants_a7khaqp((void *)&gm_l39, t92, GM_CONV(gm_csguankko4lmhd, t93));
                                uint8_t t94 = (uint8_t)((((uint8_t *)&gm_l39))[24] != 0);
                                if (!t94) {
                                    goto gm_lb12;
                                }
                                memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l39), 24);
                                gm_l7 = ((uint64_t)0ULL);
                                uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                gm_l6 = t95;
                                {
                                    for (;;) {
                                        uint64_t t96 = gm_l7;
                                        uint64_t t97 = gm_l6;
                                        uint8_t t98 = (t96 < t97);
                                        if (!t98) {
                                            goto gm_lb13;
                                        }
                                        {
                                            {
                                                {
                                                    uint64_t t99 = gm_l7;
                                                    gm_l8 = t99;
                                                    void * t100 = (void *)((uint8_t *)&F.l3);
                                                    uint64_t t101 = gm_l8;
                                                    uint32_t t102 = (uint32_t)t101;
                                                    gm_taevtln836oa8 t103;
                                                    memset(&t103, 0, sizeof t103);
                                                    GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)5ULL));
                                                    GM_ST(uint32_t, ((uint8_t *)&t103 + 8), t102);
                                                    gm_tftb1odc739t5 t104 = gm_f_push_95uqvlq(t100, &t103);
                                                    gm_l40 = t104;
                                                    uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                                                    if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_src8lmgjh8bsvgp, 261, 47);
                                                    goto gm_lb16;
                                                }
gm_lb17:;
                                                gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 261, 17);
                                            }
gm_lb16:;
                                        }
gm_lb15:;
                                        uint64_t t106 = gm_l7;
                                        uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                                        uint64_t t109 = r107;
                                        gm_l7 = t109;
                                    }
                                }
gm_lb13:;
                                goto gm_lb11;
                            }
gm_lb12:;
                            gm_th2lqbo77pvsc t110;
                            memset(&t110, 0, sizeof t110);
                            memmove(gm_out, ((uint8_t *)&t110), 24);
                            goto gm_fdone;
                        }
gm_lb11:;
                    }
                    goto gm_lb1;
                }
gm_lb7:;
            }
            {
                {
                    void * t111 = gm_l4;
                    gm_t88rcucg1b7uk t112 = gm_l1;
                    void * t113 = gm_l0;
                    void * t114 = GM_LD(void *, ((uint8_t *)t113));
                    void * t115 = (void *)(((uint8_t *)t114) + 88);
                    uint32_t t116 = gm_f_ptr_bits_glps5i7(t115);
                    gm_f_int_bounds_7npnb0b((void *)&gm_l41, (void *)NULL, cx, NULL, t111, t112, t116);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t117 = (uint8_t)((((uint8_t *)&gm_l41))[96] != 0);
                    if (t117) {
                        memmove(((uint8_t *)&gm_l42), ((uint8_t *)&gm_l41), 96);
                    } else {
                        gm_th2lqbo77pvsc t118;
                        memset(&t118, 0, sizeof t118);
                        memmove(gm_out, ((uint8_t *)&t118), 24);
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&F.l9), ((uint8_t *)&gm_l42), 96);
                    memmove(((uint8_t *)&F.l10), ((uint8_t *)&F.l9), 48);
                    gm_tbci5ospk5nnn t119 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l9) + 48));
                    gm_tbci5ospk5nnn t120;
                    gm_f_one_08qg1op((void *)&t120, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_add_420lj6o((void *)&F.l11, (void *)NULL, cx, NULL, &t119, &t120);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_trqfvmjtt4sij t121;
                    gm_f_list_ghuba9i((void *)&t121, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l12 = t121;
                    void * t122 = (void *)((uint8_t *)&F.l12);
                    gm_tftb1odc739t5 t123 = gm_f_push_33sq608(t122, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l10)));
                    gm_l43 = t123;
                    uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                    if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_src8lmgjh8bsvgp, 271, 13);
                    void * t125 = (void *)((uint8_t *)&F.l12);
                    gm_tftb1odc739t5 t126 = gm_f_push_33sq608(t125, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                    gm_l44 = t126;
                    uint8_t t127 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                    if (!t127) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 1, gm_src8lmgjh8bsvgp, 272, 13);
                    gm_b16_8 t128 = gm_l2;
                    gm_l13 = t128;
                    gm_l14 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t129 = gm_l14;
                            uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                            uint8_t t131 = (t129 < t130);
                            if (!t131) {
                                goto gm_lb19;
                            }
                            {
                                void * t132 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                uint64_t t134 = gm_l14;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l15), (((uint8_t *)t132) + (uintptr_t)t134 * 136), 136);
                                        {
                                            {
                                                uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&F.l15) + 0));
                                                uint8_t t136 = (uint8_t)(t135 == ((uint8_t)1ULL));
                                                if (!t136) {
                                                    goto gm_lb25;
                                                }
                                                memmove(((uint8_t *)&F.l16), ((((uint8_t *)&F.l15) + 8) + 8), 104);
                                                uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                                                uint8_t t138 = (uint8_t)(t137 == ((uint8_t)7ULL));
                                                if (!t138) {
                                                    goto gm_lb25;
                                                }
                                                memmove(((uint8_t *)&F.l17), (((uint8_t *)&F.l16) + 8), 48);
                                                memmove(((uint8_t *)&F.l18), ((((uint8_t *)&F.l16) + 8) + 48), 48);
                                                void * t139 = (void *)((uint8_t *)&F.l12);
                                                gm_tftb1odc739t5 t140 = gm_f_push_33sq608(t139, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l17)));
                                                gm_l45 = t140;
                                                uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                                if (!t141) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_src8lmgjh8bsvgp, 275, 21);
                                                void * t142 = (void *)((uint8_t *)&F.l12);
                                                gm_tbci5ospk5nnn t143 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l18));
                                                gm_tbci5ospk5nnn t144;
                                                gm_f_one_08qg1op((void *)&t144, (void *)NULL, cx, NULL);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_tbci5ospk5nnn t145;
                                                gm_f_add_420lj6o((void *)&t145, (void *)NULL, cx, NULL, &t143, &t144);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_tftb1odc739t5 t146 = gm_f_push_33sq608(t142, &t145);
                                                gm_l46 = t146;
                                                uint8_t t147 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                                if (!t147) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_src8lmgjh8bsvgp, 276, 21);
                                                goto gm_lb24;
                                            }
gm_lb25:;
                                        }
gm_lb24:;
                                        goto gm_lb22;
                                    }
gm_lb23:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 273, 13);
                                }
gm_lb22:;
                            }
gm_lb21:;
                            uint64_t t148 = gm_l14;
                            uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
                            uint64_t t151 = r149;
                            gm_l14 = t151;
                        }
                    }
gm_lb19:;
                    gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                    gm_closure t153 = ((gm_closure){GM_PTR_TAG(gm_f_fn_j6p61la), NULL, cx});
                    gm_f_sort_by_sbaob8l(t152, t153);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_trqfvmjtt4sij t154;
                    gm_f_list_ghuba9i((void *)&t154, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l19 = t154;
                    gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                    gm_l20 = t155;
                    gm_l21 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t156 = gm_l21;
                            uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                            uint8_t t158 = (t156 < t157);
                            if (!t158) {
                                goto gm_lb26;
                            }
                            {
                                void * t159 = GM_LD(void *, ((uint8_t *)&gm_l20));
                                uint64_t t160 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                uint64_t t161 = gm_l21;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l22), (((uint8_t *)t159) + (uintptr_t)t161 * 48), 48);
                                        uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 8));
                                        uint8_t t163 = ((t162 == ((uint64_t)0ULL)));
                                        if (t163) {
                                            gm_l47 = ((uint8_t)1);
                                        } else {
                                            void * t164 = GM_LD(void *, ((uint8_t *)&F.l19));
                                            uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 8));
                                            uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 8));
                                            uint64_t r167; bool o168 = __builtin_sub_overflow(t166, ((uint64_t)1ULL), &r167);
                                            if (o168) gm_trap("integer overflow in `-`", gm_src8lmgjh8bsvgp, 282, 54);
                                            uint64_t t169 = r167;
                                            if ((uintptr_t)t169 >= (uintptr_t)(t165)) gm_trap_index((uintptr_t)t169, t165, gm_src8lmgjh8bsvgp, 282, 43);
                                            int32_t t170 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)t164) + (uintptr_t)t169 * 48)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l22)));
                                            uint8_t t171 = (!(t170 == ((int32_t)0LL)));
                                            gm_l47 = t171;
                                        }
                                        uint8_t t172 = gm_l47;
                                        if (t172) {
                                            void * t173 = (void *)((uint8_t *)&F.l19);
                                            gm_tftb1odc739t5 t174 = gm_f_push_33sq608(t173, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l22)));
                                            gm_l48 = t174;
                                            uint8_t t175 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                            if (!t175) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_src8lmgjh8bsvgp, 282, 92);
                                        }
                                        goto gm_lb29;
                                    }
gm_lb30:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 281, 13);
                                }
gm_lb29:;
                            }
gm_lb28:;
                            uint64_t t176 = gm_l21;
                            uint64_t r177; bool o178 = __builtin_add_overflow(t176, ((uint64_t)1ULL), &r177);
                            uint64_t t179 = r177;
                            gm_l21 = t179;
                        }
                    }
gm_lb26:;
                    gm_l24 = ((uint64_t)1ULL);
                    uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 8));
                    gm_l23 = t180;
                    {
                        for (;;) {
                            uint64_t t181 = gm_l24;
                            uint64_t t182 = gm_l23;
                            uint8_t t183 = (t181 < t182);
                            if (!t183) {
                                goto gm_lb31;
                            }
                            {
                                {
                                    {
                                        uint64_t t184 = gm_l24;
                                        gm_l25 = t184;
                                        void * t185 = GM_LD(void *, ((uint8_t *)&F.l19));
                                        uint64_t t186 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 8));
                                        uint64_t t187 = gm_l25;
                                        uint64_t r188; bool o189 = __builtin_sub_overflow(t187, ((uint64_t)1ULL), &r188);
                                        if (o189) gm_trap("integer overflow in `-`", gm_src8lmgjh8bsvgp, 285, 33);
                                        uint64_t t190 = r188;
                                        if ((uintptr_t)t190 >= (uintptr_t)(t186)) gm_trap_index((uintptr_t)t190, t186, gm_src8lmgjh8bsvgp, 285, 22);
                                        memmove(((uint8_t *)&F.l26), (((uint8_t *)t185) + (uintptr_t)t190 * 48), 48);
                                        void * t191 = GM_LD(void *, ((uint8_t *)&F.l19));
                                        uint64_t t192 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 8));
                                        uint64_t t193 = gm_l25;
                                        if ((uintptr_t)t193 >= (uintptr_t)(t192)) gm_trap_index((uintptr_t)t193, t192, gm_src8lmgjh8bsvgp, 286, 22);
                                        memmove(((uint8_t *)&F.l27), (((uint8_t *)t191) + (uintptr_t)t193 * 48), 48);
                                        {
                                            {
                                                int32_t t194 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l26)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l10)));
                                                uint8_t t195 = (t194 >= ((int32_t)0LL));
                                                if (!t195) {
                                                    goto gm_lb37;
                                                }
                                                int32_t t196 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l27)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                                                uint8_t t197 = (t196 <= ((int32_t)0LL));
                                                if (!t197) {
                                                    goto gm_lb37;
                                                }
                                                int32_t t198 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l26)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l27)));
                                                uint8_t t199 = (t198 < ((int32_t)0LL));
                                                if (!t199) {
                                                    goto gm_lb37;
                                                }
                                                void * t200 = (void *)((uint8_t *)&F.l3);
                                                gm_tbci5ospk5nnn t201 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l26));
                                                gm_tbci5ospk5nnn t202 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l27));
                                                gm_tbci5ospk5nnn t203;
                                                gm_f_one_08qg1op((void *)&t203, (void *)NULL, cx, NULL);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_tbci5ospk5nnn t204;
                                                gm_f_sub_pktm5cr((void *)&t204, (void *)NULL, cx, NULL, &t202, &t203);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_tngtuiuskn938 t205;
                                                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t205 + 0), t201);
                                                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t205 + 48), t204);
                                                gm_taevtln836oa8 t206;
                                                GM_ST(uint8_t, (uint8_t *)&t206 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t206 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t206 + 4, 0);
                                                GM_ST(uint8_t, ((uint8_t *)&t206 + 0), ((uint8_t)7ULL));
                                                GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t206 + 8), t205);
                                                gm_tftb1odc739t5 t207 = gm_f_push_95uqvlq(t200, &t206);
                                                gm_l49 = t207;
                                                uint8_t t208 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                                if (!t208) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_src8lmgjh8bsvgp, 287, 75);
                                                goto gm_lb36;
                                            }
gm_lb37:;
                                        }
gm_lb36:;
                                        goto gm_lb34;
                                    }
gm_lb35:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 284, 13);
                                }
gm_lb34:;
                            }
gm_lb33:;
                            uint64_t t209 = gm_l24;
                            uint64_t r210; bool o211 = __builtin_add_overflow(t209, ((uint64_t)1ULL), &r210);
                            uint64_t t212 = r210;
                            gm_l24 = t212;
                        }
                    }
gm_lb31:;
                    goto gm_lb1;
                }
gm_lb18:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 242, 5);
            }
        }
gm_lb1:;
    }
    {
        gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_th2lqbo77pvsc t214;
        GM_ST(uint8_t, (uint8_t *)&t214 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t214 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t214 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t214), t213);
        ((uint8_t *)&t214)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t214), 24);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_alternatives_r0dto2k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_alternatives_r0dto2k F;
    void * gm_l0; /* s */
    void * gm_l1; /* p */
    gm_b16_8 gm_l2; /* ps */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    void * gm_l6; /* q */
    GM_STACK_CHECK(&F, 352u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l7;
    {
        {
            void * t9 = gm_l1;
            uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)10ULL));
            if (!t11) {
                goto gm_lb2;
            }
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)t9) + 8) + 8));
            gm_l2 = t12;
            gm_trqfvmjtt4sij t13;
            gm_f_list_99597qp((void *)&t13, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l3 = t13;
            gm_b16_8 t14 = gm_l2;
            gm_l4 = t14;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t15 = gm_l5;
                    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t17 = (t15 < t16);
                    if (!t17) {
                        goto gm_lb3;
                    }
                    {
                        void * t18 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t20 = gm_l5;
                        {
                            {
                                void * t21 = GM_LD(void *, (((uint8_t *)t18) + (uintptr_t)t20 * 8));
                                gm_l6 = t21;
                                void * t22 = (void *)((uint8_t *)&F.l3);
                                void * t23 = gm_l0;
                                void * t24 = gm_l6;
                                gm_b16_8 t25 = gm_f_alternatives_r0dto2k((void *)NULL, cx, NULL, t23, t24);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
                                gm_tftb1odc739t5 t27 = gm_f_extend_eht4gub(t22, t26);
                                gm_l7 = t27;
                                uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                                if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_src8lmgjh8bsvgp, 143, 23);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 143, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t29 = gm_l5;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l5 = t32;
                }
            }
gm_lb3:;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
            F.ret = t33;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t34 = gm_l0;
    void * t35 = gm_l1;
    gm_b16_8 t36 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t34, t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t36;
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
__attribute__((aligned(4))) void gm_f_check_match_mj577a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_check_match_mj577a4 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* pats */
    gm_b16_8 gm_l3; /* guarded */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* p */
    gm_b16_8 gm_l10; /* alts */
    uint8_t gm_l11; /* any_useful */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_th2lqbo77pvsc gm_l21; /* missing */
    gm_toceffb697p0u gm_l22; /* present_only */
    gm_b16_8 gm_l23; /* w */
    gm_b16_8 gm_l24; /* m */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    void * gm_l27; /* p */
    gm_tl874t2atnlh3 gm_l28; /* at */
    GM_STACK_CHECK(&F, 1684u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t22qobfhrnoif gm_l29;
    gm_tftb1odc739t5 gm_l30;
    uint8_t gm_l31;
    gm_th2lqbo77pvsc gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tftb1odc739t5 gm_l36;
    gm_th2lqbo77pvsc gm_l37;
    gm_toceffb697p0u gm_l38;
    gm_trqfvmjtt4sij t29;
    gm_f_list_furncbs((void *)&t29, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t29;
    gm_trqfvmjtt4sij t30;
    gm_f_list_5dhgg4i((void *)&t30, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t30;
    gm_b16_8 t31 = gm_l2;
    gm_l6 = t31;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t32 = gm_l7;
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t34 = (t32 < t33);
            if (!t34) {
                goto gm_lb1;
            }
            {
                uint64_t t35 = gm_l7;
                void * t36 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t38 = gm_l7;
                void * t39 = GM_LD(void *, (((uint8_t *)t36) + (uintptr_t)t38 * 8));
                gm_t22qobfhrnoif t40;
                GM_ST(uint64_t, ((uint8_t *)&t40 + 0), t35);
                GM_ST(void *, ((uint8_t *)&t40 + 8), t39);
                gm_l29 = t40;
                {
                    {
                        uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&gm_l29));
                        gm_l8 = t41;
                        void * t42 = GM_LD(void *, (((uint8_t *)&gm_l29) + 8));
                        gm_l9 = t42;
                        void * t43 = gm_l0;
                        void * t44 = gm_l9;
                        gm_b16_8 t45 = gm_f_alternatives_r0dto2k((void *)NULL, cx, NULL, t43, t44);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l10 = t45;
                        gm_l11 = ((uint8_t)0);
                        gm_b16_8 t46 = gm_l10;
                        gm_l12 = t46;
                        gm_l13 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t47 = gm_l13;
                                uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                uint8_t t49 = (t47 < t48);
                                if (!t49) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t50 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                    uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                    uint64_t t52 = gm_l13;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l14), (((uint8_t *)t50) + (uintptr_t)t52 * 136), 136);
                                            gm_trqfvmjtt4sij t53;
                                            gm_f_list_99597qp((void *)&t53, (void *)NULL, cx, NULL);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            F.l15 = t53;
                                            void * t54 = (void *)((uint8_t *)&F.l15);
                                            gm_tftb1odc739t5 t55 = gm_f_push_h9qsb4d(t54, &(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l14)));
                                            gm_l30 = t55;
                                            uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                                            if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_src8lmgjh8bsvgp, 57, 13);
                                            void * t57 = gm_l0;
                                            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
                                            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                                            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
                                            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                                            gm_f_useful_ge1iad5((void *)&gm_l32, (void *)NULL, cx, NULL, t57, t59, t61);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            {
                                                {
                                                    uint8_t t62 = (uint8_t)((((uint8_t *)&gm_l32))[16] != 0);
                                                    if (t62) {
                                                        goto gm_lb12;
                                                    }
                                                    gm_l31 = ((uint8_t)0);
                                                    goto gm_lb11;
                                                }
gm_lb12:;
                                                {
                                                    gm_l31 = ((uint8_t)1);
                                                    goto gm_lb11;
                                                }
gm_lb13:;
                                                gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 58, 16);
                                            }
gm_lb11:;
                                            uint8_t t63 = gm_l31;
                                            if (t63) {
                                                gm_l11 = ((uint8_t)1);
                                            }
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 55, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t64 = gm_l13;
                                uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                                uint64_t t67 = r65;
                                gm_l13 = t67;
                            }
                        }
gm_lb6:;
                        uint8_t t68 = gm_l11;
                        uint8_t t69 = (uint8_t)!t68;
                        if (t69) {
                            void * t70 = (void *)((uint8_t *)&F.l5);
                            uint64_t t71 = gm_l8;
                            gm_tftb1odc739t5 t72 = gm_f_push_ucgmlo2(t70, t71);
                            gm_l33 = t72;
                            uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                            if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_src8lmgjh8bsvgp, 60, 29);
                        }
                        void * t74 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t76 = gm_l8;
                        if ((uintptr_t)t76 >= (uintptr_t)(t75)) gm_trap_index((uintptr_t)t76, t75, gm_src8lmgjh8bsvgp, 61, 16);
                        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)t74) + (uintptr_t)t76 * 1));
                        uint8_t t78 = (uint8_t)!t77;
                        if (t78) {
                            gm_b16_8 t79 = gm_l10;
                            gm_l16 = t79;
                            gm_l17 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t80 = gm_l17;
                                    uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                    uint8_t t82 = (t80 < t81);
                                    if (!t82) {
                                        goto gm_lb14;
                                    }
                                    {
                                        void * t83 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                        uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                        uint64_t t85 = gm_l17;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l18), (((uint8_t *)t83) + (uintptr_t)t85 * 136), 136);
                                                gm_trqfvmjtt4sij t86;
                                                gm_f_list_99597qp((void *)&t86, (void *)NULL, cx, NULL);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                F.l19 = t86;
                                                void * t87 = (void *)((uint8_t *)&F.l19);
                                                gm_tftb1odc739t5 t88 = gm_f_push_h9qsb4d(t87, &(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l18)));
                                                gm_l34 = t88;
                                                uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                                if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_src8lmgjh8bsvgp, 64, 17);
                                                void * t90 = (void *)((uint8_t *)&F.l4);
                                                gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                gm_tftb1odc739t5 t92 = gm_f_push_s94euqb(t90, t91);
                                                gm_l35 = t92;
                                                uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                                if (!t93) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_src8lmgjh8bsvgp, 65, 17);
                                                goto gm_lb17;
                                            }
gm_lb18:;
                                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 62, 13);
                                        }
gm_lb17:;
                                    }
gm_lb16:;
                                    uint64_t t94 = gm_l17;
                                    uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                                    uint64_t t97 = r95;
                                    gm_l17 = t97;
                                }
                            }
gm_lb14:;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 52, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t98 = gm_l7;
            uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
            uint64_t t101 = r99;
            gm_l7 = t101;
        }
    }
gm_lb1:;
    gm_trqfvmjtt4sij t102;
    gm_f_list_99597qp((void *)&t102, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l20 = t102;
    void * t103 = (void *)((uint8_t *)&F.l20);
    gm_t88rcucg1b7uk t104 = gm_l1;
    gm_t8oufsr65o5ha t105;
    memset(&t105, 0, sizeof t105);
    GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t105 + 8), t104);
    gm_tftb1odc739t5 t106 = gm_f_push_h9qsb4d(t103, &t105);
    gm_l36 = t106;
    uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
    if (!t107) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_src8lmgjh8bsvgp, 70, 5);
    gm_th2lqbo77pvsc t108;
    memset(&t108, 0, sizeof t108);
    gm_l21 = t108;
    gm_toceffb697p0u t109;
    memset(&t109, 0, sizeof t109);
    gm_l22 = t109;
    {
        {
            void * t110 = gm_l0;
            gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
            gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
            gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
            gm_f_useful_ge1iad5((void *)&gm_l37, (void *)NULL, cx, NULL, t110, t112, t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t115 = (uint8_t)((((uint8_t *)&gm_l37))[16] != 0);
            if (!t115) {
                goto gm_lb20;
            }
            gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l37));
            gm_l23 = t116;
            void * t117 = gm_l0;
            void * t118 = GM_LD(void *, ((uint8_t *)&gm_l23));
            uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t119)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t119, gm_src8lmgjh8bsvgp, 74, 31);
            void * t120 = (void *)(((uint8_t *)t118) + (uintptr_t)((uint64_t)0ULL) * 136);
            gm_b16_8 t121 = gm_f_show_witness_fmsa425((void *)NULL, cx, NULL, t117, t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t121;
            gm_b16_8 t122 = gm_l24;
            gm_th2lqbo77pvsc t123;
            GM_ST(uint8_t, (uint8_t *)&t123 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t123 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t123 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t123), t122);
            ((uint8_t *)&t123)[16] = 1;
            gm_l21 = t123;
            gm_b16_8 t124 = gm_l24;
            gm_b16_8 t125;
            GM_ST(void *, (uint8_t *)&t125, (void *)(gm_squedv51820an));
            GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(4));
            uint8_t t126 = gm_f_has_word_n4uc7oo(t124, t125);
            if (t126) {
                gm_b16_8 t127 = gm_l2;
                gm_l25 = t127;
                gm_l26 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t128 = gm_l26;
                        uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                        uint8_t t130 = (t128 < t129);
                        if (!t130) {
                            goto gm_lb21;
                        }
                        {
                            void * t131 = GM_LD(void *, ((uint8_t *)&gm_l25));
                            uint64_t t132 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                            uint64_t t133 = gm_l26;
                            {
                                {
                                    void * t134 = GM_LD(void *, (((uint8_t *)t131) + (uintptr_t)t133 * 8));
                                    gm_l27 = t134;
                                    {
                                        {
                                            void * t135 = gm_l27;
                                            gm_toceffb697p0u t136 = gm_f_nested_present_binding_r48665i(t135, ((uint8_t)1));
                                            gm_l38 = t136;
                                            uint8_t t137 = (uint8_t)((((uint8_t *)&gm_l38))[12] != 0);
                                            if (!t137) {
                                                goto gm_lb27;
                                            }
                                            gm_tl874t2atnlh3 t138 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l38));
                                            gm_l28 = t138;
                                            gm_tl874t2atnlh3 t139 = gm_l28;
                                            gm_toceffb697p0u t140;
                                            GM_ST(uint8_t, (uint8_t *)&t140 + 13, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t140 + 14, 0);
                                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t140), t139);
                                            ((uint8_t *)&t140)[12] = 1;
                                            gm_l22 = t140;
                                            goto gm_lb21;
                                        }
gm_lb27:;
                                    }
gm_lb26:;
                                    goto gm_lb24;
                                }
gm_lb25:;
                                gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 77, 13);
                            }
gm_lb24:;
                        }
gm_lb23:;
                        uint64_t t141 = gm_l26;
                        uint64_t r142; bool o143 = __builtin_add_overflow(t141, ((uint64_t)1ULL), &r142);
                        uint64_t t144 = r142;
                        gm_l26 = t144;
                    }
                }
gm_lb21:;
            }
            goto gm_lb19;
        }
gm_lb20:;
    }
gm_lb19:;
    gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
    gm_toceffb697p0u t146 = gm_l22;
    gm_th2lqbo77pvsc t147 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l21));
    gm_tqs4ilbobv1qq t148;
    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t148 + 0), t147);
    GM_ST(gm_b16_8, ((uint8_t *)&t148 + 24), t145);
    GM_ST(gm_toceffb697p0u, ((uint8_t *)&t148 + 40), t146);
    memmove(gm_out, ((uint8_t *)&t148), 56);
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
inline __attribute__((aligned(4))) uint8_t gm_f_column_covers_gn61qhr(gm_b16_8 a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_column_covers_gn61qhr F;
    gm_b16_8 gm_l0; /* column */
    void * gm_l1; /* c */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 142u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t9 = gm_l0;
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
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t15 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t13) + (uintptr_t)t15 * 136), 136);
                        {
                            {
                                uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                uint8_t t17 = (uint8_t)(t16 == ((uint8_t)1ULL));
                                if (!t17) {
                                    goto gm_lb7;
                                }
                                memmove(((uint8_t *)&F.l5), ((((uint8_t *)&F.l4) + 8) + 8), 104);
                                void * t18 = (void *)((uint8_t *)&F.l5);
                                void * t19 = gm_l1;
                                uint8_t t20 = gm_f_covers_59m49eg(t18, t19);
                                if (!t20) {
                                    goto gm_lb7;
                                }
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 449, 5);
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
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_covers_59m49eg(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_covers_59m49eg F;
    void * gm_l0; /* head */
    void * gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l6;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)7ULL));
            if (!t6) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), (((uint8_t *)t4) + 8), 48);
            memmove(((uint8_t *)&F.l3), ((((uint8_t *)t4) + 8) + 48), 48);
            void * t7 = gm_l1;
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t7) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)7ULL));
            if (!t9) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l4), (((uint8_t *)t7) + 8), 48);
            memmove(((uint8_t *)&F.l5), ((((uint8_t *)t7) + 8) + 48), 48);
            int32_t t10 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l2)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)));
            uint8_t t11 = (t10 <= ((int32_t)0LL));
            if (t11) {
                int32_t t12 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
                uint8_t t13 = (t12 <= ((int32_t)0LL));
                gm_l6 = t13;
            } else {
                gm_l6 = ((uint8_t)0);
            }
            uint8_t t14 = gm_l6;
            F.ret = t14;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t15 = gm_l0;
    void * t16 = gm_l1;
    uint8_t t17 = gm_f_ctor_eq_fhbfduc(t15, t16);
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_ctor_eq_fhbfduc(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ctor_eq_fhbfduc F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    uint8_t gm_l2; /* x */
    uint32_t gm_l3; /* x */
    gm_b16_8 gm_l8; /* x */
    gm_b16_8 gm_l9; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 1719u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    void * t16 = gm_l0;
    {
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
            if (!t18) {
                goto gm_lb2;
            }
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t16) + 8));
            gm_l2 = t19;
            void * t20 = gm_l1;
            uint8_t t21 = gm_l2;
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)t20) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)0ULL));
            if (t23) {
                uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t20) + 8));
                t23 = (uint8_t)(t24 == t21);
            }
            uint8_t t25 = (uint8_t)(t23);
            gm_taevtln836oa8 t26 = GM_LD(gm_taevtln836oa8, ((uint8_t *)t20));
            gm_taevtln836oa8 t27;
            memset(&t27, 0, sizeof t27);
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t27 + 8), t21);
            gm_l10 = t25;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)1ULL));
            if (!t29) {
                goto gm_lb3;
            }
            void * t30 = gm_l1;
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t30) + 0));
            uint8_t t32 = (uint8_t)((t31 == ((uint8_t)1ULL)));
            gm_taevtln836oa8 t33 = GM_LD(gm_taevtln836oa8, ((uint8_t *)t30));
            gm_taevtln836oa8 t34;
            memset(&t34, 0, sizeof t34);
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)1ULL));
            gm_l10 = t32;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)2ULL));
            if (!t36) {
                goto gm_lb4;
            }
            void * t37 = gm_l1;
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t37) + 0));
            uint8_t t39 = (uint8_t)((t38 == ((uint8_t)2ULL)));
            gm_taevtln836oa8 t40 = GM_LD(gm_taevtln836oa8, ((uint8_t *)t37));
            gm_taevtln836oa8 t41;
            memset(&t41, 0, sizeof t41);
            GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)2ULL));
            gm_l10 = t39;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)3ULL));
            if (!t43) {
                goto gm_lb5;
            }
            void * t44 = gm_l1;
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)t44) + 0));
            uint8_t t46 = (uint8_t)((t45 == ((uint8_t)3ULL)));
            gm_taevtln836oa8 t47 = GM_LD(gm_taevtln836oa8, ((uint8_t *)t44));
            gm_taevtln836oa8 t48;
            memset(&t48, 0, sizeof t48);
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)3ULL));
            gm_l10 = t46;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)4ULL));
            if (!t50) {
                goto gm_lb6;
            }
            void * t51 = gm_l1;
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)t51) + 0));
            uint8_t t53 = (uint8_t)((t52 == ((uint8_t)4ULL)));
            gm_taevtln836oa8 t54 = GM_LD(gm_taevtln836oa8, ((uint8_t *)t51));
            gm_taevtln836oa8 t55;
            memset(&t55, 0, sizeof t55);
            GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)4ULL));
            gm_l10 = t53;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)5ULL));
            if (!t57) {
                goto gm_lb7;
            }
            uint32_t t58 = GM_LD(uint32_t, (((uint8_t *)t16) + 8));
            gm_l3 = t58;
            void * t59 = gm_l1;
            uint32_t t60 = gm_l3;
            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)t59) + 0));
            uint8_t t62 = (uint8_t)(t61 == ((uint8_t)5ULL));
            if (t62) {
                uint32_t t63 = GM_LD(uint32_t, (((uint8_t *)t59) + 8));
                t62 = (uint8_t)(t63 == t60);
            }
            uint8_t t64 = (uint8_t)(t62);
            gm_taevtln836oa8 t65 = GM_LD(gm_taevtln836oa8, ((uint8_t *)t59));
            gm_taevtln836oa8 t66;
            memset(&t66, 0, sizeof t66);
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)5ULL));
            GM_ST(uint32_t, ((uint8_t *)&t66 + 8), t60);
            gm_l10 = t64;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
            uint8_t t68 = (uint8_t)(t67 == ((uint8_t)6ULL));
            if (!t68) {
                goto gm_lb8;
            }
            void * t69 = gm_l1;
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)t69) + 0));
            uint8_t t71 = (uint8_t)((t70 == ((uint8_t)6ULL)));
            gm_taevtln836oa8 t72 = GM_LD(gm_taevtln836oa8, ((uint8_t *)t69));
            gm_taevtln836oa8 t73;
            memset(&t73, 0, sizeof t73);
            GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)6ULL));
            gm_l10 = t71;
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
            uint8_t t75 = (uint8_t)(t74 == ((uint8_t)7ULL));
            if (!t75) {
                goto gm_lb9;
            }
            memmove(((uint8_t *)&F.l4), (((uint8_t *)t16) + 8), 48);
            memmove(((uint8_t *)&F.l5), ((((uint8_t *)t16) + 8) + 48), 48);
            {
                {
                    void * t76 = gm_l1;
                    uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)t76) + 0));
                    uint8_t t78 = (uint8_t)(t77 == ((uint8_t)7ULL));
                    if (!t78) {
                        goto gm_lb11;
                    }
                    memmove(((uint8_t *)&F.l6), (((uint8_t *)t76) + 8), 48);
                    memmove(((uint8_t *)&F.l7), ((((uint8_t *)t76) + 8) + 48), 48);
                    uint8_t t79 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
                    if (t79) {
                        uint8_t t80 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                        gm_l12 = t80;
                    } else {
                        gm_l12 = ((uint8_t)0);
                    }
                    uint8_t t81 = gm_l12;
                    gm_l11 = t81;
                    goto gm_lb10;
                }
gm_lb11:;
                gm_l11 = ((uint8_t)0);
            }
gm_lb10:;
            uint8_t t82 = gm_l11;
            gm_l10 = t82;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
            uint8_t t84 = (uint8_t)(t83 == ((uint8_t)8ULL));
            if (!t84) {
                goto gm_lb12;
            }
            gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)t16) + 8));
            gm_l8 = t85;
            {
                {
                    void * t86 = gm_l1;
                    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)t86) + 0));
                    uint8_t t88 = (uint8_t)(t87 == ((uint8_t)8ULL));
                    if (!t88) {
                        goto gm_lb14;
                    }
                    gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)t86) + 8));
                    gm_l9 = t89;
                    gm_b16_8 t90 = gm_l8;
                    gm_b16_8 t91 = gm_l9;
                    uint8_t t92 = (gm_eq0iqiubfm7e(((uint8_t *)&t90), ((uint8_t *)&t91)));
                    gm_l13 = t92;
                    goto gm_lb13;
                }
gm_lb14:;
                gm_l13 = ((uint8_t)0);
            }
gm_lb13:;
            uint8_t t93 = gm_l13;
            gm_l10 = t93;
            goto gm_lb1;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 322, 58);
    }
gm_lb1:;
    uint8_t t94 = gm_l10;
    F.ret = t94;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_dpat_ty_2ipsk5e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_dpat_ty_2ipsk5e F;
    void * gm_l0; /* d */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk gm_l3;
    void * t5 = gm_l0;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t5) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)0ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 8));
            gm_l1 = t8;
            gm_t88rcucg1b7uk t9 = gm_l1;
            gm_l3 = t9;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t5) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)1ULL));
            if (!t11) {
                goto gm_lb3;
            }
            gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 8));
            gm_l2 = t12;
            gm_t88rcucg1b7uk t13 = gm_l2;
            gm_l3 = t13;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 27, 40);
    }
gm_lb1:;
    gm_t88rcucg1b7uk t14 = gm_l3;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_first_uncovered_fefms0c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_first_uncovered_fefms0c F;
    gm_b16_8 gm_l0; /* column */
    gm_b16_8 gm_l1; /* cs */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l6; /* v */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint32_t gm_l13; /* v */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint32_t gm_l18; /* v */
    uint8_t gm_l19; /* covered */
    GM_STACK_CHECK(&F, 1057u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l20;
    gm_t1nq69orraetc gm_l21;
    uint8_t gm_l22;
    {
        gm_l2 = ((uint64_t)0ULL);
        gm_b16_8 t30 = gm_l1;
        gm_l3 = t30;
        gm_l4 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t31 = gm_l4;
                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint8_t t33 = (t31 < t32);
                if (!t33) {
                    goto gm_lb2;
                }
                {
                    void * t34 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint64_t t36 = gm_l4;
                    {
                        {
                            memmove(((uint8_t *)&F.l5), (((uint8_t *)t34) + (uintptr_t)t36 * 104), 104);
                            {
                                {
                                    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                    uint8_t t38 = (uint8_t)(t37 == ((uint8_t)5ULL));
                                    if (!t38) {
                                        goto gm_lb8;
                                    }
                                    uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 8));
                                    gm_l6 = t39;
                                    uint64_t t40 = gm_l2;
                                    uint32_t t41 = gm_l6;
                                    uint64_t t42 = (uint64_t)t41;
                                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                                    if (o44) gm_trap("integer overflow in `+`", gm_src8lmgjh8bsvgp, 433, 41);
                                    uint64_t t45 = r43;
                                    uint64_t t46 = (t40 > t45 ? t40 : t45);
                                    gm_l2 = t46;
                                    goto gm_lb7;
                                }
gm_lb8:;
                            }
gm_lb7:;
                            goto gm_lb5;
                        }
gm_lb6:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 432, 5);
                    }
gm_lb5:;
                }
gm_lb4:;
                uint64_t t47 = gm_l4;
                uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                uint64_t t50 = r48;
                gm_l4 = t50;
            }
        }
gm_lb2:;
        gm_trqfvmjtt4sij t51;
        gm_f_list_s83jn2q((void *)&t51, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t51;
        gm_l9 = ((uint64_t)0ULL);
        uint64_t t52 = gm_l2;
        gm_l8 = t52;
        {
            for (;;) {
                uint64_t t53 = gm_l9;
                uint64_t t54 = gm_l8;
                uint8_t t55 = (t53 < t54);
                if (!t55) {
                    goto gm_lb9;
                }
                {
                    {
                        {
                            void * t56 = (void *)((uint8_t *)&F.l7);
                            gm_tftb1odc739t5 t57 = gm_f_push_vd1290p(t56, ((uint8_t)0));
                            gm_l20 = t57;
                            uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                            if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_src8lmgjh8bsvgp, 437, 21);
                            goto gm_lb12;
                        }
gm_lb13:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 437, 5);
                    }
gm_lb12:;
                }
gm_lb11:;
                uint64_t t59 = gm_l9;
                uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                uint64_t t62 = r60;
                gm_l9 = t62;
            }
        }
gm_lb9:;
        gm_b16_8 t63 = gm_l0;
        gm_l10 = t63;
        gm_l11 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t64 = gm_l11;
                uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                uint8_t t66 = (t64 < t65);
                if (!t66) {
                    goto gm_lb14;
                }
                {
                    void * t67 = GM_LD(void *, ((uint8_t *)&gm_l10));
                    uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint64_t t69 = gm_l11;
                    {
                        {
                            memmove(((uint8_t *)&F.l12), (((uint8_t *)t67) + (uintptr_t)t69 * 136), 136);
                            {
                                {
                                    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
                                    uint8_t t71 = (uint8_t)(t70 == ((uint8_t)1ULL));
                                    if (!t71) {
                                        goto gm_lb20;
                                    }
                                    uint8_t t72 = GM_LD(uint8_t, (((((uint8_t *)&F.l12) + 8) + 8) + 0));
                                    uint8_t t73 = (uint8_t)(t72 == ((uint8_t)5ULL));
                                    if (!t73) {
                                        goto gm_lb20;
                                    }
                                    uint32_t t74 = GM_LD(uint32_t, (((((uint8_t *)&F.l12) + 8) + 8) + 8));
                                    gm_l13 = t74;
                                    uint32_t t75 = gm_l13;
                                    uint64_t t76 = (uint64_t)t75;
                                    uint64_t t77 = gm_l2;
                                    uint8_t t78 = (t76 < t77);
                                    if (!t78) {
                                        goto gm_lb20;
                                    }
                                    void * t79 = GM_LD(void *, ((uint8_t *)&F.l7));
                                    uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                                    uint32_t t81 = gm_l13;
                                    uint64_t t82 = (uint64_t)t81;
                                    if ((uintptr_t)t82 >= (uintptr_t)(t80)) gm_trap_index((uintptr_t)t82, t80, gm_src8lmgjh8bsvgp, 439, 61);
                                    GM_ST(uint8_t, (((uint8_t *)t79) + (uintptr_t)t82 * 1), ((uint8_t)1));
                                    goto gm_lb19;
                                }
gm_lb20:;
                            }
gm_lb19:;
                            goto gm_lb17;
                        }
gm_lb18:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 438, 5);
                    }
gm_lb17:;
                }
gm_lb16:;
                uint64_t t83 = gm_l11;
                uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                uint64_t t86 = r84;
                gm_l11 = t86;
            }
        }
gm_lb14:;
        gm_b16_8 t87 = gm_l1;
        gm_l14 = t87;
        gm_l15 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t88 = gm_l15;
                uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint8_t t90 = (t88 < t89);
                if (!t90) {
                    goto gm_lb21;
                }
                {
                    uint64_t t91 = gm_l15;
                    void * t92 = GM_LD(void *, ((uint8_t *)&gm_l14));
                    uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint64_t t94 = gm_l15;
                    gm_taevtln836oa8 t95 = GM_LD(gm_taevtln836oa8, (((uint8_t *)t92) + (uintptr_t)t94 * 104));
                    gm_t1nq69orraetc t96;
                    GM_ST(uint64_t, ((uint8_t *)&t96 + 0), t91);
                    GM_ST(gm_taevtln836oa8, ((uint8_t *)&t96 + 8), t95);
                    gm_l21 = t96;
                    {
                        {
                            uint64_t t97 = GM_LD(uint64_t, ((uint8_t *)&gm_l21));
                            gm_l16 = t97;
                            memmove(((uint8_t *)&F.l17), (((uint8_t *)&gm_l21) + 8), 104);
                            {
                                {
                                    uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                                    uint8_t t99 = (uint8_t)(t98 == ((uint8_t)5ULL));
                                    if (!t99) {
                                        goto gm_lb27;
                                    }
                                    uint32_t t100 = GM_LD(uint32_t, (((uint8_t *)&F.l17) + 8));
                                    gm_l18 = t100;
                                    void * t101 = GM_LD(void *, ((uint8_t *)&F.l7));
                                    uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                                    uint32_t t103 = gm_l18;
                                    uint64_t t104 = (uint64_t)t103;
                                    if ((uintptr_t)t104 >= (uintptr_t)(t102)) gm_trap_index((uintptr_t)t104, t102, gm_src8lmgjh8bsvgp, 442, 42);
                                    uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)t101) + (uintptr_t)t104 * 1));
                                    gm_l22 = t105;
                                    goto gm_lb26;
                                }
gm_lb27:;
                                gm_b16_8 t106 = gm_l0;
                                void * t107 = (void *)((uint8_t *)&F.l17);
                                uint8_t t108 = gm_f_column_covers_gn61qhr(t106, t107);
                                gm_l22 = t108;
                            }
gm_lb26:;
                            uint8_t t109 = gm_l22;
                            gm_l19 = t109;
                            uint8_t t110 = gm_l19;
                            uint8_t t111 = (uint8_t)!t110;
                            if (t111) {
                                uint64_t t112 = gm_l16;
                                gm_tf5srh4mp4b3i t113;
                                GM_ST(uint8_t, (uint8_t *)&t113 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t113 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t113 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t113), t112);
                                ((uint8_t *)&t113)[8] = 1;
                                F.ret = t113;
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                                goto gm_ld29_1;
                            }
                            goto gm_lb24;
                        }
gm_lb25:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 441, 5);
                    }
gm_lb24:;
                }
gm_lb23:;
                uint64_t t114 = gm_l15;
                uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
                uint64_t t117 = r115;
                gm_l15 = t117;
            }
        }
gm_lb21:;
        gm_tf5srh4mp4b3i t118;
        memset(&t118, 0, sizeof t118);
        F.ret = t118;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld29_1;
    }
    {
        void * t119 = (void *)((uint8_t *)&F.l7);
        gm_f_deinit_kuh4e9u(t119);
    }
    goto gm_ls120;
gm_ld29_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t121 = (void *)((uint8_t *)&F.l7);
        gm_f_deinit_kuh4e9u(t121);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls120:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_j6p61la(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_word_n4uc7oo(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_word_n4uc7oo F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l1; /* word */
    uint64_t gm_l2; /* start */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 282u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = ((uint64_t)0ULL);
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l3 = t9;
    uint64_t t10 = gm_l4;
    uint64_t t11 = gm_l3;
    uint8_t t12 = (t10 <= t11);
    gm_l5 = t12;
    {
        for (;;) {
            uint8_t t13 = gm_l5;
            if (!t13) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t14 = gm_l4;
                        gm_l6 = t14;
                        {
                            {
                                uint64_t t15 = gm_l6;
                                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint8_t t17 = (t15 < t16);
                                if (!t17) {
                                    goto gm_lb7;
                                }
                                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t20 = gm_l6;
                                uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t18) + (uintptr_t)t20 * 1));
                                uint8_t t22 = gm_f_is_name_char_eanps9t(t21);
                                if (!t22) {
                                    goto gm_lb7;
                                }
                                goto gm_lb3;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        void * t23 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t25 = gm_l2;
                        uint64_t t26 = gm_l6;
                        uint64_t t27 = (uintptr_t)t25;
                        uint64_t t28 = (uintptr_t)t26;
                        if (t27 > t28 || t28 > (uintptr_t)t24) gm_trap_slice(t27, t28, t24, gm_src8lmgjh8bsvgp, 93, 12);
                        uint64_t t29 = ((uintptr_t)t26) - (uintptr_t)t25;
                        gm_b16_8 t30;
                        GM_ST(void *, (uint8_t *)&t30, (void *)(((uint8_t *)t23 + (uintptr_t)t25 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(t29));
                        gm_b16_8 t31 = gm_l1;
                        uint8_t t32 = (gm_eq0iqiubfm7e(((uint8_t *)&t30), ((uint8_t *)&t31)));
                        if (t32) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        uint64_t t33 = gm_l6;
                        uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                        if (o35) gm_trap("integer overflow in `+`", gm_src8lmgjh8bsvgp, 94, 17);
                        uint64_t t36 = r34;
                        gm_l2 = t36;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 91, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t37 = gm_l4;
            uint64_t t38 = gm_l3;
            uint8_t t39 = ((t37 == t38));
            if (t39) {
                gm_l5 = ((uint8_t)0);
            } else {
                uint64_t t40 = gm_l4;
                uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                uint64_t t43 = r41;
                gm_l4 = t43;
            }
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
inline __attribute__((aligned(4))) void gm_f_int_bounds_7npnb0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2) {
    struct gm_fr_int_bounds_7npnb0b F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* ptr_bits */
    uint8_t gm_l3; /* signed */
    uint64_t gm_l4; /* bits */
    uint8_t gm_l5; /* sg */
    uint16_t gm_l6; /* b */
    GM_STACK_CHECK(&F, 1149u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l8;
    gm_l3 = ((uint8_t)0);
    gm_l4 = ((uint64_t)0ULL);
    void * t7 = gm_l0;
    gm_t88rcucg1b7uk t8 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l8, t7, GM_CONV(gm_csguankko4lmhd, t8));
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)0ULL));
            if (!t10) {
                goto gm_lb2;
            }
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 8));
            gm_l5 = t11;
            uint16_t t12 = GM_LD(uint16_t, ((((uint8_t *)&gm_l8) + 8) + 2));
            gm_l6 = t12;
            uint8_t t13 = gm_l5;
            gm_l3 = t13;
            uint16_t t14 = gm_l6;
            uint64_t t15 = (uint64_t)t14;
            gm_l4 = t15;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)1ULL));
            if (!t17) {
                goto gm_lb3;
            }
            uint32_t t18 = gm_l2;
            uint64_t t19 = (uint64_t)t18;
            gm_l4 = t19;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)2ULL));
            if (!t21) {
                goto gm_lb4;
            }
            gm_l3 = ((uint8_t)1);
            uint32_t t22 = gm_l2;
            uint64_t t23 = (uint64_t)t22;
            gm_l4 = t23;
            goto gm_lb1;
        }
gm_lb4:;
        {
            gm_tvegkb6ouf3m0 t24;
            memset(&t24, 0, sizeof t24);
            memmove(gm_out, ((uint8_t *)&t24), 104);
            goto gm_fdone;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 298, 5);
    }
gm_lb1:;
    uint8_t t25 = gm_l3;
    if (t25) {
        gm_tbci5ospk5nnn t26;
        gm_f_one_08qg1op((void *)&t26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t27 = gm_l4;
        uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
        if (o29) gm_trap("integer overflow in `-`", gm_src8lmgjh8bsvgp, 311, 34);
        uint64_t t30 = r28;
        gm_f_shl_qqecl81((void *)&F.l7, (void *)NULL, cx, NULL, &t26, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t31;
        gm_f_neg_agd5c5k((void *)&t31, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t32 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l7));
        gm_tbci5ospk5nnn t33;
        gm_f_one_08qg1op((void *)&t33, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t34;
        gm_f_sub_pktm5cr((void *)&t34, (void *)NULL, cx, NULL, &t32, &t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t35;
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t35 + 0), t31);
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t35 + 48), t34);
        gm_tvegkb6ouf3m0 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 97, 0);
        GM_ST(uint16_t, (uint8_t *)&t36 + 98, 0);
        GM_ST(uint32_t, (uint8_t *)&t36 + 100, 0);
        GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t36), t35);
        ((uint8_t *)&t36)[96] = 1;
        memmove(gm_out, ((uint8_t *)&t36), 104);
        goto gm_fdone;
    }
    gm_tbci5ospk5nnn t37;
    gm_f_zero_s25l990((void *)&t37, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t38;
    gm_f_one_08qg1op((void *)&t38, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t39 = gm_l4;
    gm_tbci5ospk5nnn t40;
    gm_f_shl_qqecl81((void *)&t40, (void *)NULL, cx, NULL, &t38, t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t41;
    gm_f_one_08qg1op((void *)&t41, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t42;
    gm_f_sub_pktm5cr((void *)&t42, (void *)NULL, cx, NULL, &t40, &t41);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tngtuiuskn938 t43;
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t43 + 0), t37);
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t43 + 48), t42);
    gm_tvegkb6ouf3m0 t44;
    GM_ST(uint8_t, (uint8_t *)&t44 + 97, 0);
    GM_ST(uint16_t, (uint8_t *)&t44 + 98, 0);
    GM_ST(uint32_t, (uint8_t *)&t44 + 100, 0);
    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t44), t43);
    ((uint8_t *)&t44)[96] = 1;
    memmove(gm_out, ((uint8_t *)&t44), 104);
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
inline __attribute__((aligned(4))) uint8_t gm_f_irrefutable_lpkmeii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_irrefutable_lpkmeii F;
    void * gm_l0; /* s */
    void * gm_l1; /* p */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 553u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    uint8_t gm_l11;
    gm_th2lqbo77pvsc gm_l12;
    gm_trqfvmjtt4sij t10;
    gm_f_list_furncbs((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t10;
    void * t11 = gm_l0;
    void * t12 = gm_l1;
    gm_b16_8 t13 = gm_f_alternatives_r0dto2k((void *)NULL, cx, NULL, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t13;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t14 = gm_l4;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t16 = (t14 < t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                void * t17 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t19 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t17) + (uintptr_t)t19 * 136), 136);
                        gm_trqfvmjtt4sij t20;
                        gm_f_list_99597qp((void *)&t20, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l6 = t20;
                        void * t21 = (void *)((uint8_t *)&F.l6);
                        gm_tftb1odc739t5 t22 = gm_f_push_h9qsb4d(t21, &(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l5)));
                        gm_l8 = t22;
                        uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                        if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_src8lmgjh8bsvgp, 132, 9);
                        void * t24 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                        gm_tftb1odc739t5 t26 = gm_f_push_s94euqb(t24, t25);
                        gm_l9 = t26;
                        uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_src8lmgjh8bsvgp, 133, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 130, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t28 = gm_l4;
            uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
            uint64_t t31 = r29;
            gm_l4 = t31;
        }
    }
gm_lb1:;
    gm_trqfvmjtt4sij t32;
    gm_f_list_99597qp((void *)&t32, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l7 = t32;
    void * t33 = (void *)((uint8_t *)&F.l7);
    void * t34 = gm_l1;
    gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t34));
    gm_t8oufsr65o5ha t36;
    memset(&t36, 0, sizeof t36);
    GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t36 + 8), t35);
    gm_tftb1odc739t5 t37 = gm_f_push_h9qsb4d(t33, &t36);
    gm_l10 = t37;
    uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_src8lmgjh8bsvgp, 136, 5);
    void * t39 = gm_l0;
    gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
    gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
    gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
    gm_f_useful_ge1iad5((void *)&gm_l12, (void *)NULL, cx, NULL, t39, t41, t43);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t44 = (uint8_t)((((uint8_t *)&gm_l12))[16] != 0);
            if (t44) {
                goto gm_lb7;
            }
            gm_l11 = ((uint8_t)1);
            goto gm_lb6;
        }
gm_lb7:;
        {
            gm_l11 = ((uint8_t)0);
            goto gm_lb6;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 137, 12);
    }
gm_lb6:;
    uint8_t t45 = gm_l11;
    F.ret = t45;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_name_char_eanps9t(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_name_char_eanps9t F;
    uint8_t gm_l0; /* c */
    uint8_t gm_l1; /* <tmp> */
    uint8_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 39u);
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
    uint8_t gm_l10;
    uint8_t t2 = gm_l0;
    gm_l1 = t2;
    uint8_t t3 = gm_l1;
    uint8_t t4 = (((uint8_t)97ULL) <= t3);
    if (t4) {
        uint8_t t5 = gm_l1;
        uint8_t t6 = (t5 <= ((uint8_t)122ULL));
        gm_l8 = t6;
    } else {
        gm_l8 = ((uint8_t)0);
    }
    uint8_t t7 = gm_l8;
    if (t7) {
        gm_l7 = ((uint8_t)1);
    } else {
        uint8_t t8 = gm_l0;
        gm_l2 = t8;
        uint8_t t9 = gm_l2;
        uint8_t t10 = (((uint8_t)65ULL) <= t9);
        if (t10) {
            uint8_t t11 = gm_l2;
            uint8_t t12 = (t11 <= ((uint8_t)90ULL));
            gm_l9 = t12;
        } else {
            gm_l9 = ((uint8_t)0);
        }
        uint8_t t13 = gm_l9;
        gm_l7 = t13;
    }
    uint8_t t14 = gm_l7;
    if (t14) {
        gm_l6 = ((uint8_t)1);
    } else {
        uint8_t t15 = gm_l0;
        gm_l3 = t15;
        uint8_t t16 = gm_l3;
        uint8_t t17 = (((uint8_t)48ULL) <= t16);
        if (t17) {
            uint8_t t18 = gm_l3;
            uint8_t t19 = (t18 <= ((uint8_t)57ULL));
            gm_l10 = t19;
        } else {
            gm_l10 = ((uint8_t)0);
        }
        uint8_t t20 = gm_l10;
        gm_l6 = t20;
    }
    uint8_t t21 = gm_l6;
    if (t21) {
        gm_l5 = ((uint8_t)1);
    } else {
        uint8_t t22 = gm_l0;
        uint8_t t23 = ((t22 == ((uint8_t)95ULL)));
        gm_l5 = t23;
    }
    uint8_t t24 = gm_l5;
    if (t24) {
        gm_l4 = ((uint8_t)1);
    } else {
        uint8_t t25 = gm_l0;
        uint8_t t26 = (t25 >= ((uint8_t)128ULL));
        gm_l4 = t26;
    }
    uint8_t t27 = gm_l4;
    F.ret = t27;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_lower_product_5gakcnb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_lower_product_5gakcnb F;
    void * gm_l0; /* s */
    void * gm_l1; /* p */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_b16_8 gm_l4; /* no_subs */
    gm_b16_8 gm_l5; /* ps */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l8; /* q */
    void * gm_l9; /* e */
    uint8_t gm_l10; /* b */
    gm_b16_8 gm_l12; /* b */
    uint8_t gm_l15; /* inclusive */
    void * gm_l17; /* q */
    void * gm_l18; /* q */
    void * gm_l19; /* q */
    uint32_t gm_l20; /* v */
    gm_b16_8 gm_l21; /* subs_tys */
    uint32_t gm_l23; /* v */
    void * gm_l24; /* q */
    gm_b16_8 gm_l25; /* fs */
    gm_b16_8 gm_l26; /* tys */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_t88rcucg1b7uk gm_l30; /* t */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l34; /* f */
    gm_b16_8 gm_l35; /* options */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    gm_b16_8 gm_l39; /* c */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_b16_8 gm_l46; /* subs */
    GM_STACK_CHECK(&F, 6673u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tftb1odc739t5 gm_l52;
    gm_tbci5ospk5nnn gm_l53;
    gm_tftb1odc739t5 gm_l54;
    gm_tftb1odc739t5 gm_l55;
    gm_taevtln836oa8 gm_l56;
    gm_tftb1odc739t5 gm_l57;
    gm_tftb1odc739t5 gm_l58;
    gm_taevtln836oa8 gm_l59;
    gm_tftb1odc739t5 gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_tftb1odc739t5 gm_l62;
    gm_tftb1odc739t5 gm_l63;
    gm_tftb1odc739t5 gm_l64;
    {
        void * t52 = gm_l1;
        gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t52));
        gm_l2 = t53;
        gm_trqfvmjtt4sij t54;
        gm_f_list_99597qp((void *)&t54, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t54;
        gm_b16_8 t55 = gm_f_empty_slice_6rs1n5s();
        gm_l4 = t55;
        void * t56 = gm_l1;
        {
            {
                {
                    {
                        {
                            uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t56) + 8) + 0));
                            uint8_t t58 = (uint8_t)(t57 == ((uint8_t)0ULL));
                            if (!t58) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t56) + 8) + 0));
                            uint8_t t60 = (uint8_t)(t59 == ((uint8_t)1ULL));
                            if (!t60) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    void * t61 = (void *)((uint8_t *)&F.l3);
                    gm_t88rcucg1b7uk t62 = gm_l2;
                    gm_t8oufsr65o5ha t63;
                    memset(&t63, 0, sizeof t63);
                    GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t63 + 8), t62);
                    gm_tftb1odc739t5 t64 = gm_f_push_h9qsb4d(t61, &t63);
                    gm_l47 = t64;
                    uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                    if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_src8lmgjh8bsvgp, 155, 28);
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t56) + 8) + 0));
                    uint8_t t67 = (uint8_t)(t66 == ((uint8_t)10ULL));
                    if (!t67) {
                        goto gm_lb6;
                    }
                    gm_b16_8 t68 = GM_LD(gm_b16_8, ((((uint8_t *)t56) + 8) + 8));
                    gm_l5 = t68;
                    gm_b16_8 t69 = gm_l5;
                    gm_l6 = t69;
                    gm_l7 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t70 = gm_l7;
                            uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint8_t t72 = (t70 < t71);
                            if (!t72) {
                                goto gm_lb7;
                            }
                            {
                                void * t73 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint64_t t75 = gm_l7;
                                {
                                    {
                                        void * t76 = GM_LD(void *, (((uint8_t *)t73) + (uintptr_t)t75 * 8));
                                        gm_l8 = t76;
                                        void * t77 = (void *)((uint8_t *)&F.l3);
                                        void * t78 = gm_l0;
                                        void * t79 = gm_l8;
                                        gm_b16_8 t80 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t78, t79);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
                                        gm_tftb1odc739t5 t82 = gm_f_extend_eht4gub(t77, t81);
                                        gm_l48 = t82;
                                        uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                        if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_src8lmgjh8bsvgp, 156, 34);
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 156, 20);
                                }
gm_lb10:;
                            }
gm_lb9:;
                            uint64_t t84 = gm_l7;
                            uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                            uint64_t t87 = r85;
                            gm_l7 = t87;
                        }
                    }
gm_lb7:;
                    goto gm_lb1;
                }
gm_lb6:;
                {
                    uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)t56) + 8) + 0));
                    uint8_t t89 = (uint8_t)(t88 == ((uint8_t)2ULL));
                    if (!t89) {
                        goto gm_lb12;
                    }
                    void * t90 = GM_LD(void *, ((((uint8_t *)t56) + 8) + 8));
                    gm_l9 = t90;
                    void * t91 = gm_l9;
                    {
                        {
                            uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)t91) + 8) + 0));
                            uint8_t t93 = (uint8_t)(t92 == ((uint8_t)4ULL));
                            if (!t93) {
                                goto gm_lb14;
                            }
                            uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t91) + 8) + 8));
                            gm_l10 = t94;
                            void * t95 = (void *)((uint8_t *)&F.l3);
                            gm_t88rcucg1b7uk t96 = gm_l2;
                            uint8_t t97 = gm_l10;
                            gm_b16_8 t98 = gm_l4;
                            gm_taevtln836oa8 t99;
                            memset(&t99, 0, sizeof t99);
                            GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)0ULL));
                            GM_ST(uint8_t, ((uint8_t *)&t99 + 8), t97);
                            gm_tbq1t03a53gpo t100;
                            GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t100 + 0), t96);
                            GM_ST(gm_taevtln836oa8, ((uint8_t *)&t100 + 8), t99);
                            GM_ST(gm_b16_8, ((uint8_t *)&t100 + 112), t98);
                            gm_t8oufsr65o5ha t101;
                            GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t101 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t101 + 8), t100);
                            gm_tftb1odc739t5 t102 = gm_f_push_h9qsb4d(t95, &t101);
                            gm_l49 = t102;
                            uint8_t t103 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                            if (!t103) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_src8lmgjh8bsvgp, 158, 25);
                            goto gm_lb13;
                        }
gm_lb14:;
                        {
                            uint8_t t104 = GM_LD(uint8_t, ((((uint8_t *)t91) + 8) + 0));
                            uint8_t t105 = (uint8_t)(t104 == ((uint8_t)0ULL));
                            if (!t105) {
                                goto gm_lb15;
                            }
                            memmove(((uint8_t *)&F.l11), ((((uint8_t *)t91) + 8) + 8), 48);
                            void * t106 = (void *)((uint8_t *)&F.l3);
                            gm_t88rcucg1b7uk t107 = gm_l2;
                            gm_tbci5ospk5nnn t108 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l11));
                            gm_tbci5ospk5nnn t109 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l11));
                            gm_tngtuiuskn938 t110;
                            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t110 + 0), t108);
                            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t110 + 48), t109);
                            gm_b16_8 t111 = gm_l4;
                            gm_taevtln836oa8 t112;
                            GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)7ULL));
                            GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t112 + 8), t110);
                            gm_tbq1t03a53gpo t113;
                            GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t113 + 0), t107);
                            GM_ST(gm_taevtln836oa8, ((uint8_t *)&t113 + 8), t112);
                            GM_ST(gm_b16_8, ((uint8_t *)&t113 + 112), t111);
                            gm_t8oufsr65o5ha t114;
                            GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t114 + 8), t113);
                            gm_tftb1odc739t5 t115 = gm_f_push_h9qsb4d(t106, &t114);
                            gm_l50 = t115;
                            uint8_t t116 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                            if (!t116) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_src8lmgjh8bsvgp, 159, 24);
                            goto gm_lb13;
                        }
gm_lb15:;
                        {
                            uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t91) + 8) + 0));
                            uint8_t t118 = (uint8_t)(t117 == ((uint8_t)5ULL));
                            if (!t118) {
                                goto gm_lb16;
                            }
                            gm_b16_8 t119 = GM_LD(gm_b16_8, ((((uint8_t *)t91) + 8) + 8));
                            gm_l12 = t119;
                            void * t120 = (void *)((uint8_t *)&F.l3);
                            gm_t88rcucg1b7uk t121 = gm_l2;
                            gm_b16_8 t122 = gm_l12;
                            gm_b16_8 t123 = gm_l4;
                            gm_taevtln836oa8 t124;
                            memset(&t124, 0, sizeof t124);
                            GM_ST(uint8_t, ((uint8_t *)&t124 + 0), ((uint8_t)8ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&t124 + 8), t122);
                            gm_tbq1t03a53gpo t125;
                            GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t125 + 0), t121);
                            GM_ST(gm_taevtln836oa8, ((uint8_t *)&t125 + 8), t124);
                            GM_ST(gm_b16_8, ((uint8_t *)&t125 + 112), t123);
                            gm_t8oufsr65o5ha t126;
                            GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t126 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t126 + 8), t125);
                            gm_tftb1odc739t5 t127 = gm_f_push_h9qsb4d(t120, &t126);
                            gm_l51 = t127;
                            uint8_t t128 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                            if (!t128) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_src8lmgjh8bsvgp, 160, 24);
                            goto gm_lb13;
                        }
gm_lb16:;
                        {
                            void * t129 = (void *)((uint8_t *)&F.l3);
                            gm_t88rcucg1b7uk t130 = gm_l2;
                            gm_t8oufsr65o5ha t131;
                            memset(&t131, 0, sizeof t131);
                            GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t131 + 8), t130);
                            gm_tftb1odc739t5 t132 = gm_f_push_h9qsb4d(t129, &t131);
                            gm_l52 = t132;
                            uint8_t t133 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                            if (!t133) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_src8lmgjh8bsvgp, 161, 18);
                            goto gm_lb13;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 157, 22);
                    }
gm_lb13:;
                    goto gm_lb1;
                }
gm_lb12:;
            }
            {
                {
                    uint8_t t134 = GM_LD(uint8_t, ((((uint8_t *)t56) + 8) + 0));
                    uint8_t t135 = (uint8_t)(t134 == ((uint8_t)3ULL));
                    if (!t135) {
                        goto gm_lb18;
                    }
                    memmove(((uint8_t *)&F.l13), ((((uint8_t *)t56) + 8) + 8), 48);
                    memmove(((uint8_t *)&F.l14), (((((uint8_t *)t56) + 8) + 8) + 48), 48);
                    uint8_t t136 = GM_LD(uint8_t, (((((uint8_t *)t56) + 8) + 8) + 96));
                    gm_l15 = t136;
                    uint8_t t137 = gm_l15;
                    if (t137) {
                        memmove(((uint8_t *)&gm_l53), ((uint8_t *)&F.l14), 48);
                    } else {
                        gm_tbci5ospk5nnn t138 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l14));
                        gm_tbci5ospk5nnn t139;
                        gm_f_one_08qg1op((void *)&t139, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_f_sub_pktm5cr((void *)&gm_l53, (void *)NULL, cx, NULL, &t138, &t139);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    memmove(((uint8_t *)&F.l16), ((uint8_t *)&gm_l53), 48);
                    void * t140 = (void *)((uint8_t *)&F.l3);
                    gm_t88rcucg1b7uk t141 = gm_l2;
                    gm_tbci5ospk5nnn t142 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l13));
                    gm_tbci5ospk5nnn t143 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l16));
                    gm_tngtuiuskn938 t144;
                    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t144 + 0), t142);
                    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t144 + 48), t143);
                    gm_b16_8 t145 = gm_l4;
                    gm_taevtln836oa8 t146;
                    GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)7ULL));
                    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t146 + 8), t144);
                    gm_tbq1t03a53gpo t147;
                    GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t147 + 0), t141);
                    GM_ST(gm_taevtln836oa8, ((uint8_t *)&t147 + 8), t146);
                    GM_ST(gm_b16_8, ((uint8_t *)&t147 + 112), t145);
                    gm_t8oufsr65o5ha t148;
                    GM_ST(uint8_t, (uint8_t *)&t148 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t148 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t148 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t148 + 8), t147);
                    gm_tftb1odc739t5 t149 = gm_f_push_h9qsb4d(t140, &t148);
                    gm_l54 = t149;
                    uint8_t t150 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                    if (!t150) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_src8lmgjh8bsvgp, 165, 13);
                    goto gm_lb1;
                }
gm_lb18:;
                {
                    uint8_t t151 = GM_LD(uint8_t, ((((uint8_t *)t56) + 8) + 0));
                    uint8_t t152 = (uint8_t)(t151 == ((uint8_t)4ULL));
                    if (!t152) {
                        goto gm_lb19;
                    }
                    void * t153 = (void *)((uint8_t *)&F.l3);
                    gm_t88rcucg1b7uk t154 = gm_l2;
                    gm_b16_8 t155 = gm_l4;
                    gm_taevtln836oa8 t156;
                    memset(&t156, 0, sizeof t156);
                    GM_ST(uint8_t, ((uint8_t *)&t156 + 0), ((uint8_t)1ULL));
                    gm_tbq1t03a53gpo t157;
                    GM_ST(uint32_t, (uint8_t *)&t157 + 4, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t157 + 0), t154);
                    GM_ST(gm_taevtln836oa8, ((uint8_t *)&t157 + 8), t156);
                    GM_ST(gm_b16_8, ((uint8_t *)&t157 + 112), t155);
                    gm_t8oufsr65o5ha t158;
                    GM_ST(uint8_t, (uint8_t *)&t158 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t158 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t158 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t158 + 8), t157);
                    gm_tftb1odc739t5 t159 = gm_f_push_h9qsb4d(t153, &t158);
                    gm_l55 = t159;
                    uint8_t t160 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                    if (!t160) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 1, gm_src8lmgjh8bsvgp, 167, 18);
                    goto gm_lb1;
                }
gm_lb19:;
                {
                    uint8_t t161 = GM_LD(uint8_t, ((((uint8_t *)t56) + 8) + 0));
                    uint8_t t162 = (uint8_t)(t161 == ((uint8_t)5ULL));
                    if (!t162) {
                        goto gm_lb20;
                    }
                    void * t163 = GM_LD(void *, ((((uint8_t *)t56) + 8) + 8));
                    gm_l17 = t163;
                    gm_t88rcucg1b7uk t164 = gm_l2;
                    void * t165 = gm_l0;
                    void * t166 = gm_l17;
                    gm_taevtln836oa8 t167;
                    memset(&t167, 0, sizeof t167);
                    GM_ST(uint8_t, ((uint8_t *)&t167 + 0), ((uint8_t)2ULL));
                    gm_b16_8 t168 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t165, t166);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t169 = (void *)((uint8_t *)&F.l3);
                    gm_b0_1 t170 = gm_f_wrap_each_2nbqohg((void *)NULL, cx, NULL, t164, &t167, t168, t169);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb20:;
                {
                    uint8_t t171 = GM_LD(uint8_t, ((((uint8_t *)t56) + 8) + 0));
                    uint8_t t172 = (uint8_t)(t171 == ((uint8_t)6ULL));
                    if (!t172) {
                        goto gm_lb21;
                    }
                    void * t173 = GM_LD(void *, ((((uint8_t *)t56) + 8) + 8));
                    gm_l18 = t173;
                    gm_t88rcucg1b7uk t174 = gm_l2;
                    void * t175 = gm_l0;
                    void * t176 = gm_l18;
                    gm_taevtln836oa8 t177;
                    memset(&t177, 0, sizeof t177);
                    GM_ST(uint8_t, ((uint8_t *)&t177 + 0), ((uint8_t)3ULL));
                    gm_b16_8 t178 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t175, t176);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t179 = (void *)((uint8_t *)&F.l3);
                    gm_b0_1 t180 = gm_f_wrap_each_2nbqohg((void *)NULL, cx, NULL, t174, &t177, t178, t179);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb21:;
            }
            {
                {
                    uint8_t t181 = GM_LD(uint8_t, ((((uint8_t *)t56) + 8) + 0));
                    uint8_t t182 = (uint8_t)(t181 == ((uint8_t)7ULL));
                    if (!t182) {
                        goto gm_lb22;
                    }
                    void * t183 = GM_LD(void *, ((((uint8_t *)t56) + 8) + 8));
                    gm_l19 = t183;
                    gm_t88rcucg1b7uk t184 = gm_l2;
                    void * t185 = gm_l0;
                    void * t186 = gm_l19;
                    gm_taevtln836oa8 t187;
                    memset(&t187, 0, sizeof t187);
                    GM_ST(uint8_t, ((uint8_t *)&t187 + 0), ((uint8_t)4ULL));
                    gm_b16_8 t188 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t185, t186);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t189 = (void *)((uint8_t *)&F.l3);
                    gm_b0_1 t190 = gm_f_wrap_each_2nbqohg((void *)NULL, cx, NULL, t184, &t187, t188, t189);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb22:;
                {
                    uint8_t t191 = GM_LD(uint8_t, ((((uint8_t *)t56) + 8) + 0));
                    uint8_t t192 = (uint8_t)(t191 == ((uint8_t)8ULL));
                    if (!t192) {
                        goto gm_lb23;
                    }
                    uint32_t t193 = GM_LD(uint32_t, ((((uint8_t *)t56) + 8) + 8));
                    gm_l20 = t193;
                    uint8_t t194 = (uint8_t)(GM_LD(void *, (((((uint8_t *)t56) + 8) + 8) + 8)) != 0);
                    if (t194) {
                        goto gm_lb23;
                    }
                    void * t195 = gm_l0;
                    gm_t88rcucg1b7uk t196 = gm_l2;
                    uint32_t t197 = gm_l20;
                    gm_taevtln836oa8 t198;
                    memset(&t198, 0, sizeof t198);
                    GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)5ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t198 + 8), t197);
                    gm_l56 = t198;
                    void * t199 = (void *)((uint8_t *)&gm_l56);
                    gm_b16_8 t200 = gm_f_sub_types_o2rpif3((void *)NULL, cx, NULL, t195, t196, t199);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l21 = t200;
                    gm_trqfvmjtt4sij t201;
                    gm_f_list_99597qp((void *)&t201, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l22 = t201;
                    uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                    uint8_t t203 = (t202 > ((uint64_t)0ULL));
                    if (t203) {
                        void * t204 = (void *)((uint8_t *)&F.l22);
                        void * t205 = GM_LD(void *, ((uint8_t *)&gm_l21));
                        uint64_t t206 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                        gm_t88rcucg1b7uk t207 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t205) + (uintptr_t)((uint64_t)0ULL) * 4));
                        gm_t8oufsr65o5ha t208;
                        memset(&t208, 0, sizeof t208);
                        GM_ST(uint8_t, ((uint8_t *)&t208 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t208 + 8), t207);
                        gm_tftb1odc739t5 t209 = gm_f_push_h9qsb4d(t204, &t208);
                        gm_l57 = t209;
                        uint8_t t210 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                        if (!t210) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_src8lmgjh8bsvgp, 174, 35);
                    }
                    void * t211 = (void *)((uint8_t *)&F.l3);
                    gm_t88rcucg1b7uk t212 = gm_l2;
                    uint32_t t213 = gm_l20;
                    gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
                    gm_taevtln836oa8 t215;
                    memset(&t215, 0, sizeof t215);
                    GM_ST(uint8_t, ((uint8_t *)&t215 + 0), ((uint8_t)5ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t215 + 8), t213);
                    gm_tbq1t03a53gpo t216;
                    GM_ST(uint32_t, (uint8_t *)&t216 + 4, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t216 + 0), t212);
                    GM_ST(gm_taevtln836oa8, ((uint8_t *)&t216 + 8), t215);
                    GM_ST(gm_b16_8, ((uint8_t *)&t216 + 112), t214);
                    gm_t8oufsr65o5ha t217;
                    GM_ST(uint8_t, (uint8_t *)&t217 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t217 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t217 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t217 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t217 + 8), t216);
                    gm_tftb1odc739t5 t218 = gm_f_push_h9qsb4d(t211, &t217);
                    gm_l58 = t218;
                    uint8_t t219 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                    if (!t219) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_src8lmgjh8bsvgp, 175, 13);
                    goto gm_lb1;
                }
gm_lb23:;
                {
                    uint8_t t220 = GM_LD(uint8_t, ((((uint8_t *)t56) + 8) + 0));
                    uint8_t t221 = (uint8_t)(t220 == ((uint8_t)8ULL));
                    if (!t221) {
                        goto gm_lb24;
                    }
                    uint32_t t222 = GM_LD(uint32_t, ((((uint8_t *)t56) + 8) + 8));
                    gm_l23 = t222;
                    uint8_t t223 = (uint8_t)(GM_LD(void *, (((((uint8_t *)t56) + 8) + 8) + 8)) != 0);
                    if (!t223) {
                        goto gm_lb24;
                    }
                    void * t224 = GM_LD(void *, (((((uint8_t *)t56) + 8) + 8) + 8));
                    gm_l24 = t224;
                    gm_t88rcucg1b7uk t225 = gm_l2;
                    uint32_t t226 = gm_l23;
                    void * t227 = gm_l0;
                    void * t228 = gm_l24;
                    gm_taevtln836oa8 t229;
                    memset(&t229, 0, sizeof t229);
                    GM_ST(uint8_t, ((uint8_t *)&t229 + 0), ((uint8_t)5ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t229 + 8), t226);
                    gm_b16_8 t230 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t227, t228);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t231 = (void *)((uint8_t *)&F.l3);
                    gm_b0_1 t232 = gm_f_wrap_each_2nbqohg((void *)NULL, cx, NULL, t225, &t229, t230, t231);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb24:;
            }
            {
                {
                    uint8_t t233 = GM_LD(uint8_t, ((((uint8_t *)t56) + 8) + 0));
                    uint8_t t234 = (uint8_t)(t233 == ((uint8_t)9ULL));
                    if (!t234) {
                        goto gm_lb25;
                    }
                    gm_b16_8 t235 = GM_LD(gm_b16_8, ((((uint8_t *)t56) + 8) + 8));
                    gm_l25 = t235;
                    void * t236 = gm_l0;
                    gm_t88rcucg1b7uk t237 = gm_l2;
                    gm_taevtln836oa8 t238;
                    memset(&t238, 0, sizeof t238);
                    GM_ST(uint8_t, ((uint8_t *)&t238 + 0), ((uint8_t)6ULL));
                    gm_l59 = t238;
                    void * t239 = (void *)((uint8_t *)&gm_l59);
                    gm_b16_8 t240 = gm_f_sub_types_o2rpif3((void *)NULL, cx, NULL, t236, t237, t239);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l26 = t240;
                    gm_trqfvmjtt4sij t241;
                    gm_f_list_99597qp((void *)&t241, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l27 = t241;
                    gm_b16_8 t242 = gm_l26;
                    gm_l28 = t242;
                    gm_l29 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t243 = gm_l29;
                            uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                            uint8_t t245 = (t243 < t244);
                            if (!t245) {
                                goto gm_lb26;
                            }
                            {
                                void * t246 = GM_LD(void *, ((uint8_t *)&gm_l28));
                                uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                uint64_t t248 = gm_l29;
                                {
                                    {
                                        gm_t88rcucg1b7uk t249 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t246) + (uintptr_t)t248 * 4));
                                        gm_l30 = t249;
                                        void * t250 = (void *)((uint8_t *)&F.l27);
                                        gm_t88rcucg1b7uk t251 = gm_l30;
                                        gm_t8oufsr65o5ha t252;
                                        memset(&t252, 0, sizeof t252);
                                        GM_ST(uint8_t, ((uint8_t *)&t252 + 0), ((uint8_t)0ULL));
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t252 + 8), t251);
                                        gm_tftb1odc739t5 t253 = gm_f_push_h9qsb4d(t250, &t252);
                                        gm_l60 = t253;
                                        uint8_t t254 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                        if (!t254) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 1, gm_src8lmgjh8bsvgp, 181, 28);
                                        goto gm_lb29;
                                    }
gm_lb30:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 181, 13);
                                }
gm_lb29:;
                            }
gm_lb28:;
                            uint64_t t255 = gm_l29;
                            uint64_t r256; bool o257 = __builtin_add_overflow(t255, ((uint64_t)1ULL), &r256);
                            uint64_t t258 = r256;
                            gm_l29 = t258;
                        }
                    }
gm_lb26:;
                    gm_trqfvmjtt4sij t259;
                    gm_f_list_furncbs((void *)&t259, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l31 = t259;
                    void * t260 = (void *)((uint8_t *)&F.l31);
                    gm_b16_8 t261 = GM_LD(gm_b16_8, ((uint8_t *)&F.l27));
                    gm_tftb1odc739t5 t262 = gm_f_push_s94euqb(t260, t261);
                    gm_l61 = t262;
                    uint8_t t263 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                    if (!t263) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_src8lmgjh8bsvgp, 183, 13);
                    gm_b16_8 t264 = gm_l25;
                    gm_l32 = t264;
                    gm_l33 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t265 = gm_l33;
                            uint64_t t266 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                            uint8_t t267 = (t265 < t266);
                            if (!t267) {
                                goto gm_lb31;
                            }
                            {
                                void * t268 = GM_LD(void *, ((uint8_t *)&gm_l32));
                                uint64_t t269 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                uint64_t t270 = gm_l33;
                                {
                                    {
                                        gm_ttggq7ng4kp3p t271 = GM_LD(gm_ttggq7ng4kp3p, (((uint8_t *)t268) + (uintptr_t)t270 * 16));
                                        gm_l34 = t271;
                                        void * t272 = gm_l0;
                                        void * t273 = GM_LD(void *, (((uint8_t *)&gm_l34) + 8));
                                        gm_b16_8 t274 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t272, t273);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l35 = t274;
                                        gm_trqfvmjtt4sij t275;
                                        gm_f_list_furncbs((void *)&t275, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l36 = t275;
                                        gm_b16_8 t276 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
                                        gm_l37 = t276;
                                        gm_l38 = ((uint64_t)0ULL);
                                        {
                                            for (;;) {
                                                uint64_t t277 = gm_l38;
                                                uint64_t t278 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                                                uint8_t t279 = (t277 < t278);
                                                if (!t279) {
                                                    goto gm_lb36;
                                                }
                                                {
                                                    void * t280 = GM_LD(void *, ((uint8_t *)&gm_l37));
                                                    uint64_t t281 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                                                    uint64_t t282 = gm_l38;
                                                    {
                                                        {
                                                            gm_b16_8 t283 = GM_LD(gm_b16_8, (((uint8_t *)t280) + (uintptr_t)t282 * 16));
                                                            gm_l39 = t283;
                                                            gm_b16_8 t284 = gm_l35;
                                                            gm_l40 = t284;
                                                            gm_l41 = ((uint64_t)0ULL);
                                                            {
                                                                for (;;) {
                                                                    uint64_t t285 = gm_l41;
                                                                    uint64_t t286 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                                                                    uint8_t t287 = (t285 < t286);
                                                                    if (!t287) {
                                                                        goto gm_lb41;
                                                                    }
                                                                    {
                                                                        void * t288 = GM_LD(void *, ((uint8_t *)&gm_l40));
                                                                        uint64_t t289 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                                                                        uint64_t t290 = gm_l41;
                                                                        {
                                                                            {
                                                                                memmove(((uint8_t *)&F.l42), (((uint8_t *)t288) + (uintptr_t)t290 * 136), 136);
                                                                                gm_trqfvmjtt4sij t291;
                                                                                gm_f_list_99597qp((void *)&t291, (void *)NULL, cx, NULL);
                                                                                if (gm_unw.kind) {
                                                                                    goto gm_lad;
                                                                                }
                                                                                F.l43 = t291;
                                                                                void * t292 = (void *)((uint8_t *)&F.l43);
                                                                                gm_b16_8 t293 = gm_l39;
                                                                                gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)&t293));
                                                                                gm_tftb1odc739t5 t295 = gm_f_extend_eht4gub(t292, t294);
                                                                                gm_l62 = t295;
                                                                                uint8_t t296 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                                                                                if (!t296) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 1, gm_src8lmgjh8bsvgp, 190, 25);
                                                                                uint32_t t297 = GM_LD(uint32_t, ((uint8_t *)&gm_l34));
                                                                                uint64_t t298 = (uint64_t)t297;
                                                                                uint64_t t299 = GM_LD(uint64_t, (((uint8_t *)&F.l43) + 8));
                                                                                uint8_t t300 = (t298 < t299);
                                                                                if (t300) {
                                                                                    void * t301 = GM_LD(void *, ((uint8_t *)&F.l43));
                                                                                    uint64_t t302 = GM_LD(uint64_t, (((uint8_t *)&F.l43) + 8));
                                                                                    uint32_t t303 = GM_LD(uint32_t, ((uint8_t *)&gm_l34));
                                                                                    uint64_t t304 = (uint64_t)t303;
                                                                                    if ((uintptr_t)t304 >= (uintptr_t)(t302)) gm_trap_index((uintptr_t)t304, t302, gm_src8lmgjh8bsvgp, 191, 60);
                                                                                    memmove((((uint8_t *)t301) + (uintptr_t)t304 * 136), ((uint8_t *)&F.l42), 136);
                                                                                }
                                                                                void * t305 = (void *)((uint8_t *)&F.l36);
                                                                                gm_b16_8 t306 = GM_LD(gm_b16_8, ((uint8_t *)&F.l43));
                                                                                gm_tftb1odc739t5 t307 = gm_f_push_s94euqb(t305, t306);
                                                                                gm_l63 = t307;
                                                                                uint8_t t308 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                                                                                if (!t308) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 1, gm_src8lmgjh8bsvgp, 192, 25);
                                                                                goto gm_lb44;
                                                                            }
gm_lb45:;
                                                                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 188, 21);
                                                                        }
gm_lb44:;
                                                                    }
gm_lb43:;
                                                                    uint64_t t309 = gm_l41;
                                                                    uint64_t r310; bool o311 = __builtin_add_overflow(t309, ((uint64_t)1ULL), &r310);
                                                                    uint64_t t312 = r310;
                                                                    gm_l41 = t312;
                                                                }
                                                            }
gm_lb41:;
                                                            goto gm_lb39;
                                                        }
gm_lb40:;
                                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 187, 17);
                                                    }
gm_lb39:;
                                                }
gm_lb38:;
                                                uint64_t t313 = gm_l38;
                                                uint64_t r314; bool o315 = __builtin_add_overflow(t313, ((uint64_t)1ULL), &r314);
                                                uint64_t t316 = r314;
                                                gm_l38 = t316;
                                            }
                                        }
gm_lb36:;
                                        memmove(((uint8_t *)&F.l31), ((uint8_t *)&F.l36), 40);
                                        goto gm_lb34;
                                    }
gm_lb35:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 184, 13);
                                }
gm_lb34:;
                            }
gm_lb33:;
                            uint64_t t317 = gm_l33;
                            uint64_t r318; bool o319 = __builtin_add_overflow(t317, ((uint64_t)1ULL), &r318);
                            uint64_t t320 = r318;
                            gm_l33 = t320;
                        }
                    }
gm_lb31:;
                    gm_b16_8 t321 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
                    gm_l44 = t321;
                    gm_l45 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t322 = gm_l45;
                            uint64_t t323 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                            uint8_t t324 = (t322 < t323);
                            if (!t324) {
                                goto gm_lb46;
                            }
                            {
                                void * t325 = GM_LD(void *, ((uint8_t *)&gm_l44));
                                uint64_t t326 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                                uint64_t t327 = gm_l45;
                                {
                                    {
                                        gm_b16_8 t328 = GM_LD(gm_b16_8, (((uint8_t *)t325) + (uintptr_t)t327 * 16));
                                        gm_l46 = t328;
                                        void * t329 = (void *)((uint8_t *)&F.l3);
                                        gm_t88rcucg1b7uk t330 = gm_l2;
                                        gm_b16_8 t331 = gm_l46;
                                        gm_taevtln836oa8 t332;
                                        memset(&t332, 0, sizeof t332);
                                        GM_ST(uint8_t, ((uint8_t *)&t332 + 0), ((uint8_t)6ULL));
                                        gm_tbq1t03a53gpo t333;
                                        GM_ST(uint32_t, (uint8_t *)&t333 + 4, 0);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t333 + 0), t330);
                                        GM_ST(gm_taevtln836oa8, ((uint8_t *)&t333 + 8), t332);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t333 + 112), t331);
                                        gm_t8oufsr65o5ha t334;
                                        GM_ST(uint8_t, (uint8_t *)&t334 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t334 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t334 + 4, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t334 + 0), ((uint8_t)1ULL));
                                        GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t334 + 8), t333);
                                        gm_tftb1odc739t5 t335 = gm_f_push_h9qsb4d(t329, &t334);
                                        gm_l64 = t335;
                                        uint8_t t336 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                                        if (!t336) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 1, gm_src8lmgjh8bsvgp, 197, 40);
                                        goto gm_lb49;
                                    }
gm_lb50:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 197, 13);
                                }
gm_lb49:;
                            }
gm_lb48:;
                            uint64_t t337 = gm_l45;
                            uint64_t r338; bool o339 = __builtin_add_overflow(t337, ((uint64_t)1ULL), &r338);
                            uint64_t t340 = r338;
                            gm_l45 = t340;
                        }
                    }
gm_lb46:;
                    goto gm_lb1;
                }
gm_lb25:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 154, 5);
            }
        }
gm_lb1:;
    }
    {
        gm_b16_8 t341 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        F.ret = t341;
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
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_nested_present_binding_r48665i(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nested_present_binding_r48665i F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* top */
    void * gm_l2; /* q */
    void * gm_l3; /* q */
    void * gm_l4; /* q */
    gm_b16_8 gm_l5; /* alts */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l8; /* q */
    gm_tl874t2atnlh3 gm_l9; /* at */
    void * gm_l10; /* q */
    gm_b16_8 gm_l11; /* fs */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l14; /* f */
    gm_tl874t2atnlh3 gm_l15; /* at */
    GM_STACK_CHECK(&F, 762u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l16;
    gm_toceffb697p0u gm_l17;
    gm_toceffb697p0u gm_l18;
    void * t32 = gm_l0;
    {
        {
            {
                {
                    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)5ULL));
                    if (!t34) {
                        goto gm_lb4;
                    }
                    void * t35 = GM_LD(void *, ((((uint8_t *)t32) + 8) + 8));
                    gm_l2 = t35;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)6ULL));
                    if (!t37) {
                        goto gm_lb5;
                    }
                    void * t38 = GM_LD(void *, ((((uint8_t *)t32) + 8) + 8));
                    gm_l2 = t38;
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            {
                {
                    uint8_t t39 = gm_l1;
                    uint8_t t40 = (uint8_t)!t39;
                    if (!t40) {
                        goto gm_lb7;
                    }
                    void * t41 = gm_l2;
                    {
                        {
                            uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)1ULL));
                            if (!t43) {
                                goto gm_lb9;
                            }
                            gm_l16 = ((uint8_t)1);
                            goto gm_lb8;
                        }
gm_lb9:;
                        {
                            gm_l16 = ((uint8_t)0);
                            goto gm_lb8;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 106, 28);
                    }
gm_lb8:;
                    uint8_t t44 = gm_l16;
                    if (!t44) {
                        goto gm_lb7;
                    }
                    void * t45 = gm_l2;
                    gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t45) + 120));
                    gm_toceffb697p0u t47;
                    GM_ST(uint8_t, (uint8_t *)&t47 + 13, 0);
                    GM_ST(uint16_t, (uint8_t *)&t47 + 14, 0);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t47), t46);
                    ((uint8_t *)&t47)[12] = 1;
                    F.ret = t47;
                    goto gm_fret;
                }
gm_lb7:;
            }
gm_lb6:;
            void * t48 = gm_l2;
            uint8_t t49 = gm_l1;
            gm_toceffb697p0u t50 = gm_f_nested_present_binding_r48665i(t48, t49);
            F.ret = t50;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)7ULL));
            if (!t52) {
                goto gm_lb11;
            }
            void * t53 = GM_LD(void *, ((((uint8_t *)t32) + 8) + 8));
            gm_l4 = t53;
            void * t54 = gm_l4;
            uint8_t t55 = gm_l1;
            gm_toceffb697p0u t56 = gm_f_nested_present_binding_r48665i(t54, t55);
            F.ret = t56;
            goto gm_fret;
        }
gm_lb11:;
        {
            uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
            uint8_t t58 = (uint8_t)(t57 == ((uint8_t)10ULL));
            if (!t58) {
                goto gm_lb12;
            }
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 8));
            gm_l5 = t59;
            gm_b16_8 t60 = gm_l5;
            gm_l6 = t60;
            gm_l7 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t61 = gm_l7;
                    uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint8_t t63 = (t61 < t62);
                    if (!t63) {
                        goto gm_lb13;
                    }
                    {
                        void * t64 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t66 = gm_l7;
                        {
                            {
                                void * t67 = GM_LD(void *, (((uint8_t *)t64) + (uintptr_t)t66 * 8));
                                gm_l8 = t67;
                                {
                                    {
                                        void * t68 = gm_l8;
                                        uint8_t t69 = gm_l1;
                                        gm_toceffb697p0u t70 = gm_f_nested_present_binding_r48665i(t68, t69);
                                        gm_l17 = t70;
                                        uint8_t t71 = (uint8_t)((((uint8_t *)&gm_l17))[12] != 0);
                                        if (!t71) {
                                            goto gm_lb19;
                                        }
                                        gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l17));
                                        gm_l9 = t72;
                                        gm_tl874t2atnlh3 t73 = gm_l9;
                                        gm_toceffb697p0u t74;
                                        GM_ST(uint8_t, (uint8_t *)&t74 + 13, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t74 + 14, 0);
                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t74), t73);
                                        ((uint8_t *)&t74)[12] = 1;
                                        F.ret = t74;
                                        goto gm_fret;
                                    }
gm_lb19:;
                                }
gm_lb18:;
                                goto gm_lb16;
                            }
gm_lb17:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 111, 13);
                        }
gm_lb16:;
                    }
gm_lb15:;
                    uint64_t t75 = gm_l7;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    uint64_t t78 = r76;
                    gm_l7 = t78;
                }
            }
gm_lb13:;
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
            uint8_t t80 = (uint8_t)(t79 == ((uint8_t)8ULL));
            if (!t80) {
                goto gm_lb20;
            }
            uint8_t t81 = (uint8_t)(GM_LD(void *, (((((uint8_t *)t32) + 8) + 8) + 8)) != 0);
            if (t81) {
                goto gm_lb20;
            }
            goto gm_lb1;
        }
gm_lb20:;
        {
            uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
            uint8_t t83 = (uint8_t)(t82 == ((uint8_t)8ULL));
            if (!t83) {
                goto gm_lb21;
            }
            uint8_t t84 = (uint8_t)(GM_LD(void *, (((((uint8_t *)t32) + 8) + 8) + 8)) != 0);
            if (!t84) {
                goto gm_lb21;
            }
            void * t85 = GM_LD(void *, (((((uint8_t *)t32) + 8) + 8) + 8));
            gm_l10 = t85;
            void * t86 = gm_l10;
            gm_toceffb697p0u t87 = gm_f_nested_present_binding_r48665i(t86, ((uint8_t)0));
            F.ret = t87;
            goto gm_fret;
        }
gm_lb21:;
        {
            uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
            uint8_t t89 = (uint8_t)(t88 == ((uint8_t)9ULL));
            if (!t89) {
                goto gm_lb22;
            }
            gm_b16_8 t90 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 8));
            gm_l11 = t90;
            gm_b16_8 t91 = gm_l11;
            gm_l12 = t91;
            gm_l13 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t92 = gm_l13;
                    uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                    uint8_t t94 = (t92 < t93);
                    if (!t94) {
                        goto gm_lb23;
                    }
                    {
                        void * t95 = GM_LD(void *, ((uint8_t *)&gm_l12));
                        uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                        uint64_t t97 = gm_l13;
                        {
                            {
                                gm_ttggq7ng4kp3p t98 = GM_LD(gm_ttggq7ng4kp3p, (((uint8_t *)t95) + (uintptr_t)t97 * 16));
                                gm_l14 = t98;
                                {
                                    {
                                        void * t99 = GM_LD(void *, (((uint8_t *)&gm_l14) + 8));
                                        gm_toceffb697p0u t100 = gm_f_nested_present_binding_r48665i(t99, ((uint8_t)0));
                                        gm_l18 = t100;
                                        uint8_t t101 = (uint8_t)((((uint8_t *)&gm_l18))[12] != 0);
                                        if (!t101) {
                                            goto gm_lb29;
                                        }
                                        gm_tl874t2atnlh3 t102 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l18));
                                        gm_l15 = t102;
                                        gm_tl874t2atnlh3 t103 = gm_l15;
                                        gm_toceffb697p0u t104;
                                        GM_ST(uint8_t, (uint8_t *)&t104 + 13, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t104 + 14, 0);
                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t104), t103);
                                        ((uint8_t *)&t104)[12] = 1;
                                        F.ret = t104;
                                        goto gm_fret;
                                    }
gm_lb29:;
                                }
gm_lb28:;
                                goto gm_lb26;
                            }
gm_lb27:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 118, 13);
                        }
gm_lb26:;
                    }
gm_lb25:;
                    uint64_t t105 = gm_l13;
                    uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                    uint64_t t108 = r106;
                    gm_l13 = t108;
                }
            }
gm_lb23:;
            goto gm_lb1;
        }
gm_lb22:;
        {
            goto gm_lb1;
        }
gm_lb30:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 104, 5);
    }
gm_lb1:;
    gm_toceffb697p0u t109;
    memset(&t109, 0, sizeof t109);
    F.ret = t109;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_rebuild_90secge(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, uint64_t a2, gm_b16_8 a3) {
    struct gm_fr_rebuild_90secge F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    uint64_t gm_l2; /* arity */
    gm_b16_8 gm_l3; /* w */
    uint64_t gm_l4; /* n */
    GM_STACK_CHECK(&F, 690u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    uint64_t t2 = gm_l2;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t4 = (t2 < t3 ? t2 : t3);
    gm_l4 = t4;
    gm_trqfvmjtt4sij t5;
    gm_f_list_99597qp((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t5;
    void * t6 = (void *)((uint8_t *)&F.l5);
    gm_t88rcucg1b7uk t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t10 = gm_l4;
    uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t12 = (uintptr_t)t10;
    if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_src8lmgjh8bsvgp, 459, 27);
    uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 136)));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
    gm_taevtln836oa8 t15 = GM_LD(gm_taevtln836oa8, ((uint8_t *)&F.l1));
    gm_tbq1t03a53gpo t16;
    GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t16 + 0), t7);
    GM_ST(gm_taevtln836oa8, ((uint8_t *)&t16 + 8), t15);
    GM_ST(gm_b16_8, ((uint8_t *)&t16 + 112), t14);
    gm_t8oufsr65o5ha t17;
    GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)1ULL));
    GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t17 + 8), t16);
    gm_tftb1odc739t5 t18 = gm_f_push_h9qsb4d(t6, &t17);
    gm_l6 = t18;
    uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src8lmgjh8bsvgp, 459, 5);
    void * t20 = (void *)((uint8_t *)&F.l5);
    void * t21 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t23 = gm_l4;
    uint64_t t24 = (uintptr_t)t23;
    uint64_t t25 = (uintptr_t)t22;
    if (t24 > t25 || t25 > (uintptr_t)t22) gm_trap_slice(t24, t25, t22, gm_src8lmgjh8bsvgp, 460, 16);
    uint64_t t26 = ((uintptr_t)t22) - (uintptr_t)t23;
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)t23 * 136)));
    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26));
    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
    gm_tftb1odc739t5 t29 = gm_f_extend_eht4gub(t20, t28);
    gm_l7 = t29;
    uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_src8lmgjh8bsvgp, 460, 5);
    gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
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
__attribute__((aligned(4))) gm_b16_8 gm_f_show_witness_fmsa425(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_witness_fmsa425 F;
    void * gm_l0; /* s */
    void * gm_l1; /* d */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_b16_8 gm_l4; /* subs */
    uint8_t gm_l5; /* b */
    gm_b16_8 gm_l8; /* b */
    uint32_t gm_l10; /* v */
    gm_b16_8 gm_l11; /* name */
    gm_tep2drsl9408b gm_l12; /* uv */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint32_t gm_l17; /* id */
    gm_b16_8 gm_l19; /* fields */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* i */
    GM_STACK_CHECK(&F, 2328u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l24;
    gm_tabb710d9mq2e gm_l25;
    gm_tabb710d9mq2e gm_l26;
    gm_tdlpv0q69lov1 gm_l27;
    gm_tabb710d9mq2e gm_l28;
    gm_tabb710d9mq2e gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_t53at80pfdus3 gm_l31;
    gm_th2lqbo77pvsc gm_l32;
    gm_t8ta482bl6be0 gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tabb710d9mq2e gm_l36;
    gm_tabb710d9mq2e gm_l37;
    gm_tabb710d9mq2e gm_l38;
    gm_tabb710d9mq2e gm_l39;
    {
        {
            {
                {
                    void * t34 = gm_l1;
                    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)t34) + 0));
                    uint8_t t36 = (uint8_t)(t35 == ((uint8_t)1ULL));
                    if (!t36) {
                        goto gm_lb2;
                    }
                    gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t34) + 8));
                    gm_l2 = t37;
                    memmove(((uint8_t *)&F.l3), ((((uint8_t *)t34) + 8) + 8), 104);
                    gm_b16_8 t38 = GM_LD(gm_b16_8, ((((uint8_t *)t34) + 8) + 112));
                    gm_l4 = t38;
                    {
                        {
                            {
                                uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                uint8_t t40 = (uint8_t)(t39 == ((uint8_t)0ULL));
                                if (!t40) {
                                    goto gm_lb4;
                                }
                                uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 8));
                                gm_l5 = t41;
                                uint8_t t42 = gm_l5;
                                if (t42) {
                                    gm_b16_8 t43;
                                    GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sehko5bb96s6p));
                                    GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(4));
                                    gm_l24 = t43;
                                } else {
                                    gm_b16_8 t44;
                                    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sogehh8t1m7tn));
                                    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(5));
                                    gm_l24 = t44;
                                }
                                gm_b16_8 t45 = gm_l24;
                                F.ret = t45;
                                goto gm_fret;
                            }
gm_lb4:;
                            {
                                uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                uint8_t t47 = (uint8_t)(t46 == ((uint8_t)1ULL));
                                if (!t47) {
                                    goto gm_lb5;
                                }
                                gm_b16_8 t48;
                                GM_ST(void *, (uint8_t *)&t48, (void *)(gm_squedv51820an));
                                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(4));
                                F.ret = t48;
                                goto gm_fret;
                            }
gm_lb5:;
                            {
                                {
                                    {
                                        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                        uint8_t t50 = (uint8_t)(t49 == ((uint8_t)2ULL));
                                        if (!t50) {
                                            goto gm_lb8;
                                        }
                                        goto gm_lb7;
                                    }
gm_lb8:;
                                    {
                                        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                        uint8_t t52 = (uint8_t)(t51 == ((uint8_t)3ULL));
                                        if (!t52) {
                                            goto gm_lb9;
                                        }
                                        goto gm_lb7;
                                    }
gm_lb9:;
                                    goto gm_lb6;
                                }
gm_lb7:;
                                void * t53 = gm_l0;
                                void * t54 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t55)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t55, gm_src8lmgjh8bsvgp, 470, 47);
                                void * t56 = (void *)(((uint8_t *)t54) + (uintptr_t)((uint64_t)0ULL) * 136);
                                gm_b16_8 t57 = gm_f_show_witness_fmsa425((void *)NULL, cx, NULL, t53, t56);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t57;
                                goto gm_fret;
                            }
gm_lb6:;
                            {
                                uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                uint8_t t59 = (uint8_t)(t58 == ((uint8_t)4ULL));
                                if (!t59) {
                                    goto gm_lb10;
                                }
                                void * t60 = gm_l0;
                                void * t61 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t62)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t62, gm_src8lmgjh8bsvgp, 471, 59);
                                void * t63 = (void *)(((uint8_t *)t61) + (uintptr_t)((uint64_t)0ULL) * 136);
                                gm_b16_8 t64 = gm_f_show_witness_fmsa425((void *)NULL, cx, NULL, t60, t63);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tnpifv20irnph t65;
                                GM_ST(gm_b16_8, ((uint8_t *)&t65 + 0), t64);
                                gm_f_format_08505jg((void *)&gm_l25, (void *)NULL, cx, NULL, t65);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 8, gm_src8lmgjh8bsvgp, 471, 24);
                                gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
                                gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                                F.ret = t68;
                                goto gm_fret;
                            }
gm_lb10:;
                            {
                                uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                uint8_t t70 = (uint8_t)(t69 == ((uint8_t)7ULL));
                                if (!t70) {
                                    goto gm_lb11;
                                }
                                memmove(((uint8_t *)&F.l6), (((uint8_t *)&F.l3) + 8), 48);
                                memmove(((uint8_t *)&F.l7), ((((uint8_t *)&F.l3) + 8) + 48), 48);
                                uint8_t t71 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                                if (t71) {
                                    gm_b16_8 t72 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t72;
                                    goto gm_fret;
                                }
                                gm_b16_8 t73 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t74 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t6j6smj7qoctu t75;
                                GM_ST(gm_b16_8, ((uint8_t *)&t75 + 0), t73);
                                GM_ST(gm_b16_8, ((uint8_t *)&t75 + 16), t74);
                                gm_f_format_hj7ahc4((void *)&gm_l26, (void *)NULL, cx, NULL, &t75);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_src8lmgjh8bsvgp, 474, 20);
                                gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                                gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                                F.ret = t78;
                                goto gm_fret;
                            }
gm_lb11:;
                        }
                        {
                            {
                                uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                uint8_t t80 = (uint8_t)(t79 == ((uint8_t)8ULL));
                                if (!t80) {
                                    goto gm_lb12;
                                }
                                gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
                                gm_l8 = t81;
                                gm_trqfvmjtt4sij t82;
                                gm_f_list_uvfu1k1((void *)&t82, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l9 = t82;
                                gm_b16_8 t83 = gm_l8;
                                void * t84 = (void *)((uint8_t *)&F.l9);
                                gm_b0_1 t85 = gm_f_write_debug_string_pns0400((void *)NULL, cx, NULL, t83, t84);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                                gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                                F.ret = t87;
                                goto gm_fret;
                            }
gm_lb12:;
                            {
                                uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                uint8_t t89 = (uint8_t)(t88 == ((uint8_t)5ULL));
                                if (!t89) {
                                    goto gm_lb13;
                                }
                                uint32_t t90 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 8));
                                gm_l10 = t90;
                                gm_b16_8 t91;
                                GM_ST(void *, (uint8_t *)&t91, (void *)(gm_sfqk3rro4g7t0));
                                GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(1));
                                gm_l11 = t91;
                                {
                                    {
                                        void * t92 = gm_l0;
                                        gm_t88rcucg1b7uk t93 = gm_l2;
                                        gm_f_union_variants_a7khaqp((void *)&gm_l27, t92, GM_CONV(gm_csguankko4lmhd, t93));
                                        uint8_t t94 = (uint8_t)((((uint8_t *)&gm_l27))[24] != 0);
                                        if (!t94) {
                                            goto gm_lb15;
                                        }
                                        memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l27), 24);
                                        void * t95 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                        uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                        uint32_t t97 = gm_l10;
                                        uint64_t t98 = (uint64_t)t97;
                                        if ((uintptr_t)t98 >= (uintptr_t)(t96)) gm_trap_index((uintptr_t)t98, t96, gm_src8lmgjh8bsvgp, 483, 52);
                                        gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)t95) + (uintptr_t)t98 * 72));
                                        gm_l11 = t99;
                                        goto gm_lb14;
                                    }
gm_lb15:;
                                }
gm_lb14:;
                                uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                uint8_t t101 = ((t100 == ((uint64_t)0ULL)));
                                if (t101) {
                                    gm_b16_8 t102 = gm_l11;
                                    gm_tnpifv20irnph t103;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t103 + 0), t102);
                                    gm_f_format_g0ibdjv((void *)&gm_l28, (void *)NULL, cx, NULL, t103);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t104 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                    if (!t104) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_src8lmgjh8bsvgp, 484, 39);
                                    gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                                    gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                                    F.ret = t106;
                                    goto gm_fret;
                                }
                                gm_b16_8 t107 = gm_l11;
                                void * t108 = gm_l0;
                                void * t109 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t110)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t110, gm_src8lmgjh8bsvgp, 485, 65);
                                void * t111 = (void *)(((uint8_t *)t109) + (uintptr_t)((uint64_t)0ULL) * 136);
                                gm_b16_8 t112 = gm_f_show_witness_fmsa425((void *)NULL, cx, NULL, t108, t111);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t6j6smj7qoctu t113;
                                GM_ST(gm_b16_8, ((uint8_t *)&t113 + 0), t107);
                                GM_ST(gm_b16_8, ((uint8_t *)&t113 + 16), t112);
                                gm_f_format_i83sdhh((void *)&gm_l29, (void *)NULL, cx, NULL, &t113);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t114 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                if (!t114) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_src8lmgjh8bsvgp, 485, 20);
                                gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
                                gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
                                F.ret = t116;
                                goto gm_fret;
                            }
gm_lb13:;
                        }
                        {
                            {
                                uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                uint8_t t118 = (uint8_t)(t117 == ((uint8_t)6ULL));
                                if (!t118) {
                                    goto gm_lb16;
                                }
                                gm_trqfvmjtt4sij t119;
                                gm_f_list_epvducg((void *)&t119, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l13 = t119;
                                gm_l15 = ((uint64_t)0ULL);
                                uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                gm_l14 = t120;
                                {
                                    for (;;) {
                                        uint64_t t121 = gm_l15;
                                        uint64_t t122 = gm_l14;
                                        uint8_t t123 = (t121 < t122);
                                        if (!t123) {
                                            goto gm_lb17;
                                        }
                                        {
                                            {
                                                {
                                                    uint64_t t124 = gm_l15;
                                                    gm_l16 = t124;
                                                    void * t125 = (void *)((uint8_t *)&F.l13);
                                                    void * t126 = gm_l0;
                                                    void * t127 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                    uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                    uint64_t t129 = gm_l16;
                                                    void * t130 = (void *)(((uint8_t *)t127) + (uintptr_t)t129 * 136);
                                                    gm_b16_8 t131 = gm_f_show_witness_fmsa425((void *)NULL, cx, NULL, t126, t130);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tftb1odc739t5 t132 = gm_f_push_3ef746b(t125, t131);
                                                    gm_l30 = t132;
                                                    uint8_t t133 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                                                    if (!t133) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_src8lmgjh8bsvgp, 489, 36);
                                                    goto gm_lb20;
                                                }
gm_lb21:;
                                                gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 489, 13);
                                            }
gm_lb20:;
                                        }
gm_lb19:;
                                        uint64_t t134 = gm_l15;
                                        uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                                        uint64_t t137 = r135;
                                        gm_l15 = t137;
                                    }
                                }
gm_lb17:;
                                {
                                    {
                                        void * t138 = gm_l0;
                                        void * t139 = (void *)(((uint8_t *)t138) + 8);
                                        gm_t88rcucg1b7uk t140 = gm_l2;
                                        gm_f_kind_jcnt4hm((void *)&gm_l31, t139, GM_CONV(gm_csguankko4lmhd, t140));
                                        uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 0));
                                        uint8_t t142 = (uint8_t)(t141 == ((uint8_t)17ULL));
                                        if (!t142) {
                                            goto gm_lb23;
                                        }
                                        uint32_t t143 = GM_LD(uint32_t, (((uint8_t *)&gm_l31) + 8));
                                        gm_l17 = t143;
                                        gm_trqfvmjtt4sij t144;
                                        gm_f_list_epvducg((void *)&t144, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l18 = t144;
                                        {
                                            {
                                                void * t145 = gm_l0;
                                                gm_t88rcucg1b7uk t146 = gm_l2;
                                                gm_f_struct_fields_chqu3it((void *)&gm_l32, t145, GM_CONV(gm_csguankko4lmhd, t146));
                                                uint8_t t147 = (uint8_t)((((uint8_t *)&gm_l32))[16] != 0);
                                                if (!t147) {
                                                    goto gm_lb25;
                                                }
                                                gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l32));
                                                gm_l19 = t148;
                                                gm_b16_8 t149 = gm_l19;
                                                gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                                                gm_l20 = t150;
                                                gm_l21 = ((uint64_t)0ULL);
                                                {
                                                    for (;;) {
                                                        uint64_t t151 = gm_l21;
                                                        uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                                        uint8_t t153 = (t151 < t152);
                                                        if (!t153) {
                                                            goto gm_lb26;
                                                        }
                                                        {
                                                            uint64_t t154 = gm_l21;
                                                            void * t155 = GM_LD(void *, ((uint8_t *)&gm_l20));
                                                            uint64_t t156 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                                            uint64_t t157 = gm_l21;
                                                            gm_t0mglksrkfa5e t158 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t155) + (uintptr_t)t157 * 40));
                                                            gm_t8ta482bl6be0 t159;
                                                            GM_ST(uint64_t, ((uint8_t *)&t159 + 0), t154);
                                                            GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&t159 + 8), t158);
                                                            gm_l33 = t159;
                                                            {
                                                                {
                                                                    uint64_t t160 = GM_LD(uint64_t, ((uint8_t *)&gm_l33));
                                                                    gm_l22 = t160;
                                                                    memmove(((uint8_t *)&F.l23), (((uint8_t *)&gm_l33) + 8), 40);
                                                                    {
                                                                        {
                                                                            uint64_t t161 = gm_l22;
                                                                            uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 8));
                                                                            uint8_t t163 = (t161 < t162);
                                                                            if (!t163) {
                                                                                goto gm_lb32;
                                                                            }
                                                                            void * t164 = GM_LD(void *, ((uint8_t *)&F.l13));
                                                                            uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 8));
                                                                            uint64_t t166 = gm_l22;
                                                                            if ((uintptr_t)t166 >= (uintptr_t)(t165)) gm_trap_index((uintptr_t)t166, t165, gm_src8lmgjh8bsvgp, 494, 52);
                                                                            gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)t164) + (uintptr_t)t166 * 16));
                                                                            gm_b16_8 t168;
                                                                            GM_ST(void *, (uint8_t *)&t168, (void *)(gm_sfdbfrv46lihd));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(1));
                                                                            uint8_t t169 = (!gm_eq0iqiubfm7e(((uint8_t *)&t167), ((uint8_t *)&t168)));
                                                                            if (!t169) {
                                                                                goto gm_lb32;
                                                                            }
                                                                            void * t170 = (void *)((uint8_t *)&F.l18);
                                                                            gm_b16_8 t171 = GM_LD(gm_b16_8, ((uint8_t *)&F.l23));
                                                                            void * t172 = GM_LD(void *, ((uint8_t *)&F.l13));
                                                                            uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 8));
                                                                            uint64_t t174 = gm_l22;
                                                                            if ((uintptr_t)t174 >= (uintptr_t)(t173)) gm_trap_index((uintptr_t)t174, t173, gm_src8lmgjh8bsvgp, 494, 120);
                                                                            gm_b16_8 t175 = GM_LD(gm_b16_8, (((uint8_t *)t172) + (uintptr_t)t174 * 16));
                                                                            gm_t6j6smj7qoctu t176;
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t176 + 0), t171);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t176 + 16), t175);
                                                                            gm_f_format_f6rro2t((void *)&gm_l34, (void *)NULL, cx, NULL, &t176);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            uint8_t t177 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                                                            if (!t177) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_src8lmgjh8bsvgp, 494, 91);
                                                                            gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                                                                            gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
                                                                            gm_tftb1odc739t5 t180 = gm_f_push_3ef746b(t170, t179);
                                                                            gm_l35 = t180;
                                                                            uint8_t t181 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                                                            if (!t181) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_src8lmgjh8bsvgp, 494, 80);
                                                                            goto gm_lb31;
                                                                        }
gm_lb32:;
                                                                    }
gm_lb31:;
                                                                    goto gm_lb29;
                                                                }
gm_lb30:;
                                                                gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 493, 21);
                                                            }
gm_lb29:;
                                                        }
gm_lb28:;
                                                        uint64_t t182 = gm_l21;
                                                        uint64_t r183; bool o184 = __builtin_add_overflow(t182, ((uint64_t)1ULL), &r183);
                                                        uint64_t t185 = r183;
                                                        gm_l21 = t185;
                                                    }
                                                }
gm_lb26:;
                                                goto gm_lb24;
                                            }
gm_lb25:;
                                        }
gm_lb24:;
                                        void * t186 = gm_l0;
                                        void * t187 = GM_LD(void *, (((uint8_t *)t186) + 1088));
                                        uint64_t t188 = GM_LD(uint64_t, ((((uint8_t *)t186) + 1088) + 8));
                                        uint32_t t189 = gm_l17;
                                        uint64_t t190 = (uint64_t)t189;
                                        if ((uintptr_t)t190 >= (uintptr_t)(t188)) gm_trap_index((uintptr_t)t190, t188, gm_src8lmgjh8bsvgp, 497, 45);
                                        gm_b16_8 t191 = GM_LD(gm_b16_8, (((uint8_t *)t187) + (uintptr_t)t190 * 184));
                                        gm_b16_8 t192 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                                        gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
                                        gm_b16_8 t194;
                                        GM_ST(void *, (uint8_t *)&t194, (void *)(gm_sg5gdnrith2i6));
                                        GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(2));
                                        gm_f_join_sfknljb((void *)&gm_l36, (void *)NULL, cx, NULL, t193, t194);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t195 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                        if (!t195) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_src8lmgjh8bsvgp, 497, 75);
                                        gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                                        gm_t6j6smj7qoctu t197;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t197 + 0), t191);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t197 + 16), t196);
                                        gm_f_format_99gc605((void *)&gm_l37, (void *)NULL, cx, NULL, &t197);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t198 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                        if (!t198) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_src8lmgjh8bsvgp, 497, 24);
                                        gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                                        gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
                                        F.ret = t200;
                                        goto gm_fret;
                                    }
gm_lb23:;
                                }
gm_lb22:;
                                gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                                gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&t201));
                                gm_b16_8 t203;
                                GM_ST(void *, (uint8_t *)&t203, (void *)(gm_sg5gdnrith2i6));
                                GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(2));
                                gm_f_join_sfknljb((void *)&gm_l38, (void *)NULL, cx, NULL, t202, t203);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t204 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                if (!t204) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 8, gm_src8lmgjh8bsvgp, 499, 39);
                                gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                                gm_tnpifv20irnph t206;
                                GM_ST(gm_b16_8, ((uint8_t *)&t206 + 0), t205);
                                gm_f_format_0gv6kss((void *)&gm_l39, (void *)NULL, cx, NULL, t206);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                                if (!t207) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 8, gm_src8lmgjh8bsvgp, 499, 20);
                                gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 8));
                                gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                                F.ret = t209;
                                goto gm_fret;
                            }
gm_lb16:;
                        }
                        {
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 467, 5);
                        }
                    }
gm_lb3:;
                }
            }
gm_lb2:;
        }
        {
            gm_b16_8 t210;
            GM_ST(void *, (uint8_t *)&t210, (void *)(gm_sfdbfrv46lihd));
            GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(1));
            F.ret = t210;
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
inline __attribute__((aligned(4))) void gm_f_specialize_si0k4v5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_specialize_si0k4v5 F;
    gm_b16_8 gm_l0; /* row */
    void * gm_l1; /* c */
    gm_b16_8 gm_l2; /* arity_tys */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_b16_8 gm_l8; /* subs */
    GM_STACK_CHECK(&F, 619u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
    if (t11) {
        gm_th2lqbo77pvsc t12;
        memset(&t12, 0, sizeof t12);
        memmove(gm_out, ((uint8_t *)&t12), 24);
        goto gm_fdone;
    }
    gm_trqfvmjtt4sij t13;
    gm_f_list_99597qp((void *)&t13, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t13;
    void * t14 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t15, gm_src8lmgjh8bsvgp, 339, 11);
    {
        {
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t14) + (uintptr_t)((uint64_t)0ULL) * 136) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)0ULL));
            if (!t17) {
                goto gm_lb2;
            }
            gm_b16_8 t18 = gm_l2;
            gm_l4 = t18;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t19 = gm_l5;
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t21 = (t19 < t20);
                    if (!t21) {
                        goto gm_lb3;
                    }
                    {
                        void * t22 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t24 = gm_l5;
                        {
                            {
                                gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t22) + (uintptr_t)t24 * 4));
                                gm_l6 = t25;
                                void * t26 = (void *)((uint8_t *)&F.l3);
                                gm_t88rcucg1b7uk t27 = gm_l6;
                                gm_t8oufsr65o5ha t28;
                                memset(&t28, 0, sizeof t28);
                                GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t28 + 8), t27);
                                gm_tftb1odc739t5 t29 = gm_f_push_h9qsb4d(t26, &t28);
                                gm_l9 = t29;
                                uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                                if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_src8lmgjh8bsvgp, 340, 42);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 340, 21);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t31 = gm_l5;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l5 = t34;
                }
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t14) + (uintptr_t)((uint64_t)0ULL) * 136) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)1ULL));
            if (!t36) {
                goto gm_lb8;
            }
            memmove(((uint8_t *)&F.l7), (((((uint8_t *)t14) + (uintptr_t)((uint64_t)0ULL) * 136) + 8) + 8), 104);
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((((uint8_t *)t14) + (uintptr_t)((uint64_t)0ULL) * 136) + 8) + 112));
            gm_l8 = t37;
            void * t38 = (void *)((uint8_t *)&F.l7);
            void * t39 = gm_l1;
            uint8_t t40 = gm_f_covers_59m49eg(t38, t39);
            uint8_t t41 = (uint8_t)!t40;
            if (t41) {
                gm_th2lqbo77pvsc t42;
                memset(&t42, 0, sizeof t42);
                memmove(gm_out, ((uint8_t *)&t42), 24);
                goto gm_fdone;
            }
            void * t43 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t44 = gm_l8;
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
            gm_tftb1odc739t5 t46 = gm_f_extend_eht4gub(t43, t45);
            gm_l10 = t46;
            uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_src8lmgjh8bsvgp, 343, 13);
            goto gm_lb1;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 339, 5);
    }
gm_lb1:;
    void * t48 = (void *)((uint8_t *)&F.l3);
    void * t49 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t51 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t52 = (uintptr_t)t50;
    if (t51 > t52 || t52 > (uintptr_t)t50) gm_trap_slice(t51, t52, t50, gm_src8lmgjh8bsvgp, 346, 16);
    uint64_t t53 = ((uintptr_t)t50) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t49 + (uintptr_t)((uint64_t)1ULL) * 136)));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53));
    gm_tftb1odc739t5 t55 = gm_f_extend_eht4gub(t48, t54);
    gm_l11 = t55;
    uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_src8lmgjh8bsvgp, 346, 5);
    gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_th2lqbo77pvsc t58;
    GM_ST(uint8_t, (uint8_t *)&t58 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t58 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t58 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t58), t57);
    ((uint8_t *)&t58)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t58), 24);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_sub_types_o2rpif3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2) {
    struct gm_fr_sub_types_o2rpif3 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    void * gm_l2; /* c */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_t88rcucg1b7uk gm_l5; /* err */
    gm_t88rcucg1b7uk gm_l6; /* ok */
    gm_b16_8 gm_l7; /* elems */
    gm_b16_8 gm_l8; /* fields */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint32_t gm_l12; /* v */
    gm_tep2drsl9408b gm_l13; /* uv */
    gm_t88rcucg1b7uk gm_l14; /* p */
    GM_STACK_CHECK(&F, 1742u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_th2lqbo77pvsc gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tdlpv0q69lov1 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_trqfvmjtt4sij t21;
    gm_f_list_nof7vfj((void *)&t21, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t21;
    void * t22 = gm_l0;
    void * t23 = (void *)(((uint8_t *)t22) + 8);
    gm_t88rcucg1b7uk t24 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l15, t23, GM_CONV(gm_csguankko4lmhd, t24));
    {
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)13ULL));
            if (!t26) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l15) + 8));
            gm_l4 = t27;
            void * t28 = gm_l2;
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)t28) + 0));
            uint8_t t30 = (uint8_t)((t29 == ((uint8_t)2ULL)));
            gm_taevtln836oa8 t31 = GM_LD(gm_taevtln836oa8, ((uint8_t *)t28));
            gm_taevtln836oa8 t32;
            memset(&t32, 0, sizeof t32);
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)2ULL));
            if (t30) {
                void * t33 = (void *)((uint8_t *)&F.l3);
                gm_t88rcucg1b7uk t34 = gm_l4;
                gm_tftb1odc739t5 t35 = gm_f_push_k3l9nic(t33, GM_CONV(gm_csguankko4lmhd, t34));
                gm_l16 = t35;
                uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_src8lmgjh8bsvgp, 215, 42);
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
            uint8_t t38 = (uint8_t)(t37 == ((uint8_t)14ULL));
            if (!t38) {
                goto gm_lb3;
            }
            gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l15) + 8));
            gm_l5 = t39;
            gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l15) + 8) + 4));
            gm_l6 = t40;
            void * t41 = gm_l2;
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)t41) + 0));
            uint8_t t43 = (uint8_t)((t42 == ((uint8_t)3ULL)));
            gm_taevtln836oa8 t44 = GM_LD(gm_taevtln836oa8, ((uint8_t *)t41));
            gm_taevtln836oa8 t45;
            memset(&t45, 0, sizeof t45);
            GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)3ULL));
            if (t43) {
                void * t46 = (void *)((uint8_t *)&F.l3);
                gm_t88rcucg1b7uk t47 = gm_l6;
                gm_tftb1odc739t5 t48 = gm_f_push_k3l9nic(t46, GM_CONV(gm_csguankko4lmhd, t47));
                gm_l17 = t48;
                uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_src8lmgjh8bsvgp, 217, 28);
            }
            void * t50 = gm_l2;
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)t50) + 0));
            uint8_t t52 = (uint8_t)((t51 == ((uint8_t)4ULL)));
            gm_taevtln836oa8 t53 = GM_LD(gm_taevtln836oa8, ((uint8_t *)t50));
            gm_taevtln836oa8 t54;
            memset(&t54, 0, sizeof t54);
            GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)4ULL));
            if (t52) {
                void * t55 = (void *)((uint8_t *)&F.l3);
                gm_t88rcucg1b7uk t56 = gm_l5;
                gm_tftb1odc739t5 t57 = gm_f_push_k3l9nic(t55, GM_CONV(gm_csguankko4lmhd, t56));
                gm_l18 = t57;
                uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_src8lmgjh8bsvgp, 218, 29);
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
            uint8_t t60 = (uint8_t)(t59 == ((uint8_t)12ULL));
            if (!t60) {
                goto gm_lb4;
            }
            gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
            gm_l7 = t61;
            void * t62 = gm_l2;
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)t62) + 0));
            uint8_t t64 = (uint8_t)((t63 == ((uint8_t)6ULL)));
            gm_taevtln836oa8 t65 = GM_LD(gm_taevtln836oa8, ((uint8_t *)t62));
            gm_taevtln836oa8 t66;
            memset(&t66, 0, sizeof t66);
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)6ULL));
            if (t64) {
                void * t67 = (void *)((uint8_t *)&F.l3);
                gm_b16_8 t68 = gm_l7;
                gm_tftb1odc739t5 t69 = gm_f_extend_lfqasgq(t67, t68);
                gm_l19 = t69;
                uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_src8lmgjh8bsvgp, 220, 48);
            }
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
            uint8_t t72 = (uint8_t)(t71 == ((uint8_t)17ULL));
            if (!t72) {
                goto gm_lb5;
            }
            void * t73 = gm_l2;
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)t73) + 0));
            uint8_t t75 = (uint8_t)((t74 == ((uint8_t)6ULL)));
            gm_taevtln836oa8 t76 = GM_LD(gm_taevtln836oa8, ((uint8_t *)t73));
            gm_taevtln836oa8 t77;
            memset(&t77, 0, sizeof t77);
            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)6ULL));
            if (t75) {
                {
                    {
                        void * t78 = gm_l0;
                        gm_t88rcucg1b7uk t79 = gm_l1;
                        gm_f_struct_fields_chqu3it((void *)&gm_l20, t78, GM_CONV(gm_csguankko4lmhd, t79));
                        uint8_t t80 = (uint8_t)((((uint8_t *)&gm_l20))[16] != 0);
                        if (!t80) {
                            goto gm_lb7;
                        }
                        gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                        gm_l8 = t81;
                        gm_b16_8 t82 = gm_l8;
                        gm_l9 = t82;
                        gm_l10 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t83 = gm_l10;
                                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                uint8_t t85 = (t83 < t84);
                                if (!t85) {
                                    goto gm_lb8;
                                }
                                {
                                    void * t86 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                    uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                    uint64_t t88 = gm_l10;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l11), (((uint8_t *)t86) + (uintptr_t)t88 * 40), 40);
                                            void * t89 = (void *)((uint8_t *)&F.l3);
                                            gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                                            gm_tftb1odc739t5 t91 = gm_f_push_k3l9nic(t89, GM_CONV(gm_csguankko4lmhd, t90));
                                            gm_l21 = t91;
                                            uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                            if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_src8lmgjh8bsvgp, 224, 39);
                                            goto gm_lb11;
                                        }
gm_lb12:;
                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 224, 21);
                                    }
gm_lb11:;
                                }
gm_lb10:;
                                uint64_t t93 = gm_l10;
                                uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                                uint64_t t96 = r94;
                                gm_l10 = t96;
                            }
                        }
gm_lb8:;
                        goto gm_lb6;
                    }
gm_lb7:;
                }
gm_lb6:;
            } else {
                {
                    {
                        void * t97 = gm_l2;
                        uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)t97) + 0));
                        uint8_t t99 = (uint8_t)(t98 == ((uint8_t)5ULL));
                        if (!t99) {
                            goto gm_lb14;
                        }
                        uint32_t t100 = GM_LD(uint32_t, (((uint8_t *)t97) + 8));
                        gm_l12 = t100;
                        {
                            {
                                void * t101 = gm_l0;
                                gm_t88rcucg1b7uk t102 = gm_l1;
                                gm_f_union_variants_a7khaqp((void *)&gm_l22, t101, GM_CONV(gm_csguankko4lmhd, t102));
                                uint8_t t103 = (uint8_t)((((uint8_t *)&gm_l22))[24] != 0);
                                if (!t103) {
                                    goto gm_lb16;
                                }
                                memmove(((uint8_t *)&gm_l13), ((uint8_t *)&gm_l22), 24);
                                {
                                    {
                                        void * t104 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                        uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                        uint32_t t106 = gm_l12;
                                        uint64_t t107 = (uint64_t)t106;
                                        if ((uintptr_t)t107 >= (uintptr_t)(t105)) gm_trap_index((uintptr_t)t107, t105, gm_src8lmgjh8bsvgp, 228, 29);
                                        uint8_t t108 = (uint8_t)((((((uint8_t *)t104) + (uintptr_t)t107 * 72) + 16))[4] != 0);
                                        if (!t108) {
                                            goto gm_lb18;
                                        }
                                        gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t104) + (uintptr_t)t107 * 72) + 16));
                                        gm_l14 = t109;
                                        void * t110 = (void *)((uint8_t *)&F.l3);
                                        gm_t88rcucg1b7uk t111 = gm_l14;
                                        gm_tftb1odc739t5 t112 = gm_f_push_k3l9nic(t110, GM_CONV(gm_csguankko4lmhd, t111));
                                        gm_l23 = t112;
                                        uint8_t t113 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                                        if (!t113) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_src8lmgjh8bsvgp, 228, 61);
                                        goto gm_lb17;
                                    }
gm_lb18:;
                                }
gm_lb17:;
                                goto gm_lb15;
                            }
gm_lb16:;
                        }
gm_lb15:;
                        goto gm_lb13;
                    }
gm_lb14:;
                }
gm_lb13:;
            }
            goto gm_lb1;
        }
gm_lb5:;
        {
            goto gm_lb1;
        }
gm_lb19:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 214, 5);
    }
gm_lb1:;
    gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    F.ret = t114;
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
__attribute__((aligned(4))) void gm_f_useful_ge1iad5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_useful_ge1iad5 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* rows */
    gm_b16_8 gm_l2; /* q */
    void * gm_l3; /* head */
    gm_t88rcucg1b7uk gm_l4; /* ty */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* r */
    gm_b16_8 gm_l14; /* all */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_b16_8 gm_l23; /* tys */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    gm_b16_8 gm_l27; /* r */
    gm_b16_8 gm_l28; /* x */
    gm_b16_8 gm_l29; /* spec_q */
    gm_b16_8 gm_l30; /* w */
    gm_th2lqbo77pvsc gm_l31; /* ctors */
    gm_tf5srh4mp4b3i gm_l32; /* missing */
    gm_b16_8 gm_l33; /* cs */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    gm_b16_8 gm_l37; /* tys */
    gm_b16_8 gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    gm_b16_8 gm_l41; /* r */
    gm_b16_8 gm_l42; /* x */
    gm_b16_8 gm_l43; /* spec_q */
    gm_b16_8 gm_l44; /* w */
    gm_b16_8 gm_l46; /* <tmp> */
    uint64_t gm_l47; /* <tmp> */
    gm_b16_8 gm_l48; /* r */
    gm_b16_8 gm_l49; /* w */
    gm_b16_8 gm_l51; /* cs */
    uint64_t gm_l52; /* i */
    gm_b16_8 gm_l55; /* <tmp> */
    uint64_t gm_l56; /* <tmp> */
    gm_t88rcucg1b7uk gm_l57; /* t */
    GM_STACK_CHECK(&F, 4355u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l59;
    gm_tftb1odc739t5 gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_th2lqbo77pvsc gm_l62;
    gm_b16_8 gm_l63;
    gm_tftb1odc739t5 gm_l64;
    gm_tftb1odc739t5 gm_l65;
    gm_th2lqbo77pvsc gm_l66;
    gm_tftb1odc739t5 gm_l67;
    gm_th2lqbo77pvsc gm_l68;
    gm_b16_8 gm_l69;
    gm_th2lqbo77pvsc gm_l70;
    uint8_t gm_l71;
    uint8_t gm_l72;
    gm_b16_8 gm_l73;
    gm_th2lqbo77pvsc gm_l74;
    gm_tftb1odc739t5 gm_l75;
    gm_th2lqbo77pvsc gm_l76;
    gm_b16_8 gm_l77;
    gm_th2lqbo77pvsc gm_l78;
    uint8_t gm_l79;
    gm_tftb1odc739t5 gm_l80;
    gm_th2lqbo77pvsc gm_l81;
    gm_b16_8 gm_l82;
    gm_tftb1odc739t5 gm_l83;
    gm_tftb1odc739t5 gm_l84;
    gm_tftb1odc739t5 gm_l85;
    {
        uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint8_t t75 = ((t74 == ((uint64_t)0ULL)));
        if (t75) {
            uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t77 = ((t76 == ((uint64_t)0ULL)));
            if (t77) {
                gm_b16_8 t78 = gm_f_empty_slice_6rs1n5s();
                gm_th2lqbo77pvsc t79;
                GM_ST(uint8_t, (uint8_t *)&t79 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t79 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t79 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t79), t78);
                ((uint8_t *)&t79)[16] = 1;
                memmove(gm_out, ((uint8_t *)&t79), 24);
                goto gm_fdone;
            }
            gm_th2lqbo77pvsc t80;
            memset(&t80, 0, sizeof t80);
            memmove(gm_out, ((uint8_t *)&t80), 24);
            goto gm_fdone;
        }
        void * t81 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t82)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t82, gm_src8lmgjh8bsvgp, 357, 14);
        void * t83 = (void *)(((uint8_t *)t81) + (uintptr_t)((uint64_t)0ULL) * 136);
        gm_l3 = t83;
        void * t84 = gm_l3;
        gm_t88rcucg1b7uk t85 = GM_CONV(gm_t88rcucg1b7uk, gm_f_dpat_ty_2ipsk5e(t84));
        gm_l4 = t85;
        gm_trqfvmjtt4sij t86;
        gm_f_list_99597qp((void *)&t86, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t86;
        gm_b16_8 t87 = gm_l1;
        gm_l6 = t87;
        gm_l7 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t88 = gm_l7;
                uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint8_t t90 = (t88 < t89);
                if (!t90) {
                    goto gm_lb1;
                }
                {
                    void * t91 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint64_t t93 = gm_l7;
                    {
                        {
                            gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)t91) + (uintptr_t)t93 * 16));
                            gm_l8 = t94;
                            uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            uint8_t t96 = (t95 > ((uint64_t)0ULL));
                            if (t96) {
                                void * t97 = (void *)((uint8_t *)&F.l5);
                                void * t98 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                gm_tftb1odc739t5 t100 = gm_f_push_h9qsb4d(t97, &(*(gm_t8oufsr65o5ha *)(((uint8_t *)t98) + (uintptr_t)((uint64_t)0ULL) * 136)));
                                gm_l59 = t100;
                                uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                                if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_src8lmgjh8bsvgp, 361, 24);
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 360, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t102 = gm_l7;
                uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                uint64_t t105 = r103;
                gm_l7 = t105;
            }
        }
gm_lb1:;
        void * t106 = gm_l3;
        {
            {
                {
                    uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)t106) + 0));
                    uint8_t t108 = (uint8_t)(t107 == ((uint8_t)1ULL));
                    if (!t108) {
                        goto gm_lb7;
                    }
                    memmove(((uint8_t *)&F.l9), ((((uint8_t *)t106) + 8) + 8), 104);
                    gm_trqfvmjtt4sij t109;
                    gm_f_list_ahp090e((void *)&t109, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l10 = t109;
                    {
                        {
                            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
                            uint8_t t111 = (uint8_t)(t110 == ((uint8_t)7ULL));
                            if (!t111) {
                                goto gm_lb9;
                            }
                            memmove(((uint8_t *)&F.l11), (((uint8_t *)&F.l9) + 8), 48);
                            memmove(((uint8_t *)&F.l12), ((((uint8_t *)&F.l9) + 8) + 48), 48);
                            gm_trqfvmjtt4sij t112;
                            gm_f_list_99597qp((void *)&t112, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l13 = t112;
                            void * t113 = (void *)((uint8_t *)&F.l13);
                            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                            gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
                            gm_tftb1odc739t5 t116 = gm_f_extend_eht4gub(t113, t115);
                            gm_l60 = t116;
                            uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                            if (!t117) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 1, gm_src8lmgjh8bsvgp, 369, 17);
                            void * t118 = (void *)((uint8_t *)&F.l13);
                            void * t119 = gm_l3;
                            gm_tftb1odc739t5 t120 = gm_f_push_h9qsb4d(t118, &(*(gm_t8oufsr65o5ha *)((uint8_t *)t119)));
                            gm_l61 = t120;
                            uint8_t t121 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                            if (!t121) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_src8lmgjh8bsvgp, 370, 17);
                            void * t122 = gm_l0;
                            gm_t88rcucg1b7uk t123 = gm_l4;
                            gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                            gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
                            gm_f_all_ctors_o7aijfe((void *)&gm_l62, (void *)NULL, cx, NULL, t122, t123, t125);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t126 = (uint8_t)((((uint8_t *)&gm_l62))[16] != 0);
                            if (t126) {
                                gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l62));
                                gm_l63 = t127;
                            } else {
                                gm_b16_8 t128 = gm_f_empty_slice_kvdmouk();
                                gm_l63 = t128;
                            }
                            gm_b16_8 t129 = gm_l63;
                            gm_l14 = t129;
                            gm_b16_8 t130 = gm_l14;
                            gm_l15 = t130;
                            gm_l16 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t131 = gm_l16;
                                    uint64_t t132 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                    uint8_t t133 = (t131 < t132);
                                    if (!t133) {
                                        goto gm_lb10;
                                    }
                                    {
                                        void * t134 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                        uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                        uint64_t t136 = gm_l16;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l17), (((uint8_t *)t134) + (uintptr_t)t136 * 104), 104);
                                                {
                                                    {
                                                        uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                                                        uint8_t t138 = (uint8_t)(t137 == ((uint8_t)7ULL));
                                                        if (!t138) {
                                                            goto gm_lb16;
                                                        }
                                                        memmove(((uint8_t *)&F.l18), (((uint8_t *)&F.l17) + 8), 48);
                                                        memmove(((uint8_t *)&F.l19), ((((uint8_t *)&F.l17) + 8) + 48), 48);
                                                        int32_t t139 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l18)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                                                        uint8_t t140 = (t139 >= ((int32_t)0LL));
                                                        if (!t140) {
                                                            goto gm_lb16;
                                                        }
                                                        int32_t t141 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l19)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l12)));
                                                        uint8_t t142 = (t141 <= ((int32_t)0LL));
                                                        if (!t142) {
                                                            goto gm_lb16;
                                                        }
                                                        void * t143 = (void *)((uint8_t *)&F.l10);
                                                        gm_tftb1odc739t5 t144 = gm_f_push_95uqvlq(t143, &(*(gm_taevtln836oa8 *)((uint8_t *)&F.l17)));
                                                        gm_l64 = t144;
                                                        uint8_t t145 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                                                        if (!t145) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 1, gm_src8lmgjh8bsvgp, 373, 82);
                                                        goto gm_lb15;
                                                    }
gm_lb16:;
                                                }
gm_lb15:;
                                                goto gm_lb13;
                                            }
gm_lb14:;
                                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 372, 17);
                                        }
gm_lb13:;
                                    }
gm_lb12:;
                                    uint64_t t146 = gm_l16;
                                    uint64_t r147; bool o148 = __builtin_add_overflow(t146, ((uint64_t)1ULL), &r147);
                                    uint64_t t149 = r147;
                                    gm_l16 = t149;
                                }
                            }
gm_lb10:;
                            goto gm_lb8;
                        }
gm_lb9:;
                        void * t150 = (void *)((uint8_t *)&F.l10);
                        gm_tftb1odc739t5 t151 = gm_f_push_95uqvlq(t150, &(*(gm_taevtln836oa8 *)((uint8_t *)&F.l9)));
                        gm_l65 = t151;
                        uint8_t t152 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                        if (!t152) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 1, gm_src8lmgjh8bsvgp, 376, 17);
                    }
gm_lb8:;
                    gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                    gm_l20 = t153;
                    gm_l21 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t154 = gm_l21;
                            uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                            uint8_t t156 = (t154 < t155);
                            if (!t156) {
                                goto gm_lb17;
                            }
                            {
                                void * t157 = GM_LD(void *, ((uint8_t *)&gm_l20));
                                uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                uint64_t t159 = gm_l21;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l22), (((uint8_t *)t157) + (uintptr_t)t159 * 104), 104);
                                        void * t160 = gm_l0;
                                        gm_t88rcucg1b7uk t161 = gm_l4;
                                        void * t162 = (void *)((uint8_t *)&F.l22);
                                        gm_b16_8 t163 = gm_f_sub_types_o2rpif3((void *)NULL, cx, NULL, t160, t161, t162);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l23 = t163;
                                        gm_trqfvmjtt4sij t164;
                                        gm_f_list_furncbs((void *)&t164, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l24 = t164;
                                        gm_b16_8 t165 = gm_l1;
                                        gm_l25 = t165;
                                        gm_l26 = ((uint64_t)0ULL);
                                        {
                                            for (;;) {
                                                uint64_t t166 = gm_l26;
                                                uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                                uint8_t t168 = (t166 < t167);
                                                if (!t168) {
                                                    goto gm_lb22;
                                                }
                                                {
                                                    void * t169 = GM_LD(void *, ((uint8_t *)&gm_l25));
                                                    uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                                    uint64_t t171 = gm_l26;
                                                    {
                                                        {
                                                            gm_b16_8 t172 = GM_LD(gm_b16_8, (((uint8_t *)t169) + (uintptr_t)t171 * 16));
                                                            gm_l27 = t172;
                                                            {
                                                                {
                                                                    gm_b16_8 t173 = gm_l27;
                                                                    gm_b16_8 t174 = GM_LD(gm_b16_8, ((uint8_t *)&t173));
                                                                    void * t175 = (void *)((uint8_t *)&F.l22);
                                                                    gm_b16_8 t176 = gm_l23;
                                                                    gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&t176));
                                                                    gm_f_specialize_si0k4v5((void *)&gm_l66, (void *)NULL, cx, NULL, t174, t175, t177);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    uint8_t t178 = (uint8_t)((((uint8_t *)&gm_l66))[16] != 0);
                                                                    if (!t178) {
                                                                        goto gm_lb28;
                                                                    }
                                                                    gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l66));
                                                                    gm_l28 = t179;
                                                                    void * t180 = (void *)((uint8_t *)&F.l24);
                                                                    gm_b16_8 t181 = gm_l28;
                                                                    gm_tftb1odc739t5 t182 = gm_f_push_s94euqb(t180, t181);
                                                                    gm_l67 = t182;
                                                                    uint8_t t183 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                                                                    if (!t183) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 1, gm_src8lmgjh8bsvgp, 382, 58);
                                                                    goto gm_lb27;
                                                                }
gm_lb28:;
                                                            }
gm_lb27:;
                                                            goto gm_lb25;
                                                        }
gm_lb26:;
                                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 381, 17);
                                                    }
gm_lb25:;
                                                }
gm_lb24:;
                                                uint64_t t184 = gm_l26;
                                                uint64_t r185; bool o186 = __builtin_add_overflow(t184, ((uint64_t)1ULL), &r185);
                                                uint64_t t187 = r185;
                                                gm_l26 = t187;
                                            }
                                        }
gm_lb22:;
                                        gm_b16_8 t188 = gm_l2;
                                        void * t189 = (void *)((uint8_t *)&F.l22);
                                        gm_b16_8 t190 = gm_l23;
                                        gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&t190));
                                        gm_f_specialize_si0k4v5((void *)&gm_l68, (void *)NULL, cx, NULL, t188, t189, t191);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t192 = (uint8_t)((((uint8_t *)&gm_l68))[16] != 0);
                                        if (t192) {
                                            gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l68));
                                            gm_l69 = t193;
                                        } else {
                                            gm_th2lqbo77pvsc t194;
                                            memset(&t194, 0, sizeof t194);
                                            memmove(gm_out, ((uint8_t *)&t194), 24);
                                            goto gm_fdone;
                                        }
                                        gm_b16_8 t195 = gm_l69;
                                        gm_l29 = t195;
                                        {
                                            {
                                                void * t196 = gm_l0;
                                                gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
                                                gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)&t197));
                                                gm_b16_8 t199 = gm_l29;
                                                gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
                                                gm_f_useful_ge1iad5((void *)&gm_l70, (void *)NULL, cx, NULL, t196, t198, t200);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t201 = (uint8_t)((((uint8_t *)&gm_l70))[16] != 0);
                                                if (!t201) {
                                                    goto gm_lb30;
                                                }
                                                gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l70));
                                                gm_l30 = t202;
                                                gm_t88rcucg1b7uk t203 = gm_l4;
                                                uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                gm_b16_8 t205 = gm_l30;
                                                gm_b16_8 t206 = gm_f_rebuild_90secge((void *)NULL, cx, NULL, t203, &(*(gm_taevtln836oa8 *)((uint8_t *)&F.l22)), t204, t205);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_th2lqbo77pvsc t207;
                                                GM_ST(uint8_t, (uint8_t *)&t207 + 17, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t207 + 18, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t207 + 20, 0);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t207), t206);
                                                ((uint8_t *)&t207)[16] = 1;
                                                memmove(gm_out, ((uint8_t *)&t207), 24);
                                                goto gm_fdone;
                                            }
gm_lb30:;
                                        }
gm_lb29:;
                                        goto gm_lb20;
                                    }
gm_lb21:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 378, 13);
                                }
gm_lb20:;
                            }
gm_lb19:;
                            uint64_t t208 = gm_l21;
                            uint64_t r209; bool o210 = __builtin_add_overflow(t208, ((uint64_t)1ULL), &r209);
                            uint64_t t211 = r209;
                            gm_l21 = t211;
                        }
                    }
gm_lb17:;
                    gm_th2lqbo77pvsc t212;
                    memset(&t212, 0, sizeof t212);
                    memmove(gm_out, ((uint8_t *)&t212), 24);
                    goto gm_fdone;
                }
gm_lb7:;
            }
            {
                {
                    {
                        uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)t106) + 0));
                        uint8_t t214 = (uint8_t)(t213 == ((uint8_t)0ULL));
                        if (!t214) {
                            goto gm_lb31;
                        }
                        void * t215 = gm_l0;
                        gm_t88rcucg1b7uk t216 = gm_l4;
                        gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                        gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&t217));
                        gm_f_all_ctors_o7aijfe((void *)&gm_l31, (void *)NULL, cx, NULL, t215, t216, t218);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tf5srh4mp4b3i t219;
                        memset(&t219, 0, sizeof t219);
                        gm_l32 = t219;
                        {
                            {
                                uint8_t t220 = (uint8_t)((((uint8_t *)&gm_l31))[16] != 0);
                                if (!t220) {
                                    goto gm_lb33;
                                }
                                gm_b16_8 t221 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
                                gm_l33 = t221;
                                gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                                gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&t222));
                                gm_b16_8 t224 = gm_l33;
                                gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&t224));
                                gm_tf5srh4mp4b3i t226 = gm_f_first_uncovered_fefms0c((void *)NULL, cx, NULL, t223, t225);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l32 = t226;
                                goto gm_lb32;
                            }
gm_lb33:;
                        }
gm_lb32:;
                        {
                            {
                                {
                                    {
                                        uint8_t t227 = (uint8_t)((((uint8_t *)&gm_l31))[16] != 0);
                                        if (t227) {
                                            goto gm_lb37;
                                        }
                                        gm_l71 = ((uint8_t)0);
                                        goto gm_lb36;
                                    }
gm_lb37:;
                                    {
                                        gm_l71 = ((uint8_t)1);
                                        goto gm_lb36;
                                    }
gm_lb38:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 394, 16);
                                }
gm_lb36:;
                                uint8_t t228 = gm_l71;
                                if (!t228) {
                                    goto gm_lb35;
                                }
                                {
                                    {
                                        uint8_t t229 = (uint8_t)((((uint8_t *)&gm_l32))[8] != 0);
                                        if (t229) {
                                            goto gm_lb40;
                                        }
                                        gm_l72 = ((uint8_t)1);
                                        goto gm_lb39;
                                    }
gm_lb40:;
                                    {
                                        gm_l72 = ((uint8_t)0);
                                        goto gm_lb39;
                                    }
gm_lb41:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 394, 38);
                                }
gm_lb39:;
                                uint8_t t230 = gm_l72;
                                if (!t230) {
                                    goto gm_lb35;
                                }
                                uint8_t t231 = (uint8_t)((((uint8_t *)&gm_l31))[16] != 0);
                                if (t231) {
                                    gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
                                    gm_l73 = t232;
                                } else {
                                    gm_b16_8 t233 = gm_f_empty_slice_kvdmouk();
                                    gm_l73 = t233;
                                }
                                gm_b16_8 t234 = gm_l73;
                                gm_l34 = t234;
                                gm_l35 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t235 = gm_l35;
                                        uint64_t t236 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                                        uint8_t t237 = (t235 < t236);
                                        if (!t237) {
                                            goto gm_lb42;
                                        }
                                        {
                                            void * t238 = GM_LD(void *, ((uint8_t *)&gm_l34));
                                            uint64_t t239 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                                            uint64_t t240 = gm_l35;
                                            {
                                                {
                                                    memmove(((uint8_t *)&F.l36), (((uint8_t *)t238) + (uintptr_t)t240 * 104), 104);
                                                    void * t241 = gm_l0;
                                                    gm_t88rcucg1b7uk t242 = gm_l4;
                                                    void * t243 = (void *)((uint8_t *)&F.l36);
                                                    gm_b16_8 t244 = gm_f_sub_types_o2rpif3((void *)NULL, cx, NULL, t241, t242, t243);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l37 = t244;
                                                    gm_trqfvmjtt4sij t245;
                                                    gm_f_list_furncbs((void *)&t245, (void *)NULL, cx, NULL);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    F.l38 = t245;
                                                    gm_b16_8 t246 = gm_l1;
                                                    gm_l39 = t246;
                                                    gm_l40 = ((uint64_t)0ULL);
                                                    {
                                                        for (;;) {
                                                            uint64_t t247 = gm_l40;
                                                            uint64_t t248 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                                                            uint8_t t249 = (t247 < t248);
                                                            if (!t249) {
                                                                goto gm_lb47;
                                                            }
                                                            {
                                                                void * t250 = GM_LD(void *, ((uint8_t *)&gm_l39));
                                                                uint64_t t251 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                                                                uint64_t t252 = gm_l40;
                                                                {
                                                                    {
                                                                        gm_b16_8 t253 = GM_LD(gm_b16_8, (((uint8_t *)t250) + (uintptr_t)t252 * 16));
                                                                        gm_l41 = t253;
                                                                        {
                                                                            {
                                                                                gm_b16_8 t254 = gm_l41;
                                                                                gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
                                                                                void * t256 = (void *)((uint8_t *)&F.l36);
                                                                                gm_b16_8 t257 = gm_l37;
                                                                                gm_b16_8 t258 = GM_LD(gm_b16_8, ((uint8_t *)&t257));
                                                                                gm_f_specialize_si0k4v5((void *)&gm_l74, (void *)NULL, cx, NULL, t255, t256, t258);
                                                                                if (gm_unw.kind) {
                                                                                    goto gm_lad;
                                                                                }
                                                                                uint8_t t259 = (uint8_t)((((uint8_t *)&gm_l74))[16] != 0);
                                                                                if (!t259) {
                                                                                    goto gm_lb53;
                                                                                }
                                                                                gm_b16_8 t260 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l74));
                                                                                gm_l42 = t260;
                                                                                void * t261 = (void *)((uint8_t *)&F.l38);
                                                                                gm_b16_8 t262 = gm_l42;
                                                                                gm_tftb1odc739t5 t263 = gm_f_push_s94euqb(t261, t262);
                                                                                gm_l75 = t263;
                                                                                uint8_t t264 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                                                                                if (!t264) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 1, gm_src8lmgjh8bsvgp, 399, 58);
                                                                                goto gm_lb52;
                                                                            }
gm_lb53:;
                                                                        }
gm_lb52:;
                                                                        goto gm_lb50;
                                                                    }
gm_lb51:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 398, 21);
                                                                }
gm_lb50:;
                                                            }
gm_lb49:;
                                                            uint64_t t265 = gm_l40;
                                                            uint64_t r266; bool o267 = __builtin_add_overflow(t265, ((uint64_t)1ULL), &r266);
                                                            uint64_t t268 = r266;
                                                            gm_l40 = t268;
                                                        }
                                                    }
gm_lb47:;
                                                    gm_b16_8 t269 = gm_l2;
                                                    void * t270 = (void *)((uint8_t *)&F.l36);
                                                    gm_b16_8 t271 = gm_l37;
                                                    gm_b16_8 t272 = GM_LD(gm_b16_8, ((uint8_t *)&t271));
                                                    gm_f_specialize_si0k4v5((void *)&gm_l76, (void *)NULL, cx, NULL, t269, t270, t272);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t273 = (uint8_t)((((uint8_t *)&gm_l76))[16] != 0);
                                                    if (t273) {
                                                        gm_b16_8 t274 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l76));
                                                        gm_l77 = t274;
                                                    } else {
                                                        gm_th2lqbo77pvsc t275;
                                                        memset(&t275, 0, sizeof t275);
                                                        memmove(gm_out, ((uint8_t *)&t275), 24);
                                                        goto gm_fdone;
                                                    }
                                                    gm_b16_8 t276 = gm_l77;
                                                    gm_l43 = t276;
                                                    {
                                                        {
                                                            void * t277 = gm_l0;
                                                            gm_b16_8 t278 = GM_LD(gm_b16_8, ((uint8_t *)&F.l38));
                                                            gm_b16_8 t279 = GM_LD(gm_b16_8, ((uint8_t *)&t278));
                                                            gm_b16_8 t280 = gm_l43;
                                                            gm_b16_8 t281 = GM_LD(gm_b16_8, ((uint8_t *)&t280));
                                                            gm_f_useful_ge1iad5((void *)&gm_l78, (void *)NULL, cx, NULL, t277, t279, t281);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            uint8_t t282 = (uint8_t)((((uint8_t *)&gm_l78))[16] != 0);
                                                            if (!t282) {
                                                                goto gm_lb55;
                                                            }
                                                            gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l78));
                                                            gm_l44 = t283;
                                                            gm_t88rcucg1b7uk t284 = gm_l4;
                                                            uint64_t t285 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                                                            gm_b16_8 t286 = gm_l44;
                                                            gm_b16_8 t287 = gm_f_rebuild_90secge((void *)NULL, cx, NULL, t284, &(*(gm_taevtln836oa8 *)((uint8_t *)&F.l36)), t285, t286);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            gm_th2lqbo77pvsc t288;
                                                            GM_ST(uint8_t, (uint8_t *)&t288 + 17, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t288 + 18, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t288 + 20, 0);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t288), t287);
                                                            ((uint8_t *)&t288)[16] = 1;
                                                            memmove(gm_out, ((uint8_t *)&t288), 24);
                                                            goto gm_fdone;
                                                        }
gm_lb55:;
                                                    }
gm_lb54:;
                                                    goto gm_lb45;
                                                }
gm_lb46:;
                                                gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 395, 17);
                                            }
gm_lb45:;
                                        }
gm_lb44:;
                                        uint64_t t289 = gm_l35;
                                        uint64_t r290; bool o291 = __builtin_add_overflow(t289, ((uint64_t)1ULL), &r290);
                                        uint64_t t292 = r290;
                                        gm_l35 = t292;
                                    }
                                }
gm_lb42:;
                                gm_th2lqbo77pvsc t293;
                                memset(&t293, 0, sizeof t293);
                                memmove(gm_out, ((uint8_t *)&t293), 24);
                                goto gm_fdone;
                            }
gm_lb35:;
                        }
gm_lb34:;
                    }
                    {
                        gm_trqfvmjtt4sij t294;
                        gm_f_list_furncbs((void *)&t294, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l45 = t294;
                        gm_b16_8 t295 = gm_l1;
                        gm_l46 = t295;
                        gm_l47 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t296 = gm_l47;
                                uint64_t t297 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 8));
                                uint8_t t298 = (t296 < t297);
                                if (!t298) {
                                    goto gm_lb56;
                                }
                                {
                                    void * t299 = GM_LD(void *, ((uint8_t *)&gm_l46));
                                    uint64_t t300 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 8));
                                    uint64_t t301 = gm_l47;
                                    {
                                        {
                                            gm_b16_8 t302 = GM_LD(gm_b16_8, (((uint8_t *)t299) + (uintptr_t)t301 * 16));
                                            gm_l48 = t302;
                                            {
                                                {
                                                    uint64_t t303 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                                                    uint8_t t304 = (t303 > ((uint64_t)0ULL));
                                                    if (!t304) {
                                                        goto gm_lb62;
                                                    }
                                                    void * t305 = GM_LD(void *, ((uint8_t *)&gm_l48));
                                                    uint64_t t306 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                                                    {
                                                        {
                                                            uint8_t t307 = GM_LD(uint8_t, ((((uint8_t *)t305) + (uintptr_t)((uint64_t)0ULL) * 136) + 0));
                                                            uint8_t t308 = (uint8_t)(t307 == ((uint8_t)0ULL));
                                                            if (!t308) {
                                                                goto gm_lb64;
                                                            }
                                                            gm_l79 = ((uint8_t)1);
                                                            goto gm_lb63;
                                                        }
gm_lb64:;
                                                        {
                                                            gm_l79 = ((uint8_t)0);
                                                            goto gm_lb63;
                                                        }
gm_lb65:;
                                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 408, 34);
                                                    }
gm_lb63:;
                                                    uint8_t t309 = gm_l79;
                                                    if (!t309) {
                                                        goto gm_lb62;
                                                    }
                                                    void * t310 = (void *)((uint8_t *)&F.l45);
                                                    void * t311 = GM_LD(void *, ((uint8_t *)&gm_l48));
                                                    uint64_t t312 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                                                    uint64_t t313 = (uintptr_t)((uint64_t)1ULL);
                                                    uint64_t t314 = (uintptr_t)t312;
                                                    if (t313 > t314 || t314 > (uintptr_t)t312) gm_trap_slice(t313, t314, t312, gm_src8lmgjh8bsvgp, 408, 66);
                                                    uint64_t t315 = ((uintptr_t)t312) - (uintptr_t)((uint64_t)1ULL);
                                                    gm_b16_8 t316;
                                                    GM_ST(void *, (uint8_t *)&t316, (void *)(((uint8_t *)t311 + (uintptr_t)((uint64_t)1ULL) * 136)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t316 + 8, (uintptr_t)(t315));
                                                    gm_tftb1odc739t5 t317 = gm_f_push_s94euqb(t310, t316);
                                                    gm_l80 = t317;
                                                    uint8_t t318 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                                                    if (!t318) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 1, gm_src8lmgjh8bsvgp, 408, 53);
                                                    goto gm_lb61;
                                                }
gm_lb62:;
                                            }
gm_lb61:;
                                            goto gm_lb59;
                                        }
gm_lb60:;
                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 407, 13);
                                    }
gm_lb59:;
                                }
gm_lb58:;
                                uint64_t t319 = gm_l47;
                                uint64_t r320; bool o321 = __builtin_add_overflow(t319, ((uint64_t)1ULL), &r320);
                                uint64_t t322 = r320;
                                gm_l47 = t322;
                            }
                        }
gm_lb56:;
                        void * t323 = gm_l0;
                        gm_b16_8 t324 = GM_LD(gm_b16_8, ((uint8_t *)&F.l45));
                        gm_b16_8 t325 = GM_LD(gm_b16_8, ((uint8_t *)&t324));
                        void * t326 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t327 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t328 = (uintptr_t)((uint64_t)1ULL);
                        uint64_t t329 = (uintptr_t)t327;
                        if (t328 > t329 || t329 > (uintptr_t)t327) gm_trap_slice(t328, t329, t327, gm_src8lmgjh8bsvgp, 410, 43);
                        uint64_t t330 = ((uintptr_t)t327) - (uintptr_t)((uint64_t)1ULL);
                        gm_b16_8 t331;
                        GM_ST(void *, (uint8_t *)&t331, (void *)(((uint8_t *)t326 + (uintptr_t)((uint64_t)1ULL) * 136)));
                        GM_ST(uintptr_t, (uint8_t *)&t331 + 8, (uintptr_t)(t330));
                        gm_f_useful_ge1iad5((void *)&gm_l81, (void *)NULL, cx, NULL, t323, t325, t331);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t332 = (uint8_t)((((uint8_t *)&gm_l81))[16] != 0);
                        if (t332) {
                            gm_b16_8 t333 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l81));
                            gm_l82 = t333;
                        } else {
                            gm_th2lqbo77pvsc t334;
                            memset(&t334, 0, sizeof t334);
                            memmove(gm_out, ((uint8_t *)&t334), 24);
                            goto gm_fdone;
                        }
                    }
                    {
                        gm_b16_8 t335 = gm_l82;
                        gm_l49 = t335;
                        gm_t88rcucg1b7uk t336 = gm_l4;
                        gm_t8oufsr65o5ha t337;
                        memset(&t337, 0, sizeof t337);
                        GM_ST(uint8_t, ((uint8_t *)&t337 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t337 + 8), t336);
                        F.l50 = t337;
                        {
                            {
                                uint8_t t338 = (uint8_t)((((uint8_t *)&gm_l31))[16] != 0);
                                if (!t338) {
                                    goto gm_lb67;
                                }
                                gm_b16_8 t339 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
                                gm_l51 = t339;
                                uint8_t t340 = (uint8_t)((((uint8_t *)&gm_l32))[8] != 0);
                                if (!t340) {
                                    goto gm_lb67;
                                }
                                uint64_t t341 = GM_LD(uint64_t, ((uint8_t *)&gm_l32));
                                gm_l52 = t341;
                                void * t342 = GM_LD(void *, ((uint8_t *)&gm_l51));
                                uint64_t t343 = GM_LD(uint64_t, (((uint8_t *)&gm_l51) + 8));
                                uint64_t t344 = gm_l52;
                                if ((uintptr_t)t344 >= (uintptr_t)(t343)) gm_trap_index((uintptr_t)t344, t343, gm_src8lmgjh8bsvgp, 414, 22);
                                memmove(((uint8_t *)&F.l53), (((uint8_t *)t342) + (uintptr_t)t344 * 104), 104);
                                gm_trqfvmjtt4sij t345;
                                gm_f_list_99597qp((void *)&t345, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l54 = t345;
                                void * t346 = gm_l0;
                                gm_t88rcucg1b7uk t347 = gm_l4;
                                void * t348 = (void *)((uint8_t *)&F.l53);
                                gm_b16_8 t349 = gm_f_sub_types_o2rpif3((void *)NULL, cx, NULL, t346, t347, t348);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l55 = t349;
                                gm_l56 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t350 = gm_l56;
                                        uint64_t t351 = GM_LD(uint64_t, (((uint8_t *)&gm_l55) + 8));
                                        uint8_t t352 = (t350 < t351);
                                        if (!t352) {
                                            goto gm_lb68;
                                        }
                                        {
                                            void * t353 = GM_LD(void *, ((uint8_t *)&gm_l55));
                                            uint64_t t354 = GM_LD(uint64_t, (((uint8_t *)&gm_l55) + 8));
                                            uint64_t t355 = gm_l56;
                                            {
                                                {
                                                    gm_t88rcucg1b7uk t356 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t353) + (uintptr_t)t355 * 4));
                                                    gm_l57 = t356;
                                                    void * t357 = (void *)((uint8_t *)&F.l54);
                                                    gm_t88rcucg1b7uk t358 = gm_l57;
                                                    gm_t8oufsr65o5ha t359;
                                                    memset(&t359, 0, sizeof t359);
                                                    GM_ST(uint8_t, ((uint8_t *)&t359 + 0), ((uint8_t)0ULL));
                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t359 + 8), t358);
                                                    gm_tftb1odc739t5 t360 = gm_f_push_h9qsb4d(t357, &t359);
                                                    gm_l83 = t360;
                                                    uint8_t t361 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
                                                    if (!t361) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l83) + 1, gm_src8lmgjh8bsvgp, 416, 49);
                                                    goto gm_lb71;
                                                }
gm_lb72:;
                                                gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 416, 17);
                                            }
gm_lb71:;
                                        }
gm_lb70:;
                                        uint64_t t362 = gm_l56;
                                        uint64_t r363; bool o364 = __builtin_add_overflow(t362, ((uint64_t)1ULL), &r363);
                                        uint64_t t365 = r363;
                                        gm_l56 = t365;
                                    }
                                }
gm_lb68:;
                                gm_t88rcucg1b7uk t366 = gm_l4;
                                gm_b16_8 t367 = GM_LD(gm_b16_8, ((uint8_t *)&F.l54));
                                gm_taevtln836oa8 t368 = GM_LD(gm_taevtln836oa8, ((uint8_t *)&F.l53));
                                gm_tbq1t03a53gpo t369;
                                GM_ST(uint32_t, (uint8_t *)&t369 + 4, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t369 + 0), t366);
                                GM_ST(gm_taevtln836oa8, ((uint8_t *)&t369 + 8), t368);
                                GM_ST(gm_b16_8, ((uint8_t *)&t369 + 112), t367);
                                gm_t8oufsr65o5ha t370;
                                GM_ST(uint8_t, (uint8_t *)&t370 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t370 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t370 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t370 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t370 + 8), t369);
                                F.l50 = t370;
                                goto gm_lb66;
                            }
gm_lb67:;
                        }
gm_lb66:;
                        gm_trqfvmjtt4sij t371;
                        gm_f_list_99597qp((void *)&t371, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l58 = t371;
                    }
                    {
                        void * t372 = (void *)((uint8_t *)&F.l58);
                        gm_tftb1odc739t5 t373 = gm_f_push_h9qsb4d(t372, &(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l50)));
                        gm_l84 = t373;
                        uint8_t t374 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
                        if (!t374) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l84) + 1, gm_src8lmgjh8bsvgp, 420, 13);
                        void * t375 = (void *)((uint8_t *)&F.l58);
                        gm_b16_8 t376 = gm_l49;
                        gm_b16_8 t377 = GM_LD(gm_b16_8, ((uint8_t *)&t376));
                        gm_tftb1odc739t5 t378 = gm_f_extend_eht4gub(t375, t377);
                        gm_l85 = t378;
                        uint8_t t379 = (uint8_t)!((((uint8_t *)&gm_l85))[0] != 0);
                        if (!t379) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l85) + 1, gm_src8lmgjh8bsvgp, 421, 13);
                        gm_b16_8 t380 = GM_LD(gm_b16_8, ((uint8_t *)&F.l58));
                        gm_th2lqbo77pvsc t381;
                        GM_ST(uint8_t, (uint8_t *)&t381 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t381 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t381 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t381), t380);
                        ((uint8_t *)&t381)[16] = 1;
                        memmove(gm_out, ((uint8_t *)&t381), 24);
                        goto gm_fdone;
                    }
                }
gm_lb31:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 363, 5);
            }
        }
gm_lb6:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_wrap_each_2nbqohg(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_wrap_each_2nbqohg F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_b16_8 gm_l2; /* ds */
    void * gm_l3; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 579u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_b16_8 t7 = gm_l2;
    gm_l4 = t7;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t8 = gm_l5;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                void * t11 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t13 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t11) + (uintptr_t)t13 * 136), 136);
                        gm_trqfvmjtt4sij t14;
                        gm_f_list_99597qp((void *)&t14, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l7 = t14;
                        void * t15 = (void *)((uint8_t *)&F.l7);
                        gm_tftb1odc739t5 t16 = gm_f_push_h9qsb4d(t15, &(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l6)));
                        gm_l8 = t16;
                        uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                        if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_src8lmgjh8bsvgp, 206, 9);
                        void * t18 = gm_l3;
                        gm_t88rcucg1b7uk t19 = gm_l0;
                        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                        gm_taevtln836oa8 t21 = GM_LD(gm_taevtln836oa8, ((uint8_t *)&F.l1));
                        gm_tbq1t03a53gpo t22;
                        GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t22 + 0), t19);
                        GM_ST(gm_taevtln836oa8, ((uint8_t *)&t22 + 8), t21);
                        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 112), t20);
                        gm_t8oufsr65o5ha t23;
                        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t23 + 8), t22);
                        gm_tftb1odc739t5 t24 = gm_f_push_h9qsb4d(t18, &t23);
                        gm_l9 = t24;
                        uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_src8lmgjh8bsvgp, 207, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 204, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t26 = gm_l5;
            uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
            uint64_t t29 = r27;
            gm_l5 = t29;
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
