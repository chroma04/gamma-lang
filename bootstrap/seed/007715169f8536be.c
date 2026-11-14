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

#if defined(GM_RT_PROGRAM) && defined(GM_RT_SEPARATE) && !defined(_WIN32)
/* The generated code of a program that is linked with the separately compiled runtime
 * needs only the declarations below, which use nothing from the operating system's headers;
 * parsing those would take the C compiler about as long as a small program. (Windows
 * builds include them all.) */
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
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; } gm_t6ok5tvcjc200;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_t5v0c8ia6a5mo f0; uint8_t f1; uint8_t pe[7]; } gm_tj8m6gmlgjilo;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_th2lqbo77pvsc f3; } gm_tl5cuhjfnc4cr;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t6ok5tvcjc200) == 40 && _Alignof(gm_t6ok5tvcjc200) == 8 && offsetof(gm_t6ok5tvcjc200, f0) == 0 && offsetof(gm_t6ok5tvcjc200, f1) == 16, "layout of `Key`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_t6ok5tvcjc200 f1; } gm_t70bah4iduq3d;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tj8m6gmlgjilo) == 64 && _Alignof(gm_tj8m6gmlgjilo) == 8 && offsetof(gm_tj8m6gmlgjilo, f0) == 0 && offsetof(gm_tj8m6gmlgjilo, f1) == 56, "layout of `?ParamInfo`");
_Static_assert(sizeof(gm_tl5cuhjfnc4cr) == 64 && _Alignof(gm_tl5cuhjfnc4cr) == 8 && offsetof(gm_tl5cuhjfnc4cr, f0) == 0 && offsetof(gm_tl5cuhjfnc4cr, f1) == 16 && offsetof(gm_tl5cuhjfnc4cr, f2) == 24 && offsetof(gm_tl5cuhjfnc4cr, f3) == 40, "layout of `Item`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t70bah4iduq3d) == 48 && _Alignof(gm_t70bah4iduq3d) == 8 && offsetof(gm_t70bah4iduq3d, f0) == 0 && offsetof(gm_t70bah4iduq3d, f1) == 8, "layout of `(kind: Kind, hover: Hover)`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t2gtbh39ptekd f0; uint8_t f1; uint8_t pe[7]; } gm_t89nb7ept390i;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t89nb7ept390i) == 72 && _Alignof(gm_t89nb7ept390i) == 8 && offsetof(gm_t89nb7ept390i, f0) == 0 && offsetof(gm_t89nb7ept390i, f1) == 64, "layout of `?ExprKind`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
typedef struct { gm_t6qooom8qa2do f0; uint8_t f1; uint8_t pe[7]; } gm_tlabdfckddbfd;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_tlabdfckddbfd) == 216 && _Alignof(gm_tlabdfckddbfd) == 8 && offsetof(gm_tlabdfckddbfd, f0) == 0 && offsetof(gm_tlabdfckddbfd, f1) == 208, "layout of `?DeclValue`");
struct gm_fr_adt_decl_of_2bflsov {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_builtin_methods_st9oapf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_decl_done_mk70ol9 {
    gm_frame h;
    gm_t6qooom8qa2do l2; /* v */
    gm_tlabdfckddbfd ret;
};
struct gm_fr_decl_info_ahk1bni {
    gm_frame h;
    gm_t89nb7ept390i l6; /* init_kind */
    gm_tv2uf4leil5hq l7; /* b */
    gm_tlabdfckddbfd l10; /* value */
    gm_t6qooom8qa2do l12; /* v */
    gm_t2gtbh39ptekd l16; /* k */
    gm_t70bah4iduq3d ret;
};
struct gm_fr_decl_item_a57s8if {
    gm_frame h;
    gm_t70bah4iduq3d l3; /* info */
    gm_t6qooom8qa2do l5; /* v */
    gm_tl5cuhjfnc4cr ret;
};
struct gm_fr_decl_visible_bd168ti {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lpi80k4_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_field_items_9bsf0mi {
    gm_frame h;
    gm_tpautr4br7h3l l6; /* def */
    gm_t0mglksrkfa5e l12; /* f */
    gm_b0_1 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_first_line_jto2r5e {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* lines */
    gm_b16_8 ret;
};
struct gm_fr_fn_6gkpg0v_2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_signature_urdu16i {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* params */
    gm_t5v0c8ia6a5mo l8; /* p */
    gm_trqfvmjtt4sij l13; /* out */
    gm_b16_8 ret;
};
struct gm_fr_is_core_file_4u6phfn {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_item_text_bv1q6fm {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* lines */
    gm_trqfvmjtt4sij l4; /* out */
    gm_b16_8 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_lines_of_ai9428u {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
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
struct gm_fr_list_f0gjjbh_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_module_members_26b4s91 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* ds */
    gm_trqfvmjtt4sij l7; /* items */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_module_scope_items_9t7jui3 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* ds */
    gm_trqfvmjtt4sij l6; /* items */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_op_info_4a09t36 {
    gm_frame h;
    gm_tv2uf4leil5hq l5; /* b */
    gm_t6qooom8qa2do l9; /* v */
    gm_trqfvmjtt4sij l13; /* ps */
    gm_t5v0c8ia6a5mo l16; /* p */
    gm_t70bah4iduq3d ret;
};
struct gm_fr_op_item_8dfr8db {
    gm_frame h;
    gm_t70bah4iduq3d l3; /* info */
    gm_tv2uf4leil5hq l6; /* b */
    gm_tl5cuhjfnc4cr ret;
};
struct gm_fr_operation_members_62l824c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_t6qooom8qa2do l3; /* v */
    gm_tv0k2vg3054a6 l8; /* op */
    gm_trqfvmjtt4sij l9; /* ps */
    gm_t5v0c8ia6a5mo l12; /* p */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_param_kind_a0aikmb {
    gm_frame h;
    gm_tnnjeerolos0a ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_1e500nc_3 {
    gm_frame h;
    gm_tl5cuhjfnc4cr l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
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
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o86m6d7_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_effects_vieja9u {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_sort_o401rjn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_src_fjcc76p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_universe_hidden_gsk2b8r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_universe_items_235nbu8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* items */
    gm_trqfvmjtt4sij l3; /* ds */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_value_members_03rtuq5 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_trqfvmjtt4sij l6; /* ds */
    gm_t6qooom8qa2do l13; /* v */
    gm_tj8m6gmlgjilo l16; /* first */
    gm_t5v0c8ia6a5mo l19; /* p */
    gm_t5v0c8ia6a5mo l20; /* p */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_variant_members_nesi2hv {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_tckdobunaulee l8; /* v */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_visible_ll07dr9 {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_adt_decl_of_2bflsov(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_builtin_methods_st9oapf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) void gm_f_decl_done_mk70ol9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_decl_info_ahk1bni(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_decl_item_a57s8if(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_effects_mg3vf2a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_3(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_field_items_9bsf0mi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, uint32_t a3);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_first_line_jto2r5e(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v_2(void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_signature_urdu16i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_format_0555s29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_22ud8a7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_279asi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_407qqln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9ndqnou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hb6cr3p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vfnel93(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_introsort_6k4m7ds(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_is_core_file_4u6phfn(void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_item_text_bv1q6fm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_lines_of_ai9428u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_module_members_26b4s91(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_module_scope_items_9t7jui3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_op_info_4a09t36(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_op_item_8dfr8db(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_operation_members_62l824c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_a0aikmb(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc_3(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) uint8_t gm_f_same_package_vmps24r(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_effects_vieja9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_sort_o401rjn(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_fjcc76p(void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_universe_hidden_gsk2b8r(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_universe_items_235nbu8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_value_members_03rtuq5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_variant_members_nesi2hv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_aisrlpj(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_clopc8o(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hnivvga(void * a0, gm_tnpifv20irnph a1);
extern uint8_t gm_c4tbnbi68ao[320] __attribute__((aligned(8)));
extern uint8_t gm_c8g6p8jp14b[96] __attribute__((aligned(8)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cfiqcgtjhen[416] __attribute__((aligned(8)));
extern uint8_t gm_cjfp4khah06[80] __attribute__((aligned(8)));
extern uint8_t gm_cjts5fiuldu[192] __attribute__((aligned(8)));
extern uint8_t gm_cka4oohkshv[512] __attribute__((aligned(8)));
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s2nf76g2su2k3[15];
extern const uint8_t gm_s3s4n6ehcrs2b[17];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s66uvortgrtsg[4];
extern const uint8_t gm_sb88rajuutaep[4];
extern const uint8_t gm_ses7tdrdt8a6u[6];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sk8fkllqjst4j[13];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srckg5f2c0p31ur[];
extern const char gm_srcm0juv8ecdls4[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_st2a9sgu5aria[11];
extern const uint8_t gm_st3p5nb1e6cc8[5];
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
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_visible_bd168ti F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint64_t gm_l2; /* from */
    void * gm_l3; /* decl */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_src6lm8esmv16p1, 176, 14);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t3 * 216);
        gm_l3 = t7;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l3;
        gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t9) + 40) + 56));
        void * t11 = gm_l3;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
        uint64_t t13 = gm_l2;
        uint8_t t14 = gm_f_visible_ll07dr9(t8, t10, t12, t13);
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5nn7ph0 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        if (!(GM_LD(void *, ((uint8_t *)&t2)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t3 = GM_LD(void *, ((uint8_t *)&t2));
        gm_l0 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = ((uint8_t *)t4);
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        uint8_t t9 = 0;
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        if (!(GM_LD(void *, ((uint8_t *)&t2)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t3 = GM_LD(void *, ((uint8_t *)&t2));
        gm_l0 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = ((uint8_t *)t4);
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        uint8_t t9 = 0;
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ijtp9lo F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)1ULL));
        if (!(GM_LD(void *, ((uint8_t *)&t2)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t3 = GM_LD(void *, ((uint8_t *)&t2));
        gm_l0 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = ((uint8_t *)t4);
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        uint8_t t9 = 0;
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lpi80k4_3 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        if (!(GM_LD(void *, ((uint8_t *)&t2)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t3 = GM_LD(void *, ((uint8_t *)&t2));
        gm_l0 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = ((uint8_t *)t4);
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 64)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        uint8_t t9 = 0;
        F.ret = t8;
        goto gm_fret;
    }
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
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_3cp9o8d(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 1);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
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
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcv1nri2247d2r, 82, 75);
    void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 80);
    F.ret = t6;
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
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, ((uint8_t *)t1));
    void * t3 = gm_l1;
    uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)t3));
    uint8_t t5 = (t2 < t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_n83ttnb F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
        gm_l2 = t7;
        {
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t13 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix2:;
    }
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
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 151, 54);
    memmove(gm_out, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 56), 56);
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
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_73v4ttb();
    gm_node *pn2 = gm_find(cx, 587034433);
    if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t3;
    t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16, ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24, t3);
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
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_5nn7ph0();
    gm_node *pn2 = gm_find(cx, 587034433);
    if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t3;
    t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16, ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_f0gjjbh_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lpi80k4_3();
    gm_node *pn2 = gm_find(cx, 587034433);
    if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t3;
    t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16, ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24, t3);
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
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ijtp9lo();
    gm_node *pn2 = gm_find(cx, 587034433);
    if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t3;
    t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16, ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24, t3);
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_59juqoo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t1 = gm_l1;
    uint64_t t2 = (uintptr_t)t1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 200) + 0) + 8);
    if (t2 >= (uintptr_t)(t4)) gm_trap_index(t2, t4, gm_srcc2qutubarto1, 547, 78);
    void * t5 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t3) + 200) + 0)) + t2 * 184) + 56);
    gm_b16_8 t6 = gm_l2;
    gm_tf5srh4mp4b3i t7 = gm_f_get_espvf96(t5, t6);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_a0aikmb(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_kind_a0aikmb F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 1128) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srckg5f2c0p31ur, 249, 62);
    gm_tnnjeerolos0a t6 = GM_LD(gm_tnnjeerolos0a, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 1128) + 0)) + t3 * 48) + 0));
    F.ret = t6;
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
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    GM_ST(void *, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 0, t1);
    void * t2 = gm_l1;
    GM_ST(void *, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 8, t2);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 16, ((uint32_t)0ULL));
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 20, ((uint32_t)0ULL));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc_3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1e500nc_3 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_lpi80k4_3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_o86m6d7_3(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tl5cuhjfnc4cr t21 = GM_LD(gm_tl5cuhjfnc4cr, ((uint8_t *)a1));
        GM_ST(gm_tl5cuhjfnc4cr, ((uint8_t *)t20 + t18 * 64), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 64)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
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
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_0jmnnpk(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_b16_8 t21 = gm_l1;
        GM_ST(gm_b16_8, ((uint8_t *)t20 + t18 * 16), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
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
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_73v4ttb();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_k4j2gj3(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        uint64_t t21 = gm_l1;
        GM_ST(uint64_t, ((uint8_t *)t20 + t18 * 8), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
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
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
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
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)1ULL), ((uint64_t)1ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
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
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o86m6d7_3 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)64ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
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
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t4;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_effects_vieja9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_effects_vieja9u F;
    void * gm_l0; /* t */
    void * gm_l1; /* set */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_uvfu1k1((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_f_printer_h7jigd7((void *)&gm_l3, t2, t3);
    }
    {
        void * t4 = (void *)((uint8_t *)&gm_l3);
        void * t5 = gm_l1;
        gm_b0_1 t6 = gm_f_effects_mg3vf2a((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        uint8_t t9 = 0;
        F.ret = t8;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_kmjeh8s F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_uvfu1k1((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_f_printer_h7jigd7((void *)&gm_l3, t2, t3);
    }
    {
        void * t4 = (void *)((uint8_t *)&gm_l3);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_b0_1 t6 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        uint8_t t9 = 0;
        F.ret = t8;
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
inline __attribute__((aligned(4))) void gm_f_sort_o401rjn(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_o401rjn F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = ((gm_closure){GM_PTR_TAG(gm_f_fn_6gkpg0v_2), NULL, cx});
        gm_f_introsort_6k4m7ds(t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_6vvt01e F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t3 > t4);
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t12 = (uintptr_t)t11;
        if (t10 > t12 || t12 > (uintptr_t)t8) gm_trap_slice(t10, t12, t8, gm_srcc24pnihhgkug, 416, 12);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t9 + t10 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12 - t10));
        gm_b16_8 t14 = gm_l1;
        uint8_t t15 = (gm_eq0iqiubfm7e(((uint8_t *)&t13), ((uint8_t *)&t14)));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
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
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 176) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 276, 56);
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 176) + 0)) + t3 * 4));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_f_trim_start_179crak(t1);
    gm_b16_8 t3 = gm_f_trim_end_6sjv6rg(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_universe_hidden_gsk2b8r(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_universe_hidden_gsk2b8r F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1 = gm_l1;
    uint64_t t2 = (uintptr_t)t1;
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 240) + 0) + 8);
    if (t2 >= (uintptr_t)(t5)) gm_trap_index(t2, t5, gm_src6lm8esmv16p1, 67, 64);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 240) + 0)) + t2 * 216) + 0) + 0));
    void * t7 = ((uint8_t *)gm_cfiqcgtjhen);
    if ((uintptr_t)0 > (uintptr_t)26 || (uintptr_t)26 > (uintptr_t)26) gm_trap_slice((uintptr_t)0, (uintptr_t)26, 26, gm_src6lm8esmv16p1, 67, 96);
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t7 + (uintptr_t)0 * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)((uintptr_t)26 - (uintptr_t)0));
    uint8_t t9 = 0;
    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t8) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t8))) + i * 16, ((uint8_t *)&t6))) { t9 = 1; break; }
    uint8_t t10 = (uint8_t)(t9);
    F.ret = t10;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visible_ll07dr9 F;
    void * gm_l0; /* s */
    gm_t9qkh0rsooc8d gm_l1; /* vis */
    uint64_t gm_l2; /* owner */
    uint64_t gm_l3; /* from */
    GM_STACK_CHECK_SHALLOW(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma3; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t9 != ((uint8_t)2ULL)) goto mn8;
    t1 = ((uint8_t)1);
    goto me2;
mn8:;
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t11 != ((uint8_t)1ULL)) goto mn10;
    void * t12 = gm_l0;
    uint64_t t13 = gm_l2;
    uint64_t t14 = gm_l3;
    uint8_t t15 = gm_f_same_package_vmps24r(t12, t13, t14);
    t1 = t15;
    goto me2;
mn10:;
ma5:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t17 != ((uint8_t)0ULL)) goto mn16;
    uint64_t t18 = gm_l2;
    uint64_t t19 = gm_l3;
    uint8_t t20 = ((t18 == t19));
    t1 = t20;
    goto me2;
mn16:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 168, 103);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_adt_decl_of_2bflsov(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_decl_of_2bflsov F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty_in */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_t88rcucg1b7uk gm_l3; /* pointee */
    uint32_t gm_l4; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 332u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        gm_t88rcucg1b7uk t2 = gm_l1;
        gm_l2 = t2;
    }
    {
        for (;;) {
            void * t4 = gm_l0;
            void * t5 = (void *)(((uint8_t *)t4) + 8);
            gm_t88rcucg1b7uk t6 = gm_l2;
            gm_t53at80pfdus3 t7;
            gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
            if (t8 != ((uint8_t)8ULL)) goto lb3;
            gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
            gm_l3 = t9;
            {
                gm_t88rcucg1b7uk t12 = gm_l3;
                gm_l2 = t12;
            }
lc3:;
        }
lb3:;
    }
    {
        void * t14 = gm_l0;
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_l2;
        gm_t53at80pfdus3 t17;
        gm_f_kind_jcnt4hm((void *)&t17, t15, GM_CONV(gm_csguankko4lmhd, t16));
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
        if (t18 != ((uint8_t)17ULL)) goto ie13;
        uint32_t t19 = GM_LD(uint32_t, ((((uint8_t *)&t17) + 8) + 0));
        gm_l4 = t19;
        {
            void * t21 = gm_l0;
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((((uint8_t *)t21) + 1088) + 0));
            gm_b16_8 t23 = t22;
            void * t24 = gm_l0;
            void * t25 = (void *)(((uint8_t *)t24) + 8);
            uint32_t t26 = gm_l4;
            uint32_t t27 = gm_f_template_9o92cvv(t25, t26);
            uint64_t t28 = (uint64_t)t27;
            uint64_t t29 = (uintptr_t)t28;
            uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&t23) + 8);
            if (t29 >= (uintptr_t)(t30)) gm_trap_index(t29, t30, gm_srcm0juv8ecdls4, 362, 12);
            gm_tf5srh4mp4b3i t31 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t23)) + t29 * 184) + 168));
            uint8_t t32 = 0;
            F.ret = t31;
            goto gm_fret;
        }
        goto ix13;
ie13:;
        {
            gm_tf5srh4mp4b3i t34;
            memset(&t34, 0, sizeof t34);
            uint8_t t35 = 0;
            F.ret = t34;
            goto gm_fret;
        }
ix13:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_builtin_methods_st9oapf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_builtin_methods_st9oapf F;
    gm_b16_8 gm_l0; /* names */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* n */
    GM_STACK_CHECK(&F, 284u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 16));
                    gm_l4 = t15;
                    {
                        void * t17 = gm_l1;
                        gm_tl5cuhjfnc4cr t18;
                        memset((uint8_t *)&t18 + 17, 0, 7);
                        gm_b16_8 t19 = gm_l4;
                        GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t19);
                        gm_t9qkh0rsooc8d t20;
                        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t18 + 16, t20);
                        gm_b16_8 t21;
                        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s2nf76g2su2k3));
                        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(14));
                        GM_ST(gm_b16_8, (uint8_t *)&t18 + 24, t21);
                        gm_th2lqbo77pvsc t22;
                        memset(&t22, 0, sizeof t22);
                        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t18 + 40, t22);
                        gm_tftb1odc739t5 t23 = gm_f_push_1e500nc_3(t17, &t18);
                        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srcm0juv8ecdls4, 269, 22);
                        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 269, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t26 = gm_l3;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l3 = t29;
                }
            }
lb4:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_decl_done_mk70ol9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_decl_done_mk70ol9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK(&F, 474u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint64_t t4 = gm_l1;
        gm_tagrkbqh054h7 t5;
        gm_f_state_a5bnsjb((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)2ULL)) goto ie2;
        F.l2 = (*(gm_t6qooom8qa2do *)(((uint8_t *)&t5) + 8));
        {
            const uint8_t *t8 = ((uint8_t *)&F.l2);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tlabdfckddbfd *)gm_out) + 209, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tlabdfckddbfd *)gm_out) + 210, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tlabdfckddbfd *)gm_out) + 212, 0);
            ((uint8_t *)&(*(gm_tlabdfckddbfd *)gm_out))[208] = 1;
            memmove((uint8_t *)&(*(gm_tlabdfckddbfd *)gm_out) + 0, t8, 208);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_tlabdfckddbfd t9;
        memset(&t9, 0, sizeof t9);
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
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
__attribute__((aligned(4))) void gm_f_decl_info_ahk1bni(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_decl_info_ahk1bni F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    void * gm_l2; /* decl */
    gm_b16_8 gm_l3; /* name */
    gm_th2lqbo77pvsc gm_l4; /* doc */
    void * gm_l5; /* item */
    gm_t9qkh0rsooc8d gm_l8; /* kind */
    gm_b16_8 gm_l9; /* code */
    uint8_t gm_l11; /* handled */
    uint32_t gm_l13; /* f */
    gm_t88rcucg1b7uk gm_l14; /* ty */
    gm_t88rcucg1b7uk gm_l15; /* ty */
    GM_STACK_CHECK(&F, 1385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srcm0juv8ecdls4, 130, 14);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t3 * 216);
        gm_l2 = t7;
    }
    {
        void * t8 = gm_l2;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 0) + 0));
        gm_l3 = t9;
    }
    {
        void * t10 = gm_l2;
        gm_th2lqbo77pvsc t11 = GM_LD(gm_th2lqbo77pvsc, ((((uint8_t *)t10) + 40) + 16));
        gm_l4 = t11;
    }
    {
        void * t12 = gm_l2;
        void * t13 = (void *)(((uint8_t *)t12) + 40);
        gm_l5 = t13;
    }
    {
        gm_t89nb7ept390i t14;
        memset(&t14, 0, sizeof t14);
        F.l6 = t14;
    }
    {
        void * t16 = gm_l5;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 64) + 0));
        if (t17 != ((uint8_t)0ULL)) goto ie15;
        gm_tv2uf4leil5hq t18 = GM_LD(gm_tv2uf4leil5hq, ((((uint8_t *)t16) + 64) + 8));
        F.l7 = t18;
        {
            void * t20 = GM_LD(void *, (((uint8_t *)&F.l7) + 40));
            const uint8_t *t21 = (((uint8_t *)t20) + 8);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t89nb7ept390i *)((uint8_t *)&F.l6)) + 65, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t89nb7ept390i *)((uint8_t *)&F.l6)) + 66, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t89nb7ept390i *)((uint8_t *)&F.l6)) + 68, 0);
            ((uint8_t *)&(*(gm_t89nb7ept390i *)((uint8_t *)&F.l6)))[64] = 1;
            memmove((uint8_t *)&(*(gm_t89nb7ept390i *)((uint8_t *)&F.l6)) + 0, t21, 64);
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        gm_t9qkh0rsooc8d t22;
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)7ULL));
        gm_l8 = t22;
    }
    {
        gm_b16_8 t23 = gm_l3;
        gm_l9 = t23;
    }
    {
        void * t24 = gm_l0;
        uint64_t t25 = gm_l1;
        gm_f_decl_done_mk70ol9((void *)&F.l10, (void *)NULL, cx, NULL, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l11 = ((uint8_t)1);
    }
    {
        if (!((((uint8_t *)&F.l10))[208] != 0)) goto ie26;
        gm_t6qooom8qa2do t27 = GM_LD(gm_t6qooom8qa2do, ((uint8_t *)&F.l10));
        F.l12 = t27;
        {
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
            switch (t39) { case ((uint8_t)0ULL): goto ma36; case ((uint8_t)1ULL): goto ma36; case ((uint8_t)2ULL): goto ma34; case ((uint8_t)3ULL): goto ma37; case ((uint8_t)4ULL): goto ma37; case ((uint8_t)5ULL): goto ma30; case ((uint8_t)6ULL): goto ma37; case ((uint8_t)7ULL): goto ma31; case ((uint8_t)8ULL): goto ma35; case ((uint8_t)9ULL): goto ma33; case ((uint8_t)10ULL): goto ma32; case ((uint8_t)11ULL): goto ma36; default: goto ma37; }
ma30:;
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
            if (t41 != ((uint8_t)5ULL)) goto mn40;
            uint32_t t42 = GM_LD(uint32_t, (((uint8_t *)&F.l12) + 8));
            gm_l13 = t42;
            {
                gm_t9qkh0rsooc8d t44;
                GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)1ULL));
                gm_l8 = t44;
            }
            {
                gm_t6j6smj7qoctu t45;
                gm_b16_8 t46 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t45 + 0, t46);
                void * t47 = gm_l0;
                uint32_t t48 = gm_l13;
                gm_b16_8 t49 = gm_f_fn_signature_urdu16i((void *)NULL, cx, NULL, t47, t48, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t45 + 16, t49);
                gm_tabb710d9mq2e t50;
                gm_f_format_vfnel93((void *)&t50, (void *)NULL, cx, NULL, &t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_srcm0juv8ecdls4, 144, 24);
                gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
                gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                gm_l9 = t52;
            }
            goto me29;
mn40:;
ma31:;
            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
            if (t54 != ((uint8_t)7ULL)) goto mn53;
            gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 8));
            gm_l14 = t55;
            {
                gm_t9qkh0rsooc8d t57;
                GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)7ULL));
                gm_l8 = t57;
            }
            {
                gm_t6j6smj7qoctu t58;
                gm_b16_8 t59 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t58 + 0, t59);
                void * t60 = gm_l0;
                gm_t88rcucg1b7uk t61 = gm_l14;
                gm_b16_8 t62 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t60, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t58 + 16, t62);
                gm_tabb710d9mq2e t63;
                gm_f_format_f6rro2t((void *)&t63, (void *)NULL, cx, NULL, &t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srcm0juv8ecdls4, 148, 24);
                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
                gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                gm_l9 = t65;
            }
            goto me29;
mn53:;
ma32:;
            uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
            if (t67 != ((uint8_t)10ULL)) goto mn66;
            gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 8));
            gm_l15 = t68;
            {
                gm_t9qkh0rsooc8d t70;
                GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)8ULL));
                gm_l8 = t70;
            }
            {
                gm_t6j6smj7qoctu t71;
                gm_b16_8 t72 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t71 + 0, t72);
                void * t73 = gm_l0;
                gm_t88rcucg1b7uk t74 = gm_l15;
                gm_b16_8 t75 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t73, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t71 + 16, t75);
                gm_tabb710d9mq2e t76;
                gm_f_format_hb6cr3p((void *)&t76, (void *)NULL, cx, NULL, &t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 8, gm_srcm0juv8ecdls4, 152, 24);
                gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 8));
                gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                gm_l9 = t78;
            }
            goto me29;
mn66:;
ma33:;
            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
            if (t80 != ((uint8_t)9ULL)) goto mn79;
            {
                gm_t9qkh0rsooc8d t82;
                GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)0ULL));
                gm_l8 = t82;
            }
            {
                void * t83 = gm_l0;
                void * t84 = gm_l5;
                gm_tl874t2atnlh3 t85 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t84) + 4));
                gm_b16_8 t86 = gm_f_first_line_jto2r5e((void *)NULL, cx, NULL, t83, t85);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t86;
            }
            goto me29;
mn79:;
ma34:;
            uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
            if (t88 != ((uint8_t)2ULL)) goto mn87;
            {
                gm_t9qkh0rsooc8d t90;
                GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)5ULL));
                gm_l8 = t90;
            }
            {
                void * t91 = gm_l0;
                void * t92 = gm_l5;
                gm_tl874t2atnlh3 t93 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t92) + 4));
                gm_b16_8 t94 = gm_f_item_text_bv1q6fm((void *)NULL, cx, NULL, t91, t93);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t94;
            }
            goto me29;
mn87:;
ma35:;
            uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
            if (t96 != ((uint8_t)8ULL)) goto mn95;
            {
                gm_t9qkh0rsooc8d t98;
                GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)7ULL));
                gm_l8 = t98;
            }
            {
                void * t99 = gm_l0;
                void * t100 = gm_l5;
                gm_tl874t2atnlh3 t101 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t100) + 4));
                gm_b16_8 t102 = gm_f_first_line_jto2r5e((void *)NULL, cx, NULL, t99, t101);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t102;
            }
            goto me29;
mn95:;
ma36:;
            uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
            if (t106 != ((uint8_t)0ULL)) goto pa105;
            goto po104;
pa105:;
            uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
            if (t108 != ((uint8_t)1ULL)) goto pa107;
            goto po104;
pa107:;
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
            if (t110 != ((uint8_t)11ULL)) goto pa109;
            goto po104;
pa109:;
            goto mn103;
po104:;
            {
                gm_l11 = ((uint8_t)0);
            }
            goto me29;
mn103:;
ma37:;
            goto me29;
mn112:;
ma38:;
            gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 141, 9);
me29:;
        }
        goto ix26;
ie26:;
        {
            gm_l11 = ((uint8_t)0);
        }
ix26:;
    }
    {
        uint8_t t116 = gm_l11;
        uint8_t t117 = (uint8_t)!t116;
        if (!t117) goto ie115;
        {
            gm_t9qkh0rsooc8d t119;
            GM_ST(uint8_t, ((uint8_t *)&t119 + 0), ((uint8_t)14ULL));
            gm_l8 = t119;
        }
        {
            if (!((((uint8_t *)&F.l6))[64] != 0)) goto ie120;
            gm_t2gtbh39ptekd t121 = GM_LD(gm_t2gtbh39ptekd, ((uint8_t *)&F.l6));
            F.l16 = t121;
            {
                gm_t9qkh0rsooc8d t123;
                uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                switch (t132) { case ((uint8_t)0ULL): goto ma130; case ((uint8_t)1ULL): goto ma130; case ((uint8_t)2ULL): goto ma130; case ((uint8_t)3ULL): goto ma130; case ((uint8_t)4ULL): goto ma130; case ((uint8_t)5ULL): goto ma130; case ((uint8_t)6ULL): goto ma130; case ((uint8_t)7ULL): goto ma130; case ((uint8_t)8ULL): goto ma130; case ((uint8_t)9ULL): goto ma130; case ((uint8_t)10ULL): goto ma130; case ((uint8_t)11ULL): goto ma130; case ((uint8_t)12ULL): goto ma130; case ((uint8_t)13ULL): goto ma130; case ((uint8_t)14ULL): goto ma130; case ((uint8_t)15ULL): goto ma130; case ((uint8_t)16ULL): goto ma130; case ((uint8_t)17ULL): goto ma130; case ((uint8_t)18ULL): goto ma130; case ((uint8_t)19ULL): goto ma130; case ((uint8_t)20ULL): goto ma130; case ((uint8_t)21ULL): goto ma130; case ((uint8_t)22ULL): goto ma130; case ((uint8_t)23ULL): goto ma130; case ((uint8_t)24ULL): goto ma130; case ((uint8_t)25ULL): goto ma130; case ((uint8_t)26ULL): goto ma130; case ((uint8_t)27ULL): goto ma130; case ((uint8_t)28ULL): goto ma130; case ((uint8_t)29ULL): goto ma130; case ((uint8_t)30ULL): goto ma130; case ((uint8_t)31ULL): goto ma130; case ((uint8_t)32ULL): goto ma125; case ((uint8_t)33ULL): goto ma126; case ((uint8_t)34ULL): goto ma127; case ((uint8_t)35ULL): goto ma130; case ((uint8_t)36ULL): goto ma129; case ((uint8_t)37ULL): goto ma130; case ((uint8_t)38ULL): goto ma128; case ((uint8_t)39ULL): goto ma128; case ((uint8_t)40ULL): goto ma130; case ((uint8_t)41ULL): goto ma130; case ((uint8_t)42ULL): goto ma130; case ((uint8_t)43ULL): goto ma130; case ((uint8_t)44ULL): goto ma130; case ((uint8_t)45ULL): goto ma130; case ((uint8_t)46ULL): goto ma130; case ((uint8_t)47ULL): goto ma130; case ((uint8_t)48ULL): goto ma130; case ((uint8_t)49ULL): goto ma130; case ((uint8_t)50ULL): goto ma130; default: goto ma130; }
ma125:;
                uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                if (t134 != ((uint8_t)32ULL)) goto mn133;
                gm_t9qkh0rsooc8d t135;
                GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)3ULL));
                t123 = t135;
                goto me124;
mn133:;
ma126:;
                uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                if (t137 != ((uint8_t)33ULL)) goto mn136;
                gm_t9qkh0rsooc8d t138;
                GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)4ULL));
                t123 = t138;
                goto me124;
mn136:;
ma127:;
                uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                if (t140 != ((uint8_t)34ULL)) goto mn139;
                gm_t9qkh0rsooc8d t141;
                GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)5ULL));
                t123 = t141;
                goto me124;
mn139:;
ma128:;
                uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                if (t145 != ((uint8_t)38ULL)) goto pa144;
                goto po143;
pa144:;
                uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                if (t147 != ((uint8_t)39ULL)) goto pa146;
                goto po143;
pa146:;
                goto mn142;
po143:;
                gm_t9qkh0rsooc8d t148;
                GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)0ULL));
                t123 = t148;
                goto me124;
mn142:;
ma129:;
                uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                if (t150 != ((uint8_t)36ULL)) goto mn149;
                gm_t9qkh0rsooc8d t151;
                GM_ST(uint8_t, ((uint8_t *)&t151 + 0), ((uint8_t)1ULL));
                t123 = t151;
                goto me124;
mn149:;
ma130:;
                gm_t9qkh0rsooc8d t153;
                GM_ST(uint8_t, ((uint8_t *)&t153 + 0), ((uint8_t)14ULL));
                t123 = t153;
                goto me124;
mn152:;
ma131:;
                gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 175, 20);
me124:;
                gm_l8 = t123;
            }
            goto ix120;
ie120:;
ix120:;
        }
        {
            gm_b16_8 t154;
            uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            if (t159 != ((uint8_t)3ULL)) goto pa158;
            goto po157;
pa158:;
            uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            if (t161 != ((uint8_t)4ULL)) goto pa160;
            goto po157;
pa160:;
            uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            if (t163 != ((uint8_t)5ULL)) goto pa162;
            goto po157;
pa162:;
            goto mn156;
po157:;
            void * t164 = gm_l0;
            void * t165 = gm_l5;
            gm_tl874t2atnlh3 t166 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t165) + 4));
            gm_b16_8 t167 = gm_f_item_text_bv1q6fm((void *)NULL, cx, NULL, t164, t166);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t154 = t167;
            goto me155;
mn156:;
            void * t169 = gm_l0;
            void * t170 = gm_l5;
            gm_tl874t2atnlh3 t171 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t170) + 4));
            gm_b16_8 t172 = gm_f_first_line_jto2r5e((void *)NULL, cx, NULL, t169, t171);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t154 = t172;
            goto me155;
mn168:;
            gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 184, 16);
me155:;
            gm_l9 = t154;
        }
        goto ix115;
ie115:;
ix115:;
    }
    {
        memset((uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 1, 0, 7);
        gm_t9qkh0rsooc8d t173 = gm_l8;
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 0, t173);
        gm_b16_8 t174 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6ok5tvcjc200 *)((uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 8)) + 0, t174);
        memcpy((uint8_t *)&(*(gm_t6ok5tvcjc200 *)((uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 8)) + 16, ((uint8_t *)&gm_l4), 24);
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
__attribute__((aligned(4))) void gm_f_decl_item_a57s8if(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_decl_item_a57s8if F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    void * gm_l2; /* decl */
    gm_b16_8 gm_l4; /* detail */
    uint32_t gm_l6; /* f */
    gm_t88rcucg1b7uk gm_l7; /* ty */
    gm_t88rcucg1b7uk gm_l8; /* ty */
    GM_STACK_CHECK(&F, 965u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srcm0juv8ecdls4, 194, 14);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t3 * 216);
        gm_l2 = t7;
    }
    {
        void * t8 = gm_l0;
        uint64_t t9 = gm_l1;
        gm_f_decl_info_ahk1bni((void *)&F.l3, (void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(0));
        gm_l4 = t10;
    }
    {
        void * t12 = gm_l0;
        uint64_t t13 = gm_l1;
        gm_tlabdfckddbfd t14;
        gm_f_decl_done_mk70ol9((void *)&t14, (void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t14))[208] != 0)) goto ie11;
        F.l5 = (*(gm_t6qooom8qa2do *)(((uint8_t *)&t14) + 0));
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
            switch (t21) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma19; case ((uint8_t)2ULL): goto ma19; case ((uint8_t)3ULL): goto ma19; case ((uint8_t)4ULL): goto ma19; case ((uint8_t)5ULL): goto ma17; case ((uint8_t)6ULL): goto ma19; case ((uint8_t)7ULL): goto ma18; case ((uint8_t)8ULL): goto ma19; case ((uint8_t)9ULL): goto ma19; case ((uint8_t)10ULL): goto ma18; case ((uint8_t)11ULL): goto ma19; default: goto ma19; }
ma17:;
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
            if (t23 != ((uint8_t)5ULL)) goto mn22;
            uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 8));
            gm_l6 = t24;
            {
                void * t26 = gm_l0;
                uint32_t t27 = gm_l6;
                gm_b16_8 t28 = gm_f_fn_signature_urdu16i((void *)NULL, cx, NULL, t26, t27, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t28;
            }
            goto me16;
mn22:;
ma18:;
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
            if (t32 != ((uint8_t)7ULL)) goto pa31;
            gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 8));
            gm_l7 = t33;
            goto po30;
pa31:;
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
            if (t35 != ((uint8_t)10ULL)) goto pa34;
            gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 8));
            gm_l7 = t36;
            goto po30;
pa34:;
            goto mn29;
po30:;
            {
                void * t38 = gm_l0;
                gm_t88rcucg1b7uk t39 = gm_l7;
                gm_b16_8 t40 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t38, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t40;
            }
            goto me16;
mn29:;
ma19:;
            {
                gm_b16_8 t43 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 0));
                gm_trqfvmjtt4sij t44;
                gm_f_lines_of_ai9428u((void *)&t44, (void *)NULL, cx, NULL, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 0));
                void * t46 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t45)));
                uint64_t t47 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t45) + 8);
                uint64_t t48 = (uintptr_t)0;
                gm_th2lqbo77pvsc t49;
                if (t48 < t47) {
                gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)t46 + t48 * 16));
                gm_th2lqbo77pvsc t51;
                GM_ST(uint8_t, (uint8_t *)&t51 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t51 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t51 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t51), t50);
                ((uint8_t *)&t51)[16] = 1;
                    t49 = t51;
                } else {
                gm_th2lqbo77pvsc t52;
                memset(&t52, 0, sizeof t52);
                    t49 = t52;
                }
                gm_b16_8 t53;
                if (((((uint8_t *)&t49))[16] != 0)) {
                    gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 0));
                    t53 = t54;
                } else {
                    gm_b16_8 t55;
                    GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(0));
                    t53 = t55;
                }
                gm_l4 = t53;
            }
            goto me16;
mn41:;
ma20:;
            gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 198, 9);
me16:;
        }
        goto ix11;
ie11:;
        {
            gm_b16_8 t57 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 0));
            gm_trqfvmjtt4sij t58;
            gm_f_lines_of_ai9428u((void *)&t58, (void *)NULL, cx, NULL, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 0));
            void * t60 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t59)));
            uint64_t t61 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t59) + 8);
            uint64_t t62 = (uintptr_t)0;
            gm_th2lqbo77pvsc t63;
            if (t62 < t61) {
            gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)t60 + t62 * 16));
            gm_th2lqbo77pvsc t65;
            GM_ST(uint8_t, (uint8_t *)&t65 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t65 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t65 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t65), t64);
            ((uint8_t *)&t65)[16] = 1;
                t63 = t65;
            } else {
            gm_th2lqbo77pvsc t66;
            memset(&t66, 0, sizeof t66);
                t63 = t66;
            }
            gm_b16_8 t67;
            if (((((uint8_t *)&t63))[16] != 0)) {
                gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 0));
                t67 = t68;
            } else {
                gm_b16_8 t69;
                GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(0));
                t67 = t69;
            }
            gm_l4 = t67;
        }
ix11:;
    }
    {
        memset((uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 17, 0, 7);
        void * t70 = gm_l2;
        gm_b16_8 t71 = GM_LD(gm_b16_8, ((((uint8_t *)t70) + 0) + 0));
        gm_t9qkh0rsooc8d t72 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t73 = gm_l4;
        gm_th2lqbo77pvsc t74 = GM_LD(gm_th2lqbo77pvsc, ((((uint8_t *)&F.l3) + 8) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 0, t71);
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 16, t72);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 24, t73);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 40, t74);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_field_items_9bsf0mi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, uint32_t a3) {
    struct gm_fr_field_items_9bsf0mi F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    void * gm_l2; /* items */
    uint32_t gm_l3; /* depth */
    uint32_t gm_l4; /* id */
    void * gm_l5; /* info */
    gm_b16_8 gm_l7; /* fs */
    gm_b16_8 gm_l8; /* fields */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    GM_STACK_CHECK(&F, 714u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l4 = t8;
        {
            uint32_t t10 = gm_l4;
            uint64_t t11 = (uint64_t)t10;
            uint64_t t12 = (uintptr_t)t11;
            void * t13 = gm_l0;
            uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 1088) + 0) + 8);
            if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srcm0juv8ecdls4, 316, 14);
            void * t15 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 1088) + 0)) + t12 * 184);
            gm_l5 = t15;
        }
        {
            void * t16 = gm_l5;
            gm_b16_8 t17;
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t16) + 24) + 0));
            switch (t23) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma20; default: goto ma22; }
ma19:;
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t16) + 24) + 0));
            if (t25 != ((uint8_t)0ULL)) goto mn24;
            gm_tpautr4br7h3l t26 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t16) + 24) + 8));
            F.l6 = t26;
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            t17 = t28;
            goto me18;
mn24:;
ma20:;
            uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t16) + 24) + 0));
            if (t30 != ((uint8_t)2ULL)) goto mn29;
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((((uint8_t *)t16) + 24) + 8));
            gm_l7 = t31;
            gm_b16_8 t32 = gm_l7;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
            t17 = t33;
            goto me18;
mn29:;
ma21:;
            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t16) + 24) + 0));
            if (t35 != ((uint8_t)1ULL)) goto mn34;
            gm_b0_1 t36;
            uint8_t t37 = 0;
            F.ret = t36;
            goto gm_fret;
            goto me18;
mn34:;
ma22:;
            gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 317, 37);
me18:;
            gm_l8 = t17;
        }
        {
            {
                gm_b16_8 t39 = gm_l8;
                gm_l9 = t39;
            }
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t41 = gm_l10;
                    uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t43 = (t41 < t42);
                    if (!t43) goto lb40;
                    {
                        gm_t8ta482bl6be0 t46;
                        uint64_t t47 = gm_l10;
                        GM_ST(uint64_t, (uint8_t *)&t46 + 0, t47);
                        uint64_t t48 = gm_l10;
                        uint64_t t49 = (uintptr_t)t48;
                        uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        memcpy((uint8_t *)&t46 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t49 * 40), 40);
                        uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&t46) + 0));
                        gm_l11 = t53;
                        F.l12 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t46) + 8));
                        {
                            void * t55 = gm_l2;
                            gm_tl5cuhjfnc4cr t56;
                            memset((uint8_t *)&t56 + 17, 0, 7);
                            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t56 + 0, t57);
                            gm_t9qkh0rsooc8d t58;
                            GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)10ULL));
                            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t56 + 16, t58);
                            void * t59 = gm_l0;
                            gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 16));
                            gm_b16_8 t61 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t59, t60);
                            if (gm_unw.kind) {
                                goto gm_ll44;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t56 + 24, t61);
                            gm_th2lqbo77pvsc t62;
                            memset(&t62, 0, sizeof t62);
                            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t56 + 40, t62);
                            gm_tftb1odc739t5 t63 = gm_f_push_1e500nc_3(t55, &t56);
                            if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srcm0juv8ecdls4, 323, 9);
                            gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
                        }
                        {
                            uint64_t t66 = gm_l11;
                            uint8_t t67 = ((t66 == ((uint64_t)0ULL)));
                            if (!t67) goto ie65;
                            uint32_t t68 = gm_l3;
                            uint8_t t69 = (t68 < ((uint32_t)4ULL));
                            if (!t69) goto ie65;
                            void * t70 = gm_l5;
                            uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)t70) + 112));
                            if (!t71) goto ie65;
                            {
                                void * t73 = gm_l0;
                                gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 16));
                                void * t75 = gm_l2;
                                uint32_t t76 = gm_l3;
                                uint32_t r77; bool o78 = __builtin_add_overflow(t76, ((uint32_t)1ULL), &r77);
                                if (o78) gm_trap("integer overflow in `+`", gm_srcm0juv8ecdls4, 324, 82);
                                uint32_t t79 = r77;
                                gm_b0_1 t80 = gm_f_field_items_9bsf0mi((void *)NULL, cx, NULL, t73, t74, t75, t79);
                                if (gm_unw.kind) {
                                    goto gm_ll44;
                                }
                            }
                            goto ix65;
ie65:;
ix65:;
                        }
                        goto me51;
mn52:;
                        gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 322, 5);
me51:;
                    }
lc40:;
                    {
                        uint64_t t82 = gm_l10;
                        uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                        uint64_t t85 = r83;
                        gm_l10 = t85;
                    }
                }
gm_ll44:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb40; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc40; }
                }
                goto gm_lad;
lb40:;
            }
        }
        goto ix2;
ie2:;
        {
            gm_b0_1 t87;
            uint8_t t88 = 0;
            F.ret = t87;
            goto gm_fret;
        }
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_first_line_jto2r5e(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_first_line_jto2r5e F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 281u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_b16_8 t4 = gm_f_src_fjcc76p(t2, GM_CONV(gm_cseahvg8v7p0vs, t3));
        gm_f_lines_of_ai9428u((void *)&F.l2, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        void * t6 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t5)));
        uint64_t t7 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t5) + 8);
        uint64_t t8 = (uintptr_t)0;
        gm_th2lqbo77pvsc t9;
        if (t8 < t7) {
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)t6 + t8 * 16));
        gm_th2lqbo77pvsc t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[16] = 1;
            t9 = t11;
        } else {
        gm_th2lqbo77pvsc t12;
        memset(&t12, 0, sizeof t12);
            t9 = t12;
        }
        gm_b16_8 t13;
        if (((((uint8_t *)&t9))[16] != 0)) {
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 0));
            t13 = t14;
        } else {
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(0));
            t13 = t15;
        }
        gm_b16_8 t16 = gm_f_trim_end_6sjv6rg(t13);
        uint8_t t17 = 0;
        F.ret = t16;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_signature_urdu16i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint8_t a2) {
    struct gm_fr_fn_signature_urdu16i F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    uint8_t gm_l2; /* skip_receiver */
    void * gm_l3; /* sig */
    uint8_t gm_l5; /* skipped */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l9; /* value_param */
    uint32_t gm_l10; /* g */
    gm_b16_8 gm_l11; /* text */
    void * gm_l12; /* d */
    GM_STACK_CHECK(&F, 1120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 344) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcm0juv8ecdls4, 60, 13);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 344) + 0)) + t4 * 136);
        gm_l3 = t7;
    }
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t8 = gm_l2;
        uint8_t t9 = (uint8_t)!t8;
        gm_l5 = t9;
    }
    {
        {
            void * t11 = gm_l3;
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)t11) + 40));
            gm_l6 = t12;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t14 = gm_l7;
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l7;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t5v0c8ia6a5mo t24 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t20 * 56));
                    F.l8 = t24;
                    {
                        uint8_t t27 = gm_l5;
                        uint8_t t28 = (uint8_t)!t27;
                        if (!t28) goto ie26;
                        uint8_t t29;
                        if ((((((uint8_t *)&F.l8) + 40))[4] != 0)) goto mn31;
                        t29 = ((uint8_t)1);
                        goto me30;
mn31:;
                        t29 = ((uint8_t)0);
                        goto me30;
mn32:;
                        gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 64, 28);
me30:;
                        if (!t29) goto ie26;
                        {
                            gm_l5 = ((uint8_t)1);
                        }
                        {
                            goto lc13;
                        }
                        goto ix26;
ie26:;
ix26:;
                    }
                    {
                        gm_l9 = ((uint8_t)0);
                    }
                    {
                        if (!(((((uint8_t *)&F.l8) + 40))[4] != 0)) goto ie34;
                        uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&F.l8) + 40));
                        gm_l10 = t35;
                        {
                            void * t37 = gm_l0;
                            uint32_t t38 = gm_l10;
                            gm_tnnjeerolos0a t39 = gm_f_param_kind_a0aikmb(t37, t38);
                            uint8_t t40;
                            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
                            if (t43 != ((uint8_t)2ULL)) goto mn42;
                            t40 = ((uint8_t)1);
                            goto me41;
mn42:;
                            t40 = ((uint8_t)0);
                            goto me41;
mn44:;
                            gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 69, 43);
me41:;
                            gm_l9 = t40;
                        }
                        goto ix34;
ie34:;
ix34:;
                    }
                    {
                        gm_b16_8 t45;
                        uint8_t t47 = gm_l9;
                        if (!t47) goto ie46;
                        gm_t6j6smj7qoctu t49;
                        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t49 + 0, t50);
                        void * t51 = gm_l0;
                        gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                        gm_b16_8 t53 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t51, t52);
                        if (gm_unw.kind) {
                            goto gm_ll17;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t49 + 16, t53);
                        gm_tabb710d9mq2e t54;
                        gm_f_format_vfnel93((void *)&t54, (void *)NULL, cx, NULL, &t49);
                        if (gm_unw.kind) {
                            goto gm_ll17;
                        }
                        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 8, gm_srcm0juv8ecdls4, 70, 34);
                        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
                        gm_b16_8 t56 = t55;
                        t45 = t56;
                        goto ix46;
ie46:;
                        gm_t6j6smj7qoctu t58;
                        gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t58 + 0, t59);
                        void * t60 = gm_l0;
                        gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                        gm_b16_8 t62 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t60, t61);
                        if (gm_unw.kind) {
                            goto gm_ll17;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t58 + 16, t62);
                        gm_tabb710d9mq2e t63;
                        gm_f_format_f6rro2t((void *)&t63, (void *)NULL, cx, NULL, &t58);
                        if (gm_unw.kind) {
                            goto gm_ll17;
                        }
                        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srcm0juv8ecdls4, 70, 89);
                        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
                        gm_b16_8 t65 = t64;
                        t45 = t65;
ix46:;
                        gm_l11 = t45;
                    }
                    {
                        void * t67 = GM_LD(void *, (((uint8_t *)&F.l8) + 32));
                        if (!(GM_LD(void *, ((uint8_t *)&t67)) != 0)) goto ie66;
                        void * t68 = GM_LD(void *, ((uint8_t *)&t67));
                        gm_l12 = t68;
                        {
                            gm_t6j6smj7qoctu t70;
                            gm_b16_8 t71 = gm_l11;
                            GM_ST(gm_b16_8, (uint8_t *)&t70 + 0, t71);
                            void * t72 = gm_l0;
                            void * t73 = gm_l12;
                            gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t73) + 72));
                            gm_b16_8 t75 = gm_f_src_fjcc76p(t72, GM_CONV(gm_cseahvg8v7p0vs, t74));
                            GM_ST(gm_b16_8, (uint8_t *)&t70 + 16, t75);
                            gm_tabb710d9mq2e t76;
                            gm_f_format_279asi0((void *)&t76, (void *)NULL, cx, NULL, &t70);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 8, gm_srcm0juv8ecdls4, 71, 36);
                            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 8));
                            gm_l11 = t77;
                        }
                        goto ix66;
ie66:;
ix66:;
                    }
                    {
                        void * t78 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t79 = gm_l11;
                        gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                        gm_tftb1odc739t5 t81 = gm_f_push_3ef746b(t78, t80);
                        if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_srcm0juv8ecdls4, 72, 9);
                        gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 63, 5);
me22:;
                }
lc13:;
                {
                    uint64_t t84 = gm_l7;
                    uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                    uint64_t t87 = r85;
                    gm_l7 = t87;
                }
            }
gm_ll17:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb13; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc13; }
            }
            goto gm_lad;
lb13:;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t88 = (void *)((uint8_t *)&F.l13);
        gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
        gm_b16_8 t91;
        GM_ST(void *, (uint8_t *)&t91, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t92;
        gm_f_join_sfknljb((void *)&t92, (void *)NULL, cx, NULL, t90, t91);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 8, gm_srcm0juv8ecdls4, 75, 25);
        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 8));
        gm_tnpifv20irnph t94;
        GM_ST(gm_b16_8, ((uint8_t *)&t94 + 0), t93);
        gm_tftb1odc739t5 t95 = gm_f_write_hnivvga(t88, t94);
        if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_srcm0juv8ecdls4, 75, 5);
        gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
    }
    {
        void * t98 = gm_l0;
        void * t99 = (void *)(((uint8_t *)t98) + 8);
        void * t100 = gm_l3;
        gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t100) + 56));
        uint8_t t102 = gm_f_is_unit_n83ttnb(t99, GM_CONV(gm_csguankko4lmhd, t101));
        uint8_t t103 = (uint8_t)!t102;
        if (!t103) goto ie97;
        {
            void * t105 = (void *)((uint8_t *)&F.l13);
            void * t106 = gm_l0;
            void * t107 = gm_l3;
            gm_t88rcucg1b7uk t108 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t107) + 56));
            gm_b16_8 t109 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t106, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t110;
            GM_ST(gm_b16_8, ((uint8_t *)&t110 + 0), t109);
            gm_tftb1odc739t5 t111 = gm_f_write_clopc8o(t105, t110);
            if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 1, gm_srcm0juv8ecdls4, 76, 37);
            gm_b0_1 t112 = GM_LD(gm_b0_1, (((uint8_t *)&t111) + 1));
        }
        goto ix97;
ie97:;
ix97:;
    }
    {
        void * t114 = gm_l3;
        uint64_t t115 = GM_LD(uintptr_t, ((((uint8_t *)t114) + 64) + 0) + 8);
        uint8_t t116 = (t115 > ((uint64_t)0ULL));
        if (!t116) goto ie113;
        {
            void * t118 = (void *)((uint8_t *)&F.l13);
            void * t119 = gm_l0;
            void * t120 = (void *)(((uint8_t *)t119) + 8);
            void * t121 = gm_l3;
            void * t122 = (void *)(((uint8_t *)t121) + 64);
            gm_b16_8 t123 = gm_f_show_effects_vieja9u((void *)NULL, cx, NULL, t120, t122);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t124;
            GM_ST(gm_b16_8, ((uint8_t *)&t124 + 0), t123);
            gm_tftb1odc739t5 t125 = gm_f_write_aisrlpj(t118, t124);
            if (((((uint8_t *)&t125))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t125) + 1, gm_srcm0juv8ecdls4, 77, 38);
            gm_b0_1 t126 = GM_LD(gm_b0_1, (((uint8_t *)&t125) + 1));
        }
        goto ix113;
ie113:;
ix113:;
    }
    {
        gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
        uint8_t t129 = 0;
        F.ret = t128;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_core_file_4u6phfn(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_core_file_4u6phfn F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    {
        uint8_t t3 = 0;
        gm_tl874t2atnlh3 t4 = gm_l1;
        gm_tl874t2atnlh3 t5 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t6 = (gm_eqff85ac10o3(((uint8_t *)&t4), ((uint8_t *)&t5)));
        if (!t6) {
            uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
            uint64_t t8 = (uint64_t)t7;
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
            uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 0) + 0) + 8);
            uint8_t t13 = (t8 >= t12);
            t3 = t13;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t15 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t16 = gm_l0;
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t20 = gm_f_file_o15fqku(t18, t19);
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)t20) + 0));
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s1f440st4mnkp));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(1));
        uint8_t t23 = gm_f_starts_with_6vvt01e(t21, t22);
        uint8_t t24 = 0;
        F.ret = t23;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_item_text_bv1q6fm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_item_text_bv1q6fm F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* text */
    GM_STACK_CHECK(&F, 367u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_b16_8 t4 = gm_f_src_fjcc76p(t2, GM_CONV(gm_cseahvg8v7p0vs, t3));
        gm_l2 = t4;
    }
    {
        gm_b16_8 t5 = gm_l2;
        gm_f_lines_of_ai9428u((void *)&F.l3, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
        uint8_t t8 = (t7 <= ((uint64_t)24ULL));
        if (!t8) goto ie6;
        {
            gm_b16_8 t10 = gm_l2;
            gm_b16_8 t11 = gm_f_trim_fdrfe6i(t10);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = (void *)((uint8_t *)&F.l4);
        uint64_t t14 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
        void * t15 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)));
        uint64_t t16 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t17 = (uintptr_t)((uint64_t)24ULL);
        if (t16 > t17 || t17 > (uintptr_t)t14) gm_trap_slice(t16, t17, t14, gm_srcm0juv8ecdls4, 87, 25);
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(((uint8_t *)t15 + t16 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(t17 - t16));
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
        gm_tabb710d9mq2e t21;
        gm_f_join_sfknljb((void *)&t21, (void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 8, gm_srcm0juv8ecdls4, 87, 16);
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
        gm_tftb1odc739t5 t24 = gm_f_extend_0cs85gs(t13, t23);
        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srcm0juv8ecdls4, 87, 5);
        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(gm_st2a9sgu5aria));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(10));
        gm_tftb1odc739t5 t28 = gm_f_extend_0cs85gs(t26, t27);
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srcm0juv8ecdls4, 88, 5);
        gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
    }
    {
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
        uint8_t t32 = 0;
        F.ret = t31;
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
__attribute__((aligned(4))) void gm_f_lines_of_ai9428u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_of_ai9428u F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l2; /* start */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* end */
    GM_STACK_CHECK(&F, 379u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_epvducg((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l3 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = gm_l3;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&t10));
                    gm_l5 = t13;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srcm0juv8ecdls4, 41, 12);
                        uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t17 * 1));
                        uint8_t t20 = ((t19 == ((uint8_t)10ULL)));
                        if (!t20) goto ie15;
                        {
                            uint64_t t22 = gm_l5;
                            gm_l6 = t22;
                        }
                        {
                            uint64_t t24 = gm_l6;
                            uint64_t t25 = gm_l2;
                            uint8_t t26 = (t24 > t25);
                            if (!t26) goto ie23;
                            uint64_t t27 = gm_l6;
                            uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                            if (o29) gm_trap("integer overflow in `-`", gm_srcm0juv8ecdls4, 43, 37);
                            uint64_t t30 = r28;
                            uint64_t t31 = (uintptr_t)t30;
                            uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t31 >= (uintptr_t)(t32)) gm_trap_index(t31, t32, gm_srcm0juv8ecdls4, 43, 32);
                            uint8_t t33 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t31 * 1));
                            uint8_t t34 = ((t33 == ((uint8_t)13ULL)));
                            if (!t34) goto ie23;
                            {
                                uint64_t t36 = gm_l6;
                                uint64_t r37; bool o38 = __builtin_sub_overflow(t36, ((uint64_t)1ULL), &r37);
                                if (o38) gm_trap("integer overflow in `-`", gm_srcm0juv8ecdls4, 43, 56);
                                uint64_t t39 = r37;
                                gm_l6 = t39;
                            }
                            goto ix23;
ie23:;
ix23:;
                        }
                        {
                            void * t40 = (void *)((uint8_t *)&F.l1);
                            uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            void * t42 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                            uint64_t t43 = gm_l2;
                            uint64_t t44 = (uintptr_t)t43;
                            uint64_t t45 = gm_l6;
                            uint64_t t46 = (uintptr_t)t45;
                            if (t44 > t46 || t46 > (uintptr_t)t41) gm_trap_slice(t44, t46, t41, gm_srcm0juv8ecdls4, 44, 22);
                            gm_b16_8 t47;
                            GM_ST(void *, (uint8_t *)&t47, (void *)(((uint8_t *)t42 + t44 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(t46 - t44));
                            gm_tftb1odc739t5 t48 = gm_f_push_3ef746b(t40, t47);
                            if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srcm0juv8ecdls4, 44, 13);
                            gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                        }
                        {
                            uint64_t t50 = gm_l5;
                            uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                            if (o52) gm_trap("integer overflow in `+`", gm_srcm0juv8ecdls4, 45, 21);
                            uint64_t t53 = r51;
                            gm_l2 = t53;
                        }
                        goto ix15;
ie15:;
ix15:;
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 40, 5);
me11:;
                }
lc4:;
                {
                    uint64_t t55 = gm_l4;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l4 = t58;
                }
            }
lb4:;
        }
    }
    {
        uint64_t t60 = gm_l2;
        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t62 = (t60 < t61);
        if (!t62) goto ie59;
        {
            void * t64 = (void *)((uint8_t *)&F.l1);
            uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t66 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t67 = gm_l2;
            uint64_t t68 = (uintptr_t)t67;
            if (t68 > (uintptr_t)t65 || (uintptr_t)t65 > (uintptr_t)t65) gm_trap_slice(t68, (uintptr_t)t65, t65, gm_srcm0juv8ecdls4, 48, 36);
            gm_b16_8 t69;
            GM_ST(void *, (uint8_t *)&t69, (void *)(((uint8_t *)t66 + t68 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)((uintptr_t)t65 - t68));
            gm_tftb1odc739t5 t70 = gm_f_push_3ef746b(t64, t69);
            if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_srcm0juv8ecdls4, 48, 27);
            gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) void gm_f_module_members_26b4s91(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2) {
    struct gm_fr_module_members_26b4s91 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* m */
    uint64_t gm_l2; /* from */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t3877pk7lv748 gm_l6; /* n */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* d */
    GM_STACK_CHECK(&F, 479u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_5dhgg4i((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint64_t t3 = gm_l1;
            uint64_t t4 = (uintptr_t)t3;
            void * t5 = gm_l0;
            void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
            uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 200) + 0) + 8);
            if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_srcm0juv8ecdls4, 223, 14);
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 200) + 0)) + t4 * 184) + 16) + 0));
            gm_l4 = t8;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l5;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l5;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t3877pk7lv748 t20 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t16 * 24));
                    gm_l6 = t20;
                    {
                        void * t23 = gm_l0;
                        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                        uint64_t t25 = gm_l2;
                        uint8_t t26 = gm_f_decl_visible_bd168ti(t23, t24, t25);
                        if (!t26) goto ie22;
                        {
                            void * t28 = (void *)((uint8_t *)&F.l3);
                            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                            gm_tftb1odc739t5 t30 = gm_f_push_ucgmlo2(t28, t29);
                            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcm0juv8ecdls4, 224, 43);
                            gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 223, 5);
me18:;
                }
lc9:;
                {
                    uint64_t t33 = gm_l5;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l5 = t36;
                }
            }
lb9:;
        }
    }
    {
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_f_sort_o401rjn(t37);
    }
    {
        gm_f_list_f0gjjbh_3((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_l8 = t39;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t41 = gm_l9;
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t43 = (t41 < t42);
                if (!t43) goto lb40;
                {
                    uint64_t t46 = gm_l9;
                    uint64_t t47 = (uintptr_t)t46;
                    uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint64_t t49 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t47 * 8));
                    uint64_t t52 = GM_LD(uint64_t, ((uint8_t *)&t49));
                    gm_l10 = t52;
                    {
                        void * t54 = (void *)((uint8_t *)&F.l7);
                        void * t55 = gm_l0;
                        uint64_t t56 = gm_l10;
                        gm_tl5cuhjfnc4cr t57;
                        gm_f_decl_item_a57s8if((void *)&t57, (void *)NULL, cx, NULL, t55, t56);
                        if (gm_unw.kind) {
                            goto gm_ll44;
                        }
                        gm_tftb1odc739t5 t58 = gm_f_push_1e500nc_3(t54, &t57);
                        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_srcm0juv8ecdls4, 228, 25);
                        gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
                    }
                    goto me50;
mn51:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 228, 5);
me50:;
                }
lc40:;
                {
                    uint64_t t61 = gm_l9;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    uint64_t t64 = r62;
                    gm_l9 = t64;
                }
            }
gm_ll44:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb40; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc40; }
            }
            goto gm_lad;
lb40:;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l7), 40);
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
inline __attribute__((aligned(4))) void gm_f_module_scope_items_9t7jui3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_module_scope_items_9t7jui3 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* m */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t3877pk7lv748 gm_l5; /* n */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* d */
    GM_STACK_CHECK(&F, 446u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_5dhgg4i((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint64_t t3 = gm_l1;
            uint64_t t4 = (uintptr_t)t3;
            void * t5 = gm_l0;
            void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
            uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 200) + 0) + 8);
            if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_srcm0juv8ecdls4, 261, 14);
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 200) + 0)) + t4 * 184) + 16) + 0));
            gm_l3 = t8;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l4;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l4;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t3877pk7lv748 t20 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t16 * 24));
                    gm_l5 = t20;
                    {
                        void * t22 = (void *)((uint8_t *)&F.l2);
                        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
                        gm_tftb1odc739t5 t24 = gm_f_push_ucgmlo2(t22, t23);
                        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srcm0juv8ecdls4, 261, 49);
                        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 261, 5);
me18:;
                }
lc9:;
                {
                    uint64_t t27 = gm_l4;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l4 = t30;
                }
            }
lb9:;
        }
    }
    {
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_f_sort_o401rjn(t31);
    }
    {
        gm_f_list_f0gjjbh_3((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_l7 = t33;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t35 = gm_l8;
                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t37 = (t35 < t36);
                if (!t37) goto lb34;
                {
                    uint64_t t40 = gm_l8;
                    uint64_t t41 = (uintptr_t)t40;
                    uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint64_t t43 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t41 * 8));
                    uint64_t t46 = GM_LD(uint64_t, ((uint8_t *)&t43));
                    gm_l9 = t46;
                    {
                        void * t48 = (void *)((uint8_t *)&F.l6);
                        void * t49 = gm_l0;
                        uint64_t t50 = gm_l9;
                        gm_tl5cuhjfnc4cr t51;
                        gm_f_decl_item_a57s8if((void *)&t51, (void *)NULL, cx, NULL, t49, t50);
                        if (gm_unw.kind) {
                            goto gm_ll38;
                        }
                        gm_tftb1odc739t5 t52 = gm_f_push_1e500nc_3(t48, &t51);
                        if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srcm0juv8ecdls4, 264, 25);
                        gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
                    }
                    goto me44;
mn45:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 264, 5);
me44:;
                }
lc34:;
                {
                    uint64_t t55 = gm_l8;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l8 = t58;
                }
            }
gm_ll38:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb34; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc34; }
            }
            goto gm_lad;
lb34:;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l6), 40);
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
__attribute__((aligned(4))) void gm_f_op_info_4a09t36(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2) {
    struct gm_fr_op_info_4a09t36 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* ed */
    uint32_t gm_l2; /* index */
    void * gm_l3; /* decl */
    gm_b16_8 gm_l4; /* name */
    void * gm_l6; /* lit */
    gm_th2lqbo77pvsc gm_l7; /* doc */
    gm_b16_8 gm_l8; /* code */
    uint32_t gm_l10; /* e */
    void * gm_l11; /* info */
    void * gm_l12; /* op */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l17; /* ret */
    gm_b16_8 gm_l18; /* kw */
    GM_STACK_CHECK(&F, 1693u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srcm0juv8ecdls4, 101, 14);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t3 * 216);
        gm_l3 = t7;
    }
    {
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(0));
        gm_l4 = t8;
    }
    {
        void * t10 = gm_l3;
        uint8_t t11 = GM_LD(uint8_t, (((((uint8_t *)t10) + 40) + 64) + 0));
        if (t11 != ((uint8_t)0ULL)) goto ie9;
        gm_tv2uf4leil5hq t12 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t10) + 40) + 64) + 8));
        F.l5 = t12;
        {
            void * t15 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
            if (t16 != ((uint8_t)34ULL)) goto ie14;
            void * t17 = GM_LD(void *, ((((uint8_t *)t15) + 8) + 8));
            gm_l6 = t17;
            {
                uint32_t t19 = gm_l2;
                uint64_t t20 = (uint64_t)t19;
                uint64_t t21 = (uintptr_t)t20;
                void * t22 = gm_l6;
                uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)t22) + 24) + 8);
                if (t21 >= (uintptr_t)(t23)) gm_trap_index(t21, t23, gm_srcm0juv8ecdls4, 104, 49);
                gm_b16_8 t24 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t22) + 24)) + t21 * 80) + 0) + 0));
                gm_l4 = t24;
            }
            goto ix14;
ie14:;
ix14:;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t25 = gm_l3;
        gm_th2lqbo77pvsc t26 = GM_LD(gm_th2lqbo77pvsc, ((((uint8_t *)t25) + 40) + 16));
        gm_l7 = t26;
    }
    {
        gm_b16_8 t27 = gm_l4;
        gm_l8 = t27;
    }
    {
        void * t29 = gm_l0;
        uint64_t t30 = gm_l1;
        gm_tlabdfckddbfd t31;
        gm_f_decl_done_mk70ol9((void *)&t31, (void *)NULL, cx, NULL, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t31))[208] != 0)) goto ie28;
        F.l9 = (*(gm_t6qooom8qa2do *)(((uint8_t *)&t31) + 0));
        {
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
            if (t34 != ((uint8_t)2ULL)) goto ie33;
            uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&F.l9) + 8));
            gm_l10 = t35;
            {
                uint32_t t37 = gm_l10;
                uint64_t t38 = (uint64_t)t37;
                uint64_t t39 = (uintptr_t)t38;
                void * t40 = gm_l0;
                uint64_t t41 = GM_LD(uintptr_t, ((((uint8_t *)t40) + 1048) + 0) + 8);
                if (t39 >= (uintptr_t)(t41)) gm_trap_index(t39, t41, gm_srcm0juv8ecdls4, 110, 22);
                void * t42 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t40) + 1048) + 0)) + t39 * 56);
                gm_l11 = t42;
            }
            {
                uint32_t t44 = gm_l2;
                uint64_t t45 = (uint64_t)t44;
                void * t46 = gm_l11;
                uint64_t t47 = GM_LD(uintptr_t, (((uint8_t *)t46) + 40) + 8);
                uint8_t t48 = (t45 < t47);
                if (!t48) goto ie43;
                {
                    uint32_t t50 = gm_l2;
                    uint64_t t51 = (uint64_t)t50;
                    uint64_t t52 = (uintptr_t)t51;
                    void * t53 = gm_l11;
                    uint64_t t54 = GM_LD(uintptr_t, (((uint8_t *)t53) + 40) + 8);
                    if (t52 >= (uintptr_t)(t54)) gm_trap_index(t52, t54, gm_srcm0juv8ecdls4, 112, 24);
                    void * t55 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t53) + 40)) + t52 * 48);
                    gm_l12 = t55;
                }
                {
                    gm_f_list_epvducg((void *)&F.l13, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    {
                        void * t57 = gm_l12;
                        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)t57) + 24));
                        gm_l14 = t58;
                    }
                    {
                        gm_l15 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t60 = gm_l15;
                            uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                            uint8_t t62 = (t60 < t61);
                            if (!t62) goto lb59;
                            {
                                uint64_t t65 = gm_l15;
                                uint64_t t66 = (uintptr_t)t65;
                                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                gm_t5v0c8ia6a5mo t70 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t66 * 56));
                                F.l16 = t70;
                                {
                                    void * t72 = (void *)((uint8_t *)&F.l13);
                                    gm_t6j6smj7qoctu t73;
                                    gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                                    GM_ST(gm_b16_8, (uint8_t *)&t73 + 0, t74);
                                    void * t75 = gm_l0;
                                    gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16));
                                    gm_b16_8 t77 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t75, t76);
                                    if (gm_unw.kind) {
                                        goto gm_ll63;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t73 + 16, t77);
                                    gm_tabb710d9mq2e t78;
                                    gm_f_format_f6rro2t((void *)&t78, (void *)NULL, cx, NULL, &t73);
                                    if (gm_unw.kind) {
                                        goto gm_ll63;
                                    }
                                    if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 8, gm_srcm0juv8ecdls4, 114, 46);
                                    gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&t78) + 8));
                                    gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                                    gm_tftb1odc739t5 t81 = gm_f_push_3ef746b(t72, t80);
                                    if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_srcm0juv8ecdls4, 114, 38);
                                    gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
                                }
                                goto me68;
mn69:;
                                gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 114, 17);
me68:;
                            }
lc59:;
                            {
                                uint64_t t84 = gm_l15;
                                uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                                uint64_t t87 = r85;
                                gm_l15 = t87;
                            }
                        }
gm_ll63:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb59; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc59; }
                        }
                        goto gm_lad;
lb59:;
                    }
                }
                {
                    gm_b16_8 t88;
                    void * t90 = gm_l0;
                    void * t91 = (void *)(((uint8_t *)t90) + 8);
                    void * t92 = gm_l12;
                    gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t92) + 40));
                    uint8_t t94 = gm_f_is_unit_n83ttnb(t91, GM_CONV(gm_csguankko4lmhd, t93));
                    if (!t94) goto ie89;
                    gm_b16_8 t96;
                    GM_ST(void *, (uint8_t *)&t96, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(0));
                    gm_b16_8 t97 = t96;
                    t88 = t97;
                    goto ix89;
ie89:;
                    void * t99 = gm_l0;
                    void * t100 = gm_l12;
                    gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t100) + 40));
                    gm_b16_8 t102 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t99, t101);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t103;
                    GM_ST(gm_b16_8, ((uint8_t *)&t103 + 0), t102);
                    gm_tabb710d9mq2e t104;
                    gm_f_format_0555s29((void *)&t104, (void *)NULL, cx, NULL, t103);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 8, gm_srcm0juv8ecdls4, 115, 63);
                    gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&t104) + 8));
                    gm_b16_8 t106 = t105;
                    gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                    t88 = t107;
ix89:;
                    gm_l17 = t88;
                }
                {
                    void * t108 = gm_l12;
                    gm_b16_8 t109;
                    uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t108) + 16) + 0));
                    switch (t115) { case ((uint8_t)0ULL): goto ma111; case ((uint8_t)1ULL): goto ma112; case ((uint8_t)2ULL): goto ma113; default: goto ma114; }
ma111:;
                    uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t108) + 16) + 0));
                    if (t117 != ((uint8_t)0ULL)) goto mn116;
                    gm_b16_8 t118;
                    GM_ST(void *, (uint8_t *)&t118, (void *)(gm_s579fb7l5l6df));
                    GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(2));
                    t109 = t118;
                    goto me110;
mn116:;
ma112:;
                    uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t108) + 16) + 0));
                    if (t120 != ((uint8_t)1ULL)) goto mn119;
                    gm_b16_8 t121;
                    GM_ST(void *, (uint8_t *)&t121, (void *)(gm_sb88rajuutaep));
                    GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(3));
                    t109 = t121;
                    goto me110;
mn119:;
ma113:;
                    uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t108) + 16) + 0));
                    if (t123 != ((uint8_t)2ULL)) goto mn122;
                    gm_b16_8 t124;
                    GM_ST(void *, (uint8_t *)&t124, (void *)(gm_s66uvortgrtsg));
                    GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(3));
                    t109 = t124;
                    goto me110;
mn122:;
ma114:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 116, 23);
me110:;
                    gm_l18 = t109;
                }
                {
                    gm_tud7oovfvnf43 t125;
                    void * t126 = gm_l11;
                    gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)t126) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t125 + 0, t127);
                    void * t128 = gm_l12;
                    gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)t128) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t125 + 16, t129);
                    gm_b16_8 t130 = gm_l18;
                    GM_ST(gm_b16_8, (uint8_t *)&t125 + 32, t130);
                    gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                    gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
                    gm_b16_8 t133;
                    GM_ST(void *, (uint8_t *)&t133, (void *)(gm_sg5gdnrith2i6));
                    GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(2));
                    gm_tabb710d9mq2e t134;
                    gm_f_join_sfknljb((void *)&t134, (void *)NULL, cx, NULL, t132, t133);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 8, gm_srcm0juv8ecdls4, 121, 80);
                    gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&t134) + 8));
                    GM_ST(gm_b16_8, (uint8_t *)&t125 + 48, t135);
                    gm_b16_8 t136 = gm_l17;
                    GM_ST(gm_b16_8, (uint8_t *)&t125 + 64, t136);
                    gm_tabb710d9mq2e t137;
                    gm_f_format_9ndqnou((void *)&t137, (void *)NULL, cx, NULL, &t125);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t137))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t137) + 8, gm_srcm0juv8ecdls4, 121, 24);
                    gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&t137) + 8));
                    gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&t138));
                    gm_l8 = t139;
                }
                goto ix43;
ie43:;
ix43:;
            }
            goto ix33;
ie33:;
ix33:;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        memset((uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 1, 0, 7);
        gm_t9qkh0rsooc8d t140;
        GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)12ULL));
        gm_t6ok5tvcjc200 t141;
        gm_b16_8 t142 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t141 + 0, t142);
        memcpy((uint8_t *)&t141 + 16, ((uint8_t *)&gm_l7), 24);
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 0, t140);
        GM_ST(gm_t6ok5tvcjc200, (uint8_t *)&(*(gm_t70bah4iduq3d *)gm_out) + 8, t141);
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
inline __attribute__((aligned(4))) void gm_f_op_item_8dfr8db(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2) {
    struct gm_fr_op_item_8dfr8db F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* ed */
    uint32_t gm_l2; /* index */
    gm_b16_8 gm_l4; /* detail */
    gm_b16_8 gm_l5; /* name */
    void * gm_l7; /* lit */
    GM_STACK_CHECK(&F, 535u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        uint32_t t4 = gm_l2;
        gm_f_op_info_4a09t36((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_trqfvmjtt4sij t6;
        gm_f_lines_of_ai9428u((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 0));
        void * t8 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t7)));
        uint64_t t9 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t7) + 8);
        uint64_t t10 = (uintptr_t)0;
        gm_th2lqbo77pvsc t11;
        if (t10 < t9) {
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)t8 + t10 * 16));
        gm_th2lqbo77pvsc t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t13), t12);
        ((uint8_t *)&t13)[16] = 1;
            t11 = t13;
        } else {
        gm_th2lqbo77pvsc t14;
        memset(&t14, 0, sizeof t14);
            t11 = t14;
        }
        gm_b16_8 t15;
        if (((((uint8_t *)&t11))[16] != 0)) {
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 0));
            t15 = t16;
        } else {
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(0));
            t15 = t17;
        }
        gm_l4 = t15;
    }
    {
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(0));
        gm_l5 = t18;
    }
    {
        uint64_t t20 = gm_l1;
        uint64_t t21 = (uintptr_t)t20;
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
        uint64_t t24 = GM_LD(uintptr_t, ((((uint8_t *)t23) + 240) + 0) + 8);
        if (t21 >= (uintptr_t)(t24)) gm_trap_index(t21, t24, gm_srcm0juv8ecdls4, 214, 23);
        uint8_t t25 = GM_LD(uint8_t, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t23) + 240) + 0)) + t21 * 216) + 40) + 64) + 0));
        if (t25 != ((uint8_t)0ULL)) goto ie19;
        gm_tv2uf4leil5hq t26 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t23) + 240) + 0)) + t21 * 216) + 40) + 64) + 8));
        F.l6 = t26;
        {
            void * t29 = GM_LD(void *, (((uint8_t *)&F.l6) + 40));
            uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            if (t30 != ((uint8_t)34ULL)) goto ie28;
            void * t31 = GM_LD(void *, ((((uint8_t *)t29) + 8) + 8));
            gm_l7 = t31;
            {
                uint32_t t33 = gm_l2;
                uint64_t t34 = (uint64_t)t33;
                uint64_t t35 = (uintptr_t)t34;
                void * t36 = gm_l7;
                uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)t36) + 24) + 8);
                if (t35 >= (uintptr_t)(t37)) gm_trap_index(t35, t37, gm_srcm0juv8ecdls4, 215, 49);
                gm_b16_8 t38 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t36) + 24)) + t35 * 80) + 0) + 0));
                gm_l5 = t38;
            }
            goto ix28;
ie28:;
ix28:;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        memset((uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 17, 0, 7);
        gm_b16_8 t39 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 0, t39);
        gm_t9qkh0rsooc8d t40 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l3) + 0));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 16, t40);
        gm_b16_8 t41 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 24, t41);
        memcpy((uint8_t *)&(*(gm_tl5cuhjfnc4cr *)gm_out) + 40, ((((uint8_t *)&F.l3) + 8) + 16), 24);
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
__attribute__((aligned(4))) void gm_f_operation_members_62l824c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_operation_members_62l824c F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint32_t gm_l4; /* e */
    void * gm_l5; /* info */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l13; /* ret */
    GM_STACK_CHECK(&F, 1473u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_f0gjjbh_3((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_tlabdfckddbfd t4;
        gm_f_decl_done_mk70ol9((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t5;
        if (((((uint8_t *)&t4))[208] != 0)) {
            t5 = (*(gm_t6qooom8qa2do *)(((uint8_t *)&t4) + 0));
        } else {
            memmove(gm_out, ((uint8_t *)&F.l2), 40);
            goto gm_fdone;
        }
        F.l3 = t5;
    }
    {
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t7 != ((uint8_t)2ULL)) goto ie6;
        uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 8));
        gm_l4 = t8;
        {
            uint32_t t10 = gm_l4;
            uint64_t t11 = (uint64_t)t10;
            uint64_t t12 = (uintptr_t)t11;
            void * t13 = gm_l0;
            uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 1048) + 0) + 8);
            if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srcm0juv8ecdls4, 346, 14);
            void * t15 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 1048) + 0)) + t12 * 56);
            gm_l5 = t15;
        }
        {
            {
                void * t17 = gm_l5;
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t17) + 40));
                gm_l6 = t18;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t20 = gm_l7;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t22 = (t20 < t21);
                    if (!t22) goto lb19;
                    {
                        uint64_t t25 = gm_l7;
                        uint64_t t26 = (uintptr_t)t25;
                        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        gm_tv0k2vg3054a6 t30 = GM_LD(gm_tv0k2vg3054a6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t26 * 48));
                        F.l8 = t30;
                        {
                            gm_f_list_epvducg((void *)&F.l9, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll23;
                            }
                        }
                        {
                            {
                                gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 24));
                                gm_l10 = t33;
                            }
                            {
                                gm_l11 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t35 = gm_l11;
                                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                    uint8_t t37 = (t35 < t36);
                                    if (!t37) goto lb34;
                                    {
                                        uint64_t t40 = gm_l11;
                                        uint64_t t41 = (uintptr_t)t40;
                                        uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                        gm_t5v0c8ia6a5mo t45 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t41 * 56));
                                        F.l12 = t45;
                                        {
                                            void * t47 = (void *)((uint8_t *)&F.l9);
                                            gm_t6j6smj7qoctu t48;
                                            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t48 + 0, t49);
                                            void * t50 = gm_l0;
                                            gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 16));
                                            gm_b16_8 t52 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t50, t51);
                                            if (gm_unw.kind) {
                                                goto gm_ll38;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t48 + 16, t52);
                                            gm_tabb710d9mq2e t53;
                                            gm_f_format_f6rro2t((void *)&t53, (void *)NULL, cx, NULL, &t48);
                                            if (gm_unw.kind) {
                                                goto gm_ll38;
                                            }
                                            if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_srcm0juv8ecdls4, 349, 38);
                                            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
                                            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                                            gm_tftb1odc739t5 t56 = gm_f_push_3ef746b(t47, t55);
                                            if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_srcm0juv8ecdls4, 349, 30);
                                            gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
                                        }
                                        goto me43;
mn44:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 349, 9);
me43:;
                                    }
lc34:;
                                    {
                                        uint64_t t59 = gm_l11;
                                        uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                                        uint64_t t62 = r60;
                                        gm_l11 = t62;
                                    }
                                }
gm_ll38:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb34; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc34; }
                                }
                                goto gm_ll23;
lb34:;
                            }
                        }
                        {
                            gm_b16_8 t63;
                            void * t65 = gm_l0;
                            void * t66 = (void *)(((uint8_t *)t65) + 8);
                            gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 40));
                            uint8_t t68 = gm_f_is_unit_n83ttnb(t66, GM_CONV(gm_csguankko4lmhd, t67));
                            if (!t68) goto ie64;
                            gm_b16_8 t70;
                            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(0));
                            gm_b16_8 t71 = t70;
                            t63 = t71;
                            goto ix64;
ie64:;
                            void * t73 = gm_l0;
                            gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 40));
                            gm_b16_8 t75 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t73, t74);
                            if (gm_unw.kind) {
                                goto gm_ll23;
                            }
                            gm_tnpifv20irnph t76;
                            GM_ST(gm_b16_8, ((uint8_t *)&t76 + 0), t75);
                            gm_tabb710d9mq2e t77;
                            gm_f_format_0555s29((void *)&t77, (void *)NULL, cx, NULL, t76);
                            if (gm_unw.kind) {
                                goto gm_ll23;
                            }
                            if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_srcm0juv8ecdls4, 350, 55);
                            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
                            gm_b16_8 t79 = t78;
                            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                            t63 = t80;
ix64:;
                            gm_l13 = t63;
                        }
                        {
                            void * t81 = (void *)((uint8_t *)&F.l2);
                            gm_tl5cuhjfnc4cr t82;
                            memset((uint8_t *)&t82 + 17, 0, 7);
                            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t82 + 0, t83);
                            gm_t9qkh0rsooc8d t84;
                            GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)12ULL));
                            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t82 + 16, t84);
                            gm_t6j6smj7qoctu t85;
                            gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                            gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                            gm_b16_8 t88;
                            GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(2));
                            gm_tabb710d9mq2e t89;
                            gm_f_join_sfknljb((void *)&t89, (void *)NULL, cx, NULL, t87, t88);
                            if (gm_unw.kind) {
                                goto gm_ll23;
                            }
                            if (((((uint8_t *)&t89))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t89) + 8, gm_srcm0juv8ecdls4, 351, 88);
                            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&t89) + 8));
                            GM_ST(gm_b16_8, (uint8_t *)&t85 + 0, t90);
                            gm_b16_8 t91 = gm_l13;
                            GM_ST(gm_b16_8, (uint8_t *)&t85 + 16, t91);
                            gm_tabb710d9mq2e t92;
                            gm_f_format_22ud8a7((void *)&t92, (void *)NULL, cx, NULL, &t85);
                            if (gm_unw.kind) {
                                goto gm_ll23;
                            }
                            if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 8, gm_srcm0juv8ecdls4, 351, 67);
                            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 8));
                            gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                            GM_ST(gm_b16_8, (uint8_t *)&t82 + 24, t94);
                            gm_th2lqbo77pvsc t95;
                            memset(&t95, 0, sizeof t95);
                            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t82 + 40, t95);
                            gm_tftb1odc739t5 t96 = gm_f_push_1e500nc_3(t81, &t82);
                            if (((((uint8_t *)&t96))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t96) + 1, gm_srcm0juv8ecdls4, 351, 9);
                            gm_b0_1 t97 = GM_LD(gm_b0_1, (((uint8_t *)&t96) + 1));
                        }
                        goto me28;
mn29:;
                        gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 347, 5);
me28:;
                    }
lc19:;
                    {
                        uint64_t t99 = gm_l7;
                        uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                        uint64_t t102 = r100;
                        gm_l7 = t102;
                    }
                }
gm_ll23:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb19; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc19; }
                }
                goto gm_lad;
lb19:;
            }
        }
        {
            memmove(gm_out, ((uint8_t *)&F.l2), 40);
            goto gm_fdone;
        }
        goto ix6;
ie6:;
        {
            memmove(gm_out, ((uint8_t *)&F.l2), 40);
            goto gm_fdone;
        }
ix6:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_src_fjcc76p(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_src_fjcc76p F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 249u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    {
        uint8_t t3 = 0;
        gm_tl874t2atnlh3 t4 = gm_l1;
        gm_tl874t2atnlh3 t5 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t6 = (gm_eqff85ac10o3(((uint8_t *)&t4), ((uint8_t *)&t5)));
        if (!t6) {
            uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
            uint64_t t8 = (uint64_t)t7;
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
            uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 0) + 0) + 8);
            uint8_t t13 = (t8 >= t12);
            t3 = t13;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(0));
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
        uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t21 = gm_f_file_o15fqku(t19, t20);
        uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)t21) + 16) + 8);
        void * t23 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t21) + 16)));
        uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        uint64_t t25 = (uint64_t)t24;
        uint64_t t26 = (uintptr_t)t25;
        uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t t28 = (uint64_t)t27;
        uint64_t t29 = (uintptr_t)t28;
        if (t26 > t29 || t29 > (uintptr_t)t22) gm_trap_slice(t26, t29, t22, gm_srcm0juv8ecdls4, 27, 12);
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(((uint8_t *)t23 + t26 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(t29 - t26));
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_universe_items_235nbu8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_universe_items_235nbu8 F;
    void * gm_l0; /* s */
    uint64_t gm_l2; /* p */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t3877pk7lv748 gm_l6; /* n */
    void * gm_l7; /* decl */
    gm_tf5srh4mp4b3i gm_l8; /* yield_decl */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* d */
    uint64_t gm_l12; /* y */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_b16_8 gm_l15; /* n */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l18; /* n */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l21; /* k */
    GM_STACK_CHECK(&F, 1561u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_f0gjjbh_3((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        if (!(((((uint8_t *)t4) + 320))[8] != 0)) goto ie2;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 320));
        gm_l2 = t5;
        {
            gm_f_list_5dhgg4i((void *)&F.l3, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                uint64_t t8 = gm_l2;
                uint64_t t9 = (uintptr_t)t8;
                void * t10 = gm_l0;
                void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
                uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 200) + 0) + 8);
                if (t9 >= (uintptr_t)(t12)) gm_trap_index(t9, t12, gm_srcm0juv8ecdls4, 237, 18);
                gm_b16_8 t13 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t11) + 200) + 0)) + t9 * 184) + 16) + 0));
                gm_l4 = t13;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t15 = gm_l5;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t17 = (t15 < t16);
                    if (!t17) goto lb14;
                    {
                        uint64_t t20 = gm_l5;
                        uint64_t t21 = (uintptr_t)t20;
                        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_t3877pk7lv748 t25 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t21 * 24));
                        gm_l6 = t25;
                        {
                            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                            uint64_t t28 = (uintptr_t)t27;
                            void * t29 = gm_l0;
                            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
                            uint64_t t31 = GM_LD(uintptr_t, ((((uint8_t *)t30) + 240) + 0) + 8);
                            if (t28 >= (uintptr_t)(t31)) gm_trap_index(t28, t31, gm_srcm0juv8ecdls4, 238, 22);
                            void * t32 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t30) + 240) + 0)) + t28 * 216);
                            gm_l7 = t32;
                        }
                        {
                            void * t34 = gm_l7;
                            uint8_t t35 = ((GM_LD(uint8_t, ((((uint8_t *)t34) + 40) + 56) + 0) == ((uint8_t)2ULL)));
                            if (!t35) goto ie33;
                            void * t36 = gm_l0;
                            uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                            uint8_t t38 = gm_f_universe_hidden_gsk2b8r(t36, t37);
                            uint8_t t39 = (uint8_t)!t38;
                            if (!t39) goto ie33;
                            void * t40 = gm_l7;
                            uint8_t t41 = (!((GM_LD(uintptr_t, ((((uint8_t *)t40) + 0) + 0) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)t40) + 0) + 0)), gm_st3p5nb1e6cc8, 4) == 0));
                            if (!t41) goto ie33;
                            {
                                void * t43 = (void *)((uint8_t *)&F.l3);
                                uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                                gm_tftb1odc739t5 t45 = gm_f_push_ucgmlo2(t43, t44);
                                if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srcm0juv8ecdls4, 239, 116);
                                gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
                            }
                            goto ix33;
ie33:;
ix33:;
                        }
                        goto me23;
mn24:;
                        gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 237, 9);
me23:;
                    }
lc14:;
                    {
                        uint64_t t48 = gm_l5;
                        uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                        uint64_t t51 = r49;
                        gm_l5 = t51;
                    }
                }
lb14:;
            }
        }
        {
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_f_sort_o401rjn(t52);
        }
        {
            void * t53 = gm_l0;
            void * t54 = GM_LD(void *, (((uint8_t *)t53) + 0));
            uint64_t t55 = gm_l2;
            gm_b16_8 t56;
            GM_ST(void *, (uint8_t *)&t56, (void *)(gm_ses7tdrdt8a6u));
            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(5));
            gm_tf5srh4mp4b3i t57 = gm_f_lookup_59juqoo(t54, t55, t56);
            gm_l8 = t57;
        }
        {
            {
                gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                gm_l9 = t59;
            }
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t61 = gm_l10;
                    uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t63 = (t61 < t62);
                    if (!t63) goto lb60;
                    {
                        uint64_t t66 = gm_l10;
                        uint64_t t67 = (uintptr_t)t66;
                        uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        uint64_t t69 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t67 * 8));
                        uint64_t t72 = GM_LD(uint64_t, ((uint8_t *)&t69));
                        gm_l11 = t72;
                        {
                            void * t74 = (void *)((uint8_t *)&F.l1);
                            void * t75 = gm_l0;
                            uint64_t t76 = gm_l11;
                            gm_tl5cuhjfnc4cr t77;
                            gm_f_decl_item_a57s8if((void *)&t77, (void *)NULL, cx, NULL, t75, t76);
                            if (gm_unw.kind) {
                                goto gm_ll64;
                            }
                            gm_tftb1odc739t5 t78 = gm_f_push_1e500nc_3(t74, &t77);
                            if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 1, gm_srcm0juv8ecdls4, 244, 13);
                            gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 1));
                        }
                        {
                            if (!((((uint8_t *)&gm_l8))[8] != 0)) goto ie80;
                            uint64_t t81 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
                            gm_l12 = t81;
                            {
                                uint64_t t84 = gm_l12;
                                uint64_t t85 = gm_l11;
                                uint8_t t86 = ((t84 == t85));
                                if (!t86) goto ie83;
                                {
                                    void * t88 = (void *)((uint8_t *)&F.l1);
                                    void * t89 = gm_l0;
                                    uint64_t t90 = gm_l12;
                                    gm_tl5cuhjfnc4cr t91;
                                    gm_f_op_item_8dfr8db((void *)&t91, (void *)NULL, cx, NULL, t89, t90, ((uint32_t)0ULL));
                                    if (gm_unw.kind) {
                                        goto gm_ll64;
                                    }
                                    gm_tftb1odc739t5 t92 = gm_f_push_1e500nc_3(t88, &t91);
                                    if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_srcm0juv8ecdls4, 248, 29);
                                    gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
                                }
                                goto ix83;
ie83:;
ix83:;
                            }
                            goto ix80;
ie80:;
ix80:;
                        }
                        goto me70;
mn71:;
                        gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 243, 9);
me70:;
                    }
lc60:;
                    {
                        uint64_t t95 = gm_l10;
                        uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
                        uint64_t t98 = r96;
                        gm_l10 = t98;
                    }
                }
gm_ll64:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb60; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc60; }
                }
                goto gm_lad;
lb60:;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t100 = ((uint8_t *)gm_c4tbnbi68ao);
            if ((uintptr_t)0 > (uintptr_t)20 || (uintptr_t)20 > (uintptr_t)20) gm_trap_slice((uintptr_t)0, (uintptr_t)20, 20, gm_srcm0juv8ecdls4, 252, 5);
            gm_b16_8 t101;
            GM_ST(void *, (uint8_t *)&t101, (void *)(((uint8_t *)t100 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)((uintptr_t)20 - (uintptr_t)0));
            gm_l13 = t101;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t103 = gm_l14;
                uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t105 = (t103 < t104);
                if (!t105) goto lb102;
                {
                    uint64_t t108 = gm_l14;
                    uint64_t t109 = (uintptr_t)t108;
                    uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t109 * 16));
                    gm_l15 = t113;
                    {
                        void * t115 = (void *)((uint8_t *)&F.l1);
                        gm_tl5cuhjfnc4cr t116;
                        memset((uint8_t *)&t116 + 17, 0, 7);
                        gm_b16_8 t117 = gm_l15;
                        GM_ST(gm_b16_8, (uint8_t *)&t116 + 0, t117);
                        gm_t9qkh0rsooc8d t118;
                        GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)14ULL));
                        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t116 + 16, t118);
                        gm_b16_8 t119;
                        GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sk8fkllqjst4j));
                        GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(12));
                        GM_ST(gm_b16_8, (uint8_t *)&t116 + 24, t119);
                        gm_th2lqbo77pvsc t120;
                        memset(&t120, 0, sizeof t120);
                        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t116 + 40, t120);
                        gm_tftb1odc739t5 t121 = gm_f_push_1e500nc_3(t115, &t116);
                        if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 1, gm_srcm0juv8ecdls4, 252, 27);
                        gm_b0_1 t122 = GM_LD(gm_b0_1, (((uint8_t *)&t121) + 1));
                    }
                    goto me111;
mn112:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 252, 5);
me111:;
                }
lc102:;
                {
                    uint64_t t124 = gm_l14;
                    uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                    uint64_t t127 = r125;
                    gm_l14 = t127;
                }
            }
lb102:;
        }
    }
    {
        {
            void * t129 = ((uint8_t *)gm_cjfp4khah06);
            if ((uintptr_t)0 > (uintptr_t)5 || (uintptr_t)5 > (uintptr_t)5) gm_trap_slice((uintptr_t)0, (uintptr_t)5, 5, gm_srcm0juv8ecdls4, 253, 5);
            gm_b16_8 t130;
            GM_ST(void *, (uint8_t *)&t130, (void *)(((uint8_t *)t129 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)((uintptr_t)5 - (uintptr_t)0));
            gm_l16 = t130;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t132 = gm_l17;
                uint64_t t133 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t134 = (t132 < t133);
                if (!t134) goto lb131;
                {
                    uint64_t t137 = gm_l17;
                    uint64_t t138 = (uintptr_t)t137;
                    uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t138 * 16));
                    gm_l18 = t142;
                    {
                        void * t144 = (void *)((uint8_t *)&F.l1);
                        gm_tl5cuhjfnc4cr t145;
                        memset((uint8_t *)&t145 + 17, 0, 7);
                        gm_b16_8 t146 = gm_l18;
                        GM_ST(gm_b16_8, (uint8_t *)&t145 + 0, t146);
                        gm_t9qkh0rsooc8d t147;
                        GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t145 + 16, t147);
                        gm_b16_8 t148;
                        GM_ST(void *, (uint8_t *)&t148, (void *)(gm_s3s4n6ehcrs2b));
                        GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(16));
                        GM_ST(gm_b16_8, (uint8_t *)&t145 + 24, t148);
                        gm_th2lqbo77pvsc t149;
                        memset(&t149, 0, sizeof t149);
                        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t145 + 40, t149);
                        gm_tftb1odc739t5 t150 = gm_f_push_1e500nc_3(t144, &t145);
                        if (((((uint8_t *)&t150))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t150) + 1, gm_srcm0juv8ecdls4, 253, 28);
                        gm_b0_1 t151 = GM_LD(gm_b0_1, (((uint8_t *)&t150) + 1));
                    }
                    goto me140;
mn141:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 253, 5);
me140:;
                }
lc131:;
                {
                    uint64_t t153 = gm_l17;
                    uint64_t r154; bool o155 = __builtin_add_overflow(t153, ((uint64_t)1ULL), &r154);
                    uint64_t t156 = r154;
                    gm_l17 = t156;
                }
            }
lb131:;
        }
    }
    {
        {
            void * t158 = ((uint8_t *)gm_cka4oohkshv);
            if ((uintptr_t)0 > (uintptr_t)32 || (uintptr_t)32 > (uintptr_t)32) gm_trap_slice((uintptr_t)0, (uintptr_t)32, 32, gm_srcm0juv8ecdls4, 254, 5);
            gm_b16_8 t159;
            GM_ST(void *, (uint8_t *)&t159, (void *)(((uint8_t *)t158 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t159 + 8, (uintptr_t)((uintptr_t)32 - (uintptr_t)0));
            gm_l19 = t159;
        }
        {
            gm_l20 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t161 = gm_l20;
                uint64_t t162 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                uint8_t t163 = (t161 < t162);
                if (!t163) goto lb160;
                {
                    uint64_t t166 = gm_l20;
                    uint64_t t167 = (uintptr_t)t166;
                    uint64_t t168 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    gm_b16_8 t171 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t167 * 16));
                    gm_l21 = t171;
                    {
                        void * t173 = (void *)((uint8_t *)&F.l1);
                        gm_tl5cuhjfnc4cr t174;
                        memset((uint8_t *)&t174 + 17, 0, 7);
                        gm_b16_8 t175 = gm_l21;
                        GM_ST(gm_b16_8, (uint8_t *)&t174 + 0, t175);
                        gm_t9qkh0rsooc8d t176;
                        GM_ST(uint8_t, ((uint8_t *)&t176 + 0), ((uint8_t)16ULL));
                        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t174 + 16, t176);
                        gm_b16_8 t177;
                        GM_ST(void *, (uint8_t *)&t177, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(0));
                        GM_ST(gm_b16_8, (uint8_t *)&t174 + 24, t177);
                        gm_th2lqbo77pvsc t178;
                        memset(&t178, 0, sizeof t178);
                        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t174 + 40, t178);
                        gm_tftb1odc739t5 t179 = gm_f_push_1e500nc_3(t173, &t174);
                        if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 1, gm_srcm0juv8ecdls4, 254, 37);
                        gm_b0_1 t180 = GM_LD(gm_b0_1, (((uint8_t *)&t179) + 1));
                    }
                    goto me169;
mn170:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 254, 5);
me169:;
                }
lc160:;
                {
                    uint64_t t182 = gm_l20;
                    uint64_t r183; bool o184 = __builtin_add_overflow(t182, ((uint64_t)1ULL), &r183);
                    uint64_t t185 = r183;
                    gm_l20 = t185;
                }
            }
lb160:;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 40);
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
__attribute__((aligned(4))) void gm_f_value_members_03rtuq5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_value_members_03rtuq5 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l3; /* id */
    void * gm_l4; /* info */
    uint32_t gm_l5; /* template */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t3877pk7lv748 gm_l9; /* n */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* d */
    uint32_t gm_l14; /* f */
    void * gm_l15; /* sig */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_t88rcucg1b7uk gm_l21; /* recv */
    gm_t88rcucg1b7uk gm_l22; /* pointee */
    uint8_t gm_l23; /* fits */
    uint32_t gm_l24; /* x */
    void * gm_l25; /* decl */
    GM_STACK_CHECK(&F, 1875u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_f0gjjbh_3((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma7:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t14 != ((uint8_t)17ULL)) goto mn13;
        uint32_t t15 = GM_LD(uint32_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l3 = t15;
        {
            void * t17 = gm_l0;
            gm_t88rcucg1b7uk t18 = gm_l1;
            void * t19 = (void *)((uint8_t *)&F.l2);
            gm_b0_1 t20 = gm_f_field_items_9bsf0mi((void *)NULL, cx, NULL, t17, t18, t19, ((uint32_t)0ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint32_t t21 = gm_l3;
            uint64_t t22 = (uint64_t)t21;
            uint64_t t23 = (uintptr_t)t22;
            void * t24 = gm_l0;
            uint64_t t25 = GM_LD(uintptr_t, ((((uint8_t *)t24) + 1088) + 0) + 8);
            if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_srcm0juv8ecdls4, 279, 22);
            void * t26 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t24) + 1088) + 0)) + t23 * 184);
            gm_l4 = t26;
        }
        {
            void * t27 = gm_l0;
            void * t28 = (void *)(((uint8_t *)t27) + 8);
            uint32_t t29 = gm_l3;
            uint32_t t30 = gm_f_template_9o92cvv(t28, t29);
            gm_l5 = t30;
        }
        {
            gm_f_list_5dhgg4i((void *)&F.l6, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t32 = gm_l4;
                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 16));
                uint64_t t34 = (uintptr_t)t33;
                void * t35 = gm_l0;
                void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
                uint64_t t37 = GM_LD(uintptr_t, ((((uint8_t *)t36) + 200) + 0) + 8);
                if (t34 >= (uintptr_t)(t37)) gm_trap_index(t34, t37, gm_srcm0juv8ecdls4, 282, 22);
                gm_b16_8 t38 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t36) + 200) + 0)) + t34 * 184) + 16) + 0));
                gm_l7 = t38;
            }
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t40 = gm_l8;
                    uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint8_t t42 = (t40 < t41);
                    if (!t42) goto lb39;
                    {
                        uint64_t t45 = gm_l8;
                        uint64_t t46 = (uintptr_t)t45;
                        uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        gm_t3877pk7lv748 t50 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t46 * 24));
                        gm_l9 = t50;
                        {
                            void * t52 = (void *)((uint8_t *)&F.l6);
                            uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 16));
                            gm_tftb1odc739t5 t54 = gm_f_push_ucgmlo2(t52, t53);
                            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srcm0juv8ecdls4, 282, 67);
                            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
                        }
                        goto me48;
mn49:;
                        gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 282, 13);
me48:;
                    }
lc39:;
                    {
                        uint64_t t57 = gm_l8;
                        uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                        uint64_t t60 = r58;
                        gm_l8 = t60;
                    }
                }
lb39:;
            }
        }
        {
            gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_f_sort_o401rjn(t61);
        }
        {
            {
                gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                gm_l10 = t63;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t65 = gm_l11;
                    uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t67 = (t65 < t66);
                    if (!t67) goto lb64;
                    {
                        uint64_t t70 = gm_l11;
                        uint64_t t71 = (uintptr_t)t70;
                        uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        uint64_t t73 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t71 * 8));
                        uint64_t t76 = GM_LD(uint64_t, ((uint8_t *)&t73));
                        gm_l12 = t76;
                        {
                            void * t78 = gm_l0;
                            uint64_t t79 = gm_l12;
                            gm_tlabdfckddbfd t80;
                            gm_f_decl_done_mk70ol9((void *)&t80, (void *)NULL, cx, NULL, t78, t79);
                            if (gm_unw.kind) {
                                goto gm_ll68;
                            }
                            gm_t6qooom8qa2do t81;
                            if (((((uint8_t *)&t80))[208] != 0)) {
                                t81 = (*(gm_t6qooom8qa2do *)(((uint8_t *)&t80) + 0));
                            } else {
                                goto lc64;
                            }
                            F.l13 = t81;
                        }
                        {
                            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
                            if (t83 != ((uint8_t)5ULL)) goto ie82;
                            uint32_t t84 = GM_LD(uint32_t, (((uint8_t *)&F.l13) + 8));
                            gm_l14 = t84;
                            {
                                uint32_t t86 = gm_l14;
                                uint64_t t87 = (uint64_t)t86;
                                uint64_t t88 = (uintptr_t)t87;
                                void * t89 = gm_l0;
                                uint64_t t90 = GM_LD(uintptr_t, ((((uint8_t *)t89) + 344) + 0) + 8);
                                if (t88 >= (uintptr_t)(t90)) gm_trap_index(t88, t90, gm_srcm0juv8ecdls4, 287, 25);
                                void * t91 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t89) + 344) + 0)) + t88 * 136);
                                gm_l15 = t91;
                            }
                            {
                                gm_tj8m6gmlgjilo t92;
                                memset(&t92, 0, sizeof t92);
                                F.l16 = t92;
                            }
                            {
                                {
                                    void * t94 = gm_l15;
                                    gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)t94) + 40));
                                    gm_l17 = t95;
                                }
                                {
                                    gm_l18 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t97 = gm_l18;
                                        uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                        uint8_t t99 = (t97 < t98);
                                        if (!t99) goto lb96;
                                        {
                                            uint64_t t102 = gm_l18;
                                            uint64_t t103 = (uintptr_t)t102;
                                            uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                            gm_t5v0c8ia6a5mo t107 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t103 * 56));
                                            F.l19 = t107;
                                            {
                                                uint8_t t110;
                                                if ((((((uint8_t *)&F.l19) + 40))[4] != 0)) goto mn112;
                                                t110 = ((uint8_t)1);
                                                goto me111;
mn112:;
                                                t110 = ((uint8_t)0);
                                                goto me111;
mn113:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 290, 24);
me111:;
                                                if (!t110) goto ie109;
                                                {
                                                    const uint8_t *t115 = ((uint8_t *)&F.l19);
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tj8m6gmlgjilo *)((uint8_t *)&F.l16)) + 57, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tj8m6gmlgjilo *)((uint8_t *)&F.l16)) + 58, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tj8m6gmlgjilo *)((uint8_t *)&F.l16)) + 60, 0);
                                                    ((uint8_t *)&(*(gm_tj8m6gmlgjilo *)((uint8_t *)&F.l16)))[56] = 1;
                                                    memmove((uint8_t *)&(*(gm_tj8m6gmlgjilo *)((uint8_t *)&F.l16)) + 0, t115, 56);
                                                }
                                                {
                                                    goto lb96;
                                                }
                                                goto ix109;
ie109:;
ix109:;
                                            }
                                            goto me105;
mn106:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 289, 17);
me105:;
                                        }
lc96:;
                                        {
                                            uint64_t t117 = gm_l18;
                                            uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                                            uint64_t t120 = r118;
                                            gm_l18 = t120;
                                        }
                                    }
lb96:;
                                }
                            }
                            {
                                gm_tj8m6gmlgjilo t121 = F.l16;
                                gm_t5v0c8ia6a5mo t122;
                                if (((((uint8_t *)&t121))[56] != 0)) {
                                    t122 = (*(gm_t5v0c8ia6a5mo *)(((uint8_t *)&t121) + 0));
                                } else {
                                    goto lc64;
                                }
                                F.l20 = t122;
                            }
                            {
                                gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l20) + 16));
                                gm_l21 = t123;
                            }
                            {
                                void * t125 = gm_l0;
                                void * t126 = (void *)(((uint8_t *)t125) + 8);
                                gm_t88rcucg1b7uk t127 = gm_l21;
                                gm_t53at80pfdus3 t128;
                                gm_f_kind_jcnt4hm((void *)&t128, t126, GM_CONV(gm_csguankko4lmhd, t127));
                                uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&t128) + 0));
                                if (t129 != ((uint8_t)8ULL)) goto ie124;
                                gm_t88rcucg1b7uk t130 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t128) + 8) + 4));
                                gm_l22 = t130;
                                {
                                    gm_t88rcucg1b7uk t132 = gm_l22;
                                    gm_l21 = t132;
                                }
                                goto ix124;
ie124:;
ix124:;
                            }
                            {
                                gm_l23 = ((uint8_t)0);
                            }
                            {
                                void * t134 = gm_l0;
                                void * t135 = (void *)(((uint8_t *)t134) + 8);
                                gm_t88rcucg1b7uk t136 = gm_l21;
                                gm_t53at80pfdus3 t137;
                                gm_f_kind_jcnt4hm((void *)&t137, t135, GM_CONV(gm_csguankko4lmhd, t136));
                                uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&t137) + 0));
                                if (t138 != ((uint8_t)17ULL)) goto ie133;
                                uint32_t t139 = GM_LD(uint32_t, ((((uint8_t *)&t137) + 8) + 0));
                                gm_l24 = t139;
                                {
                                    void * t141 = gm_l0;
                                    void * t142 = (void *)(((uint8_t *)t141) + 8);
                                    uint32_t t143 = gm_l24;
                                    uint32_t t144 = gm_f_template_9o92cvv(t142, t143);
                                    uint32_t t145 = gm_l5;
                                    uint8_t t146 = ((t144 == t145));
                                    gm_l23 = t146;
                                }
                                goto ix133;
ie133:;
ix133:;
                            }
                            {
                                uint8_t t148 = gm_l23;
                                if (!t148) goto ie147;
                                {
                                    uint64_t t150 = gm_l12;
                                    uint64_t t151 = (uintptr_t)t150;
                                    void * t152 = gm_l0;
                                    void * t153 = GM_LD(void *, (((uint8_t *)t152) + 0));
                                    uint64_t t154 = GM_LD(uintptr_t, ((((uint8_t *)t153) + 240) + 0) + 8);
                                    if (t151 >= (uintptr_t)(t154)) gm_trap_index(t151, t154, gm_srcm0juv8ecdls4, 301, 30);
                                    void * t155 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t153) + 240) + 0)) + t151 * 216);
                                    gm_l25 = t155;
                                }
                                {
                                    void * t156 = (void *)((uint8_t *)&F.l2);
                                    gm_tl5cuhjfnc4cr t157;
                                    memset((uint8_t *)&t157 + 17, 0, 7);
                                    void * t158 = gm_l25;
                                    gm_b16_8 t159 = GM_LD(gm_b16_8, ((((uint8_t *)t158) + 0) + 0));
                                    GM_ST(gm_b16_8, (uint8_t *)&t157 + 0, t159);
                                    gm_t9qkh0rsooc8d t160;
                                    GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)2ULL));
                                    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t157 + 16, t160);
                                    void * t161 = gm_l0;
                                    uint32_t t162 = gm_l14;
                                    gm_b16_8 t163 = gm_f_fn_signature_urdu16i((void *)NULL, cx, NULL, t161, t162, ((uint8_t)1));
                                    if (gm_unw.kind) {
                                        goto gm_ll68;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t157 + 24, t163);
                                    void * t164 = gm_l25;
                                    memcpy((uint8_t *)&t157 + 40, ((((uint8_t *)t164) + 40) + 16), 24);
                                    gm_tftb1odc739t5 t165 = gm_f_push_1e500nc_3(t156, &t157);
                                    if (((((uint8_t *)&t165))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t165) + 1, gm_srcm0juv8ecdls4, 302, 21);
                                    gm_b0_1 t166 = GM_LD(gm_b0_1, (((uint8_t *)&t165) + 1));
                                }
                                goto ix147;
ie147:;
ix147:;
                            }
                            goto ix82;
ie82:;
                            {
                                goto lc64;
                            }
ix82:;
                        }
                        goto me74;
mn75:;
                        gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 284, 13);
me74:;
                    }
lc64:;
                    {
                        uint64_t t169 = gm_l11;
                        uint64_t r170; bool o171 = __builtin_add_overflow(t169, ((uint64_t)1ULL), &r170);
                        uint64_t t172 = r170;
                        gm_l11 = t172;
                    }
                }
gm_ll68:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb64; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc64; }
                }
                goto gm_lad;
lb64:;
            }
        }
        goto me6;
mn13:;
ma8:;
        uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t176 != ((uint8_t)0ULL)) goto pa175;
        goto po174;
pa175:;
        uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t178 != ((uint8_t)1ULL)) goto pa177;
        goto po174;
pa177:;
        uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t180 != ((uint8_t)2ULL)) goto pa179;
        goto po174;
pa179:;
        uint8_t t182 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t182 != ((uint8_t)3ULL)) goto pa181;
        goto po174;
pa181:;
        goto mn173;
po174:;
        void * t183 = ((uint8_t *)gm_cjts5fiuldu);
        if ((uintptr_t)0 > (uintptr_t)12 || (uintptr_t)12 > (uintptr_t)12) gm_trap_slice((uintptr_t)0, (uintptr_t)12, 12, gm_srcm0juv8ecdls4, 306, 63);
        gm_b16_8 t184;
        GM_ST(void *, (uint8_t *)&t184, (void *)(((uint8_t *)t183 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)((uintptr_t)12 - (uintptr_t)0));
        void * t185 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t186 = gm_f_builtin_methods_st9oapf((void *)NULL, cx, NULL, t184, t185);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn173:;
ma9:;
        uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t190 != ((uint8_t)10ULL)) goto pa189;
        goto po188;
pa189:;
        uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t192 != ((uint8_t)11ULL)) goto pa191;
        goto po188;
pa191:;
        goto mn187;
po188:;
        void * t193 = ((uint8_t *)gm_c8g6p8jp14b);
        if ((uintptr_t)0 > (uintptr_t)6 || (uintptr_t)6 > (uintptr_t)6) gm_trap_slice((uintptr_t)0, (uintptr_t)6, 6, gm_srcm0juv8ecdls4, 307, 49);
        gm_b16_8 t194;
        GM_ST(void *, (uint8_t *)&t194, (void *)(((uint8_t *)t193 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)((uintptr_t)6 - (uintptr_t)0));
        void * t195 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t196 = gm_f_builtin_methods_st9oapf((void *)NULL, cx, NULL, t194, t195);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn187:;
ma10:;
        goto me6;
mn197:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 276, 5);
me6:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
__attribute__((aligned(4))) void gm_f_variant_members_nesi2hv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_variant_members_nesi2hv F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l3; /* id */
    void * gm_l4; /* info */
    gm_b16_8 gm_l5; /* variants */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l9; /* p */
    gm_b16_8 gm_l10; /* detail */
    GM_STACK_CHECK(&F, 678u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_f0gjjbh_3((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l3 = t8;
        {
            uint32_t t10 = gm_l3;
            uint64_t t11 = (uint64_t)t10;
            uint64_t t12 = (uintptr_t)t11;
            void * t13 = gm_l0;
            uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 1088) + 0) + 8);
            if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srcm0juv8ecdls4, 332, 14);
            void * t15 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 1088) + 0)) + t12 * 184);
            gm_l4 = t15;
        }
        {
            void * t17 = gm_l4;
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t17) + 24) + 0));
            if (t18 != ((uint8_t)1ULL)) goto ie16;
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((((uint8_t *)t17) + 24) + 8) + 8));
            gm_l5 = t19;
            {
                {
                    gm_b16_8 t22 = gm_l5;
                    gm_l6 = t22;
                }
                {
                    gm_l7 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t24 = gm_l7;
                        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        uint8_t t26 = (t24 < t25);
                        if (!t26) goto lb23;
                        {
                            uint64_t t29 = gm_l7;
                            uint64_t t30 = (uintptr_t)t29;
                            uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            gm_tckdobunaulee t34 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t30 * 72));
                            F.l8 = t34;
                            {
                                gm_b16_8 t36;
                                if (!(((((uint8_t *)&F.l8) + 16))[4] != 0)) goto ie37;
                                gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                                gm_l9 = t38;
                                void * t40 = gm_l0;
                                gm_t88rcucg1b7uk t41 = gm_l9;
                                gm_b16_8 t42 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t40, t41);
                                if (gm_unw.kind) {
                                    goto gm_ll27;
                                }
                                gm_tnpifv20irnph t43;
                                GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t42);
                                gm_tabb710d9mq2e t44;
                                gm_f_format_407qqln((void *)&t44, (void *)NULL, cx, NULL, t43);
                                if (gm_unw.kind) {
                                    goto gm_ll27;
                                }
                                if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_srcm0juv8ecdls4, 335, 39);
                                gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
                                gm_b16_8 t46 = t45;
                                gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                                t36 = t47;
                                goto ix37;
ie37:;
                                gm_b16_8 t49;
                                GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(0));
                                gm_b16_8 t50 = t49;
                                t36 = t50;
ix37:;
                                gm_l10 = t36;
                            }
                            {
                                void * t51 = (void *)((uint8_t *)&F.l2);
                                gm_tl5cuhjfnc4cr t52;
                                memset((uint8_t *)&t52 + 17, 0, 7);
                                gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t52 + 0, t53);
                                gm_t9qkh0rsooc8d t54;
                                GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)11ULL));
                                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t52 + 16, t54);
                                gm_b16_8 t55 = gm_l10;
                                GM_ST(gm_b16_8, (uint8_t *)&t52 + 24, t55);
                                gm_th2lqbo77pvsc t56;
                                memset(&t56, 0, sizeof t56);
                                GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t52 + 40, t56);
                                gm_tftb1odc739t5 t57 = gm_f_push_1e500nc_3(t51, &t52);
                                if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srcm0juv8ecdls4, 336, 9);
                                gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
                            }
                            goto me32;
mn33:;
                            gm_trap("no match arm matched the scrutinee", gm_srcm0juv8ecdls4, 334, 5);
me32:;
                        }
lc23:;
                        {
                            uint64_t t60 = gm_l7;
                            uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                            uint64_t t63 = r61;
                            gm_l7 = t63;
                        }
                    }
gm_ll27:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb23; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc23; }
                    }
                    goto gm_lad;
lb23:;
                }
            }
            {
                memmove(gm_out, ((uint8_t *)&F.l2), 40);
                goto gm_fdone;
            }
            goto ix16;
ie16:;
            {
                memmove(gm_out, ((uint8_t *)&F.l2), 40);
                goto gm_fdone;
            }
ix16:;
        }
        goto ix2;
ie2:;
        {
            memmove(gm_out, ((uint8_t *)&F.l2), 40);
            goto gm_fdone;
        }
ix2:;
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
