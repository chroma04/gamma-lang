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
#define GM_STACK_CHECK(p, n) do { if ((uintptr_t)__builtin_frame_address(0) < gm_stack_limit + (uintptr_t)(n) + sizeof(*(p))) gm_stack_overflow(); } while (0)
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
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct gm_csv4kcdnuj265c { uint32_t f0; uint32_t f1; gm_csguankko4lmhd f2; } gm_csv4kcdnuj265c;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; int32_t f1; uint8_t pe[4]; } gm_t0bf4lga76ktk;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t p3[6]; gm_b16_8 f3; void * f4; void * f5; gm_tl874t2atnlh3 f6; uint8_t pe[4]; } gm_t24301fas1b2a;
typedef struct { uint64_t f0; gm_b16_8 f1; uint16_t f2; uint8_t pe[6]; } gm_t3568dh2osse0;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_t5g5vgung6h6s;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_tf5srh4mp4b3i f0; uint8_t f1; uint8_t pe[7]; } gm_th3eueiipivq6;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; } gm_tov4mtrfp5e7v;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_csv4kcdnuj265c) == 12 && _Alignof(gm_csv4kcdnuj265c) == 4 && offsetof(gm_csv4kcdnuj265c, f0) == 0 && offsetof(gm_csv4kcdnuj265c, f1) == 4 && offsetof(gm_csv4kcdnuj265c, f2) == 8, "layout of `BitField`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0bf4lga76ktk) == 24 && _Alignof(gm_t0bf4lga76ktk) == 8 && offsetof(gm_t0bf4lga76ktk, f0) == 0 && offsetof(gm_t0bf4lga76ktk, f1) == 16, "layout of `(string, i32)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t24301fas1b2a) == 72 && _Alignof(gm_t24301fas1b2a) == 8 && offsetof(gm_t24301fas1b2a, f0) == 0 && offsetof(gm_t24301fas1b2a, f1) == 16 && offsetof(gm_t24301fas1b2a, f2) == 17 && offsetof(gm_t24301fas1b2a, f3) == 24 && offsetof(gm_t24301fas1b2a, f4) == 40 && offsetof(gm_t24301fas1b2a, f5) == 48 && offsetof(gm_t24301fas1b2a, f6) == 56, "layout of `FieldDecl`");
_Static_assert(sizeof(gm_t3568dh2osse0) == 32 && _Alignof(gm_t3568dh2osse0) == 8 && offsetof(gm_t3568dh2osse0, f0) == 0 && offsetof(gm_t3568dh2osse0, f1) == 8 && offsetof(gm_t3568dh2osse0, f2) == 24, "layout of `(u64, string, u16)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t5g5vgung6h6s) == 32 && _Alignof(gm_t5g5vgung6h6s) == 8 && offsetof(gm_t5g5vgung6h6s, f0) == 0 && offsetof(gm_t5g5vgung6h6s, f1) == 8 && offsetof(gm_t5g5vgung6h6s, f2) == 24, "layout of `(u32, []u8, u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t24301fas1b2a f1; } gm_t8uai8b3boj78;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { gm_th3eueiipivq6 f0; uint8_t f1; uint8_t pe[7]; } gm_tbm0n9ujhrlhf;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
typedef struct { gm_tnnjeerolos0a f0; gm_trqfvmjtt4sij f1; } gm_tdtrcq4ju76rh;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th3eueiipivq6) == 24 && _Alignof(gm_th3eueiipivq6) == 8 && offsetof(gm_th3eueiipivq6, f0) == 0 && offsetof(gm_th3eueiipivq6, f1) == 16, "layout of `(len: ?u64, is_const: bool)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tnljbma3s3h16;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tov4mtrfp5e7v) == 12 && _Alignof(gm_tov4mtrfp5e7v) == 4 && offsetof(gm_tov4mtrfp5e7v, f0) == 0 && offsetof(gm_tov4mtrfp5e7v, f1) == 4 && offsetof(gm_tov4mtrfp5e7v, f2) == 8, "layout of `BitField`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tpv68m88f9lcr;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnljbma3s3h16 v0; gm_t7bjbmukv26e7 v1; } f1; } gm_t03a3imodqi32;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8uai8b3boj78) == 80 && _Alignof(gm_t8uai8b3boj78) == 8 && offsetof(gm_t8uai8b3boj78, f0) == 0 && offsetof(gm_t8uai8b3boj78, f1) == 8, "layout of `(i: usize, x: FieldDecl)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbm0n9ujhrlhf) == 32 && _Alignof(gm_tbm0n9ujhrlhf) == 8 && offsetof(gm_tbm0n9ujhrlhf, f0) == 0 && offsetof(gm_tbm0n9ujhrlhf, f1) == 24, "layout of `?(len: ?u64, is_const: bool)`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?ValTy`");
_Static_assert(sizeof(gm_tdtrcq4ju76rh) == 48 && _Alignof(gm_tdtrcq4ju76rh) == 8 && offsetof(gm_tdtrcq4ju76rh, f0) == 0 && offsetof(gm_tdtrcq4ju76rh, f1) == 8, "layout of `ParamData`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t24301fas1b2a v0; gm_t7bjbmukv26e7 v1; } f1; } gm_tfc3oljlofib6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpautr4br7h3l v0; gm_tcmkompbl09il v1; gm_b16_8 v2; } f1; } gm_ti0kgh3pc3in2;
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tnljbma3s3h16) == 64 && _Alignof(gm_tnljbma3s3h16) == 8 && offsetof(gm_tnljbma3s3h16, f0) == 0 && offsetof(gm_tnljbma3s3h16, f1) == 32 && offsetof(gm_tnljbma3s3h16, f2) == 40 && offsetof(gm_tnljbma3s3h16, f3) == 48, "layout of `Variant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpv68m88f9lcr) == 56 && _Alignof(gm_tpv68m88f9lcr) == 8 && offsetof(gm_tpv68m88f9lcr, f0) == 0 && offsetof(gm_tpv68m88f9lcr, f1) == 32 && offsetof(gm_tpv68m88f9lcr, f2) == 40, "layout of `Constraint`");
_Static_assert(sizeof(gm_t03a3imodqi32) == 72 && _Alignof(gm_t03a3imodqi32) == 8 && offsetof(gm_t03a3imodqi32, f0) == 0 && offsetof(gm_t03a3imodqi32, f1) == 8, "layout of `UnionMember`");
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_ti0kgh3pc3in2 f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t p6[7]; gm_b16_8 f6; gm_b16_8 f7; uint8_t f8; uint8_t p9[3]; gm_tl874t2atnlh3 f9; gm_tf5srh4mp4b3i f10; } gm_t6art7bprjiqb;
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tfc3oljlofib6) == 80 && _Alignof(gm_tfc3oljlofib6) == 8 && offsetof(gm_tfc3oljlofib6, f0) == 0 && offsetof(gm_tfc3oljlofib6, f1) == 8, "layout of `StructMember`");
_Static_assert(sizeof(gm_ti0kgh3pc3in2) == 56 && _Alignof(gm_ti0kgh3pc3in2) == 8 && offsetof(gm_ti0kgh3pc3in2, f0) == 0 && offsetof(gm_ti0kgh3pc3in2, f1) == 8, "layout of `AdtKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
_Static_assert(sizeof(gm_t6art7bprjiqb) == 184 && _Alignof(gm_t6art7bprjiqb) == 8 && offsetof(gm_t6art7bprjiqb, f0) == 0 && offsetof(gm_t6art7bprjiqb, f1) == 16 && offsetof(gm_t6art7bprjiqb, f2) == 24 && offsetof(gm_t6art7bprjiqb, f3) == 80 && offsetof(gm_t6art7bprjiqb, f4) == 96 && offsetof(gm_t6art7bprjiqb, f5) == 112 && offsetof(gm_t6art7bprjiqb, f6) == 120 && offsetof(gm_t6art7bprjiqb, f7) == 136 && offsetof(gm_t6art7bprjiqb, f8) == 152 && offsetof(gm_t6art7bprjiqb, f9) == 156 && offsetof(gm_t6art7bprjiqb, f10) == 168, "layout of `AdtInfo`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_adt_luat93d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_adt_mentions_param_0jsjgma {
    gm_frame h;
    gm_ti0kgh3pc3in2 l2; /* kind */
    uint8_t ret;
};
struct gm_fr_attr_power_of_two_gj02n2t {
    gm_frame h;
    gm_t5vbkdq81qfs7 l5; /* r */
    gm_t0ialelq3ql1k l6; /* v */
    gm_tbci5ospk5nnn l7; /* i */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_bit_width_b7ofh2h {
    gm_frame h;
    gm_tpautr4br7h3l l5; /* def */
    gm_tckdobunaulee l11; /* v */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_bits_layout_u6kkccp {
    gm_frame h;
    gm_tpautr4br7h3l l3; /* sd */
    gm_trqfvmjtt4sij l6; /* out */
    gm_t0mglksrkfa5e l9; /* fd */
    gm_t57euh612ligr ret;
};
struct gm_fr_c_union_fields_btdvc8b {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_check_bits_struct_ophej5b {
    gm_frame h;
    gm_t0mglksrkfa5e l10; /* f */
    gm_b0_1 ret;
};
struct gm_fr_contains_by_value_pcdi4kq {
    gm_frame h;
    gm_ti0kgh3pc3in2 l5; /* kind */
    uint8_t ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_declare_adt_iefkbkk {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_declare_adt_in_74tov03 {
    gm_frame h;
    gm_trqfvmjtt4sij l16; /* generics */
    gm_trqfvmjtt4sij l17; /* defaults */
    gm_tf2orqcrphmek l20; /* p */
    gm_t6qooom8qa2do l27; /* v */
    gm_ti0kgh3pc3in2 l28; /* kind */
    gm_tpv68m88f9lcr l34; /* c */
    gm_trqfvmjtt4sij l35; /* stack */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_declare_local_adt_6j4pdai {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_adt_tdp3hro {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_3jt8vgk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3jt8vgk_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8fnsr5h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_atlcs4j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_c06andg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d53jqcq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dijpe9t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_evi8iki {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f1ngfj0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hu049sk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ito0pap {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ose1ba9 {
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
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_u4e74ha {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_struct_b5i94nj {
    gm_frame h;
    gm_ti0kgh3pc3in2 ret;
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
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_field_bits_2ktfm1k {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_flatten_2js7j92 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_tnpifv20irnph ret;
};
struct gm_fr_fresh_adt_u9rnf9f {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_fresh_param_442g4m9 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_instance_args_15ek7cf {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_instantiate_adt_lbjcui1 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* gargs */
    gm_t6art7bprjiqb l8; /* info */
    gm_trqfvmjtt4sij l10; /* copy */
    gm_t6art7bprjiqb l12; /* inst_info */
    gm_trqfvmjtt4sij l13; /* map */
    gm_tpautr4br7h3l l18; /* def */
    gm_trqfvmjtt4sij l21; /* vs */
    gm_tckdobunaulee l24; /* v */
    gm_ti0kgh3pc3in2 l28; /* kind */
    gm_trqfvmjtt4sij l33; /* all */
    gm_trqfvmjtt4sij l34; /* infinite */
    gm_trqfvmjtt4sij l38; /* seen */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_fits_imbqv0c {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* v */
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
struct gm_fr_is_infinitely_sized_mhhcucl {
    gm_frame h;
    gm_ti0kgh3pc3in2 l5; /* kind */
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_negative_k87pp3n {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5kg5cjm_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_au01ire {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_g6uul2f {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_gst7tuh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ja7tv3o {
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
struct gm_fr_list_ol3ji66 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pmp06kq_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_spqn36o {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vmvl81b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_many_ptr_l4v11f8 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_member_count_otu9tc0 {
    gm_frame h;
    gm_ti0kgh3pc3in2 l0; /* kind */
    gm_tpautr4br7h3l l1; /* def */
    uint64_t ret;
};
struct gm_fr_member_ty_esvuguv {
    gm_frame h;
    gm_ti0kgh3pc3in2 l0; /* kind */
    gm_tpautr4br7h3l l2; /* def */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_new_adt_info_5pl2srl {
    gm_frame h;
    gm_ti0kgh3pc3in2 l2; /* kind */
    gm_t6art7bprjiqb ret;
};
struct gm_fr_new_param_7010dt4 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_optional_rh6okj3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_kind_of_1jngakt {
    gm_frame h;
    gm_tnnjeerolos0a ret;
};
struct gm_fr_param_n8kgsdm {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_pending_layout_ikknuv1 {
    gm_frame h;
    gm_ti0kgh3pc3in2 l3; /* kind */
    gm_t9805qtonbsag ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_push_type_alias_rr9mg3i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_remove_in_progress_lrie478 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_result_o9ras10 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_scope_mark_i3b2mvo {
    gm_frame h;
    gm_t66j1im9ip128 ret;
};
struct gm_fr_set_state_3j300fi {
    gm_frame h;
    gm_tagrkbqh054h7 l2; /* v */
    gm_b0_1 ret;
};
struct gm_fr_set_template_o09q96o {
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
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_struct_fields_chqu3it {
    gm_frame h;
    gm_tpautr4br7h3l l3; /* def */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_struct_kind_5l1hs46 {
    gm_frame h;
    gm_tkacitepg5fk0 l10; /* a */
    gm_trqfvmjtt4sij l13; /* fields */
    gm_trqfvmjtt4sij l14; /* defaults */
    gm_trqfvmjtt4sij l15; /* default_exprs */
    gm_trqfvmjtt4sij l17; /* members */
    gm_t24301fas1b2a l21; /* f */
    gm_tkacitepg5fk0 l26; /* a */
    gm_t0mglksrkfa5e l34; /* x */
    gm_ti0kgh3pc3in2 ret;
};
struct gm_fr_subst_a2s1mb3 {
    gm_frame h;
    gm_t53at80pfdus3 l4; /* k */
    gm_trqfvmjtt4sij l14; /* new_args */
    gm_toa2vhe3l4hfr l17; /* a */
    gm_tddtjfggdat0d l19; /* sig */
    gm_tddtjfggdat0d l20; /* sig */
    gm_trqfvmjtt4sij l51; /* out */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_subst_effects_jnlbu5m {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* refs */
    gm_trqfvmjtt4sij l7; /* args */
    gm_toa2vhe3l4hfr l10; /* a */
    gm_toa2vhe3l4hfr l12; /* v */
    gm_tnpifv20irnph ret;
};
struct gm_fr_subst_fields_pqf9lb0 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_t0mglksrkfa5e l6; /* f */
    gm_b16_8 ret;
};
struct gm_fr_subst_sig_9ngh936 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* params */
    gm_tddtjfggdat0d ret;
};
struct gm_fr_taken_struct_members_1ltbb18 {
    gm_frame h;
    gm_tfc3oljlofib6 l6; /* m */
    gm_t24301fas1b2a l7; /* f */
    gm_t7bjbmukv26e7 l8; /* w */
    gm_b0_1 ret;
};
struct gm_fr_taken_union_members_pavth81 {
    gm_frame h;
    gm_t03a3imodqi32 l6; /* m */
    gm_tnljbma3s3h16 l7; /* v */
    gm_t7bjbmukv26e7 l8; /* w */
    gm_b0_1 ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_to_u128_haka99r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_t9p38b040grms ret;
};
struct gm_fr_to_u64_m01t1da {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tf5srh4mp4b3i ret;
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
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_union_kind_j2gcmto {
    gm_frame h;
    gm_tkacitepg5fk0 l7; /* a */
    gm_trqfvmjtt4sij l12; /* variants */
    gm_trqfvmjtt4sij l13; /* spans */
    gm_tbci5ospk5nnn l14; /* next */
    gm_trqfvmjtt4sij l15; /* members */
    gm_tnljbma3s3h16 l18; /* v */
    gm_tckdobunaulee l21; /* x */
    gm_tbci5ospk5nnn l24; /* tag */
    gm_t0ialelq3ql1k l26; /* c */
    gm_tbci5ospk5nnn l27; /* i */
    gm_trqfvmjtt4sij l28; /* fields */
    gm_tckdobunaulee l32; /* v */
    gm_tckdobunaulee l39; /* v */
    gm_tckdobunaulee l43; /* w */
    gm_t4gut2c9fj1kt l44; /* d */
    gm_ti0kgh3pc3in2 ret;
};
struct gm_fr_union_variants_a7khaqp {
    gm_frame h;
    gm_tdlpv0q69lov1 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_adt_luat93d(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_adt_mentions_param_0jsjgma(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_attr_power_of_two_gj02n2t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_bit_width_b7ofh2h(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
__attribute__((aligned(4))) void gm_f_c_union_fields_btdvc8b(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_c_union_fields_btdvc8b_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_bits_struct_ophej5b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_default_4bf68q8(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_len_exprs_m8s6m0h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_toceffb697p0u a2);
__attribute__((aligned(4))) uint8_t gm_f_contains_by_value_pcdi4kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2, void * a3);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_in_74tov03(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4, uint8_t a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_constraint_s1juggf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_local_adt_6j4pdai(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_empty_adt_tdp3hro(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c06andg(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d53jqcq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dijpe9t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_evi8iki(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f1ngfj0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hu049sk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u4e74ha(void);
inline __attribute__((aligned(4))) void gm_f_empty_struct_b5i94nj(void *gm_out);
__attribute__((aligned(4))) gm_ti0kgh3pc3in2 gm_f_empty_struct_b5i94nj_c(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_constraint_defer_52jt3m9(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_74ac4hb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_lfqasgq(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_flatten_2js7j92(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) void gm_f_format_0au88qf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_45bh632(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5iu9hua(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_72n0e07(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9kb6o1u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_a5p8et1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_atintr3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bc9gr0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bsjq7um(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c5ago2c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fiq5e0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nag8m5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_otdvqpt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_slub3h9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_uaa99rp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_adt_u9rnf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_param_442g4m9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_6qnso5a(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_41b084m(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_instance_args_15ek7cf(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_instantiate_adt_lbjcui1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_infinitely_sized_mhhcucl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_key_u64_ljklb56(const void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_len_expr_i134fvg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5kg5cjm_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_au01ire(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_g6uul2f(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_gst7tuh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ja7tv3o(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ol3ji66(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_spqn36o(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint64_t gm_f_member_count_otu9tc0(const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_member_count_otu9tc0_c(gm_ti0kgh3pc3in2 a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_member_ty_esvuguv(const void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_member_ty_esvuguv_c(gm_ti0kgh3pc3in2 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_new_adt_info_5pl2srl(void *gm_out, gm_b16_8 a0, uint64_t a1, const void * a2, gm_cseahvg8v7p0vs a3, gm_tf5srh4mp4b3i a4);
__attribute__((aligned(4))) gm_t6art7bprjiqb gm_f_new_adt_info_5pl2srl_c(gm_b16_8 a0, uint64_t a1, gm_ti0kgh3pc3in2 a2, gm_cseahvg8v7p0vs a3, gm_tf5srh4mp4b3i a4);
__attribute__((aligned(4))) uint32_t gm_f_new_param_7010dt4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tnnjeerolos0a a2);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_of_1jngakt(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_pending_layout_ikknuv1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_1j8vbii(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_83inprj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bth3krn(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ghqse9f(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_j84ih6c(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_p52mcnt(void * a0, gm_csv4kcdnuj265c a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r2k6rjf_2(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_sp4si0f(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u4kbe6d(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vrmo7c5(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_f1iksvi(void * a0, const void * a1, uint32_t a2);
__attribute__((aligned(4))) uint32_t gm_f_remove_10muhi8(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_remove_in_progress_lrie478(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_set_template_o09q96o(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_soa_shape_of_ohenvjc(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_struct_fields_chqu3it_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_struct_kind_5l1hs46(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, void * a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_subst_fields_pqf9lb0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_subst_sig_9ngh936(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_soa_2chqte3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_taken_struct_members_1ltbb18(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_taken_union_members_pavth81(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_type_size_ok_tr1llg4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_union_kind_j2gcmto(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tdlpv0q69lov1 gm_f_union_variants_a7khaqp_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_when_cond_9a46a5h(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s2ph9f020upd2[7];
extern const uint8_t gm_s3lik4tvvjana[65];
extern const uint8_t gm_s4ko774bb2ea9[56];
extern const uint8_t gm_s6vvtrmtnt3j1[5];
extern const uint8_t gm_s8h39pil8lgh0[41];
extern const uint8_t gm_sacmu7hr5dknu[88];
extern const uint8_t gm_sbe7dr7t400bh[16];
extern const uint8_t gm_sbse0oko82bp9[42];
extern const uint8_t gm_seoeuc6nik756[40];
extern const uint8_t gm_sf3irv2ans6de[57];
extern const uint8_t gm_sf7l7ca9coehi[85];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sgbumttiou4de[8];
extern const uint8_t gm_shncvaenjonsu[41];
extern const uint8_t gm_sisjr6ep7j5j0[79];
extern const uint8_t gm_sj1k4t2de5144[6];
extern const uint8_t gm_slpp558cs8j3v[36];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3k278vcmqgdr[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcsffver18si54[];
extern const uint8_t gm_su38t05utsk53[67];
extern const uint8_t gm_suuv0qpi4v4e4[8];
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
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_420lj6o F;
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t4, t5, t8);
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
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 40);
        gm_tbgd14esf0jvi t4;
        memset((uint8_t *)&t4 + 12, 0, 4);
        gm_tl874t2atnlh3 t5 = gm_l1;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tftb1odc739t5 t7 = gm_f_push_kiclmcn(t3, &t4);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcgfglnv8p059r, 92, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_adt_luat93d(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_adt_luat93d F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    gm_b16_8 gm_l2; /* args */
    GM_STACK_CHECK(&F, 148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tiilmfd16v3fc t2;
    memset((uint8_t *)&t2 + 4, 0, 4);
    uint32_t t3 = gm_l1;
    GM_ST(uint32_t, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4 = gm_l2;
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 8, t4);
    gm_t53at80pfdus3 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)17ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t5 + 8), t2);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t6;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_copy_pmkg67r F;
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_go2qg0b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t4 = t3;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_tftb1odc739t5 t6 = gm_f_extend_7hs67uj(t2, t5);
        gm_tftb1odc739t5 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srclv3pj5es0f8o, 330, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t9);
        memcpy((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_declare_adt_iefkbkk F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_tf5srh4mp4b3i gm_l2; /* decl */
    gm_b16_8 gm_l3; /* name */
    void * gm_l4; /* lit */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    void * t1 = gm_l0;
    uint64_t t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_l2;
    gm_b16_8 t4 = gm_l3;
    void * t5 = gm_l4;
    gm_t88rcucg1b7uk t6 = gm_f_declare_adt_in_74tov03((void *)NULL, cx, NULL, t1, t2, t3, t4, t5, ((uint8_t)0));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t6;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        gm_f_push_o4ckdr1(t4, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 56)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk_2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)1ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8fnsr5h F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8q3kspo_2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_atlcs4j F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 72)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c06andg(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c06andg F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 72)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d53jqcq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d53jqcq F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 80)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dijpe9t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dijpe9t F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 64)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_evi8iki(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_evi8iki F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 12)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f1ngfj0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f1ngfj0 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 88)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hu049sk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hu049sk F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 72)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)1ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ito0pap F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 56)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ose1ba9 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 12)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)1ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u4e74ha(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_u4e74ha F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 72)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_empty_struct_b5i94nj(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_struct_b5i94nj F;
    GM_STACK_CHECK_SHALLOW(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tpautr4br7h3l t1;
    memset((uint8_t *)&t1 + 17, 0, 7);
    gm_b16_8 t2 = gm_f_empty_slice_ose1ba9();
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    GM_ST(uint8_t, (uint8_t *)&t1 + 16, ((uint8_t)0));
    gm_tf5srh4mp4b3i t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t1 + 24, t3);
    gm_tkn42u7na3t81 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t1 + 40, t4);
    gm_ti0kgh3pc3in2 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tpautr4br7h3l, ((uint8_t *)&t5 + 8), t1);
    F.ret = t5;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t6 = t5;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        int32_t t11 = gm_f_mag_cmp_s39veq6(t7, t10);
        uint8_t t12 = ((t11 == ((int32_t)0LL)));
        t1 = t12;
    } else t1 = 0;
    F.ret = t1;
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
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        gm_tl874t2atnlh3 t5 = gm_l1;
        gm_b16_8 t6 = gm_l2;
        gm_b0_1 t7 = gm_f_report_eardf07((void *)NULL, cx, NULL, t4, t5, t6);
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
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)26ULL));
    gm_t88rcucg1b7uk t3 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t3;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_adt_u9rnf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fresh_adt_u9rnf9f F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* name */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 136) + 0) + 8);
        uint32_t t4 = (uint32_t)t3;
        gm_l2 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 136);
        gm_b16_8 t7 = gm_l1;
        gm_tftb1odc739t5 t8 = gm_f_push_3ef746b(t6, t7);
        gm_tftb1odc739t5 t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_src0ih77f43mhf2, 263, 5);
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 176);
        uint32_t t13 = gm_l2;
        gm_tftb1odc739t5 t14 = gm_f_push_im9d4ro(t12, t13);
        gm_tftb1odc739t5 t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_src0ih77f43mhf2, 264, 5);
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
    }
    {
        uint32_t t17 = gm_l2;
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_param_442g4m9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fresh_param_442g4m9 F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 256);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_src0ih77f43mhf2, 286, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 256) + 0) + 8);
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_src0ih77f43mhf2, 287, 13);
        uint64_t t12 = r10;
        uint32_t t13 = (uint32_t)t12;
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_param_u9t0efs F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 40) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 249, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)1ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ci6airg F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    uint16_t t3 = gm_l2;
    gm_tjcj4j2gbh6c8 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(uint16_t, ((uint8_t *)&t4 + 2), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t6;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
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
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l1 = t5;
        {
            uint8_t t7 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l1) + 0) + 8) == 7) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l1) + 0)), gm_sgbumttiou4de, 7) == 0));
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t10 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)26ULL)));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_8d4f9ek F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 142u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t10 != ((uint8_t)0ULL)) goto pa9;
    goto po8;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t12 != ((uint8_t)1ULL)) goto pa11;
    goto po8;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t14 != ((uint8_t)2ULL)) goto pa13;
    goto po8;
pa13:;
    goto mn7;
po8:;
    t5 = ((uint8_t)1);
    goto me6;
mn7:;
    t5 = ((uint8_t)0);
    goto me6;
mn15:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_negative_k87pp3n F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    F.ret = t1;
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
    GM_STACK_CHECK_SHALLOW(&F, 104u);
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
    gm_t53at80pfdus3 t6 = GM_LD(gm_t53at80pfdus3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 56));
    F.ret = t6;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_084fcd6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
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
inline __attribute__((aligned(4))) void gm_f_list_5kg5cjm_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5kg5cjm_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3jt8vgk_2();
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
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7s4om10 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_r81t436();
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
inline __attribute__((aligned(4))) void gm_f_list_au01ire(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_au01ire F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_f1ngfj0();
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
inline __attribute__((aligned(4))) void gm_f_list_g6uul2f(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g6uul2f F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_hu049sk();
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
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_go2qg0b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_d914i5u();
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
inline __attribute__((aligned(4))) void gm_f_list_gst7tuh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_gst7tuh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_c06andg();
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
inline __attribute__((aligned(4))) void gm_f_list_ja7tv3o(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ja7tv3o F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_dijpe9t();
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
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nof7vfj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_rcd2jln();
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
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_oalq03d F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8fnsr5h();
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
inline __attribute__((aligned(4))) void gm_f_list_ol3ji66(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ol3ji66 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_evi8iki();
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
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pmp06kq_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8q3kspo_2();
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
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s83jn2q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s1lbe5s();
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
inline __attribute__((aligned(4))) void gm_f_list_spqn36o(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_spqn36o F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ose1ba9();
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
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vmvl81b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fh7svmj();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_many_ptr_l4v11f8 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)9ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t6;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_agd5c5k F;
    GM_STACK_CHECK(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t2 = 0;
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        uint8_t t4 = (uint8_t)!t3;
        if (t4) {
            uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)&F.l0) + 8) + 0) + 8);
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            t2 = t6;
        } else t2 = 0;
        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), t2);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_optional_rh6okj3 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* inner */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)13ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t3 + 8), t2);
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_param_n8kgsdm F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)19ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 8), t2);
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t3);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_570g79n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint32_t gm_l2; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l1 = t3;
    }
    {
        uint64_t t5 = gm_l1;
        uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
        if (!t6) goto ie4;
        {
            gm_t9805qtonbsag t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t10 = gm_l1;
        uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 130, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 130, 10);
        uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 4));
        gm_l2 = t17;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 0) + 8);
        void * t21 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 0)));
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l1;
        uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        uint32_t t29 = gm_l2;
        gm_t9805qtonbsag t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[4] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bits_glps5i7 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 0));
    uint64_t r3; bool o4 = __builtin_mul_overflow(t2, ((uint64_t)8ULL), &r3);
    if (o4) gm_trap("integer overflow in `*`", gm_src4o48ksu43djd, 91, 49);
    uint64_t t5 = r3;
    uint32_t t6 = (uint32_t)t5;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_ptr_t1h2m2d F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)8ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t6;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_push_type_alias_rr9mg3i F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 1488);
        gm_tep2drsl9408b t4;
        memset((uint8_t *)&t4 + 20, 0, 4);
        gm_b16_8 t5 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_t88rcucg1b7uk t6 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t4 + 16, t6);
        gm_tftb1odc739t5 t7 = gm_f_push_hv483nm(t3, &t4);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcsffver18si54, 63, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_remove_in_progress_lrie478(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_remove_in_progress_lrie478 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 1728) + 0));
        gm_b16_8 t5 = t4;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        uint32_t t7 = gm_l1;
        gm_tf5srh4mp4b3i t8 = gm_f_index_of_41b084m(t6, t7);
        gm_tf5srh4mp4b3i t9 = t8;
        if (!((((uint8_t *)&t9))[8] != 0)) goto ie2;
        uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)&t9));
        gm_l2 = t10;
        {
            {
                void * t13 = gm_l0;
                void * t14 = (void *)(((uint8_t *)t13) + 1728);
                uint64_t t15 = gm_l2;
                uint32_t t16 = gm_f_remove_10muhi8(t14, t15);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_b16_8 t4 = gm_l2;
        gm_t4gut2c9fj1kt t5;
        gm_f_error_s6hn871((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_push_o4ckdr1(t2, &t5);
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
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 1488);
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)a1) + 0));
        gm_f_truncate_7r782uc(t3, t4);
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 1528);
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        gm_f_truncate_daelje0(t6, t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 1608);
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
        gm_f_truncate_dsb08pk(t9, t10);
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 1568);
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
        gm_f_truncate_upeumbt(t12, t13);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_result_o9ras10 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* err */
    gm_t88rcucg1b7uk gm_l2; /* ok */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tgglga8ro3isc t4;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)14ULL));
    GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t6;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scope_mark_i3b2mvo F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)t1) + 1488) + 0) + 8);
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 1528) + 0) + 8);
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1608) + 0) + 8);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 1568) + 0) + 8);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 0, t2);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 8, t4);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 16, t6);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 24, t8);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2) {
    struct gm_fr_set_state_3j300fi F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 496u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        {
            void * t3 = gm_l0;
            uint64_t t4 = gm_l1;
            gm_tagrkbqh054h7 t5;
            gm_f_state_a5bnsjb((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint64_t t6 = gm_l1;
        uint64_t t7 = (uintptr_t)t6;
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 304) + 0) + 8);
        if (t7 >= (uintptr_t)(t9)) gm_trap_index(t7, t9, gm_srclv0dcbbfhves, 752, 5);
        gm_tagrkbqh054h7 t10 = F.l2;
        GM_ST(gm_tagrkbqh054h7, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t8) + 304) + 0)) + t7 * 216), t10);
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
inline __attribute__((aligned(4))) void gm_f_set_template_o09q96o(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_set_template_o09q96o F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    uint32_t gm_l2; /* template */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
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
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 176) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_src0ih77f43mhf2, 272, 5);
        uint32_t t7 = gm_l2;
        GM_ST(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 176) + 0)) + t4 * 4), t7);
    }
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_kmjeh8s F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 145u);
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
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        uint8_t t10 = 0;
        F.ret = t9;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_slice_5dt9121 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)10ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t6;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t3 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = (void *)((uint8_t *)&F.l1);
        gm_tabb710d9mq2e t5;
        gm_f_into_slice_hq0te56((void *)&t5, t4);
        gm_tabb710d9mq2e t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srclv3pj5es0f8o, 624, 12);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        uint8_t t8 = 0;
        F.ret = t7;
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
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u128_haka99r F;
    GM_STACK_CHECK_SHALLOW(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (!t3) goto ie2;
        {
            gm_t9p38b040grms t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_mag_to_u128_f843dal((void *)&(*(gm_t9p38b040grms *)gm_out), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u64_m01t1da F;
    unsigned __int128 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9p38b040grms t2;
        gm_f_to_u128_haka99r((void *)&t2, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        gm_t9p38b040grms t3 = t2;
        unsigned __int128 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            unsigned __int128 t5 = GM_LD(unsigned __int128, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_tf5srh4mp4b3i t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        unsigned __int128 t8 = gm_l1;
        gm_tf5srh4mp4b3i t9;
        uint64_t t10 = (uint64_t)t8;
        gm_tf5srh4mp4b3i t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[8] = 1;
        gm_tf5srh4mp4b3i t12;
        memset(&t12, 0, sizeof t12);
        t9 = ((unsigned __int128)(t8) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t11 : t12;
        uint8_t t13 = 0;
        F.ret = t9;
        goto gm_fret;
    }
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 224)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t1, ((uint8_t)0), ((uint16_t)8ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t2;
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
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
    gm_trqfvmjtt4sij t1;
    gm_f_list_go2qg0b((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_adt_mentions_param_0jsjgma(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_mentions_param_0jsjgma F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_t88rcucg1b7uk gm_l6; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1088) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_src3k278vcmqgdr, 73, 13);
        gm_ti0kgh3pc3in2 t7 = GM_LD(gm_ti0kgh3pc3in2, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24));
        F.l2 = t7;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t9 = gm_f_member_count_otu9tc0(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l2)));
            gm_l3 = t9;
        }
        {
            for (;;) {
                uint64_t t11 = gm_l4;
                uint64_t t12 = gm_l3;
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l4;
                    uint64_t t17 = t16;
                    uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&t17));
                    gm_l5 = t20;
                    {
                        uint64_t t22 = gm_l5;
                        gm_tkn42u7na3t81 t23 = gm_f_member_ty_esvuguv(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l2)), t22);
                        gm_tkn42u7na3t81 t24 = t23;
                        gm_t88rcucg1b7uk t25;
                        if (((((uint8_t *)&t24))[4] != 0)) {
                            gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t24) + 0));
                            t25 = t26;
                        } else {
                            goto lc10;
                        }
                        gm_l6 = t25;
                    }
                    {
                        void * t28 = gm_l0;
                        void * t29 = (void *)(((uint8_t *)t28) + 8);
                        gm_t88rcucg1b7uk t30 = gm_l6;
                        uint8_t t31 = gm_f_has_param_u9t0efs(t29, GM_CONV(gm_csguankko4lmhd, t30));
                        if (!t31) goto ie27;
                        {
                            uint8_t t33 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix27;
ie27:;
ix27:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 74, 5);
me18:;
                }
lc10:;
                {
                    uint64_t t35 = gm_l4;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l4 = t38;
                }
            }
lb10:;
        }
    }
    {
        uint8_t t39 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_attr_power_of_two_gj02n2t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_attr_power_of_two_gj02n2t F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* a */
    gm_b16_8 gm_l3; /* args */
    void * gm_l4; /* arg */
    uint64_t gm_l8; /* n */
    GM_STACK_CHECK(&F, 1429u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        gm_th2lqbo77pvsc t3 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t2) + 32));
        gm_th2lqbo77pvsc t4 = t3;
        gm_b16_8 t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_b16_8 t7 = gm_f_empty_slice_g3tddrb();
            t5 = t7;
        }
        gm_l3 = t5;
    }
    {
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t10 = (!(t9 == ((uint64_t)1ULL)));
        if (!t10) goto ie8;
        {
            void * t12 = gm_l0;
            void * t13 = gm_l2;
            gm_tl874t2atnlh3 t14 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t13) + 56));
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sbse0oko82bp9));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(41));
            gm_b0_1 t16 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t12, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tf5srh4mp4b3i t17;
            memset(&t17, 0, sizeof t17);
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        uint64_t t19 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_src3k278vcmqgdr, 449, 13);
        void * t21 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t19 * 88);
        gm_l4 = t21;
    }
    {
        void * t23 = gm_l0;
        uint64_t t24 = gm_l1;
        void * t25 = gm_l4;
        gm_tkv1vap90vht2 t26;
        gm_f_try_untyped_fj0h8i5((void *)&t26, (void *)NULL, cx, NULL, t23, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkv1vap90vht2 t27 = t26;
        if (!((((uint8_t *)&t27))[208] != 0)) goto ie22;
        gm_t5vbkdq81qfs7 t28 = GM_LD(gm_t5vbkdq81qfs7, ((uint8_t *)&t27));
        F.l5 = t28;
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
            if (t31 != ((uint8_t)0ULL)) goto ie30;
            gm_t0ialelq3ql1k t32 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l5) + 8));
            F.l6 = t32;
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
            if (t33 != ((uint8_t)0ULL)) goto ie30;
            gm_tbci5ospk5nnn t34 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l6) + 8));
            F.l7 = t34;
            {
                gm_tf5srh4mp4b3i t37 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                gm_tf5srh4mp4b3i t38 = t37;
                if (!((((uint8_t *)&t38))[8] != 0)) goto ie36;
                uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&t38));
                gm_l8 = t39;
                {
                    uint64_t t42 = gm_l8;
                    uint8_t t43 = (t42 > ((uint64_t)0ULL));
                    if (!t43) goto ie41;
                    uint64_t t44 = gm_l8;
                    uint64_t t45 = gm_l8;
                    uint64_t r46; bool o47 = __builtin_sub_overflow(t45, ((uint64_t)1ULL), &r46);
                    if (o47) gm_trap("integer overflow in `-`", gm_src3k278vcmqgdr, 453, 35);
                    uint64_t t48 = r46;
                    uint64_t t49 = (uint64_t)(t44 & t48);
                    uint8_t t50 = ((t49 == ((uint64_t)0ULL)));
                    if (!t50) goto ie41;
                    {
                        uint64_t t52 = gm_l8;
                        gm_tf5srh4mp4b3i t53;
                        GM_ST(uint8_t, (uint8_t *)&t53 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t53 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t53 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t53), t52);
                        ((uint8_t *)&t53)[8] = 1;
                        uint8_t t54 = 0;
                        F.ret = t53;
                        goto gm_fret;
                    }
                    goto ix41;
ie41:;
ix41:;
                }
                goto ix36;
ie36:;
ix36:;
            }
            {
                void * t55 = gm_l0;
                void * t56 = gm_l4;
                gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t56) + 72));
                gm_b16_8 t58;
                GM_ST(void *, (uint8_t *)&t58, (void *)(gm_slpp558cs8j3v));
                GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(35));
                gm_b0_1 t59 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t55, t57, t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tf5srh4mp4b3i t60;
                memset(&t60, 0, sizeof t60);
                uint8_t t61 = 0;
                F.ret = t60;
                goto gm_fret;
            }
            goto ix30;
ie30:;
ix30:;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t62 = gm_l0;
        void * t63 = gm_l4;
        gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 72));
        gm_b16_8 t65;
        GM_ST(void *, (uint8_t *)&t65, (void *)(gm_shncvaenjonsu));
        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(40));
        gm_b0_1 t66 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t62, t64, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tf5srh4mp4b3i t67;
        memset(&t67, 0, sizeof t67);
        uint8_t t68 = 0;
        F.ret = t67;
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_bit_width_b7ofh2h(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bit_width_b7ofh2h F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint16_t gm_l2; /* bits */
    uint32_t gm_l3; /* id */
    void * gm_l4; /* info */
    gm_t88rcucg1b7uk gm_l6; /* u */
    gm_t88rcucg1b7uk gm_l7; /* tag */
    gm_b16_8 gm_l8; /* variants */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    GM_STACK_CHECK(&F, 738u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma8:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t15 != ((uint8_t)4ULL)) goto mn14;
        gm_tf5srh4mp4b3i t16;
        GM_ST(uint8_t, (uint8_t *)&t16 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t16 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t16), ((uint64_t)1ULL));
        ((uint8_t *)&t16)[8] = 1;
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
        goto me7;
mn14:;
ma9:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t19 != ((uint8_t)0ULL)) goto mn18;
        uint16_t t20 = GM_LD(uint16_t, ((((uint8_t *)&t6) + 8) + 2));
        gm_l2 = t20;
        uint16_t t21 = gm_l2;
        uint64_t t22 = (uint64_t)t21;
        gm_tf5srh4mp4b3i t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t23), t22);
        ((uint8_t *)&t23)[8] = 1;
        uint8_t t24 = 0;
        F.ret = t23;
        goto gm_fret;
        goto me7;
mn18:;
ma10:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t26 != ((uint8_t)17ULL)) goto mn25;
        uint32_t t27 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l3 = t27;
        {
            uint32_t t29 = gm_l3;
            uint64_t t30 = (uint64_t)t29;
            uint64_t t31 = (uintptr_t)t30;
            void * t32 = gm_l0;
            uint64_t t33 = GM_LD(uintptr_t, ((((uint8_t *)t32) + 1088) + 0) + 8);
            if (t31 >= (uintptr_t)(t33)) gm_trap_index(t31, t33, gm_src3k278vcmqgdr, 283, 22);
            void * t34 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t32) + 1088) + 0)) + t31 * 184);
            gm_l4 = t34;
        }
        {
            void * t35 = gm_l4;
            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t35) + 24) + 0));
            switch (t41) { case ((uint8_t)0ULL): goto ma37; case ((uint8_t)1ULL): goto ma38; case ((uint8_t)2ULL): goto ma39; default: goto ma39; }
ma37:;
            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t35) + 24) + 0));
            if (t43 != ((uint8_t)0ULL)) goto mn42;
            gm_tpautr4br7h3l t44 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t35) + 24) + 8));
            F.l5 = t44;
            {
                gm_tkn42u7na3t81 t46 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l5) + 40));
                gm_tkn42u7na3t81 t47 = t46;
                gm_t88rcucg1b7uk t48;
                if (((((uint8_t *)&t47))[4] != 0)) {
                    gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t47) + 0));
                    t48 = t49;
                } else {
                    gm_tf5srh4mp4b3i t50;
                    memset(&t50, 0, sizeof t50);
                    uint8_t t51 = 0;
                    F.ret = t50;
                    goto gm_fret;
                }
                gm_l6 = t48;
            }
            {
                void * t52 = gm_l0;
                gm_t88rcucg1b7uk t53 = gm_l6;
                gm_tf5srh4mp4b3i t54 = gm_f_bit_width_b7ofh2h(t52, GM_CONV(gm_csguankko4lmhd, t53));
                uint8_t t55 = 0;
                F.ret = t54;
                goto gm_fret;
            }
            goto me36;
mn42:;
ma38:;
            uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t35) + 24) + 0));
            if (t57 != ((uint8_t)1ULL)) goto mn56;
            gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t35) + 24) + 8) + 0));
            gm_l7 = t58;
            gm_b16_8 t59 = GM_LD(gm_b16_8, (((((uint8_t *)t35) + 24) + 8) + 8));
            gm_l8 = t59;
            {
                void * t62 = gm_l4;
                uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)t62) + 152));
                uint8_t t64 = (uint8_t)!t63;
                if (!t64) goto ie61;
                {
                    gm_tf5srh4mp4b3i t66;
                    memset(&t66, 0, sizeof t66);
                    uint8_t t67 = 0;
                    F.ret = t66;
                    goto gm_fret;
                }
                goto ix61;
ie61:;
ix61:;
            }
            {
                {
                    gm_b16_8 t69 = gm_l8;
                    gm_l9 = t69;
                }
                {
                    gm_l10 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t71 = gm_l10;
                        uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        uint8_t t73 = (t71 < t72);
                        if (!t73) goto lb70;
                        {
                            uint64_t t76 = gm_l10;
                            uint64_t t77 = (uintptr_t)t76;
                            uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            gm_tckdobunaulee t81 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t77 * 72));
                            F.l11 = t81;
                            {
                                uint8_t t84;
                                if ((((((uint8_t *)&F.l11) + 16))[4] != 0)) goto mn86;
                                t84 = ((uint8_t)0);
                                goto me85;
mn86:;
                                t84 = ((uint8_t)1);
                                goto me85;
mn87:;
                                gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 293, 28);
me85:;
                                if (!t84) goto ie83;
                                {
                                    gm_tf5srh4mp4b3i t89;
                                    memset(&t89, 0, sizeof t89);
                                    uint8_t t90 = 0;
                                    F.ret = t89;
                                    goto gm_fret;
                                }
                                goto ix83;
ie83:;
ix83:;
                            }
                            goto me79;
mn80:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 292, 21);
me79:;
                        }
lc70:;
                        {
                            uint64_t t92 = gm_l10;
                            uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                            uint64_t t95 = r93;
                            gm_l10 = t95;
                        }
                    }
lb70:;
                }
            }
            {
                void * t96 = gm_l0;
                gm_t88rcucg1b7uk t97 = gm_l7;
                gm_tf5srh4mp4b3i t98 = gm_f_bit_width_b7ofh2h(t96, GM_CONV(gm_csguankko4lmhd, t97));
                uint8_t t99 = 0;
                F.ret = t98;
                goto gm_fret;
            }
            goto me36;
mn56:;
ma39:;
            gm_tf5srh4mp4b3i t101;
            memset(&t101, 0, sizeof t101);
            uint8_t t102 = 0;
            F.ret = t101;
            goto gm_fret;
            goto me36;
mn100:;
ma40:;
            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 284, 13);
me36:;
        }
        goto me7;
mn25:;
ma11:;
        gm_tf5srh4mp4b3i t104;
        memset(&t104, 0, sizeof t104);
        uint8_t t105 = 0;
        F.ret = t104;
        goto gm_fret;
        goto me7;
mn103:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 279, 5);
me7:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_layout_u6kkccp F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    gm_t88rcucg1b7uk gm_l4; /* u */
    uint32_t gm_l5; /* off */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l10; /* w */
    GM_STACK_CHECK(&F, 683u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l2 = t9;
        {
            uint32_t t12 = gm_l2;
            uint64_t t13 = (uint64_t)t12;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 1088) + 0) + 8);
            if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_src3k278vcmqgdr, 328, 20);
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 1088) + 0)) + t14 * 184) + 24) + 0));
            if (t17 != ((uint8_t)0ULL)) goto ie11;
            gm_tpautr4br7h3l t18 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 1088) + 0)) + t14 * 184) + 24) + 8));
            F.l3 = t18;
            {
                gm_tkn42u7na3t81 t20 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l3) + 40));
                gm_tkn42u7na3t81 t21 = t20;
                gm_t88rcucg1b7uk t22;
                if (((((uint8_t *)&t21))[4] != 0)) {
                    gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t21) + 0));
                    t22 = t23;
                } else {
                    gm_t57euh612ligr t24;
                    memset(&t24, 0, sizeof t24);
                    uint8_t t25 = 0;
                    F.ret = t24;
                    goto gm_fret;
                }
                gm_l4 = t22;
            }
            {
                gm_l5 = ((uint32_t)0ULL);
            }
            {
                gm_f_list_ol3ji66((void *)&F.l6, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                    gm_l7 = t27;
                }
                {
                    gm_l8 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t29 = gm_l8;
                        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t31 = (t29 < t30);
                        if (!t31) goto lb28;
                        {
                            uint64_t t34 = gm_l8;
                            uint64_t t35 = (uintptr_t)t34;
                            uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            gm_t0mglksrkfa5e t39 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t35 * 40));
                            F.l9 = t39;
                            {
                                void * t41 = gm_l0;
                                gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                                uint32_t t43 = gm_f_field_bits_2ktfm1k((void *)NULL, cx, NULL, t41, t42);
                                if (gm_unw.kind) {
                                    goto gm_ll32;
                                }
                                gm_l10 = t43;
                            }
                            {
                                void * t44 = (void *)((uint8_t *)&F.l6);
                                uint32_t t45 = gm_l5;
                                uint32_t t46 = gm_l10;
                                gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                                gm_tov4mtrfp5e7v t48;
                                GM_ST(uint32_t, ((uint8_t *)&t48 + 0), t45);
                                GM_ST(uint32_t, ((uint8_t *)&t48 + 4), t46);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t48 + 8), t47);
                                gm_tftb1odc739t5 t49 = gm_f_push_p52mcnt(t44, GM_CONV(gm_csv4kcdnuj265c, t48));
                                gm_tftb1odc739t5 t50 = t49;
                                if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_src3k278vcmqgdr, 334, 9);
                                gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
                            }
                            {
                                uint32_t t52 = gm_l5;
                                uint32_t t53 = gm_l10;
                                uint32_t r54; bool o55 = __builtin_add_overflow(t52, t53, &r54);
                                if (o55) gm_trap("integer overflow in `+`", gm_src3k278vcmqgdr, 335, 9);
                                uint32_t t56 = r54;
                                gm_l5 = t56;
                            }
                            goto me37;
mn38:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 332, 5);
me37:;
                        }
lc28:;
                        {
                            uint64_t t58 = gm_l8;
                            uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                            uint64_t t61 = r59;
                            gm_l8 = t61;
                        }
                    }
gm_ll32:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb28; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc28; }
                    }
                    goto gm_lad;
lb28:;
                }
            }
            {
                gm_tcmkompbl09il t62;
                memset((uint8_t *)&t62 + 4, 0, 4);
                gm_t88rcucg1b7uk t63 = gm_l4;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t62 + 0, t63);
                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                gm_b16_8 t65 = t64;
                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                GM_ST(gm_b16_8, (uint8_t *)&t62 + 8, t66);
                gm_t57euh612ligr t67;
                GM_ST(uint8_t, (uint8_t *)&t67 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t67 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t67 + 28, 0);
                GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t67), t62);
                ((uint8_t *)&t67)[24] = 1;
                uint8_t t68 = 0;
                F.ret = t67;
                goto gm_fret;
            }
            goto ix11;
ie11:;
            {
                gm_t57euh612ligr t70;
                memset(&t70, 0, sizeof t70);
                uint8_t t71 = 0;
                F.ret = t70;
                goto gm_fret;
            }
ix11:;
        }
        goto ix2;
ie2:;
        {
            gm_t57euh612ligr t73;
            memset(&t73, 0, sizeof t73);
            uint8_t t74 = 0;
            F.ret = t73;
            goto gm_fret;
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
inline __attribute__((aligned(4))) void gm_f_c_union_fields_btdvc8b(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_c_union_fields_btdvc8b F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* id */
    gm_b16_8 gm_l3; /* fields */
    GM_STACK_CHECK_SHALLOW(&F, 313u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l2 = t9;
        {
            uint32_t t12 = gm_l2;
            uint64_t t13 = (uint64_t)t12;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 1088) + 0) + 8);
            if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_src3k278vcmqgdr, 503, 24);
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 1088) + 0)) + t14 * 184) + 24) + 0));
            if (t17 != ((uint8_t)2ULL)) goto ie11;
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 1088) + 0)) + t14 * 184) + 24) + 8));
            gm_l3 = t18;
            {
                gm_b16_8 t20 = gm_l3;
                gm_th2lqbo77pvsc t21;
                GM_ST(uint8_t, (uint8_t *)&t21 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t21 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t21 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t21), t20);
                ((uint8_t *)&t21)[16] = 1;
                uint8_t t22 = 0;
                F.ret = t21;
                goto gm_fret;
            }
            goto ix11;
ie11:;
            {
                gm_th2lqbo77pvsc t24;
                memset(&t24, 0, sizeof t24);
                uint8_t t25 = 0;
                F.ret = t24;
                goto gm_fret;
            }
ix11:;
        }
        goto ix2;
ie2:;
        {
            gm_th2lqbo77pvsc t27;
            memset(&t27, 0, sizeof t27);
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_c_union_fields_btdvc8b_c(void * a0, gm_csguankko4lmhd a1) { gm_th2lqbo77pvsc gm_r; gm_f_c_union_fields_btdvc8b(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_check_bits_struct_ophej5b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_check_bits_struct_ophej5b F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* underlying */
    gm_b16_8 gm_l3; /* fields */
    gm_b16_8 gm_l4; /* msg */
    uint8_t gm_l5; /* signed */
    uint16_t gm_l6; /* width */
    uint64_t gm_l7; /* total */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l11; /* w */
    GM_STACK_CHECK(&F, 1023u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_b16_8 t4 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tabb710d9mq2e t6;
        gm_f_format_a5p8et1((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_src3k278vcmqgdr, 259, 12);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_l4 = t8;
    }
    {
        void * t10 = gm_l0;
        void * t11 = (void *)(((uint8_t *)t10) + 8);
        gm_t88rcucg1b7uk t12 = gm_l2;
        gm_t53at80pfdus3 t13;
        gm_f_kind_jcnt4hm((void *)&t13, t11, GM_CONV(gm_csguankko4lmhd, t12));
        gm_t53at80pfdus3 t14 = t13;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t15 != ((uint8_t)0ULL)) goto ie9;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)&t14) + 8) + 0));
        gm_l5 = t16;
        uint16_t t17 = GM_LD(uint16_t, ((((uint8_t *)&t14) + 8) + 2));
        gm_l6 = t17;
        {
            uint8_t t20 = gm_l5;
            if (!t20) goto ie19;
            {
                void * t22 = gm_l0;
                gm_tl874t2atnlh3 t23 = gm_l1;
                gm_b16_8 t24 = gm_l4;
                gm_b16_8 t25 = t24;
                gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
                gm_b0_1 t27 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t22, t23, t26);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix19;
ie19:;
ix19:;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            {
                gm_b16_8 t29 = gm_l3;
                gm_l8 = t29;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t31 = gm_l9;
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t33 = (t31 < t32);
                    if (!t33) goto lb30;
                    {
                        uint64_t t36 = gm_l9;
                        uint64_t t37 = (uintptr_t)t36;
                        uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        gm_t0mglksrkfa5e t41 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t37 * 40));
                        F.l10 = t41;
                        {
                            void * t44 = gm_l0;
                            gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                            gm_tf5srh4mp4b3i t46 = gm_f_bit_width_b7ofh2h(t44, GM_CONV(gm_csguankko4lmhd, t45));
                            gm_tf5srh4mp4b3i t47 = t46;
                            if (!((((uint8_t *)&t47))[8] != 0)) goto ie43;
                            uint64_t t48 = GM_LD(uint64_t, ((uint8_t *)&t47));
                            gm_l11 = t48;
                            {
                                uint64_t t50 = gm_l7;
                                uint64_t t51 = gm_l11;
                                uint64_t r52; bool o53 = __builtin_add_overflow(t50, t51, &r52);
                                if (o53) gm_trap("integer overflow in `+`", gm_src3k278vcmqgdr, 268, 13);
                                uint64_t t54 = r52;
                                gm_l7 = t54;
                            }
                            goto ix43;
ie43:;
                            {
                                void * t56 = gm_l0;
                                gm_tl874t2atnlh3 t57 = gm_l1;
                                gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                                gm_tnpifv20irnph t59;
                                GM_ST(gm_b16_8, ((uint8_t *)&t59 + 0), t58);
                                gm_tabb710d9mq2e t60;
                                gm_f_format_bc9gr0b((void *)&t60, (void *)NULL, cx, NULL, t59);
                                if (gm_unw.kind) {
                                    goto gm_ll34;
                                }
                                gm_tabb710d9mq2e t61 = t60;
                                if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 8, gm_src3k278vcmqgdr, 270, 27);
                                gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t61) + 8));
                                gm_b16_8 t63 = t62;
                                gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
                                gm_b0_1 t65 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t56, t57, t64);
                                if (gm_unw.kind) {
                                    goto gm_ll34;
                                }
                            }
ix43:;
                        }
                        goto me39;
mn40:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 266, 5);
me39:;
                    }
lc30:;
                    {
                        uint64_t t67 = gm_l9;
                        uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                        uint64_t t70 = r68;
                        gm_l9 = t70;
                    }
                }
gm_ll34:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb30; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc30; }
                }
                goto gm_lad;
lb30:;
            }
        }
        {
            uint64_t t72 = gm_l7;
            uint16_t t73 = gm_l6;
            uint64_t t74 = (uint64_t)t73;
            uint8_t t75 = (!(t72 == t74));
            if (!t75) goto ie71;
            {
                void * t77 = gm_l0;
                gm_tl874t2atnlh3 t78 = gm_l1;
                gm_t3568dh2osse0 t79;
                memset((uint8_t *)&t79 + 26, 0, 6);
                uint64_t t80 = gm_l7;
                GM_ST(uint64_t, (uint8_t *)&t79 + 0, t80);
                void * t81 = gm_l0;
                gm_t88rcucg1b7uk t82 = gm_l2;
                gm_b16_8 t83 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t81, t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t79 + 8, t83);
                uint16_t t84 = gm_l6;
                GM_ST(uint16_t, (uint8_t *)&t79 + 24, t84);
                gm_tabb710d9mq2e t85;
                gm_f_format_c5ago2c((void *)&t85, (void *)NULL, cx, NULL, &t79);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t86 = t85;
                if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 8, gm_src3k278vcmqgdr, 274, 23);
                gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 8));
                gm_b16_8 t88 = t87;
                gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
                gm_b0_1 t90 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t77, t78, t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix71;
ie71:;
ix71:;
        }
        goto ix9;
ie9:;
        {
            {
                void * t93 = gm_l0;
                gm_tl874t2atnlh3 t94 = gm_l1;
                gm_b16_8 t95 = gm_l4;
                gm_b16_8 t96 = t95;
                gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
                gm_b0_1 t98 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t93, t94, t97);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t99;
                uint8_t t100 = 0;
                F.ret = t99;
                goto gm_fret;
            }
        }
ix9:;
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
__attribute__((aligned(4))) uint8_t gm_f_contains_by_value_pcdi4kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2, void * a3) {
    struct gm_fr_contains_by_value_pcdi4kq F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* target_ */
    void * gm_l3; /* seen */
    uint32_t gm_l4; /* id */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_t88rcucg1b7uk gm_l10; /* elem */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    gm_b16_8 gm_l13; /* elems */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t88rcucg1b7uk gm_l16; /* e */
    gm_t88rcucg1b7uk gm_l17; /* err */
    gm_t88rcucg1b7uk gm_l18; /* ok */
    GM_STACK_CHECK(&F, 794u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; default: goto ma12; }
ma8:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t16 != ((uint8_t)17ULL)) goto mn15;
        uint32_t t17 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l4 = t17;
        {
            uint32_t t20 = gm_l4;
            uint32_t t21 = gm_l2;
            uint8_t t22 = ((t20 == t21));
            if (!t22) goto ie19;
            void * t23 = gm_l3;
            uint64_t t24 = GM_LD(uintptr_t, (((uint8_t *)t23) + 0) + 8);
            uint8_t t25 = (t24 > ((uint64_t)0ULL));
            if (!t25) goto ie19;
            {
                uint8_t t27 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix19;
ie19:;
ix19:;
        }
        {
            uint32_t t29 = gm_l4;
            void * t30 = gm_l3;
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t30) + 0));
            gm_b16_8 t32 = t31;
            uint8_t t33 = 0;
            uint32_t t34 = t29;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t32) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t32))) + i * 4, ((uint8_t *)&t34))) { t33 = 1; break; }
            uint8_t t35 = (uint8_t)(t33);
            if (!t35) goto ie28;
            {
                uint8_t t37 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix28;
ie28:;
ix28:;
        }
        {
            void * t38 = gm_l3;
            uint32_t t39 = gm_l4;
            gm_tftb1odc739t5 t40 = gm_f_push_im9d4ro(t38, t39);
            gm_tftb1odc739t5 t41 = t40;
            if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_src3k278vcmqgdr, 650, 13);
            gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
        }
        {
            uint32_t t43 = gm_l4;
            uint64_t t44 = (uint64_t)t43;
            uint64_t t45 = (uintptr_t)t44;
            void * t46 = gm_l0;
            uint64_t t47 = GM_LD(uintptr_t, ((((uint8_t *)t46) + 1088) + 0) + 8);
            if (t45 >= (uintptr_t)(t47)) gm_trap_index(t45, t47, gm_src3k278vcmqgdr, 651, 21);
            gm_ti0kgh3pc3in2 t48 = GM_LD(gm_ti0kgh3pc3in2, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t46) + 1088) + 0)) + t45 * 184) + 24));
            F.l5 = t48;
        }
        {
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                uint64_t t50 = gm_f_member_count_otu9tc0(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l5)));
                gm_l6 = t50;
            }
            {
                for (;;) {
                    uint64_t t52 = gm_l7;
                    uint64_t t53 = gm_l6;
                    uint8_t t54 = (t52 < t53);
                    if (!t54) goto lb51;
                    {
                        uint64_t t57 = gm_l7;
                        uint64_t t58 = t57;
                        uint64_t t61 = GM_LD(uint64_t, ((uint8_t *)&t58));
                        gm_l8 = t61;
                        {
                            uint64_t t63 = gm_l8;
                            gm_tkn42u7na3t81 t64 = gm_f_member_ty_esvuguv(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l5)), t63);
                            gm_tkn42u7na3t81 t65 = t64;
                            gm_t88rcucg1b7uk t66;
                            if (((((uint8_t *)&t65))[4] != 0)) {
                                gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t65) + 0));
                                t66 = t67;
                            } else {
                                goto lc51;
                            }
                            gm_l9 = t66;
                        }
                        {
                            void * t69 = gm_l0;
                            gm_t88rcucg1b7uk t70 = gm_l9;
                            uint32_t t71 = gm_l2;
                            void * t72 = gm_l3;
                            uint8_t t73 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t69, t70, t71, t72);
                            if (gm_unw.kind) {
                                goto gm_ll55;
                            }
                            if (!t73) goto ie68;
                            {
                                uint8_t t75 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix68;
ie68:;
ix68:;
                        }
                        goto me59;
mn60:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 652, 13);
me59:;
                    }
lc51:;
                    {
                        uint64_t t77 = gm_l7;
                        uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                        uint64_t t80 = r78;
                        gm_l7 = t80;
                    }
                }
gm_ll55:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb51; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc51; }
                }
                goto gm_lad;
lb51:;
            }
        }
        {
            uint8_t t81 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me7;
mn15:;
ma9:;
        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t85 != ((uint8_t)11ULL)) goto pa84;
        gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 8));
        gm_l10 = t86;
        goto po83;
pa84:;
        uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t88 != ((uint8_t)20ULL)) goto pa87;
        gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l10 = t89;
        goto po83;
pa87:;
        uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t91 != ((uint8_t)13ULL)) goto pa90;
        gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l10 = t92;
        goto po83;
pa90:;
        goto mn82;
po83:;
        void * t93 = gm_l0;
        gm_t88rcucg1b7uk t94 = gm_l10;
        uint32_t t95 = gm_l2;
        void * t96 = gm_l3;
        uint8_t t97 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t93, t94, t95, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t98 = 0;
        F.ret = t97;
        goto gm_fret;
        goto me7;
mn82:;
ma10:;
        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t100 != ((uint8_t)12ULL)) goto mn99;
        gm_b16_8 t101 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
        gm_l13 = t101;
        {
            {
                gm_b16_8 t104 = gm_l13;
                gm_l14 = t104;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t106 = gm_l15;
                    uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t108 = (t106 < t107);
                    if (!t108) goto lb105;
                    {
                        uint64_t t111 = gm_l15;
                        uint64_t t112 = (uintptr_t)t111;
                        uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        gm_t88rcucg1b7uk t116 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t112 * 4));
                        gm_l16 = t116;
                        {
                            void * t119 = gm_l0;
                            gm_t88rcucg1b7uk t120 = gm_l16;
                            uint32_t t121 = gm_l2;
                            void * t122 = gm_l3;
                            uint8_t t123 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t119, t120, t121, t122);
                            if (gm_unw.kind) {
                                goto gm_ll109;
                            }
                            if (!t123) goto ie118;
                            {
                                uint8_t t125 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix118;
ie118:;
ix118:;
                        }
                        goto me114;
mn115:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 660, 13);
me114:;
                    }
lc105:;
                    {
                        uint64_t t127 = gm_l15;
                        uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                        uint64_t t130 = r128;
                        gm_l15 = t130;
                    }
                }
gm_ll109:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb105; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc105; }
                }
                goto gm_lad;
lb105:;
            }
        }
        {
            uint8_t t131 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me7;
mn99:;
ma11:;
        uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t133 != ((uint8_t)14ULL)) goto mn132;
        gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 0));
        gm_l17 = t134;
        gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l18 = t135;
        uint8_t t136 = 0;
        void * t137 = gm_l0;
        gm_t88rcucg1b7uk t138 = gm_l17;
        uint32_t t139 = gm_l2;
        void * t140 = gm_l3;
        uint8_t t141 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t137, t138, t139, t140);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t141) {
            void * t142 = gm_l0;
            gm_t88rcucg1b7uk t143 = gm_l18;
            uint32_t t144 = gm_l2;
            void * t145 = gm_l3;
            uint8_t t146 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t142, t143, t144, t145);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t136 = t146;
        } else t136 = 1;
        uint8_t t147 = 0;
        F.ret = t136;
        goto gm_fret;
        goto me7;
mn132:;
ma12:;
        uint8_t t149 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me7;
mn148:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 646, 5);
me7:;
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_in_74tov03(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4, uint8_t a5) {
    struct gm_fr_declare_adt_in_74tov03 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_tf5srh4mp4b3i gm_l2; /* decl */
    gm_b16_8 gm_l3; /* name */
    void * gm_l4; /* lit */
    uint8_t gm_l5; /* local */
    gm_b16_8 gm_l6; /* attrs */
    void * gm_l7; /* inner */
    gm_b16_8 gm_l8; /* a */
    void * gm_l9; /* i */
    uint32_t gm_l10; /* id */
    gm_t88rcucg1b7uk gm_l11; /* ty */
    void * gm_l12; /* st */
    void * gm_l13; /* u */
    gm_th2lqbo77pvsc gm_l14; /* params */
    gm_t66j1im9ip128 gm_l15; /* mark */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_tnnjeerolos0a gm_l21; /* kind */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_t08olq97rscn3 gm_l24; /* n */
    uint32_t gm_l25; /* pid */
    uint64_t gm_l26; /* d */
    gm_b16_8 gm_l29; /* wheres */
    void * gm_l30; /* st */
    void * gm_l31; /* u */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    GM_STACK_CHECK(&F, 4212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        gm_b16_8 t2 = gm_f_empty_slice_atlcs4j();
        gm_l6 = t2;
    }
    {
        void * t3 = gm_l4;
        gm_l7 = t3;
    }
    {
        void * t5 = gm_l4;
        uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t6 != ((uint8_t)35ULL)) goto ie4;
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((((uint8_t *)t5) + 8) + 8) + 0));
        gm_l8 = t7;
        void * t8 = GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 16));
        gm_l9 = t8;
        {
            gm_b16_8 t10 = gm_l8;
            gm_l6 = t10;
        }
        {
            void * t11 = gm_l9;
            gm_l7 = t11;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t12 = gm_l0;
        void * t13 = (void *)(((uint8_t *)t12) + 8);
        gm_b16_8 t14 = gm_l3;
        uint32_t t15 = gm_f_fresh_adt_u9rnf9f((void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t15;
    }
    {
        void * t16 = gm_l0;
        void * t17 = (void *)(((uint8_t *)t16) + 1088);
        gm_b16_8 t18 = gm_l3;
        uint64_t t19 = gm_l1;
        gm_ti0kgh3pc3in2 t20;
        gm_f_empty_struct_b5i94nj((void *)&t20);
        void * t21 = gm_l4;
        gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 72));
        gm_tf5srh4mp4b3i t23 = gm_l2;
        gm_t6art7bprjiqb t24;
        gm_f_new_adt_info_5pl2srl((void *)&t24, t18, t19, &t20, GM_CONV(gm_cseahvg8v7p0vs, t22), t23);
        gm_tftb1odc739t5 t25 = gm_f_push_j84ih6c(t17, &t24);
        gm_tftb1odc739t5 t26 = t25;
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_src3k278vcmqgdr, 103, 5);
        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
    }
    {
        void * t28 = gm_l0;
        void * t29 = (void *)(((uint8_t *)t28) + 8);
        uint32_t t30 = gm_l10;
        gm_b16_8 t31 = gm_f_empty_slice_fh7svmj();
        gm_b16_8 t32 = t31;
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        gm_t88rcucg1b7uk t34 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t29, t30, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t34;
    }
    {
        uint8_t t36 = gm_l5;
        if (!t36) goto ie35;
        {
            void * t38 = gm_l0;
            gm_b16_8 t39 = gm_l3;
            gm_t88rcucg1b7uk t40 = gm_l11;
            gm_b0_1 t41 = gm_f_push_type_alias_rr9mg3i((void *)NULL, cx, NULL, t38, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        void * t42 = gm_l7;
        gm_th2lqbo77pvsc t43;
        uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
        switch (t49) { case ((uint8_t)0ULL): goto ma47; case ((uint8_t)1ULL): goto ma47; case ((uint8_t)2ULL): goto ma47; case ((uint8_t)3ULL): goto ma47; case ((uint8_t)4ULL): goto ma47; case ((uint8_t)5ULL): goto ma47; case ((uint8_t)6ULL): goto ma47; case ((uint8_t)7ULL): goto ma47; case ((uint8_t)8ULL): goto ma47; case ((uint8_t)9ULL): goto ma47; case ((uint8_t)10ULL): goto ma47; case ((uint8_t)11ULL): goto ma47; case ((uint8_t)12ULL): goto ma47; case ((uint8_t)13ULL): goto ma47; case ((uint8_t)14ULL): goto ma47; case ((uint8_t)15ULL): goto ma47; case ((uint8_t)16ULL): goto ma47; case ((uint8_t)17ULL): goto ma47; case ((uint8_t)18ULL): goto ma47; case ((uint8_t)19ULL): goto ma47; case ((uint8_t)20ULL): goto ma47; case ((uint8_t)21ULL): goto ma47; case ((uint8_t)22ULL): goto ma47; case ((uint8_t)23ULL): goto ma47; case ((uint8_t)24ULL): goto ma47; case ((uint8_t)25ULL): goto ma47; case ((uint8_t)26ULL): goto ma47; case ((uint8_t)27ULL): goto ma47; case ((uint8_t)28ULL): goto ma47; case ((uint8_t)29ULL): goto ma47; case ((uint8_t)30ULL): goto ma47; case ((uint8_t)31ULL): goto ma47; case ((uint8_t)32ULL): goto ma45; case ((uint8_t)33ULL): goto ma46; case ((uint8_t)34ULL): goto ma47; case ((uint8_t)35ULL): goto ma47; case ((uint8_t)36ULL): goto ma47; case ((uint8_t)37ULL): goto ma47; case ((uint8_t)38ULL): goto ma47; case ((uint8_t)39ULL): goto ma47; case ((uint8_t)40ULL): goto ma47; case ((uint8_t)41ULL): goto ma47; case ((uint8_t)42ULL): goto ma47; case ((uint8_t)43ULL): goto ma47; case ((uint8_t)44ULL): goto ma47; case ((uint8_t)45ULL): goto ma47; case ((uint8_t)46ULL): goto ma47; case ((uint8_t)47ULL): goto ma47; case ((uint8_t)48ULL): goto ma47; case ((uint8_t)49ULL): goto ma47; case ((uint8_t)50ULL): goto ma47; default: goto ma47; }
ma45:;
        uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
        if (t51 != ((uint8_t)32ULL)) goto mn50;
        void * t52 = GM_LD(void *, ((((uint8_t *)t42) + 8) + 8));
        gm_l12 = t52;
        void * t53 = gm_l12;
        gm_th2lqbo77pvsc t54 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t53) + 0));
        t43 = t54;
        goto me44;
mn50:;
ma46:;
        uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
        if (t56 != ((uint8_t)33ULL)) goto mn55;
        void * t57 = GM_LD(void *, ((((uint8_t *)t42) + 8) + 8));
        gm_l13 = t57;
        void * t58 = gm_l13;
        gm_th2lqbo77pvsc t59 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t58) + 8));
        t43 = t59;
        goto me44;
mn55:;
ma47:;
        gm_th2lqbo77pvsc t61;
        memset(&t61, 0, sizeof t61);
        t43 = t61;
        goto me44;
mn60:;
ma48:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 108, 28);
me44:;
        gm_l14 = t43;
    }
    {
        void * t62 = gm_l0;
        gm_f_scope_mark_i3b2mvo((void *)&gm_l15, t62);
    }
    {
        gm_f_list_go2qg0b((void *)&F.l16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_pmp06kq_2((void *)&F.l17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_th2lqbo77pvsc t64 = gm_l14;
            gm_th2lqbo77pvsc t65 = t64;
            gm_b16_8 t66;
            if (((((uint8_t *)&t65))[16] != 0)) {
                gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 0));
                t66 = t67;
            } else {
                gm_b16_8 t68 = gm_f_empty_slice_ito0pap();
                t66 = t68;
            }
            gm_l18 = t66;
        }
        {
            gm_l19 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t70 = gm_l19;
                uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t72 = (t70 < t71);
                if (!t72) goto lb69;
                {
                    uint64_t t75 = gm_l19;
                    uint64_t t76 = (uintptr_t)t75;
                    uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    gm_tf2orqcrphmek t80 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t76 * 56));
                    F.l20 = t80;
                    {
                        void * t82 = gm_l0;
                        uint64_t t83 = gm_l1;
                        void * t84 = (void *)((uint8_t *)&F.l20);
                        gm_tnnjeerolos0a t85 = gm_f_param_kind_of_1jngakt((void *)NULL, cx, NULL, t82, t83, t84);
                        if (gm_unw.kind) {
                            goto gm_ll73;
                        }
                        gm_l21 = t85;
                    }
                    {
                        {
                            gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                            gm_l22 = t87;
                        }
                        {
                            gm_l23 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t89 = gm_l23;
                                uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                uint8_t t91 = (t89 < t90);
                                if (!t91) goto lb88;
                                {
                                    uint64_t t94 = gm_l23;
                                    uint64_t t95 = (uintptr_t)t94;
                                    uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                    gm_t08olq97rscn3 t99 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t95 * 32));
                                    gm_l24 = t99;
                                    {
                                        void * t101 = gm_l0;
                                        gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 0));
                                        gm_tnnjeerolos0a t103 = gm_l21;
                                        uint32_t t104 = gm_f_new_param_7010dt4((void *)NULL, cx, NULL, t101, t102, t103);
                                        if (gm_unw.kind) {
                                            goto gm_ll92;
                                        }
                                        gm_l25 = t104;
                                    }
                                    {
                                        void * t105 = (void *)((uint8_t *)&F.l16);
                                        uint32_t t106 = gm_l25;
                                        gm_tftb1odc739t5 t107 = gm_f_push_im9d4ro(t105, t106);
                                        gm_tftb1odc739t5 t108 = t107;
                                        if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_src3k278vcmqgdr, 120, 13);
                                        gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
                                    }
                                    {
                                        void * t110 = (void *)((uint8_t *)&F.l17);
                                        void * t111 = GM_LD(void *, (((uint8_t *)&F.l20) + 32));
                                        gm_tftb1odc739t5 t112 = gm_f_push_r2k6rjf_2(t110, t111);
                                        gm_tftb1odc739t5 t113 = t112;
                                        if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 1, gm_src3k278vcmqgdr, 121, 13);
                                        gm_b0_1 t114 = GM_LD(gm_b0_1, (((uint8_t *)&t113) + 1));
                                    }
                                    {
                                        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                        if (t117 != ((uint8_t)2ULL)) goto mn116;
                                        void * t118 = gm_l0;
                                        void * t119 = (void *)(((uint8_t *)t118) + 1528);
                                        gm_tommf4j9upomc t120;
                                        memset((uint8_t *)&t120 + 20, 0, 4);
                                        gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 0));
                                        GM_ST(gm_b16_8, (uint8_t *)&t120 + 0, t121);
                                        uint32_t t122 = gm_l25;
                                        GM_ST(uint32_t, (uint8_t *)&t120 + 16, t122);
                                        gm_tftb1odc739t5 t123 = gm_f_push_sp4si0f(t119, &t120);
                                        gm_tftb1odc739t5 t124 = t123;
                                        if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 1, gm_src3k278vcmqgdr, 123, 30);
                                        gm_b0_1 t125 = GM_LD(gm_b0_1, (((uint8_t *)&t124) + 1));
                                        goto me115;
mn116:;
                                        void * t127 = gm_l0;
                                        void * t128 = (void *)(((uint8_t *)t127) + 1488);
                                        gm_tep2drsl9408b t129;
                                        memset((uint8_t *)&t129 + 20, 0, 4);
                                        gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 0));
                                        GM_ST(gm_b16_8, (uint8_t *)&t129 + 0, t130);
                                        void * t131 = gm_l0;
                                        void * t132 = (void *)(((uint8_t *)t131) + 8);
                                        uint32_t t133 = gm_l25;
                                        gm_t88rcucg1b7uk t134 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t132, t133);
                                        if (gm_unw.kind) {
                                            goto gm_ll92;
                                        }
                                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t129 + 16, t134);
                                        gm_tftb1odc739t5 t135 = gm_f_push_hv483nm(t128, &t129);
                                        gm_tftb1odc739t5 t136 = t135;
                                        if (((((uint8_t *)&t136))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t136) + 1, gm_src3k278vcmqgdr, 124, 22);
                                        gm_b0_1 t137 = GM_LD(gm_b0_1, (((uint8_t *)&t136) + 1));
                                        goto me115;
mn126:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 122, 13);
me115:;
                                    }
                                    goto me97;
mn98:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 118, 9);
me97:;
                                }
lc88:;
                                {
                                    uint64_t t139 = gm_l23;
                                    uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                                    uint64_t t142 = r140;
                                    gm_l23 = t142;
                                }
                            }
gm_ll92:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb88; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc88; }
                            }
                            goto gm_ll73;
lb88:;
                        }
                    }
                    goto me78;
mn79:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 116, 5);
me78:;
                }
lc69:;
                {
                    uint64_t t144 = gm_l19;
                    uint64_t r145; bool o146 = __builtin_add_overflow(t144, ((uint64_t)1ULL), &r145);
                    uint64_t t147 = r145;
                    gm_l19 = t147;
                }
            }
gm_ll73:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb69; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc69; }
            }
            goto gm_lad;
lb69:;
        }
    }
    {
        uint32_t t148 = gm_l10;
        uint64_t t149 = (uint64_t)t148;
        uint64_t t150 = (uintptr_t)t149;
        void * t151 = gm_l0;
        uint64_t t152 = GM_LD(uintptr_t, ((((uint8_t *)t151) + 1088) + 0) + 8);
        if (t150 >= (uintptr_t)(t152)) gm_trap_index(t150, t152, gm_src3k278vcmqgdr, 128, 5);
        gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t151) + 1088) + 0)) + t150 * 184) + 120), t153);
    }
    {
        uint32_t t154 = gm_l10;
        uint64_t t155 = (uint64_t)t154;
        uint64_t t156 = (uintptr_t)t155;
        void * t157 = gm_l0;
        uint64_t t158 = GM_LD(uintptr_t, ((((uint8_t *)t157) + 1088) + 0) + 8);
        if (t156 >= (uintptr_t)(t158)) gm_trap_index(t156, t158, gm_src3k278vcmqgdr, 129, 5);
        gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t157) + 1088) + 0)) + t156 * 184) + 136), t159);
    }
    {
        if (!((((uint8_t *)&gm_l2))[8] != 0)) goto ie160;
        uint64_t t161 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        gm_l26 = t161;
        {
            gm_t6qooom8qa2do t163;
            uint64_t t165 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
            uint8_t t166 = ((t165 == ((uint64_t)0ULL)));
            if (!t166) goto ie164;
            gm_t88rcucg1b7uk t168 = gm_l11;
            gm_t6qooom8qa2do t169;
            memset(&t169, 0, sizeof t169);
            GM_ST(uint8_t, ((uint8_t *)&t169 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t169 + 8), t168);
            gm_t6qooom8qa2do t170 = t169;
            t163 = t170;
            goto ix164;
ie164:;
            uint32_t t172 = gm_l10;
            gm_t6qooom8qa2do t173;
            memset(&t173, 0, sizeof t173);
            GM_ST(uint8_t, ((uint8_t *)&t173 + 0), ((uint8_t)1ULL));
            GM_ST(uint32_t, ((uint8_t *)&t173 + 8), t172);
            gm_t6qooom8qa2do t174 = t173;
            t163 = t174;
ix164:;
            F.l27 = t163;
        }
        {
            void * t175 = gm_l0;
            uint64_t t176 = gm_l26;
            gm_t6qooom8qa2do t177 = F.l27;
            gm_tagrkbqh054h7 t178;
            GM_ST(uint8_t, (uint8_t *)&t178 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t178 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t178 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t178 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&t178 + 8), t177);
            gm_b0_1 t179 = gm_f_set_state_3j300fi((void *)NULL, cx, NULL, t175, t176, &t178);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix160;
ie160:;
ix160:;
    }
    {
        void * t180 = gm_l0;
        void * t181 = (void *)(((uint8_t *)t180) + 1728);
        uint32_t t182 = gm_l10;
        gm_tftb1odc739t5 t183 = gm_f_push_im9d4ro(t181, t182);
        gm_tftb1odc739t5 t184 = t183;
        if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 1, gm_src3k278vcmqgdr, 135, 5);
        gm_b0_1 t185 = GM_LD(gm_b0_1, (((uint8_t *)&t184) + 1));
    }
    {
        void * t186 = gm_l0;
        void * t187 = gm_l0;
        uint32_t t188 = GM_LD(uint32_t, (((uint8_t *)t187) + 1768));
        uint32_t r189; bool o190 = __builtin_add_overflow(t188, ((uint32_t)1ULL), &r189);
        if (o190) gm_trap("integer overflow in `+`", gm_src3k278vcmqgdr, 136, 5);
        uint32_t t191 = r189;
        GM_ST(uint32_t, (((uint8_t *)t186) + 1768), t191);
    }
    {
        gm_f_empty_struct_b5i94nj((void *)&F.l28);
    }
    {
        gm_b16_8 t192 = gm_f_empty_slice_3jt8vgk();
        gm_l29 = t192;
    }
    {
        void * t193 = gm_l7;
        uint8_t t199 = GM_LD(uint8_t, ((((uint8_t *)t193) + 8) + 0));
        switch (t199) { case ((uint8_t)0ULL): goto ma197; case ((uint8_t)1ULL): goto ma197; case ((uint8_t)2ULL): goto ma197; case ((uint8_t)3ULL): goto ma197; case ((uint8_t)4ULL): goto ma197; case ((uint8_t)5ULL): goto ma197; case ((uint8_t)6ULL): goto ma197; case ((uint8_t)7ULL): goto ma197; case ((uint8_t)8ULL): goto ma197; case ((uint8_t)9ULL): goto ma197; case ((uint8_t)10ULL): goto ma197; case ((uint8_t)11ULL): goto ma197; case ((uint8_t)12ULL): goto ma197; case ((uint8_t)13ULL): goto ma197; case ((uint8_t)14ULL): goto ma197; case ((uint8_t)15ULL): goto ma197; case ((uint8_t)16ULL): goto ma197; case ((uint8_t)17ULL): goto ma197; case ((uint8_t)18ULL): goto ma197; case ((uint8_t)19ULL): goto ma197; case ((uint8_t)20ULL): goto ma197; case ((uint8_t)21ULL): goto ma197; case ((uint8_t)22ULL): goto ma197; case ((uint8_t)23ULL): goto ma197; case ((uint8_t)24ULL): goto ma197; case ((uint8_t)25ULL): goto ma197; case ((uint8_t)26ULL): goto ma197; case ((uint8_t)27ULL): goto ma197; case ((uint8_t)28ULL): goto ma197; case ((uint8_t)29ULL): goto ma197; case ((uint8_t)30ULL): goto ma197; case ((uint8_t)31ULL): goto ma197; case ((uint8_t)32ULL): goto ma195; case ((uint8_t)33ULL): goto ma196; case ((uint8_t)34ULL): goto ma197; case ((uint8_t)35ULL): goto ma197; case ((uint8_t)36ULL): goto ma197; case ((uint8_t)37ULL): goto ma197; case ((uint8_t)38ULL): goto ma197; case ((uint8_t)39ULL): goto ma197; case ((uint8_t)40ULL): goto ma197; case ((uint8_t)41ULL): goto ma197; case ((uint8_t)42ULL): goto ma197; case ((uint8_t)43ULL): goto ma197; case ((uint8_t)44ULL): goto ma197; case ((uint8_t)45ULL): goto ma197; case ((uint8_t)46ULL): goto ma197; case ((uint8_t)47ULL): goto ma197; case ((uint8_t)48ULL): goto ma197; case ((uint8_t)49ULL): goto ma197; case ((uint8_t)50ULL): goto ma197; default: goto ma197; }
ma195:;
        uint8_t t201 = GM_LD(uint8_t, ((((uint8_t *)t193) + 8) + 0));
        if (t201 != ((uint8_t)32ULL)) goto mn200;
        void * t202 = GM_LD(void *, ((((uint8_t *)t193) + 8) + 8));
        gm_l30 = t202;
        {
            void * t204 = gm_l0;
            uint64_t t205 = gm_l1;
            uint32_t t206 = gm_l10;
            void * t207 = gm_l30;
            gm_b16_8 t208 = gm_l6;
            gm_b16_8 t209 = t208;
            gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&t209));
            gm_f_struct_kind_5l1hs46((void *)&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l28)), (void *)NULL, cx, NULL, t204, t205, t206, t207, t210);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t211 = gm_l30;
            gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)t211) + 24));
            gm_l29 = t212;
        }
        goto me194;
mn200:;
ma196:;
        uint8_t t214 = GM_LD(uint8_t, ((((uint8_t *)t193) + 8) + 0));
        if (t214 != ((uint8_t)33ULL)) goto mn213;
        void * t215 = GM_LD(void *, ((((uint8_t *)t193) + 8) + 8));
        gm_l31 = t215;
        {
            void * t218 = gm_l31;
            void * t219 = GM_LD(void *, (((uint8_t *)t218) + 0));
            void * t220 = t219;
            uint8_t t221;
            if ((GM_LD(void *, ((uint8_t *)&t220)) != 0)) goto mn223;
            t221 = ((uint8_t)0);
            goto me222;
mn223:;
            t221 = ((uint8_t)1);
            goto me222;
mn224:;
            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 145, 16);
me222:;
            if (!t221) goto ie217;
            {
                uint32_t t226 = gm_l10;
                uint64_t t227 = (uint64_t)t226;
                uint64_t t228 = (uintptr_t)t227;
                void * t229 = gm_l0;
                uint64_t t230 = GM_LD(uintptr_t, ((((uint8_t *)t229) + 1088) + 0) + 8);
                if (t228 >= (uintptr_t)(t230)) gm_trap_index(t228, t230, gm_src3k278vcmqgdr, 145, 41);
                GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t229) + 1088) + 0)) + t228 * 184) + 152), ((uint8_t)1));
            }
            goto ix217;
ie217:;
ix217:;
        }
        {
            void * t231 = gm_l0;
            uint64_t t232 = gm_l1;
            void * t233 = gm_l31;
            gm_b16_8 t234 = gm_l6;
            gm_b16_8 t235 = t234;
            gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
            gm_f_union_kind_j2gcmto((void *)&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l28)), (void *)NULL, cx, NULL, t231, t232, t233, t236);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t237 = gm_l31;
            gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)t237) + 32));
            gm_l29 = t238;
        }
        goto me194;
mn213:;
ma197:;
        {
            void * t241 = gm_l0;
            void * t242 = gm_l4;
            gm_tl874t2atnlh3 t243 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t242) + 72));
            gm_b16_8 t244;
            GM_ST(void *, (uint8_t *)&t244, (void *)(gm_s4ko774bb2ea9));
            GM_ST(uintptr_t, (uint8_t *)&t244 + 8, (uintptr_t)(55));
            gm_b0_1 t245 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t241, t243, t244);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t246 = gm_l0;
            uint32_t t247 = gm_l10;
            gm_f_remove_in_progress_lrie478(t246, t247);
        }
        {
            void * t248 = gm_l0;
            gm_b0_1 t249 = gm_f_end_constraint_defer_52jt3m9((void *)NULL, cx, NULL, t248);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t250 = gm_l0;
            void * t251 = (void *)(((uint8_t *)t250) + 8);
            gm_t88rcucg1b7uk t252 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t251);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t253 = 0;
            F.ret = t252;
            goto gm_fret;
        }
        goto me194;
mn239:;
ma198:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 139, 5);
me194:;
    }
    {
        void * t254 = gm_l0;
        uint32_t t255 = gm_l10;
        gm_f_remove_in_progress_lrie478(t254, t255);
    }
    {
        uint32_t t256 = gm_l10;
        uint64_t t257 = (uint64_t)t256;
        uint64_t t258 = (uintptr_t)t257;
        void * t259 = gm_l0;
        uint64_t t260 = GM_LD(uintptr_t, ((((uint8_t *)t259) + 1088) + 0) + 8);
        if (t258 >= (uintptr_t)(t260)) gm_trap_index(t258, t260, gm_src3k278vcmqgdr, 157, 5);
        gm_ti0kgh3pc3in2 t261 = F.l28;
        GM_ST(gm_ti0kgh3pc3in2, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t259) + 1088) + 0)) + t258 * 184) + 24), t261);
    }
    {
        {
            gm_b16_8 t263 = gm_l29;
            gm_l32 = t263;
        }
        {
            gm_l33 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t265 = gm_l33;
                uint64_t t266 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                uint8_t t267 = (t265 < t266);
                if (!t267) goto lb264;
                {
                    uint64_t t270 = gm_l33;
                    uint64_t t271 = (uintptr_t)t270;
                    uint64_t t272 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                    gm_tpv68m88f9lcr t275 = GM_LD(gm_tpv68m88f9lcr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t271 * 56));
                    F.l34 = t275;
                    {
                        void * t277 = gm_l0;
                        uint64_t t278 = gm_l1;
                        void * t279 = (void *)((uint8_t *)&F.l34);
                        gm_b0_1 t280 = gm_f_declare_constraint_s1juggf((void *)NULL, cx, NULL, t277, t278, t279);
                        if (gm_unw.kind) {
                            goto gm_ll268;
                        }
                    }
                    goto me273;
mn274:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 158, 5);
me273:;
                }
lc264:;
                {
                    uint64_t t282 = gm_l33;
                    uint64_t r283; bool o284 = __builtin_add_overflow(t282, ((uint64_t)1ULL), &r283);
                    uint64_t t285 = r283;
                    gm_l33 = t285;
                }
            }
gm_ll268:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb264; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc264; }
            }
            goto gm_lad;
lb264:;
        }
    }
    {
        void * t286 = gm_l0;
        gm_b0_1 t287 = gm_f_end_constraint_defer_52jt3m9((void *)NULL, cx, NULL, t286);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t288 = gm_l0;
        gm_f_restore_scope_edqq1v8(t288, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l15)));
    }
    {
        uint64_t t290 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
        uint8_t t291 = (t290 > ((uint64_t)0ULL));
        if (!t291) goto ie289;
        {
            gm_t88rcucg1b7uk t293 = gm_l11;
            uint8_t t294 = 0;
            F.ret = t293;
            goto gm_fret;
        }
        goto ix289;
ie289:;
ix289:;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l35, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t296 = gm_l0;
        gm_t88rcucg1b7uk t297 = gm_l11;
        void * t298 = (void *)((uint8_t *)&F.l35);
        uint8_t t299 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t296, t297, t298);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t299) goto ie295;
        {
            void * t301 = gm_l0;
            void * t302 = gm_l4;
            gm_tl874t2atnlh3 t303 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t302) + 72));
            gm_b16_8 t304 = gm_l3;
            gm_tnpifv20irnph t305;
            GM_ST(gm_b16_8, ((uint8_t *)&t305 + 0), t304);
            gm_tabb710d9mq2e t306;
            gm_f_format_0au88qf((void *)&t306, (void *)NULL, cx, NULL, t305);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t307 = t306;
            if (((((uint8_t *)&t307))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t307) + 8, gm_src3k278vcmqgdr, 164, 27);
            gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&t307) + 8));
            gm_b16_8 t309 = t308;
            gm_b16_8 t310 = GM_LD(gm_b16_8, ((uint8_t *)&t309));
            gm_b0_1 t311 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t301, t303, t310);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t312 = gm_l0;
            uint32_t t313 = gm_l10;
            gm_f_empty_adt_tdp3hro(t312, t313);
        }
        {
            void * t314 = gm_l0;
            void * t315 = (void *)(((uint8_t *)t314) + 8);
            gm_t88rcucg1b7uk t316 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t315);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t317 = 0;
            F.ret = t316;
            goto gm_fret;
        }
        goto ix295;
ie295:;
ix295:;
    }
    {
        void * t319 = gm_l0;
        gm_t88rcucg1b7uk t320 = gm_l11;
        void * t321 = gm_l4;
        gm_tl874t2atnlh3 t322 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t321) + 72));
        uint8_t t323 = gm_f_type_size_ok_tr1llg4((void *)NULL, cx, NULL, t319, t320, t322);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t324 = (uint8_t)!t323;
        if (!t324) goto ie318;
        {
            void * t326 = gm_l0;
            uint32_t t327 = gm_l10;
            gm_f_empty_adt_tdp3hro(t326, t327);
        }
        {
            void * t328 = gm_l0;
            void * t329 = (void *)(((uint8_t *)t328) + 8);
            gm_t88rcucg1b7uk t330 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t329);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t331 = 0;
            F.ret = t330;
            goto gm_fret;
        }
        goto ix318;
ie318:;
ix318:;
    }
    {
        gm_t88rcucg1b7uk t332 = gm_l11;
        uint8_t t333 = 0;
        F.ret = t332;
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_local_adt_6j4pdai(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_declare_local_adt_6j4pdai F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    void * gm_l3; /* lit */
    void * gm_l4; /* inner */
    void * gm_l5; /* i */
    void * gm_l6; /* st */
    void * gm_l7; /* u */
    uint8_t gm_l8; /* generic */
    gm_t88rcucg1b7uk gm_l9; /* ty */
    uint32_t gm_l10; /* id */
    GM_STACK_CHECK(&F, 682u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l3;
        gm_l4 = t2;
    }
    {
        void * t4 = gm_l3;
        uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t5 != ((uint8_t)35ULL)) goto ie3;
        void * t6 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l5 = t6;
        {
            void * t8 = gm_l5;
            gm_l4 = t8;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        void * t9 = gm_l4;
        uint8_t t10;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma14; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma14; case ((uint8_t)13ULL): goto ma14; case ((uint8_t)14ULL): goto ma14; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma14; case ((uint8_t)18ULL): goto ma14; case ((uint8_t)19ULL): goto ma14; case ((uint8_t)20ULL): goto ma14; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma14; case ((uint8_t)27ULL): goto ma14; case ((uint8_t)28ULL): goto ma14; case ((uint8_t)29ULL): goto ma14; case ((uint8_t)30ULL): goto ma14; case ((uint8_t)31ULL): goto ma14; case ((uint8_t)32ULL): goto ma12; case ((uint8_t)33ULL): goto ma13; case ((uint8_t)34ULL): goto ma14; case ((uint8_t)35ULL): goto ma14; case ((uint8_t)36ULL): goto ma14; case ((uint8_t)37ULL): goto ma14; case ((uint8_t)38ULL): goto ma14; case ((uint8_t)39ULL): goto ma14; case ((uint8_t)40ULL): goto ma14; case ((uint8_t)41ULL): goto ma14; case ((uint8_t)42ULL): goto ma14; case ((uint8_t)43ULL): goto ma14; case ((uint8_t)44ULL): goto ma14; case ((uint8_t)45ULL): goto ma14; case ((uint8_t)46ULL): goto ma14; case ((uint8_t)47ULL): goto ma14; case ((uint8_t)48ULL): goto ma14; case ((uint8_t)49ULL): goto ma14; case ((uint8_t)50ULL): goto ma14; default: goto ma14; }
ma12:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t18 != ((uint8_t)32ULL)) goto mn17;
        void * t19 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
        gm_l6 = t19;
        void * t20 = gm_l6;
        uint8_t t21;
        if ((((((uint8_t *)t20) + 0))[16] != 0)) goto mn23;
        t21 = ((uint8_t)0);
        goto me22;
mn23:;
        t21 = ((uint8_t)1);
        goto me22;
mn24:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 53, 24);
me22:;
        t10 = t21;
        goto me11;
mn17:;
ma13:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t26 != ((uint8_t)33ULL)) goto mn25;
        void * t27 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
        gm_l7 = t27;
        void * t28 = gm_l7;
        uint8_t t29;
        if ((((((uint8_t *)t28) + 8))[16] != 0)) goto mn31;
        t29 = ((uint8_t)0);
        goto me30;
mn31:;
        t29 = ((uint8_t)1);
        goto me30;
mn32:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 54, 22);
me30:;
        t10 = t29;
        goto me11;
mn25:;
ma14:;
        t10 = ((uint8_t)0);
        goto me11;
mn33:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 52, 16);
me11:;
        gm_l8 = t10;
    }
    {
        uint8_t t35 = gm_l8;
        if (!t35) goto ie34;
        {
            void * t37 = gm_l0;
            void * t38 = gm_l3;
            gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 72));
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sf7l7ca9coehi));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(84));
            gm_b0_1 t41 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t37, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t42 = gm_l0;
            gm_b16_8 t43 = gm_l2;
            void * t44 = gm_l0;
            void * t45 = (void *)(((uint8_t *)t44) + 8);
            gm_t88rcucg1b7uk t46 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t47 = gm_f_push_type_alias_rr9mg3i((void *)NULL, cx, NULL, t42, t43, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t48 = gm_l0;
            void * t49 = (void *)(((uint8_t *)t48) + 8);
            gm_t88rcucg1b7uk t50 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t51 = 0;
            F.ret = t50;
            goto gm_fret;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        void * t52 = gm_l0;
        uint64_t t53 = gm_l1;
        gm_tf5srh4mp4b3i t54;
        memset(&t54, 0, sizeof t54);
        gm_b16_8 t55 = gm_l2;
        void * t56 = gm_l3;
        gm_t88rcucg1b7uk t57 = gm_f_declare_adt_in_74tov03((void *)NULL, cx, NULL, t52, t53, t54, t55, t56, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t57;
    }
    {
        void * t59 = gm_l0;
        void * t60 = (void *)(((uint8_t *)t59) + 8);
        gm_t88rcucg1b7uk t61 = gm_l9;
        gm_t53at80pfdus3 t62;
        gm_f_kind_jcnt4hm((void *)&t62, t60, GM_CONV(gm_csguankko4lmhd, t61));
        gm_t53at80pfdus3 t63 = t62;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
        if (t64 != ((uint8_t)17ULL)) goto ie58;
        uint32_t t65 = GM_LD(uint32_t, ((((uint8_t *)&t63) + 8) + 0));
        gm_l10 = t65;
        void * t66 = gm_l0;
        uint32_t t67 = gm_l10;
        uint8_t t68 = gm_f_adt_mentions_param_0jsjgma(t66, t67);
        if (!t68) goto ie58;
        {
            void * t70 = gm_l0;
            void * t71 = gm_l3;
            gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t71) + 72));
            gm_b16_8 t73 = gm_l2;
            gm_tnpifv20irnph t74;
            GM_ST(gm_b16_8, ((uint8_t *)&t74 + 0), t73);
            gm_tabb710d9mq2e t75;
            gm_f_format_otdvqpt((void *)&t75, (void *)NULL, cx, NULL, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t76 = t75;
            if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 8, gm_src3k278vcmqgdr, 64, 27);
            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 8));
            gm_b16_8 t78 = t77;
            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
            gm_b0_1 t80 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t70, t72, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t81 = gm_l0;
            uint32_t t82 = gm_l10;
            gm_f_empty_adt_tdp3hro(t81, t82);
        }
        {
            void * t83 = gm_l0;
            void * t84 = (void *)(((uint8_t *)t83) + 8);
            gm_t88rcucg1b7uk t85 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t86 = 0;
            F.ret = t85;
            goto gm_fret;
        }
        goto ix58;
ie58:;
ix58:;
    }
    {
        gm_t88rcucg1b7uk t87 = gm_l9;
        uint8_t t88 = 0;
        F.ret = t87;
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
inline __attribute__((aligned(4))) void gm_f_empty_adt_tdp3hro(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_adt_tdp3hro F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1088) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_src3k278vcmqgdr, 182, 5);
        uint8_t *t7 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24);
        gm_f_empty_struct_b5i94nj((void *)&(*(gm_ti0kgh3pc3in2 *)t7));
    }
    {
        uint32_t t8 = gm_l1;
        uint64_t t9 = (uint64_t)t8;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 1088) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_src3k278vcmqgdr, 183, 5);
        GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t11) + 1088) + 0)) + t10 * 184) + 112), ((uint8_t)0));
    }
    {
        uint32_t t13 = gm_l1;
        uint64_t t14 = (uint64_t)t13;
        uint64_t t15 = (uintptr_t)t14;
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 1088) + 0) + 8);
        if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_src3k278vcmqgdr, 184, 5);
        uint8_t *t18 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1088) + 0)) + t15 * 184) + 80);
        gm_b16_8 t19 = gm_f_empty_slice_s1lbe5s();
        GM_ST(gm_b16_8, t18, t19);
    }
    {
        uint32_t t20 = gm_l1;
        uint64_t t21 = (uint64_t)t20;
        uint64_t t22 = (uintptr_t)t21;
        void * t23 = gm_l0;
        uint64_t t24 = GM_LD(uintptr_t, ((((uint8_t *)t23) + 1088) + 0) + 8);
        if (t22 >= (uintptr_t)(t24)) gm_trap_index(t22, t24, gm_src3k278vcmqgdr, 185, 5);
        uint8_t *t25 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t23) + 1088) + 0)) + t22 * 184) + 96);
        gm_b16_8 t26 = gm_f_empty_slice_8q3kspo_2();
        GM_ST(gm_b16_8, t25, t26);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_empty_struct_b5i94nj(void *gm_out);
__attribute__((aligned(4))) gm_ti0kgh3pc3in2 gm_f_empty_struct_b5i94nj_c(void) { gm_ti0kgh3pc3in2 gm_r; gm_f_empty_struct_b5i94nj(&gm_r); return gm_r; }
inline __attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_field_bits_2ktfm1k F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* w */
    GM_STACK_CHECK(&F, 190u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_tf5srh4mp4b3i t5 = gm_f_bit_width_b7ofh2h(t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_tf5srh4mp4b3i t6 = t5;
        if (!((((uint8_t *)&t6))[8] != 0)) goto ie2;
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&t6));
        gm_l2 = t7;
        {
            uint64_t t9 = gm_l2;
            uint32_t t10 = (uint32_t)t9;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        gm_t88rcucg1b7uk t13 = gm_l1;
        gm_tbccvikg6b96e t14;
        gm_f_layout_a5ovnrm((void *)&t14, (void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbccvikg6b96e t15 = t14;
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
        uint64_t r17; bool o18 = __builtin_mul_overflow(((uint64_t)8ULL), t16, &r17);
        if (o18) gm_trap("integer overflow in `*`", gm_src3k278vcmqgdr, 308, 13);
        uint64_t t19 = r17;
        uint32_t t20 = (uint32_t)t19;
        uint8_t t21 = 0;
        F.ret = t20;
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_flatten_2js7j92(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_flatten_2js7j92 F;
    void * gm_l0; /* s */
    gm_tnpifv20irnph gm_l1; /* set */
    uint8_t gm_l2; /* spliced */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tiilmfd16v3fc gm_l6; /* e */
    gm_t88rcucg1b7uk gm_l7; /* t */
    gm_tnpifv20irnph gm_l8; /* inner */
    GM_STACK_CHECK(&F, 530u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint8_t)0);
    }
    {
        gm_f_list_084fcd6((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 0));
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_tiilmfd16v3fc t15 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 24));
                    gm_l6 = t15;
                    {
                        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                        uint8_t t19 = ((t18 == ((uint32_t)0ULL)));
                        if (!t19) goto ie17;
                        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)&gm_l6) + 8) + 8);
                        uint8_t t21 = (t20 > ((uint64_t)0ULL));
                        if (!t21) goto ie17;
                        {
                            uint64_t t24 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t25 = GM_LD(uintptr_t, (((uint8_t *)&gm_l6) + 8) + 8);
                            if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_src3k278vcmqgdr, 780, 28);
                            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l6) + 8)) + t24 * 40) + 0));
                            if (t26 != ((uint8_t)0ULL)) goto ie23;
                            gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l6) + 8)) + t24 * 40) + 8));
                            gm_l7 = t27;
                            void * t28 = gm_l0;
                            void * t29 = (void *)(((uint8_t *)t28) + 8);
                            gm_t88rcucg1b7uk t30 = gm_l7;
                            gm_t53at80pfdus3 t31;
                            gm_f_kind_jcnt4hm((void *)&t31, t29, GM_CONV(gm_csguankko4lmhd, t30));
                            gm_t53at80pfdus3 t32 = t31;
                            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t32) + 0));
                            if (t33 != ((uint8_t)24ULL)) goto ie23;
                            gm_tnpifv20irnph t34 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&t32) + 8));
                            gm_l8 = t34;
                            {
                                void * t36 = (void *)((uint8_t *)&F.l3);
                                gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                                gm_tftb1odc739t5 t38 = gm_f_extend_74ac4hb(t36, t37);
                                gm_tftb1odc739t5 t39 = t38;
                                if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_src3k278vcmqgdr, 781, 17);
                                gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
                            }
                            {
                                gm_l2 = ((uint8_t)1);
                            }
                            {
                                goto lc4;
                            }
                            goto ix23;
ie23:;
ix23:;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    {
                        void * t41 = (void *)((uint8_t *)&F.l3);
                        gm_tftb1odc739t5 t42 = gm_f_push_d1r4h19(t41, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l6)));
                        gm_tftb1odc739t5 t43 = t42;
                        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_src3k278vcmqgdr, 786, 9);
                        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 778, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t46 = gm_l5;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l5 = t49;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t51 = gm_l2;
        uint8_t t52 = (uint8_t)!t51;
        if (!t52) goto ie50;
        {
            gm_tnpifv20irnph t54 = gm_l1;
            uint8_t t55 = 0;
            F.ret = t54;
            goto gm_fret;
        }
        goto ix50;
ie50:;
ix50:;
    }
    {
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t57 = t56;
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
        gm_tnpifv20irnph t59 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t60 = 0;
        F.ret = t59;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_instance_args_15ek7cf(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_instance_args_15ek7cf F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* inst */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t5g5vgung6h6s gm_l5; /* i */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l8; /* a */
    GM_STACK_CHECK(&F, 400u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_vmvl81b((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 1168) + 0));
            gm_l3 = t4;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t5g5vgung6h6s t16 = GM_LD(gm_t5g5vgung6h6s, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 32));
                    gm_l5 = t16;
                    {
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 24));
                        uint32_t t20 = gm_l1;
                        uint8_t t21 = ((t19 == t20));
                        if (!t21) goto ie18;
                        {
                            {
                                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                                gm_l6 = t24;
                            }
                            {
                                gm_l7 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t26 = gm_l7;
                                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    uint8_t t28 = (t26 < t27);
                                    if (!t28) goto lb25;
                                    {
                                        uint64_t t31 = gm_l7;
                                        uint64_t t32 = (uintptr_t)t31;
                                        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t32 * 4));
                                        gm_l8 = t36;
                                        {
                                            void * t38 = (void *)((uint8_t *)&F.l2);
                                            gm_t88rcucg1b7uk t39 = gm_l8;
                                            gm_toa2vhe3l4hfr t40;
                                            memset(&t40, 0, sizeof t40);
                                            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)0ULL));
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t40 + 8), t39);
                                            gm_tftb1odc739t5 t41 = gm_f_push_s5vr2a2(t38, &t40);
                                            gm_tftb1odc739t5 t42 = t41;
                                            if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_src3k278vcmqgdr, 631, 31);
                                            gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
                                        }
                                        goto me34;
mn35:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 631, 13);
me34:;
                                    }
lc25:;
                                    {
                                        uint64_t t45 = gm_l7;
                                        uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                                        uint64_t t48 = r46;
                                        gm_l7 = t48;
                                    }
                                }
lb25:;
                            }
                        }
                        {
                            goto lb5;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 629, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t50 = gm_l4;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    uint64_t t53 = r51;
                    gm_l4 = t53;
                }
            }
lb5:;
        }
    }
    {
        gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        uint8_t t55 = 0;
        F.ret = t54;
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_instantiate_adt_lbjcui1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_instantiate_adt_lbjcui1 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* template */
    gm_b16_8 gm_l2; /* args */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* a */
    uint32_t gm_l7; /* inst */
    uint32_t gm_l9; /* inst */
    gm_t88rcucg1b7uk gm_l11; /* ty */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint32_t gm_l17; /* p */
    gm_t88rcucg1b7uk gm_l19; /* tag */
    gm_b16_8 gm_l20; /* variants */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_tkn42u7na3t81 gm_l25; /* payload */
    gm_t88rcucg1b7uk gm_l26; /* p */
    gm_b16_8 gm_l27; /* fields */
    uint8_t gm_l29; /* concrete */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    gm_t88rcucg1b7uk gm_l32; /* a */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    gm_t5g5vgung6h6s gm_l37; /* i */
    gm_t88rcucg1b7uk gm_l39; /* t */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    gm_t5g5vgung6h6s gm_l42; /* i */
    gm_t88rcucg1b7uk gm_l43; /* t */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_t5g5vgung6h6s gm_l46; /* i */
    gm_t88rcucg1b7uk gm_l47; /* t */
    GM_STACK_CHECK(&F, 4412u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_vmvl81b((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 4));
                    gm_l6 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l3);
                        gm_t88rcucg1b7uk t18 = gm_l6;
                        gm_toa2vhe3l4hfr t19;
                        memset(&t19, 0, sizeof t19);
                        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t19 + 8), t18);
                        gm_tftb1odc739t5 t20 = gm_f_push_s5vr2a2(t17, &t19);
                        gm_tftb1odc739t5 t21 = t20;
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_src3k278vcmqgdr, 549, 21);
                        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 549, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t24 = gm_l5;
                    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                    uint64_t t27 = r25;
                    gm_l5 = t27;
                }
            }
lb4:;
        }
    }
    {
        void * t29 = gm_l0;
        void * t30 = (void *)(((uint8_t *)t29) + 1208);
        gm_tiilmfd16v3fc t31;
        memset((uint8_t *)&t31 + 4, 0, 4);
        uint32_t t32 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t31 + 0, t32);
        gm_b16_8 t33 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t31 + 8, t33);
        gm_t9805qtonbsag t34 = gm_f_get_6qnso5a(t30, &t31);
        gm_t9805qtonbsag t35 = t34;
        if (!((((uint8_t *)&t35))[4] != 0)) goto ie28;
        uint32_t t36 = GM_LD(uint32_t, ((uint8_t *)&t35));
        gm_l7 = t36;
        {
            void * t38 = gm_l0;
            void * t39 = (void *)(((uint8_t *)t38) + 8);
            uint32_t t40 = gm_l7;
            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t42 = t41;
            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
            gm_t88rcucg1b7uk t44 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t39, t40, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t47 = gm_l0;
        uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)t47) + 1816));
        uint8_t t49 = (t48 >= ((uint32_t)64ULL));
        if (!t49) goto ie46;
        {
            void * t52 = gm_l0;
            uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)t52) + 1864));
            uint8_t t54 = (uint8_t)!t53;
            if (!t54) goto ie51;
            {
                void * t56 = gm_l0;
                GM_ST(uint8_t, (((uint8_t *)t56) + 1864), ((uint8_t)1));
            }
            {
                void * t57 = gm_l0;
                uint32_t t58 = gm_l1;
                uint64_t t59 = (uint64_t)t58;
                uint64_t t60 = (uintptr_t)t59;
                void * t61 = gm_l0;
                uint64_t t62 = GM_LD(uintptr_t, ((((uint8_t *)t61) + 1088) + 0) + 8);
                if (t60 >= (uintptr_t)(t62)) gm_trap_index(t60, t62, gm_src3k278vcmqgdr, 556, 21);
                gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t61) + 1088) + 0)) + t60 * 184) + 156));
                gm_t0bf4lga76ktk t64;
                memset((uint8_t *)&t64 + 20, 0, 4);
                uint32_t t65 = gm_l1;
                uint64_t t66 = (uint64_t)t65;
                uint64_t t67 = (uintptr_t)t66;
                void * t68 = gm_l0;
                uint64_t t69 = GM_LD(uintptr_t, ((((uint8_t *)t68) + 1088) + 0) + 8);
                if (t67 >= (uintptr_t)(t69)) gm_trap_index(t67, t69, gm_src3k278vcmqgdr, 556, 156);
                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t68) + 1088) + 0)) + t67 * 184) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t64 + 0, t70);
                GM_ST(int32_t, (uint8_t *)&t64 + 16, ((int32_t)64LL));
                gm_tabb710d9mq2e t71;
                gm_f_format_bsjq7um((void *)&t71, (void *)NULL, cx, NULL, &t64);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t72 = t71;
                if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_src3k278vcmqgdr, 556, 57);
                gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
                gm_b16_8 t74 = t73;
                gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                gm_b0_1 t76 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t57, t63, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix51;
ie51:;
ix51:;
        }
        {
            void * t77 = gm_l0;
            void * t78 = (void *)(((uint8_t *)t77) + 8);
            gm_t88rcucg1b7uk t79 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t80 = 0;
            F.ret = t79;
            goto gm_fret;
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        uint32_t t81 = gm_l1;
        uint64_t t82 = (uint64_t)t81;
        uint64_t t83 = (uintptr_t)t82;
        void * t84 = gm_l0;
        uint64_t t85 = GM_LD(uintptr_t, ((((uint8_t *)t84) + 1088) + 0) + 8);
        if (t83 >= (uintptr_t)(t85)) gm_trap_index(t83, t85, gm_src3k278vcmqgdr, 560, 13);
        gm_t6art7bprjiqb t86 = GM_LD(gm_t6art7bprjiqb, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t84) + 1088) + 0)) + t83 * 184));
        F.l8 = t86;
    }
    {
        void * t87 = gm_l0;
        void * t88 = (void *)(((uint8_t *)t87) + 8);
        gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        uint32_t t90 = gm_f_fresh_adt_u9rnf9f((void *)NULL, cx, NULL, t88, t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t90;
    }
    {
        void * t91 = gm_l0;
        void * t92 = (void *)(((uint8_t *)t91) + 8);
        uint32_t t93 = gm_l9;
        uint32_t t94 = gm_l1;
        gm_f_set_template_o09q96o(t92, t93, t94);
    }
    {
        gm_f_list_nof7vfj((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t95 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t96 = gm_l2;
        gm_tftb1odc739t5 t97 = gm_f_extend_lfqasgq(t95, t96);
        gm_tftb1odc739t5 t98 = t97;
        if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 1, gm_src3k278vcmqgdr, 564, 5);
        gm_b0_1 t99 = GM_LD(gm_b0_1, (((uint8_t *)&t98) + 1));
    }
    {
        void * t100 = gm_l0;
        void * t101 = (void *)(((uint8_t *)t100) + 1168);
        gm_t5g5vgung6h6s t102;
        memset((uint8_t *)&t102 + 4, 0, 4);
        memset((uint8_t *)&t102 + 28, 0, 4);
        uint32_t t103 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t102 + 0, t103);
        gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t102 + 8, t104);
        uint32_t t105 = gm_l9;
        GM_ST(uint32_t, (uint8_t *)&t102 + 24, t105);
        gm_tftb1odc739t5 t106 = gm_f_push_u4kbe6d(t101, &t102);
        gm_tftb1odc739t5 t107 = t106;
        if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_src3k278vcmqgdr, 565, 5);
        gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
    }
    {
        void * t109 = gm_l0;
        void * t110 = (void *)(((uint8_t *)t109) + 1208);
        gm_tiilmfd16v3fc t111;
        memset((uint8_t *)&t111 + 4, 0, 4);
        uint32_t t112 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t111 + 0, t112);
        gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        gm_b16_8 t114 = t113;
        gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
        GM_ST(gm_b16_8, (uint8_t *)&t111 + 8, t115);
        uint32_t t116 = gm_l9;
        gm_tftb1odc739t5 t117 = gm_f_put_f1iksvi(t110, &t111, t116);
        gm_tftb1odc739t5 t118 = t117;
        if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 1, gm_src3k278vcmqgdr, 566, 5);
        gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 1));
    }
    {
        void * t120 = gm_l0;
        void * t121 = (void *)(((uint8_t *)t120) + 8);
        uint32_t t122 = gm_l9;
        gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t124 = t123;
        gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
        gm_t88rcucg1b7uk t126 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t121, t122, t125);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t126;
    }
    {
        gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 16));
        gm_ti0kgh3pc3in2 t129;
        gm_f_empty_struct_b5i94nj((void *)&t129);
        gm_tl874t2atnlh3 t130 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 156));
        gm_tf5srh4mp4b3i t131;
        memset(&t131, 0, sizeof t131);
        gm_f_new_adt_info_5pl2srl((void *)&F.l12, t127, t128, &t129, GM_CONV(gm_cseahvg8v7p0vs, t130), t131);
    }
    {
        gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 80));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l12) + 80), t132);
    }
    {
        gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 96));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l12) + 96), t133);
    }
    {
        uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 112));
        GM_ST(uint8_t, (((uint8_t *)&F.l12) + 112), t134);
    }
    {
        uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 152));
        GM_ST(uint8_t, (((uint8_t *)&F.l12) + 152), t135);
    }
    {
        void * t136 = gm_l0;
        void * t137 = (void *)(((uint8_t *)t136) + 1088);
        gm_tftb1odc739t5 t138 = gm_f_push_j84ih6c(t137, &(*(gm_t6art7bprjiqb *)((uint8_t *)&F.l12)));
        gm_tftb1odc739t5 t139 = t138;
        if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 1, gm_src3k278vcmqgdr, 573, 5);
        gm_b0_1 t140 = GM_LD(gm_b0_1, (((uint8_t *)&t139) + 1));
    }
    {
        gm_f_list_oalq03d((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 120));
            gm_b16_8 t143 = t142;
            gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
            gm_l14 = t144;
        }
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t146 = gm_l15;
                uint64_t t147 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint8_t t148 = (t146 < t147);
                if (!t148) goto lb145;
                {
                    uint64_t t151 = gm_l15;
                    uint64_t t152 = gm_l15;
                    uint64_t t153 = (uintptr_t)t152;
                    uint64_t t154 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint32_t t155 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t153 * 4));
                    gm_tn68el8d14vku t156;
                    GM_ST(uint32_t, (uint8_t *)&t156 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t156 + 0), t151);
                    GM_ST(uint32_t, ((uint8_t *)&t156 + 8), t155);
                    gm_tn68el8d14vku t157 = t156;
                    uint64_t t160 = GM_LD(uint64_t, (((uint8_t *)&t157) + 0));
                    gm_l16 = t160;
                    uint32_t t161 = GM_LD(uint32_t, (((uint8_t *)&t157) + 8));
                    gm_l17 = t161;
                    {
                        uint64_t t164 = gm_l16;
                        uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        uint8_t t166 = (t164 < t165);
                        if (!t166) goto ie163;
                        {
                            void * t168 = (void *)((uint8_t *)&F.l13);
                            uint32_t t169 = gm_l17;
                            uint64_t t170 = gm_l16;
                            uint64_t t171 = (uintptr_t)t170;
                            uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t171 >= (uintptr_t)(t172)) gm_trap_index(t171, t172, gm_src3k278vcmqgdr, 576, 56);
                            gm_t88rcucg1b7uk t173 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t171 * 4));
                            gm_tplpbqf3duvn3 t174;
                            GM_ST(uint32_t, ((uint8_t *)&t174 + 0), t169);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t174 + 4), t173);
                            gm_tftb1odc739t5 t175 = gm_f_push_h69n5fi(t168, GM_CONV(gm_cs7e9ppsd384bj, t174));
                            gm_tftb1odc739t5 t176 = t175;
                            if (((((uint8_t *)&t176))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t176) + 1, gm_src3k278vcmqgdr, 576, 27);
                            gm_b0_1 t177 = GM_LD(gm_b0_1, (((uint8_t *)&t176) + 1));
                        }
                        goto ix163;
ie163:;
ix163:;
                    }
                    goto me158;
mn159:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 575, 5);
me158:;
                }
lc145:;
                {
                    uint64_t t179 = gm_l15;
                    uint64_t r180; bool o181 = __builtin_add_overflow(t179, ((uint64_t)1ULL), &r180);
                    uint64_t t182 = r180;
                    gm_l15 = t182;
                }
            }
lb145:;
        }
    }
    {
        void * t183 = gm_l0;
        gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        gm_b16_8 t185 = t184;
        gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
        gm_toceffb697p0u t187;
        memset(&t187, 0, sizeof t187);
        gm_b0_1 t188 = gm_f_check_len_exprs_m8s6m0h((void *)NULL, cx, NULL, t183, t186, t187);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t189 = gm_l0;
        void * t190 = gm_l0;
        uint32_t t191 = GM_LD(uint32_t, (((uint8_t *)t190) + 1816));
        uint32_t r192; bool o193 = __builtin_add_overflow(t191, ((uint32_t)1ULL), &r192);
        if (o193) gm_trap("integer overflow in `+`", gm_src3k278vcmqgdr, 579, 5);
        uint32_t t194 = r192;
        GM_ST(uint32_t, (((uint8_t *)t189) + 1816), t194);
    }
    {
        gm_ti0kgh3pc3in2 t195;
        uint8_t t201 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 24) + 0));
        switch (t201) { case ((uint8_t)0ULL): goto ma197; case ((uint8_t)1ULL): goto ma198; case ((uint8_t)2ULL): goto ma199; default: goto ma200; }
ma197:;
        uint8_t t203 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 24) + 0));
        if (t203 != ((uint8_t)0ULL)) goto mn202;
        gm_tpautr4br7h3l t204 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)&F.l8) + 24) + 8));
        F.l18 = t204;
        gm_tpautr4br7h3l t205;
        memset((uint8_t *)&t205 + 17, 0, 7);
        void * t206 = gm_l0;
        gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
        gm_b16_8 t208 = t207;
        gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
        gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        gm_b16_8 t211 = t210;
        gm_b16_8 t212 = GM_LD(gm_b16_8, ((uint8_t *)&t211));
        gm_b16_8 t213 = gm_f_subst_fields_pqf9lb0((void *)NULL, cx, NULL, t206, t209, t212);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t205 + 0, t213);
        uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 16));
        GM_ST(uint8_t, (uint8_t *)&t205 + 16, t214);
        gm_tf5srh4mp4b3i t215 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l18) + 24));
        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t205 + 24, t215);
        gm_tkn42u7na3t81 t216 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l18) + 40));
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t205 + 40, t216);
        gm_ti0kgh3pc3in2 t217;
        GM_ST(uint8_t, (uint8_t *)&t217 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t217 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t217 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t217 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tpautr4br7h3l, ((uint8_t *)&t217 + 8), t205);
        t195 = t217;
        goto me196;
mn202:;
ma198:;
        uint8_t t219 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 24) + 0));
        if (t219 != ((uint8_t)1ULL)) goto mn218;
        gm_t88rcucg1b7uk t220 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)&F.l8) + 24) + 8) + 0));
        gm_l19 = t220;
        gm_b16_8 t221 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l8) + 24) + 8) + 8));
        gm_l20 = t221;
        {
            gm_f_list_g6uul2f((void *)&F.l21, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t224 = gm_l20;
                gm_l22 = t224;
            }
            {
                gm_l23 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t226 = gm_l23;
                    uint64_t t227 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                    uint8_t t228 = (t226 < t227);
                    if (!t228) goto lb225;
                    {
                        uint64_t t231 = gm_l23;
                        uint64_t t232 = (uintptr_t)t231;
                        uint64_t t233 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                        gm_tckdobunaulee t236 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t232 * 72));
                        F.l24 = t236;
                        {
                            gm_tkn42u7na3t81 t238;
                            memset(&t238, 0, sizeof t238);
                            gm_l25 = t238;
                        }
                        {
                            if (!(((((uint8_t *)&F.l24) + 16))[4] != 0)) goto ie239;
                            gm_t88rcucg1b7uk t240 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l24) + 16));
                            gm_l26 = t240;
                            {
                                void * t242 = gm_l0;
                                gm_t88rcucg1b7uk t243 = gm_l26;
                                gm_b16_8 t244 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                                gm_b16_8 t245 = t244;
                                gm_b16_8 t246 = GM_LD(gm_b16_8, ((uint8_t *)&t245));
                                gm_t88rcucg1b7uk t247 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t242, t243, t246);
                                if (gm_unw.kind) {
                                    goto gm_ll229;
                                }
                                gm_tkn42u7na3t81 t248;
                                GM_ST(uint8_t, (uint8_t *)&t248 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t248 + 6, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t248), t247);
                                ((uint8_t *)&t248)[4] = 1;
                                gm_l25 = t248;
                            }
                            goto ix239;
ie239:;
ix239:;
                        }
                        {
                            void * t249 = (void *)((uint8_t *)&F.l21);
                            gm_tckdobunaulee t250;
                            gm_b16_8 t251 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t250 + 0, t251);
                            gm_tkn42u7na3t81 t252 = gm_l25;
                            GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t250 + 16, t252);
                            memcpy((uint8_t *)&t250 + 24, (((uint8_t *)&F.l24) + 24), 48);
                            gm_tftb1odc739t5 t253 = gm_f_push_1j8vbii(t249, &t250);
                            gm_tftb1odc739t5 t254 = t253;
                            if (((((uint8_t *)&t254))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t254) + 1, gm_src3k278vcmqgdr, 587, 17);
                            gm_b0_1 t255 = GM_LD(gm_b0_1, (((uint8_t *)&t254) + 1));
                        }
                        goto me234;
mn235:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 584, 13);
me234:;
                    }
lc225:;
                    {
                        uint64_t t257 = gm_l23;
                        uint64_t r258; bool o259 = __builtin_add_overflow(t257, ((uint64_t)1ULL), &r258);
                        uint64_t t260 = r258;
                        gm_l23 = t260;
                    }
                }
gm_ll229:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb225; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc225; }
                }
                goto gm_lad;
lb225:;
            }
        }
        gm_tcmkompbl09il t261;
        memset((uint8_t *)&t261 + 4, 0, 4);
        gm_t88rcucg1b7uk t262 = gm_l19;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t261 + 0, t262);
        gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t261 + 8, t263);
        gm_ti0kgh3pc3in2 t264;
        GM_ST(uint8_t, (uint8_t *)&t264 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t264 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t264 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t264 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t264 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t264 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t264 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t264 + 8), t261);
        gm_ti0kgh3pc3in2 t265 = t264;
        t195 = t265;
        goto me196;
mn218:;
ma199:;
        uint8_t t267 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 24) + 0));
        if (t267 != ((uint8_t)2ULL)) goto mn266;
        gm_b16_8 t268 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l8) + 24) + 8));
        gm_l27 = t268;
        void * t269 = gm_l0;
        gm_b16_8 t270 = gm_l27;
        gm_b16_8 t271 = t270;
        gm_b16_8 t272 = GM_LD(gm_b16_8, ((uint8_t *)&t271));
        gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        gm_b16_8 t274 = t273;
        gm_b16_8 t275 = GM_LD(gm_b16_8, ((uint8_t *)&t274));
        gm_b16_8 t276 = gm_f_subst_fields_pqf9lb0((void *)NULL, cx, NULL, t269, t272, t275);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ti0kgh3pc3in2 t277;
        memset(&t277, 0, sizeof t277);
        GM_ST(uint8_t, ((uint8_t *)&t277 + 0), ((uint8_t)2ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t277 + 8), t276);
        t195 = t277;
        goto me196;
mn266:;
ma200:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 580, 21);
me196:;
        F.l28 = t195;
    }
    {
        uint32_t t278 = gm_l9;
        uint64_t t279 = (uint64_t)t278;
        uint64_t t280 = (uintptr_t)t279;
        void * t281 = gm_l0;
        uint64_t t282 = GM_LD(uintptr_t, ((((uint8_t *)t281) + 1088) + 0) + 8);
        if (t280 >= (uintptr_t)(t282)) gm_trap_index(t280, t282, gm_src3k278vcmqgdr, 593, 5);
        gm_ti0kgh3pc3in2 t283 = F.l28;
        GM_ST(gm_ti0kgh3pc3in2, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t281) + 1088) + 0)) + t280 * 184) + 24), t283);
    }
    {
        void * t284 = gm_l0;
        void * t285 = gm_l0;
        uint32_t t286 = GM_LD(uint32_t, (((uint8_t *)t285) + 1816));
        uint32_t r287; bool o288 = __builtin_sub_overflow(t286, ((uint32_t)1ULL), &r287);
        if (o288) gm_trap("integer overflow in `-`", gm_src3k278vcmqgdr, 594, 5);
        uint32_t t289 = r287;
        GM_ST(uint32_t, (((uint8_t *)t284) + 1816), t289);
    }
    {
        gm_l29 = ((uint8_t)1);
    }
    {
        {
            gm_b16_8 t291 = gm_l2;
            gm_l30 = t291;
        }
        {
            gm_l31 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t293 = gm_l31;
                uint64_t t294 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                uint8_t t295 = (t293 < t294);
                if (!t295) goto lb292;
                {
                    uint64_t t298 = gm_l31;
                    uint64_t t299 = (uintptr_t)t298;
                    uint64_t t300 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                    gm_t88rcucg1b7uk t303 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t299 * 4));
                    gm_l32 = t303;
                    {
                        void * t306 = gm_l0;
                        void * t307 = (void *)(((uint8_t *)t306) + 8);
                        gm_t88rcucg1b7uk t308 = gm_l32;
                        uint8_t t309 = gm_f_has_param_u9t0efs(t307, GM_CONV(gm_csguankko4lmhd, t308));
                        if (!t309) goto ie305;
                        {
                            gm_l29 = ((uint8_t)0);
                        }
                        goto ix305;
ie305:;
ix305:;
                    }
                    goto me301;
mn302:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 596, 5);
me301:;
                }
lc292:;
                {
                    uint64_t t312 = gm_l31;
                    uint64_t r313; bool o314 = __builtin_add_overflow(t312, ((uint64_t)1ULL), &r313);
                    uint64_t t315 = r313;
                    gm_l31 = t315;
                }
            }
lb292:;
        }
    }
    {
        uint8_t t317 = gm_l29;
        if (!t317) goto ie316;
        {
            void * t319 = gm_l0;
            void * t320 = (void *)(((uint8_t *)t319) + 1824);
            gm_t5g5vgung6h6s t321;
            memset((uint8_t *)&t321 + 4, 0, 4);
            memset((uint8_t *)&t321 + 28, 0, 4);
            uint32_t t322 = gm_l1;
            GM_ST(uint32_t, (uint8_t *)&t321 + 0, t322);
            gm_b16_8 t323 = gm_f_empty_slice_rcd2jln();
            GM_ST(gm_b16_8, (uint8_t *)&t321 + 8, t323);
            uint32_t t324 = gm_l9;
            GM_ST(uint32_t, (uint8_t *)&t321 + 24, t324);
            gm_tftb1odc739t5 t325 = gm_f_push_u4kbe6d(t320, &t321);
            gm_tftb1odc739t5 t326 = t325;
            if (((((uint8_t *)&t326))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t326) + 1, gm_src3k278vcmqgdr, 599, 19);
            gm_b0_1 t327 = GM_LD(gm_b0_1, (((uint8_t *)&t326) + 1));
        }
        goto ix316;
ie316:;
ix316:;
    }
    {
        void * t329 = gm_l0;
        uint32_t t330 = GM_LD(uint32_t, (((uint8_t *)t329) + 1816));
        uint8_t t331 = ((t330 == ((uint32_t)0ULL)));
        if (!t331) goto ie328;
        {
            void * t333 = gm_l0;
            gm_trqfvmjtt4sij t334 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t333) + 1824));
            F.l33 = t334;
        }
        {
            void * t335 = gm_l0;
            uint8_t *t336 = (((uint8_t *)t335) + 1824);
            gm_f_list_au01ire((void *)&(*(gm_trqfvmjtt4sij *)t336), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_go2qg0b((void *)&F.l34, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t338 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
                gm_l35 = t338;
            }
            {
                gm_l36 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t340 = gm_l36;
                    uint64_t t341 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                    uint8_t t342 = (t340 < t341);
                    if (!t342) goto lb339;
                    {
                        uint64_t t345 = gm_l36;
                        uint64_t t346 = (uintptr_t)t345;
                        uint64_t t347 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                        gm_t5g5vgung6h6s t350 = GM_LD(gm_t5g5vgung6h6s, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l35)) + t346 * 32));
                        gm_l37 = t350;
                        {
                            gm_f_list_go2qg0b((void *)&F.l38, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll343;
                            }
                        }
                        {
                            void * t352 = gm_l0;
                            void * t353 = (void *)(((uint8_t *)t352) + 8);
                            uint32_t t354 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 24));
                            void * t355 = gm_l0;
                            uint32_t t356 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 24));
                            gm_b16_8 t357 = gm_f_instance_args_15ek7cf((void *)NULL, cx, NULL, t355, t356);
                            if (gm_unw.kind) {
                                goto gm_ll343;
                            }
                            gm_b16_8 t358 = t357;
                            gm_b16_8 t359 = GM_LD(gm_b16_8, ((uint8_t *)&t358));
                            gm_t88rcucg1b7uk t360 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t353, t354, t359);
                            if (gm_unw.kind) {
                                goto gm_ll343;
                            }
                            gm_l39 = t360;
                        }
                        {
                            void * t362 = gm_l0;
                            gm_t88rcucg1b7uk t363 = gm_l39;
                            uint32_t t364 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 24));
                            void * t365 = (void *)((uint8_t *)&F.l38);
                            uint8_t t366 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t362, t363, t364, t365);
                            if (gm_unw.kind) {
                                goto gm_ll343;
                            }
                            if (!t366) goto ie361;
                            {
                                void * t368 = (void *)((uint8_t *)&F.l34);
                                uint32_t t369 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 24));
                                gm_tftb1odc739t5 t370 = gm_f_push_im9d4ro(t368, t369);
                                gm_tftb1odc739t5 t371 = t370;
                                if (((((uint8_t *)&t371))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t371) + 1, gm_src3k278vcmqgdr, 609, 56);
                                gm_b0_1 t372 = GM_LD(gm_b0_1, (((uint8_t *)&t371) + 1));
                            }
                            goto ix361;
ie361:;
ix361:;
                        }
                        goto me348;
mn349:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 606, 9);
me348:;
                    }
lc339:;
                    {
                        uint64_t t374 = gm_l36;
                        uint64_t r375; bool o376 = __builtin_add_overflow(t374, ((uint64_t)1ULL), &r375);
                        uint64_t t377 = r375;
                        gm_l36 = t377;
                    }
                }
gm_ll343:;
                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb339; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc339; }
                }
                goto gm_lad;
lb339:;
            }
        }
        {
            {
                gm_b16_8 t379 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
                gm_l40 = t379;
            }
            {
                gm_l41 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t381 = gm_l41;
                    uint64_t t382 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                    uint8_t t383 = (t381 < t382);
                    if (!t383) goto lb380;
                    {
                        uint64_t t386 = gm_l41;
                        uint64_t t387 = (uintptr_t)t386;
                        uint64_t t388 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                        gm_t5g5vgung6h6s t391 = GM_LD(gm_t5g5vgung6h6s, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t387 * 32));
                        gm_l42 = t391;
                        {
                            uint32_t t394 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 24));
                            gm_b16_8 t395 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
                            gm_b16_8 t396 = t395;
                            uint8_t t397 = 0;
                            uint32_t t398 = t394;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t396) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t396))) + i * 4, ((uint8_t *)&t398))) { t397 = 1; break; }
                            uint8_t t399 = (uint8_t)(!t397);
                            if (!t399) goto ie393;
                            {
                                goto lc380;
                            }
                            goto ix393;
ie393:;
ix393:;
                        }
                        {
                            void * t401 = gm_l0;
                            void * t402 = (void *)(((uint8_t *)t401) + 8);
                            uint32_t t403 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 24));
                            void * t404 = gm_l0;
                            uint32_t t405 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 24));
                            gm_b16_8 t406 = gm_f_instance_args_15ek7cf((void *)NULL, cx, NULL, t404, t405);
                            if (gm_unw.kind) {
                                goto gm_ll384;
                            }
                            gm_b16_8 t407 = t406;
                            gm_b16_8 t408 = GM_LD(gm_b16_8, ((uint8_t *)&t407));
                            gm_t88rcucg1b7uk t409 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t402, t403, t408);
                            if (gm_unw.kind) {
                                goto gm_ll384;
                            }
                            gm_l43 = t409;
                        }
                        {
                            void * t410 = gm_l0;
                            uint32_t t411 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 0));
                            uint64_t t412 = (uint64_t)t411;
                            uint64_t t413 = (uintptr_t)t412;
                            void * t414 = gm_l0;
                            uint64_t t415 = GM_LD(uintptr_t, ((((uint8_t *)t414) + 1088) + 0) + 8);
                            if (t413 >= (uintptr_t)(t415)) gm_trap_index(t413, t415, gm_src3k278vcmqgdr, 614, 21);
                            gm_tl874t2atnlh3 t416 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t414) + 1088) + 0)) + t413 * 184) + 156));
                            void * t417 = gm_l0;
                            gm_t88rcucg1b7uk t418 = gm_l43;
                            gm_b16_8 t419 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t417, t418);
                            if (gm_unw.kind) {
                                goto gm_ll384;
                            }
                            gm_tnpifv20irnph t420;
                            GM_ST(gm_b16_8, ((uint8_t *)&t420 + 0), t419);
                            gm_tabb710d9mq2e t421;
                            gm_f_format_0au88qf((void *)&t421, (void *)NULL, cx, NULL, t420);
                            if (gm_unw.kind) {
                                goto gm_ll384;
                            }
                            gm_tabb710d9mq2e t422 = t421;
                            if (((((uint8_t *)&t422))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t422) + 8, gm_src3k278vcmqgdr, 614, 59);
                            gm_b16_8 t423 = GM_LD(gm_b16_8, (((uint8_t *)&t422) + 8));
                            gm_b16_8 t424 = t423;
                            gm_b16_8 t425 = GM_LD(gm_b16_8, ((uint8_t *)&t424));
                            gm_b0_1 t426 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t410, t416, t425);
                            if (gm_unw.kind) {
                                goto gm_ll384;
                            }
                        }
                        {
                            void * t427 = gm_l0;
                            uint32_t t428 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 24));
                            gm_f_empty_adt_tdp3hro(t427, t428);
                        }
                        goto me389;
mn390:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 611, 9);
me389:;
                    }
lc380:;
                    {
                        uint64_t t430 = gm_l41;
                        uint64_t r431; bool o432 = __builtin_add_overflow(t430, ((uint64_t)1ULL), &r431);
                        uint64_t t433 = r431;
                        gm_l41 = t433;
                    }
                }
gm_ll384:;
                if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb380; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc380; }
                }
                goto gm_lad;
lb380:;
            }
        }
        {
            {
                gm_b16_8 t435 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
                gm_l44 = t435;
            }
            {
                gm_l45 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t437 = gm_l45;
                    uint64_t t438 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                    uint8_t t439 = (t437 < t438);
                    if (!t439) goto lb436;
                    {
                        uint64_t t442 = gm_l45;
                        uint64_t t443 = (uintptr_t)t442;
                        uint64_t t444 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                        gm_t5g5vgung6h6s t447 = GM_LD(gm_t5g5vgung6h6s, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l44)) + t443 * 32));
                        gm_l46 = t447;
                        {
                            uint32_t t450 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 24));
                            gm_b16_8 t451 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
                            gm_b16_8 t452 = t451;
                            uint8_t t453 = 0;
                            uint32_t t454 = t450;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t452) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t452))) + i * 4, ((uint8_t *)&t454))) { t453 = 1; break; }
                            uint8_t t455 = (uint8_t)(t453);
                            if (!t455) goto ie449;
                            {
                                goto lc436;
                            }
                            goto ix449;
ie449:;
ix449:;
                        }
                        {
                            void * t457 = gm_l0;
                            void * t458 = (void *)(((uint8_t *)t457) + 8);
                            uint32_t t459 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 24));
                            void * t460 = gm_l0;
                            uint32_t t461 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 24));
                            gm_b16_8 t462 = gm_f_instance_args_15ek7cf((void *)NULL, cx, NULL, t460, t461);
                            if (gm_unw.kind) {
                                goto gm_ll440;
                            }
                            gm_b16_8 t463 = t462;
                            gm_b16_8 t464 = GM_LD(gm_b16_8, ((uint8_t *)&t463));
                            gm_t88rcucg1b7uk t465 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t458, t459, t464);
                            if (gm_unw.kind) {
                                goto gm_ll440;
                            }
                            gm_l47 = t465;
                        }
                        {
                            void * t467 = gm_l0;
                            gm_t88rcucg1b7uk t468 = gm_l47;
                            uint32_t t469 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 0));
                            uint64_t t470 = (uint64_t)t469;
                            uint64_t t471 = (uintptr_t)t470;
                            void * t472 = gm_l0;
                            uint64_t t473 = GM_LD(uintptr_t, ((((uint8_t *)t472) + 1088) + 0) + 8);
                            if (t471 >= (uintptr_t)(t473)) gm_trap_index(t471, t473, gm_src3k278vcmqgdr, 620, 38);
                            gm_tl874t2atnlh3 t474 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t472) + 1088) + 0)) + t471 * 184) + 156));
                            uint8_t t475 = gm_f_type_size_ok_tr1llg4((void *)NULL, cx, NULL, t467, t468, t474);
                            if (gm_unw.kind) {
                                goto gm_ll440;
                            }
                            uint8_t t476 = (uint8_t)!t475;
                            if (!t476) goto ie466;
                            {
                                void * t478 = gm_l0;
                                uint32_t t479 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 24));
                                gm_f_empty_adt_tdp3hro(t478, t479);
                            }
                            goto ix466;
ie466:;
ix466:;
                        }
                        goto me445;
mn446:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 617, 9);
me445:;
                    }
lc436:;
                    {
                        uint64_t t481 = gm_l45;
                        uint64_t r482; bool o483 = __builtin_add_overflow(t481, ((uint64_t)1ULL), &r482);
                        uint64_t t484 = r482;
                        gm_l45 = t484;
                    }
                }
gm_ll440:;
                if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb436; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc436; }
                }
                goto gm_lad;
lb436:;
            }
        }
        goto ix328;
ie328:;
ix328:;
    }
    {
        gm_t88rcucg1b7uk t485 = gm_l11;
        uint8_t t486 = 0;
        F.ret = t485;
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
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_int_fits_imbqv0c F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint8_t gm_l3; /* signed */
    uint64_t gm_l4; /* bits */
    uint8_t gm_l5; /* sg */
    uint16_t gm_l6; /* b */
    GM_STACK_CHECK(&F, 530u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    {
        gm_l3 = ((uint8_t)0);
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma8:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t15 != ((uint8_t)0ULL)) goto mn14;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l5 = t16;
        uint16_t t17 = GM_LD(uint16_t, ((((uint8_t *)&t6) + 8) + 2));
        gm_l6 = t17;
        {
            uint8_t t19 = gm_l5;
            gm_l3 = t19;
        }
        {
            uint16_t t20 = gm_l6;
            uint64_t t21 = (uint64_t)t20;
            gm_l4 = t21;
        }
        goto me7;
mn14:;
ma9:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t23 != ((uint8_t)1ULL)) goto mn22;
        {
            void * t25 = gm_l0;
            void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
            void * t27 = (void *)(((uint8_t *)t26) + 88);
            uint32_t t28 = gm_f_ptr_bits_glps5i7(t27);
            uint64_t t29 = (uint64_t)t28;
            gm_l4 = t29;
        }
        goto me7;
mn22:;
ma10:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t31 != ((uint8_t)2ULL)) goto mn30;
        {
            gm_l3 = ((uint8_t)1);
        }
        {
            void * t33 = gm_l0;
            void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
            void * t35 = (void *)(((uint8_t *)t34) + 88);
            uint32_t t36 = gm_f_ptr_bits_glps5i7(t35);
            uint64_t t37 = (uint64_t)t36;
            gm_l4 = t37;
        }
        goto me7;
mn30:;
ma11:;
        uint8_t t39 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me7;
mn38:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 423, 5);
me7:;
    }
    {
        uint8_t t41 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l1)));
        if (!t41) goto ie40;
        {
            uint8_t t44 = gm_l3;
            uint8_t t45 = (uint8_t)!t44;
            if (!t45) goto ie43;
            {
                uint8_t t47 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix43;
ie43:;
ix43:;
        }
        {
            gm_tbci5ospk5nnn t48 = F.l1;
            gm_tbci5ospk5nnn t49;
            gm_f_one_08qg1op((void *)&t49, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t50;
            gm_f_add_420lj6o((void *)&t50, (void *)NULL, cx, NULL, &t48, &t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t51;
            gm_f_neg_agd5c5k((void *)&t51, (void *)NULL, cx, NULL, &t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t52 = gm_f_bits_lr3of4i(&t51);
            uint64_t t53 = gm_l4;
            uint64_t r54; bool o55 = __builtin_sub_overflow(t53, ((uint64_t)1ULL), &r54);
            if (o55) gm_trap("integer overflow in `-`", gm_src3k278vcmqgdr, 438, 52);
            uint64_t t56 = r54;
            uint8_t t57 = (t52 <= t56);
            uint8_t t58 = 0;
            F.ret = t57;
            goto gm_fret;
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        uint64_t t59 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l1)));
        uint64_t t60;
        uint8_t t62 = gm_l3;
        if (!t62) goto ie61;
        uint64_t t64 = gm_l4;
        uint64_t r65; bool o66 = __builtin_sub_overflow(t64, ((uint64_t)1ULL), &r65);
        if (o66) gm_trap("integer overflow in `-`", gm_src3k278vcmqgdr, 440, 37);
        uint64_t t67 = r65;
        uint64_t t68 = t67;
        t60 = t68;
        goto ix61;
ie61:;
        uint64_t t70 = gm_l4;
        uint64_t t71 = t70;
        t60 = t71;
ix61:;
        uint8_t t72 = (t59 <= t60);
        uint8_t t73 = 0;
        F.ret = t72;
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
__attribute__((aligned(4))) uint8_t gm_f_is_infinitely_sized_mhhcucl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2) {
    struct gm_fr_is_infinitely_sized_mhhcucl F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    void * gm_l2; /* stack */
    uint32_t gm_l3; /* id */
    uint8_t gm_l4; /* r */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_t88rcucg1b7uk gm_l10; /* elem */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    gm_b16_8 gm_l13; /* elems */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t88rcucg1b7uk gm_l16; /* e */
    gm_t88rcucg1b7uk gm_l17; /* err */
    gm_t88rcucg1b7uk gm_l18; /* ok */
    GM_STACK_CHECK(&F, 760u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; default: goto ma12; }
ma8:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t16 != ((uint8_t)17ULL)) goto mn15;
        uint32_t t17 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l3 = t17;
        {
            uint32_t t20 = gm_l3;
            void * t21 = gm_l2;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 0));
            gm_b16_8 t23 = t22;
            uint8_t t24 = 0;
            uint32_t t25 = t20;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t23) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t23))) + i * 4, ((uint8_t *)&t25))) { t24 = 1; break; }
            uint8_t t26 = (uint8_t)(t24);
            if (!t26) goto ie19;
            {
                uint8_t t28 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix19;
ie19:;
ix19:;
        }
        {
            void * t29 = gm_l2;
            uint32_t t30 = gm_l3;
            gm_tftb1odc739t5 t31 = gm_f_push_im9d4ro(t29, t30);
            gm_tftb1odc739t5 t32 = t31;
            if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_src3k278vcmqgdr, 468, 13);
            gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
        }
        {
            gm_l4 = ((uint8_t)0);
        }
        {
            uint32_t t34 = gm_l3;
            uint64_t t35 = (uint64_t)t34;
            uint64_t t36 = (uintptr_t)t35;
            void * t37 = gm_l0;
            uint64_t t38 = GM_LD(uintptr_t, ((((uint8_t *)t37) + 1088) + 0) + 8);
            if (t36 >= (uintptr_t)(t38)) gm_trap_index(t36, t38, gm_src3k278vcmqgdr, 470, 21);
            gm_ti0kgh3pc3in2 t39 = GM_LD(gm_ti0kgh3pc3in2, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t37) + 1088) + 0)) + t36 * 184) + 24));
            F.l5 = t39;
        }
        {
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                uint64_t t41 = gm_f_member_count_otu9tc0(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l5)));
                gm_l6 = t41;
            }
            {
                for (;;) {
                    uint64_t t43 = gm_l7;
                    uint64_t t44 = gm_l6;
                    uint8_t t45 = (t43 < t44);
                    if (!t45) goto lb42;
                    {
                        uint64_t t48 = gm_l7;
                        uint64_t t49 = t48;
                        uint64_t t52 = GM_LD(uint64_t, ((uint8_t *)&t49));
                        gm_l8 = t52;
                        {
                            uint64_t t54 = gm_l8;
                            gm_tkn42u7na3t81 t55 = gm_f_member_ty_esvuguv(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l5)), t54);
                            gm_tkn42u7na3t81 t56 = t55;
                            gm_t88rcucg1b7uk t57;
                            if (((((uint8_t *)&t56))[4] != 0)) {
                                gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t56) + 0));
                                t57 = t58;
                            } else {
                                goto lc42;
                            }
                            gm_l9 = t57;
                        }
                        {
                            void * t60 = gm_l0;
                            gm_t88rcucg1b7uk t61 = gm_l9;
                            void * t62 = gm_l2;
                            uint8_t t63 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t60, t61, t62);
                            if (gm_unw.kind) {
                                goto gm_ll46;
                            }
                            if (!t63) goto ie59;
                            {
                                gm_l4 = ((uint8_t)1);
                            }
                            {
                                goto lb42;
                            }
                            goto ix59;
ie59:;
ix59:;
                        }
                        goto me50;
mn51:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 471, 13);
me50:;
                    }
lc42:;
                    {
                        uint64_t t66 = gm_l7;
                        uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
                        uint64_t t69 = r67;
                        gm_l7 = t69;
                    }
                }
gm_ll46:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb42; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc42; }
                }
                goto gm_lad;
lb42:;
            }
        }
        {
            {
                void * t71 = gm_l2;
                gm_t9805qtonbsag t72 = gm_f_pop_570g79n(t71);
            }
        }
        {
            uint8_t t73 = gm_l4;
            uint8_t t74 = 0;
            F.ret = t73;
            goto gm_fret;
        }
        goto me7;
mn15:;
ma9:;
        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t78 != ((uint8_t)11ULL)) goto pa77;
        gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 8));
        gm_l10 = t79;
        goto po76;
pa77:;
        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t81 != ((uint8_t)20ULL)) goto pa80;
        gm_t88rcucg1b7uk t82 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l10 = t82;
        goto po76;
pa80:;
        uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t84 != ((uint8_t)13ULL)) goto pa83;
        gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l10 = t85;
        goto po76;
pa83:;
        goto mn75;
po76:;
        void * t86 = gm_l0;
        gm_t88rcucg1b7uk t87 = gm_l10;
        void * t88 = gm_l2;
        uint8_t t89 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t86, t87, t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t90 = 0;
        F.ret = t89;
        goto gm_fret;
        goto me7;
mn75:;
ma10:;
        uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t92 != ((uint8_t)12ULL)) goto mn91;
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
        gm_l13 = t93;
        {
            {
                gm_b16_8 t96 = gm_l13;
                gm_l14 = t96;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t98 = gm_l15;
                    uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t100 = (t98 < t99);
                    if (!t100) goto lb97;
                    {
                        uint64_t t103 = gm_l15;
                        uint64_t t104 = (uintptr_t)t103;
                        uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        gm_t88rcucg1b7uk t108 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t104 * 4));
                        gm_l16 = t108;
                        {
                            void * t111 = gm_l0;
                            gm_t88rcucg1b7uk t112 = gm_l16;
                            void * t113 = gm_l2;
                            uint8_t t114 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t111, t112, t113);
                            if (gm_unw.kind) {
                                goto gm_ll101;
                            }
                            if (!t114) goto ie110;
                            {
                                uint8_t t116 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix110;
ie110:;
ix110:;
                        }
                        goto me106;
mn107:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 483, 13);
me106:;
                    }
lc97:;
                    {
                        uint64_t t118 = gm_l15;
                        uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                        uint64_t t121 = r119;
                        gm_l15 = t121;
                    }
                }
gm_ll101:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb97; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc97; }
                }
                goto gm_lad;
lb97:;
            }
        }
        {
            uint8_t t122 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me7;
mn91:;
ma11:;
        uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t124 != ((uint8_t)14ULL)) goto mn123;
        gm_t88rcucg1b7uk t125 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 0));
        gm_l17 = t125;
        gm_t88rcucg1b7uk t126 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l18 = t126;
        uint8_t t127 = 0;
        void * t128 = gm_l0;
        gm_t88rcucg1b7uk t129 = gm_l17;
        void * t130 = gm_l2;
        uint8_t t131 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t128, t129, t130);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t131) {
            void * t132 = gm_l0;
            gm_t88rcucg1b7uk t133 = gm_l18;
            void * t134 = gm_l2;
            uint8_t t135 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t132, t133, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t127 = t135;
        } else t127 = 1;
        uint8_t t136 = 0;
        F.ret = t127;
        goto gm_fret;
        goto me7;
mn123:;
ma12:;
        uint8_t t138 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me7;
mn137:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 465, 5);
me7:;
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
inline __attribute__((aligned(4))) uint64_t gm_f_member_count_otu9tc0(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_member_count_otu9tc0 F;
    gm_b16_8 gm_l2; /* fields */
    gm_b16_8 gm_l3; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma4; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    gm_tpautr4br7h3l t10 = GM_LD(gm_tpautr4br7h3l, (((uint8_t *)a0) + 8));
    F.l1 = t10;
    uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
    t1 = t11;
    goto me2;
mn8:;
ma4:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t13 != ((uint8_t)2ULL)) goto mn12;
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l2 = t14;
    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
    t1 = t15;
    goto me2;
mn12:;
ma5:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t17 != ((uint8_t)1ULL)) goto mn16;
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 8));
    gm_l3 = t18;
    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
    t1 = t19;
    goto me2;
mn16:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 82, 47);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint64_t gm_f_member_count_otu9tc0_c(gm_ti0kgh3pc3in2 a0) { return gm_f_member_count_otu9tc0(&a0); }
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_member_ty_esvuguv(const void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_member_ty_esvuguv F;
    uint64_t gm_l1; /* i */
    gm_b16_8 gm_l3; /* fields */
    gm_b16_8 gm_l4; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 252u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_tkn42u7na3t81 t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma4; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    gm_tpautr4br7h3l t10 = GM_LD(gm_tpautr4br7h3l, (((uint8_t *)a0) + 8));
    F.l2 = t10;
    uint64_t t11 = gm_l1;
    uint64_t t12 = (uintptr_t)t11;
    uint64_t t13 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
    if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_src3k278vcmqgdr, 90, 21);
    gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t12 * 40) + 16));
    gm_tkn42u7na3t81 t15;
    GM_ST(uint8_t, (uint8_t *)&t15 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t15 + 6, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15), t14);
    ((uint8_t *)&t15)[4] = 1;
    t1 = t15;
    goto me2;
mn8:;
ma4:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t17 != ((uint8_t)2ULL)) goto mn16;
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l3 = t18;
    uint64_t t19 = gm_l1;
    uint64_t t20 = (uintptr_t)t19;
    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
    if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_src3k278vcmqgdr, 91, 24);
    gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t20 * 40) + 16));
    gm_tkn42u7na3t81 t23;
    GM_ST(uint8_t, (uint8_t *)&t23 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t23 + 6, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t23), t22);
    ((uint8_t *)&t23)[4] = 1;
    t1 = t23;
    goto me2;
mn16:;
ma5:;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t25 != ((uint8_t)1ULL)) goto mn24;
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 8));
    gm_l4 = t26;
    uint64_t t27 = gm_l1;
    uint64_t t28 = (uintptr_t)t27;
    uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
    if (t28 >= (uintptr_t)(t29)) gm_trap_index(t28, t29, gm_src3k278vcmqgdr, 92, 28);
    gm_tkn42u7na3t81 t30 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t28 * 72) + 16));
    t1 = t30;
    goto me2;
mn24:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 89, 52);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_member_ty_esvuguv_c(gm_ti0kgh3pc3in2 a0, uint64_t a1) { return gm_f_member_ty_esvuguv(&a0, a1); }
inline __attribute__((aligned(4))) void gm_f_new_adt_info_5pl2srl(void *gm_out, gm_b16_8 a0, uint64_t a1, const void * a2, gm_cseahvg8v7p0vs a3, gm_tf5srh4mp4b3i a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_new_adt_info_5pl2srl F;
    gm_b16_8 gm_l0; /* name */
    uint64_t gm_l1; /* module */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tf5srh4mp4b3i gm_l4; /* decl */
    GM_STACK_CHECK_SHALLOW(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = GM_CONV(gm_tl874t2atnlh3, a3);
    gm_l4 = a4;
    memset((uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 113, 0, 7);
    memset((uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 153, 0, 3);
    gm_b16_8 t1 = gm_l0;
    uint64_t t2 = gm_l1;
    gm_ti0kgh3pc3in2 t3 = GM_LD(gm_ti0kgh3pc3in2, ((uint8_t *)a2));
    gm_b16_8 t4 = gm_f_empty_slice_s1lbe5s();
    gm_b16_8 t5 = gm_f_empty_slice_8q3kspo_2();
    gm_b16_8 t6 = gm_f_empty_slice_d914i5u();
    gm_b16_8 t7 = gm_f_empty_slice_8q3kspo_2();
    gm_tl874t2atnlh3 t8 = gm_l3;
    gm_tf5srh4mp4b3i t9 = gm_l4;
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 16, t2);
    GM_ST(gm_ti0kgh3pc3in2, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 24, t3);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 80, t4);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 96, t5);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 112, ((uint8_t)0));
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 120, t6);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 136, t7);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 152, ((uint8_t)0));
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 156, t8);
    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 168, t9);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t6art7bprjiqb gm_f_new_adt_info_5pl2srl_c(gm_b16_8 a0, uint64_t a1, gm_ti0kgh3pc3in2 a2, gm_cseahvg8v7p0vs a3, gm_tf5srh4mp4b3i a4) { gm_t6art7bprjiqb gm_r; gm_f_new_adt_info_5pl2srl(&gm_r, a0, a1, &a2, a3, a4); return gm_r; }
inline __attribute__((aligned(4))) uint32_t gm_f_new_param_7010dt4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tnnjeerolos0a a2) {
    struct gm_fr_new_param_7010dt4 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_tnnjeerolos0a gm_l2; /* kind */
    uint32_t gm_l3; /* id */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_b16_8 t4 = gm_l1;
        uint32_t t5 = gm_f_fresh_param_442g4m9((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 1128);
        gm_tdtrcq4ju76rh t8;
        gm_tnnjeerolos0a t9 = gm_l2;
        GM_ST(gm_tnnjeerolos0a, (uint8_t *)&t8 + 0, t9);
        gm_f_list_5kg5cjm_2((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t8 + 8)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t10 = gm_f_push_bth3krn(t7, &t8);
        gm_tftb1odc739t5 t11 = t10;
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_src3k278vcmqgdr, 37, 5);
        gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
    }
    {
        uint32_t t13 = gm_l3;
        uint8_t t14 = 0;
        F.ret = t13;
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
inline __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_of_1jngakt(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_param_kind_of_1jngakt F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* p */
    void * gm_l3; /* t */
    gm_t08olq97rscn3 gm_l4; /* i */
    GM_STACK_CHECK(&F, 299u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 24));
        void * t4 = t3;
        void * t5;
        if ((GM_LD(void *, ((uint8_t *)&t4)) != 0)) {
            void * t6 = GM_LD(void *, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            {
                void * t8 = gm_l0;
                void * t9 = gm_l2;
                gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t9) + 40));
                gm_b16_8 t11;
                GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sf3irv2ans6de));
                GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(56));
                gm_b0_1 t12 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t8, t10, t11);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t13 = gm_l0;
            void * t14 = (void *)(((uint8_t *)t13) + 8);
            gm_t88rcucg1b7uk t15 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnnjeerolos0a t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t16 + 4), t15);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        gm_l3 = t5;
    }
    {
        void * t19 = gm_l3;
        uint8_t t20 = gm_f_is_effects_kind_uutbf9u(t19);
        if (!t20) goto ie18;
        {
            gm_tnnjeerolos0a t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)1ULL));
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t25 = gm_l3;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
        if (t26 != ((uint8_t)6ULL)) goto ie24;
        gm_t08olq97rscn3 t27 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t25) + 8) + 8));
        gm_l4 = t27;
        uint8_t t28 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l4) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l4) + 0)), gm_s2737i1iq57nf, 4) == 0));
        if (!t28) goto ie24;
        {
            gm_tnnjeerolos0a t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            uint8_t t31 = 0;
            F.ret = t30;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = gm_l0;
        uint64_t t33 = gm_l1;
        void * t34 = gm_l3;
        gm_t88rcucg1b7uk t35 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t32, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnnjeerolos0a t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t36 + 4), t35);
        uint8_t t37 = 0;
        F.ret = t36;
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_pending_layout_ikknuv1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pending_layout_ikknuv1 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* id */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_t88rcucg1b7uk gm_l7; /* t */
    uint32_t gm_l8; /* p */
    gm_t8nckeqfenl8a gm_l9; /* a */
    gm_tgglga8ro3isc gm_l10; /* a */
    gm_t88rcucg1b7uk gm_l11; /* t */
    gm_t6j6smj7qoctu gm_l12; /* t */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_t88rcucg1b7uk gm_l15; /* e */
    uint32_t gm_l16; /* p */
    gm_tgglga8ro3isc gm_l17; /* r */
    uint32_t gm_l18; /* p */
    GM_STACK_CHECK(&F, 1039u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 1728) + 0) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) goto ie2;
        {
            gm_t9805qtonbsag t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 8);
        gm_t88rcucg1b7uk t11 = gm_l1;
        gm_t53at80pfdus3 t12;
        gm_f_kind_jcnt4hm((void *)&t12, t10, GM_CONV(gm_csguankko4lmhd, t11));
        gm_t53at80pfdus3 t13 = t12;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        switch (t23) { case ((uint8_t)0ULL): goto ma21; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma21; case ((uint8_t)3ULL): goto ma21; case ((uint8_t)4ULL): goto ma21; case ((uint8_t)5ULL): goto ma21; case ((uint8_t)6ULL): goto ma21; case ((uint8_t)7ULL): goto ma21; case ((uint8_t)8ULL): goto ma21; case ((uint8_t)9ULL): goto ma21; case ((uint8_t)10ULL): goto ma21; case ((uint8_t)11ULL): goto ma16; case ((uint8_t)12ULL): goto ma19; case ((uint8_t)13ULL): goto ma18; case ((uint8_t)14ULL): goto ma20; case ((uint8_t)15ULL): goto ma21; case ((uint8_t)16ULL): goto ma21; case ((uint8_t)17ULL): goto ma15; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma21; case ((uint8_t)20ULL): goto ma17; case ((uint8_t)21ULL): goto ma21; case ((uint8_t)22ULL): goto ma21; case ((uint8_t)23ULL): goto ma21; case ((uint8_t)24ULL): goto ma21; case ((uint8_t)25ULL): goto ma21; case ((uint8_t)26ULL): goto ma21; default: goto ma21; }
ma15:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t25 != ((uint8_t)17ULL)) goto mn24;
        uint32_t t26 = GM_LD(uint32_t, ((((uint8_t *)&t13) + 8) + 0));
        gm_l2 = t26;
        {
            void * t29 = gm_l0;
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((((uint8_t *)t29) + 1728) + 0));
            gm_b16_8 t31 = t30;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            uint32_t t33 = gm_l2;
            gm_tf5srh4mp4b3i t34 = gm_f_index_of_41b084m(t32, t33);
            gm_tf5srh4mp4b3i t35 = t34;
            uint8_t t36;
            if (((((uint8_t *)&t35))[8] != 0)) goto mn38;
            t36 = ((uint8_t)0);
            goto me37;
mn38:;
            t36 = ((uint8_t)1);
            goto me37;
mn39:;
            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 797, 16);
me37:;
            if (!t36) goto ie28;
            {
                uint32_t t41 = gm_l2;
                gm_t9805qtonbsag t42;
                GM_ST(uint8_t, (uint8_t *)&t42 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t42 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t42), t41);
                ((uint8_t *)&t42)[4] = 1;
                uint8_t t43 = 0;
                F.ret = t42;
                goto gm_fret;
            }
            goto ix28;
ie28:;
ix28:;
        }
        {
            uint32_t t44 = gm_l2;
            uint64_t t45 = (uint64_t)t44;
            uint64_t t46 = (uintptr_t)t45;
            void * t47 = gm_l0;
            uint64_t t48 = GM_LD(uintptr_t, ((((uint8_t *)t47) + 1088) + 0) + 8);
            if (t46 >= (uintptr_t)(t48)) gm_trap_index(t46, t48, gm_src3k278vcmqgdr, 798, 21);
            gm_ti0kgh3pc3in2 t49 = GM_LD(gm_ti0kgh3pc3in2, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t47) + 1088) + 0)) + t46 * 184) + 24));
            F.l3 = t49;
        }
        {
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                uint64_t t51 = gm_f_member_count_otu9tc0(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l3)));
                gm_l4 = t51;
            }
            {
                for (;;) {
                    uint64_t t53 = gm_l5;
                    uint64_t t54 = gm_l4;
                    uint8_t t55 = (t53 < t54);
                    if (!t55) goto lb52;
                    {
                        uint64_t t58 = gm_l5;
                        uint64_t t59 = t58;
                        uint64_t t62 = GM_LD(uint64_t, ((uint8_t *)&t59));
                        gm_l6 = t62;
                        {
                            uint64_t t64 = gm_l6;
                            gm_tkn42u7na3t81 t65 = gm_f_member_ty_esvuguv(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l3)), t64);
                            gm_tkn42u7na3t81 t66 = t65;
                            gm_t88rcucg1b7uk t67;
                            if (((((uint8_t *)&t66))[4] != 0)) {
                                gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t66) + 0));
                                t67 = t68;
                            } else {
                                goto lc52;
                            }
                            gm_l7 = t67;
                        }
                        {
                            void * t70 = gm_l0;
                            gm_t88rcucg1b7uk t71 = gm_l7;
                            gm_t9805qtonbsag t72 = gm_f_pending_layout_ikknuv1(t70, GM_CONV(gm_csguankko4lmhd, t71));
                            gm_t9805qtonbsag t73 = t72;
                            if (!((((uint8_t *)&t73))[4] != 0)) goto ie69;
                            uint32_t t74 = GM_LD(uint32_t, ((uint8_t *)&t73));
                            gm_l8 = t74;
                            {
                                uint32_t t76 = gm_l8;
                                gm_t9805qtonbsag t77;
                                GM_ST(uint8_t, (uint8_t *)&t77 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t77 + 6, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t77), t76);
                                ((uint8_t *)&t77)[4] = 1;
                                uint8_t t78 = 0;
                                F.ret = t77;
                                goto gm_fret;
                            }
                            goto ix69;
ie69:;
ix69:;
                        }
                        goto me60;
mn61:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 799, 13);
me60:;
                    }
lc52:;
                    {
                        uint64_t t80 = gm_l5;
                        uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
                        uint64_t t83 = r81;
                        gm_l5 = t83;
                    }
                }
lb52:;
            }
        }
        {
            gm_t9805qtonbsag t84;
            memset(&t84, 0, sizeof t84);
            uint8_t t85 = 0;
            F.ret = t84;
            goto gm_fret;
        }
        goto me14;
mn24:;
ma16:;
        uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t87 != ((uint8_t)11ULL)) goto mn86;
        gm_t8nckeqfenl8a t88 = GM_LD(gm_t8nckeqfenl8a, (((uint8_t *)&t13) + 8));
        gm_l9 = t88;
        void * t89 = gm_l0;
        gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l9) + 8));
        gm_t9805qtonbsag t91 = gm_f_pending_layout_ikknuv1(t89, GM_CONV(gm_csguankko4lmhd, t90));
        uint8_t t92 = 0;
        F.ret = t91;
        goto gm_fret;
        goto me14;
mn86:;
ma17:;
        uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t94 != ((uint8_t)20ULL)) goto mn93;
        gm_tgglga8ro3isc t95 = GM_LD(gm_tgglga8ro3isc, (((uint8_t *)&t13) + 8));
        gm_l10 = t95;
        void * t96 = gm_l0;
        gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 4));
        gm_t9805qtonbsag t98 = gm_f_pending_layout_ikknuv1(t96, GM_CONV(gm_csguankko4lmhd, t97));
        uint8_t t99 = 0;
        F.ret = t98;
        goto gm_fret;
        goto me14;
mn93:;
ma18:;
        uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t101 != ((uint8_t)13ULL)) goto mn100;
        gm_t88rcucg1b7uk t102 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t13) + 8));
        gm_l11 = t102;
        void * t103 = gm_l0;
        gm_t88rcucg1b7uk t104 = gm_l11;
        gm_t9805qtonbsag t105 = gm_f_pending_layout_ikknuv1(t103, GM_CONV(gm_csguankko4lmhd, t104));
        uint8_t t106 = 0;
        F.ret = t105;
        goto gm_fret;
        goto me14;
mn100:;
ma19:;
        uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t108 != ((uint8_t)12ULL)) goto mn107;
        gm_t6j6smj7qoctu t109 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t13) + 8));
        gm_l12 = t109;
        {
            {
                gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                gm_l13 = t112;
            }
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t114 = gm_l14;
                    uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    uint8_t t116 = (t114 < t115);
                    if (!t116) goto lb113;
                    {
                        uint64_t t119 = gm_l14;
                        uint64_t t120 = (uintptr_t)t119;
                        uint64_t t121 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t120 * 4));
                        gm_l15 = t124;
                        {
                            void * t127 = gm_l0;
                            gm_t88rcucg1b7uk t128 = gm_l15;
                            gm_t9805qtonbsag t129 = gm_f_pending_layout_ikknuv1(t127, GM_CONV(gm_csguankko4lmhd, t128));
                            gm_t9805qtonbsag t130 = t129;
                            if (!((((uint8_t *)&t130))[4] != 0)) goto ie126;
                            uint32_t t131 = GM_LD(uint32_t, ((uint8_t *)&t130));
                            gm_l16 = t131;
                            {
                                uint32_t t133 = gm_l16;
                                gm_t9805qtonbsag t134;
                                GM_ST(uint8_t, (uint8_t *)&t134 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t134 + 6, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t134), t133);
                                ((uint8_t *)&t134)[4] = 1;
                                uint8_t t135 = 0;
                                F.ret = t134;
                                goto gm_fret;
                            }
                            goto ix126;
ie126:;
ix126:;
                        }
                        goto me122;
mn123:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 809, 13);
me122:;
                    }
lc113:;
                    {
                        uint64_t t137 = gm_l14;
                        uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                        uint64_t t140 = r138;
                        gm_l14 = t140;
                    }
                }
lb113:;
            }
        }
        {
            gm_t9805qtonbsag t141;
            memset(&t141, 0, sizeof t141);
            uint8_t t142 = 0;
            F.ret = t141;
            goto gm_fret;
        }
        goto me14;
mn107:;
ma20:;
        uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t144 != ((uint8_t)14ULL)) goto mn143;
        gm_tgglga8ro3isc t145 = GM_LD(gm_tgglga8ro3isc, (((uint8_t *)&t13) + 8));
        gm_l17 = t145;
        {
            void * t148 = gm_l0;
            gm_t88rcucg1b7uk t149 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 0));
            gm_t9805qtonbsag t150 = gm_f_pending_layout_ikknuv1(t148, GM_CONV(gm_csguankko4lmhd, t149));
            gm_t9805qtonbsag t151 = t150;
            if (!((((uint8_t *)&t151))[4] != 0)) goto ie147;
            uint32_t t152 = GM_LD(uint32_t, ((uint8_t *)&t151));
            gm_l18 = t152;
            {
                uint32_t t154 = gm_l18;
                gm_t9805qtonbsag t155;
                GM_ST(uint8_t, (uint8_t *)&t155 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t155 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t155), t154);
                ((uint8_t *)&t155)[4] = 1;
                uint8_t t156 = 0;
                F.ret = t155;
                goto gm_fret;
            }
            goto ix147;
ie147:;
ix147:;
        }
        {
            void * t157 = gm_l0;
            gm_t88rcucg1b7uk t158 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 4));
            gm_t9805qtonbsag t159 = gm_f_pending_layout_ikknuv1(t157, GM_CONV(gm_csguankko4lmhd, t158));
            uint8_t t160 = 0;
            F.ret = t159;
            goto gm_fret;
        }
        goto me14;
mn143:;
ma21:;
        gm_t9805qtonbsag t162;
        memset(&t162, 0, sizeof t162);
        uint8_t t163 = 0;
        F.ret = t162;
        goto gm_fret;
        goto me14;
mn161:;
ma22:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 795, 5);
me14:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_remove_in_progress_lrie478(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_struct_fields_chqu3it F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 329u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l2 = t9;
        {
            uint32_t t12 = gm_l2;
            uint64_t t13 = (uint64_t)t12;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 1088) + 0) + 8);
            if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_src3k278vcmqgdr, 496, 21);
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 1088) + 0)) + t14 * 184) + 24) + 0));
            if (t17 != ((uint8_t)0ULL)) goto ie11;
            gm_tpautr4br7h3l t18 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 1088) + 0)) + t14 * 184) + 24) + 8));
            F.l3 = t18;
            {
                gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                gm_th2lqbo77pvsc t21;
                GM_ST(uint8_t, (uint8_t *)&t21 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t21 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t21 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t21), t20);
                ((uint8_t *)&t21)[16] = 1;
                uint8_t t22 = 0;
                F.ret = t21;
                goto gm_fret;
            }
            goto ix11;
ie11:;
            {
                gm_th2lqbo77pvsc t24;
                memset(&t24, 0, sizeof t24);
                uint8_t t25 = 0;
                F.ret = t24;
                goto gm_fret;
            }
ix11:;
        }
        goto ix2;
ie2:;
        {
            gm_th2lqbo77pvsc t27;
            memset(&t27, 0, sizeof t27);
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_struct_fields_chqu3it_c(void * a0, gm_csguankko4lmhd a1) { gm_th2lqbo77pvsc gm_r; gm_f_struct_fields_chqu3it(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_struct_kind_5l1hs46(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, void * a3, gm_b16_8 a4) {
    struct gm_fr_struct_kind_5l1hs46 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    uint32_t gm_l2; /* id */
    void * gm_l3; /* st */
    gm_b16_8 gm_l4; /* attrs */
    uint8_t gm_l5; /* packed */
    gm_tf5srh4mp4b3i gm_l6; /* align */
    gm_tkn42u7na3t81 gm_l7; /* bits */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l11; /* args */
    gm_b16_8 gm_l12; /* other */
    uint8_t gm_l16; /* has_using */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_t88rcucg1b7uk gm_l22; /* ty */
    gm_tf5srh4mp4b3i gm_l23; /* field_align */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_b16_8 gm_l27; /* other */
    void * gm_l28; /* d */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    gm_t08olq97rscn3 gm_l31; /* n */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_t88rcucg1b7uk gm_l35; /* b */
    GM_STACK_CHECK(&F, 3245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_l5 = ((uint8_t)0);
    }
    {
        gm_tf5srh4mp4b3i t2;
        memset(&t2, 0, sizeof t2);
        gm_l6 = t2;
    }
    {
        gm_tkn42u7na3t81 t3;
        memset(&t3, 0, sizeof t3);
        gm_l7 = t3;
    }
    {
        {
            gm_b16_8 t5 = gm_l4;
            gm_l8 = t5;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l9;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l9;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_tkacitepg5fk0 t17 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t13 * 72));
                    F.l10 = t17;
                    {
                        if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l10) + 0) + 0) + 8) == 6) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l10) + 0) + 0)), gm_s2ph9f020upd2, 6) == 0)) goto mn20;
                        {
                            gm_l5 = ((uint8_t)1);
                        }
                        goto me19;
mn20:;
                        if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l10) + 0) + 0) + 8) == 5) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l10) + 0) + 0)), gm_sj1k4t2de5144, 5) == 0)) goto mn22;
                        {
                            void * t24 = gm_l0;
                            uint64_t t25 = gm_l1;
                            void * t26 = (void *)((uint8_t *)&F.l10);
                            gm_tf5srh4mp4b3i t27 = gm_f_attr_power_of_two_gj02n2t((void *)NULL, cx, NULL, t24, t25, t26);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            gm_l6 = t27;
                        }
                        goto me19;
mn22:;
                        if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l10) + 0) + 0) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l10) + 0) + 0)), gm_s6vvtrmtnt3j1, 4) == 0)) goto mn28;
                        {
                            gm_th2lqbo77pvsc t30 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l10) + 32));
                            gm_th2lqbo77pvsc t31 = t30;
                            gm_b16_8 t32;
                            if (((((uint8_t *)&t31))[16] != 0)) {
                                gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 0));
                                t32 = t33;
                            } else {
                                gm_b16_8 t34 = gm_f_empty_slice_g3tddrb();
                                t32 = t34;
                            }
                            gm_l11 = t32;
                        }
                        {
                            uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                            uint8_t t37 = ((t36 == ((uint64_t)1ULL)));
                            if (!t37) goto ie35;
                            {
                                void * t39 = gm_l0;
                                uint64_t t40 = gm_l1;
                                uint64_t t41 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_src3k278vcmqgdr, 199, 52);
                                void * t43 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t41 * 88);
                                gm_t88rcucg1b7uk t44 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t39, t40, t43);
                                if (gm_unw.kind) {
                                    goto gm_ll10;
                                }
                                gm_tkn42u7na3t81 t45;
                                GM_ST(uint8_t, (uint8_t *)&t45 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t45 + 6, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t45), t44);
                                ((uint8_t *)&t45)[4] = 1;
                                gm_l7 = t45;
                            }
                            goto ix35;
ie35:;
                            {
                                void * t47 = gm_l0;
                                gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l10) + 56));
                                gm_b16_8 t49;
                                GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s3lik4tvvjana));
                                GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(64));
                                gm_b0_1 t50 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t47, t48, t49);
                                if (gm_unw.kind) {
                                    goto gm_ll10;
                                }
                            }
ix35:;
                        }
                        goto me19;
mn28:;
                        gm_b16_8 t52 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l10) + 0) + 0));
                        gm_l12 = t52;
                        void * t53 = gm_l0;
                        gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l10) + 56));
                        gm_b16_8 t55 = gm_l12;
                        gm_tnpifv20irnph t56;
                        GM_ST(gm_b16_8, ((uint8_t *)&t56 + 0), t55);
                        gm_tabb710d9mq2e t57;
                        gm_f_format_5iu9hua((void *)&t57, (void *)NULL, cx, NULL, t56);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_tabb710d9mq2e t58 = t57;
                        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 8, gm_src3k278vcmqgdr, 204, 38);
                        gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 8));
                        gm_b16_8 t60 = t59;
                        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                        gm_b0_1 t62 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t53, t54, t61);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        goto me19;
mn51:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 193, 9);
me19:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 192, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t64 = gm_l9;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                    uint64_t t67 = r65;
                    gm_l9 = t67;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_lad;
lb6:;
        }
    }
    {
        gm_f_list_spqn36o((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_s83jn2q((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_pmp06kq_2((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l16 = ((uint8_t)0);
    }
    {
        gm_f_list_gst7tuh((void *)&F.l17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t68 = gm_l0;
        uint64_t t69 = gm_l1;
        void * t70 = gm_l3;
        gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)t70) + 40));
        gm_b16_8 t72 = t71;
        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
        void * t74 = (void *)((uint8_t *)&F.l17);
        gm_b0_1 t75 = gm_f_taken_struct_members_1ltbb18((void *)NULL, cx, NULL, t68, t69, t73, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
            gm_b16_8 t78 = t77;
            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
            gm_l18 = t79;
        }
        {
            gm_l19 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t81 = gm_l19;
                uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t83 = (t81 < t82);
                if (!t83) goto lb80;
                {
                    gm_t8uai8b3boj78 t86;
                    uint64_t t87 = gm_l19;
                    GM_ST(uint64_t, (uint8_t *)&t86 + 0, t87);
                    uint64_t t88 = gm_l19;
                    uint64_t t89 = (uintptr_t)t88;
                    uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    memcpy((uint8_t *)&t86 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t89 * 72), 72);
                    gm_t8uai8b3boj78 t91 = t86;
                    uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&t91) + 0));
                    gm_l20 = t94;
                    gm_t24301fas1b2a t95 = GM_LD(gm_t24301fas1b2a, (((uint8_t *)&t91) + 8));
                    F.l21 = t95;
                    {
                        void * t97 = gm_l0;
                        uint64_t t98 = gm_l1;
                        void * t99 = GM_LD(void *, (((uint8_t *)&F.l21) + 40));
                        gm_t88rcucg1b7uk t100 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t97, t98, t99);
                        if (gm_unw.kind) {
                            goto gm_ll84;
                        }
                        gm_l22 = t100;
                    }
                    {
                        void * t102 = gm_l0;
                        void * t103 = (void *)(((uint8_t *)t102) + 8);
                        gm_t88rcucg1b7uk t104 = gm_l22;
                        gm_t53at80pfdus3 t105;
                        gm_f_kind_jcnt4hm((void *)&t105, t103, GM_CONV(gm_csguankko4lmhd, t104));
                        gm_t53at80pfdus3 t106 = t105;
                        uint8_t t107;
                        uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&t106) + 0));
                        if (t110 != ((uint8_t)15ULL)) goto mn109;
                        t107 = ((uint8_t)1);
                        goto me108;
mn109:;
                        t107 = ((uint8_t)0);
                        goto me108;
mn111:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 215, 12);
me108:;
                        if (!t107) goto ie101;
                        {
                            void * t113 = gm_l0;
                            void * t114 = GM_LD(void *, (((uint8_t *)&F.l21) + 40));
                            gm_tl874t2atnlh3 t115 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t114) + 72));
                            gm_b16_8 t116;
                            GM_ST(void *, (uint8_t *)&t116, (void *)(gm_sacmu7hr5dknu));
                            GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(87));
                            gm_b0_1 t117 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t113, t115, t116);
                            if (gm_unw.kind) {
                                goto gm_ll84;
                            }
                        }
                        goto ix101;
ie101:;
ix101:;
                    }
                    {
                        gm_tf5srh4mp4b3i t118;
                        memset(&t118, 0, sizeof t118);
                        gm_l23 = t118;
                    }
                    {
                        {
                            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
                            gm_l24 = t120;
                        }
                        {
                            gm_l25 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t122 = gm_l25;
                                uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                uint8_t t124 = (t122 < t123);
                                if (!t124) goto lb121;
                                {
                                    uint64_t t127 = gm_l25;
                                    uint64_t t128 = (uintptr_t)t127;
                                    uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                    gm_tkacitepg5fk0 t132 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t128 * 72));
                                    F.l26 = t132;
                                    {
                                        if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l26) + 0) + 0) + 8) == 5) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l26) + 0) + 0)), gm_sj1k4t2de5144, 5) == 0)) goto mn135;
                                        {
                                            void * t137 = gm_l0;
                                            uint64_t t138 = gm_l1;
                                            void * t139 = (void *)((uint8_t *)&F.l26);
                                            gm_tf5srh4mp4b3i t140 = gm_f_attr_power_of_two_gj02n2t((void *)NULL, cx, NULL, t137, t138, t139);
                                            if (gm_unw.kind) {
                                                goto gm_ll125;
                                            }
                                            gm_l23 = t140;
                                        }
                                        goto me134;
mn135:;
                                        gm_b16_8 t142 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l26) + 0) + 0));
                                        gm_l27 = t142;
                                        void * t143 = gm_l0;
                                        gm_tl874t2atnlh3 t144 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 56));
                                        gm_b16_8 t145 = gm_l27;
                                        gm_tnpifv20irnph t146;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t146 + 0), t145);
                                        gm_tabb710d9mq2e t147;
                                        gm_f_format_slub3h9((void *)&t147, (void *)NULL, cx, NULL, t146);
                                        if (gm_unw.kind) {
                                            goto gm_ll125;
                                        }
                                        gm_tabb710d9mq2e t148 = t147;
                                        if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 8, gm_src3k278vcmqgdr, 222, 42);
                                        gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&t148) + 8));
                                        gm_b16_8 t150 = t149;
                                        gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
                                        gm_b0_1 t152 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t143, t144, t151);
                                        if (gm_unw.kind) {
                                            goto gm_ll125;
                                        }
                                        goto me134;
mn141:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 220, 13);
me134:;
                                    }
                                    goto me130;
mn131:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 219, 9);
me130:;
                                }
lc121:;
                                {
                                    uint64_t t154 = gm_l25;
                                    uint64_t r155; bool o156 = __builtin_add_overflow(t154, ((uint64_t)1ULL), &r155);
                                    uint64_t t157 = r155;
                                    gm_l25 = t157;
                                }
                            }
gm_ll125:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb121; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc121; }
                            }
                            goto gm_ll84;
lb121:;
                        }
                    }
                    {
                        uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&F.l21) + 17));
                        if (!t159) goto ie158;
                        {
                            uint8_t t162 = 0;
                            uint64_t t163 = gm_l20;
                            uint8_t t164 = (!(t163 == ((uint64_t)0ULL)));
                            if (!t164) {
                                uint64_t t165 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 24) + 8);
                                uint8_t t166 = (!(t165 == ((uint64_t)1ULL)));
                                t162 = t166;
                            } else t162 = 1;
                            if (!t162) goto ie161;
                            {
                                void * t168 = gm_l0;
                                gm_tl874t2atnlh3 t169 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l21) + 56));
                                gm_b16_8 t170;
                                GM_ST(void *, (uint8_t *)&t170, (void *)(gm_sisjr6ep7j5j0));
                                GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(78));
                                gm_b0_1 t171 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t168, t169, t170);
                                if (gm_unw.kind) {
                                    goto gm_ll84;
                                }
                            }
                            goto ix161;
ie161:;
                            void * t173 = gm_l0;
                            void * t174 = (void *)(((uint8_t *)t173) + 8);
                            gm_t88rcucg1b7uk t175 = gm_l22;
                            gm_t53at80pfdus3 t176;
                            gm_f_kind_jcnt4hm((void *)&t176, t174, GM_CONV(gm_csguankko4lmhd, t175));
                            gm_t53at80pfdus3 t177 = t176;
                            uint8_t t178;
                            uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&t177) + 0));
                            if (t181 != ((uint8_t)17ULL)) goto mn180;
                            t178 = ((uint8_t)0);
                            goto me179;
mn180:;
                            t178 = ((uint8_t)1);
                            goto me179;
mn182:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 228, 23);
me179:;
                            if (!t178) goto ie172;
                            void * t183 = gm_l0;
                            void * t184 = (void *)(((uint8_t *)t183) + 8);
                            gm_t88rcucg1b7uk t185 = gm_l22;
                            uint8_t t186 = gm_f_is_error_8k27gd1(t184, GM_CONV(gm_csguankko4lmhd, t185));
                            uint8_t t187 = (uint8_t)!t186;
                            if (!t187) goto ie172;
                            {
                                void * t189 = gm_l0;
                                gm_tl874t2atnlh3 t190 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l21) + 56));
                                void * t191 = gm_l0;
                                gm_t88rcucg1b7uk t192 = gm_l22;
                                gm_b16_8 t193 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t191, t192);
                                if (gm_unw.kind) {
                                    goto gm_ll84;
                                }
                                gm_tnpifv20irnph t194;
                                GM_ST(gm_b16_8, ((uint8_t *)&t194 + 0), t193);
                                gm_tabb710d9mq2e t195;
                                gm_f_format_fiq5e0p((void *)&t195, (void *)NULL, cx, NULL, t194);
                                if (gm_unw.kind) {
                                    goto gm_ll84;
                                }
                                gm_tabb710d9mq2e t196 = t195;
                                if (((((uint8_t *)&t196))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t196) + 8, gm_src3k278vcmqgdr, 229, 33);
                                gm_b16_8 t197 = GM_LD(gm_b16_8, (((uint8_t *)&t196) + 8));
                                gm_b16_8 t198 = t197;
                                gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)&t198));
                                gm_b0_1 t200 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t189, t190, t199);
                                if (gm_unw.kind) {
                                    goto gm_ll84;
                                }
                            }
                            goto ix172;
ie172:;
                            {
                                gm_l16 = ((uint8_t)1);
                            }
ix172:;
ix161:;
                        }
                        goto ix158;
ie158:;
ix158:;
                    }
                    {
                        void * t203 = GM_LD(void *, (((uint8_t *)&F.l21) + 48));
                        void * t204 = t203;
                        if (!(GM_LD(void *, ((uint8_t *)&t204)) != 0)) goto ie202;
                        void * t205 = GM_LD(void *, ((uint8_t *)&t204));
                        gm_l28 = t205;
                        {
                            void * t207 = gm_l0;
                            uint64_t t208 = gm_l1;
                            void * t209 = gm_l28;
                            gm_t88rcucg1b7uk t210 = gm_l22;
                            gm_b0_1 t211 = gm_f_check_default_4bf68q8((void *)NULL, cx, NULL, t207, t208, t209, t210);
                            if (gm_unw.kind) {
                                goto gm_ll84;
                            }
                        }
                        goto ix202;
ie202:;
ix202:;
                    }
                    {
                        {
                            gm_b16_8 t213 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 24));
                            gm_l29 = t213;
                        }
                        {
                            gm_l30 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t215 = gm_l30;
                                uint64_t t216 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                                uint8_t t217 = (t215 < t216);
                                if (!t217) goto lb214;
                                {
                                    uint64_t t220 = gm_l30;
                                    uint64_t t221 = (uintptr_t)t220;
                                    uint64_t t222 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                                    gm_t08olq97rscn3 t225 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t221 * 32));
                                    gm_l31 = t225;
                                    {
                                        uint8_t t228 = (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l31) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l31) + 0)), gm_sfdbfrv46lihd, 1) == 0));
                                        if (!t228) goto ie227;
                                        {
                                            {
                                                gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                                                gm_l32 = t231;
                                            }
                                            {
                                                gm_l33 = ((uint64_t)0ULL);
                                            }
                                            {
                                                for (;;) {
                                                    uint64_t t233 = gm_l33;
                                                    uint64_t t234 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                                                    uint8_t t235 = (t233 < t234);
                                                    if (!t235) goto lb232;
                                                    {
                                                        uint64_t t238 = gm_l33;
                                                        uint64_t t239 = (uintptr_t)t238;
                                                        uint64_t t240 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                                                        gm_t0mglksrkfa5e t243 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t239 * 40));
                                                        F.l34 = t243;
                                                        {
                                                            gm_b16_8 t246 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
                                                            gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 0));
                                                            gm_b16_8 t248 = t246;
                                                            gm_b16_8 t249 = t247;
                                                            uint8_t t250 = (gm_eq0iqiubfm7e(((uint8_t *)&t248), ((uint8_t *)&t249)));
                                                            if (!t250) goto ie245;
                                                            {
                                                                void * t252 = gm_l0;
                                                                gm_tl874t2atnlh3 t253 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l31) + 16));
                                                                gm_b16_8 t254 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 0));
                                                                gm_tnpifv20irnph t255;
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t255 + 0), t254);
                                                                gm_tabb710d9mq2e t256;
                                                                gm_f_format_atintr3((void *)&t256, (void *)NULL, cx, NULL, t255);
                                                                if (gm_unw.kind) {
                                                                    goto gm_ll236;
                                                                }
                                                                gm_tabb710d9mq2e t257 = t256;
                                                                if (((((uint8_t *)&t257))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t257) + 8, gm_src3k278vcmqgdr, 241, 41);
                                                                gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)&t257) + 8));
                                                                gm_b16_8 t259 = t258;
                                                                gm_b16_8 t260 = GM_LD(gm_b16_8, ((uint8_t *)&t259));
                                                                gm_b0_1 t261 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t252, t253, t260);
                                                                if (gm_unw.kind) {
                                                                    goto gm_ll236;
                                                                }
                                                            }
                                                            {
                                                                goto lb232;
                                                            }
                                                            goto ix245;
ie245:;
ix245:;
                                                        }
                                                        goto me241;
mn242:;
                                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 239, 17);
me241:;
                                                    }
lc232:;
                                                    {
                                                        uint64_t t263 = gm_l33;
                                                        uint64_t r264; bool o265 = __builtin_add_overflow(t263, ((uint64_t)1ULL), &r264);
                                                        uint64_t t266 = r264;
                                                        gm_l33 = t266;
                                                    }
                                                }
gm_ll236:;
                                                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb232; }
                                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc232; }
                                                }
                                                goto gm_ll218;
lb232:;
                                            }
                                        }
                                        goto ix227;
ie227:;
ix227:;
                                    }
                                    {
                                        void * t267 = (void *)((uint8_t *)&F.l13);
                                        gm_t0mglksrkfa5e t268;
                                        memset((uint8_t *)&t268 + 21, 0, 3);
                                        gm_b16_8 t269 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 0));
                                        GM_ST(gm_b16_8, (uint8_t *)&t268 + 0, t269);
                                        gm_t88rcucg1b7uk t270 = gm_l22;
                                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t268 + 16, t270);
                                        gm_t9qkh0rsooc8d t271 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l21) + 16));
                                        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t268 + 20, t271);
                                        gm_tf5srh4mp4b3i t272 = gm_l23;
                                        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t268 + 24, t272);
                                        gm_tftb1odc739t5 t273 = gm_f_push_vrmo7c5(t267, &t268);
                                        gm_tftb1odc739t5 t274 = t273;
                                        if (((((uint8_t *)&t274))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t274) + 1, gm_src3k278vcmqgdr, 246, 13);
                                        gm_b0_1 t275 = GM_LD(gm_b0_1, (((uint8_t *)&t274) + 1));
                                    }
                                    {
                                        void * t276 = (void *)((uint8_t *)&F.l14);
                                        void * t277 = GM_LD(void *, (((uint8_t *)&F.l21) + 48));
                                        void * t278 = t277;
                                        uint8_t t279;
                                        if ((GM_LD(void *, ((uint8_t *)&t278)) != 0)) goto mn281;
                                        t279 = ((uint8_t)0);
                                        goto me280;
mn281:;
                                        t279 = ((uint8_t)1);
                                        goto me280;
mn282:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 247, 27);
me280:;
                                        gm_tftb1odc739t5 t283 = gm_f_push_vd1290p(t276, t279);
                                        gm_tftb1odc739t5 t284 = t283;
                                        if (((((uint8_t *)&t284))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t284) + 1, gm_src3k278vcmqgdr, 247, 13);
                                        gm_b0_1 t285 = GM_LD(gm_b0_1, (((uint8_t *)&t284) + 1));
                                    }
                                    {
                                        void * t286 = (void *)((uint8_t *)&F.l15);
                                        void * t287 = GM_LD(void *, (((uint8_t *)&F.l21) + 48));
                                        gm_tftb1odc739t5 t288 = gm_f_push_r2k6rjf_2(t286, t287);
                                        gm_tftb1odc739t5 t289 = t288;
                                        if (((((uint8_t *)&t289))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t289) + 1, gm_src3k278vcmqgdr, 248, 13);
                                        gm_b0_1 t290 = GM_LD(gm_b0_1, (((uint8_t *)&t289) + 1));
                                    }
                                    goto me223;
mn224:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 237, 9);
me223:;
                                }
lc214:;
                                {
                                    uint64_t t292 = gm_l30;
                                    uint64_t r293; bool o294 = __builtin_add_overflow(t292, ((uint64_t)1ULL), &r293);
                                    uint64_t t295 = r293;
                                    gm_l30 = t295;
                                }
                            }
gm_ll218:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb214; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc214; }
                            }
                            goto gm_ll84;
lb214:;
                        }
                    }
                    goto me92;
mn93:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 213, 5);
me92:;
                }
lc80:;
                {
                    uint64_t t297 = gm_l19;
                    uint64_t r298; bool o299 = __builtin_add_overflow(t297, ((uint64_t)1ULL), &r298);
                    uint64_t t300 = r298;
                    gm_l19 = t300;
                }
            }
gm_ll84:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb80; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc80; }
            }
            goto gm_lad;
lb80:;
        }
    }
    {
        if (!((((uint8_t *)&gm_l7))[4] != 0)) goto ie301;
        gm_t88rcucg1b7uk t302 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l7));
        gm_l35 = t302;
        {
            void * t304 = gm_l0;
            void * t305 = gm_l3;
            gm_tl874t2atnlh3 t306 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t305) + 56));
            gm_t88rcucg1b7uk t307 = gm_l35;
            gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
            gm_b16_8 t309 = t308;
            gm_b16_8 t310 = GM_LD(gm_b16_8, ((uint8_t *)&t309));
            gm_b0_1 t311 = gm_f_check_bits_struct_ophej5b((void *)NULL, cx, NULL, t304, t306, t307, t310);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix301;
ie301:;
ix301:;
    }
    {
        uint32_t t312 = gm_l2;
        uint64_t t313 = (uint64_t)t312;
        uint64_t t314 = (uintptr_t)t313;
        void * t315 = gm_l0;
        uint64_t t316 = GM_LD(uintptr_t, ((((uint8_t *)t315) + 1088) + 0) + 8);
        if (t314 >= (uintptr_t)(t316)) gm_trap_index(t314, t316, gm_src3k278vcmqgdr, 252, 5);
        gm_b16_8 t317 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t315) + 1088) + 0)) + t314 * 184) + 80), t317);
    }
    {
        uint32_t t318 = gm_l2;
        uint64_t t319 = (uint64_t)t318;
        uint64_t t320 = (uintptr_t)t319;
        void * t321 = gm_l0;
        uint64_t t322 = GM_LD(uintptr_t, ((((uint8_t *)t321) + 1088) + 0) + 8);
        if (t320 >= (uintptr_t)(t322)) gm_trap_index(t320, t322, gm_src3k278vcmqgdr, 253, 5);
        gm_b16_8 t323 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t321) + 1088) + 0)) + t320 * 184) + 96), t323);
    }
    {
        uint32_t t324 = gm_l2;
        uint64_t t325 = (uint64_t)t324;
        uint64_t t326 = (uintptr_t)t325;
        void * t327 = gm_l0;
        uint64_t t328 = GM_LD(uintptr_t, ((((uint8_t *)t327) + 1088) + 0) + 8);
        if (t326 >= (uintptr_t)(t328)) gm_trap_index(t326, t328, gm_src3k278vcmqgdr, 254, 5);
        uint8_t t329 = gm_l16;
        GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t327) + 1088) + 0)) + t326 * 184) + 112), t329);
    }
    {
        gm_tpautr4br7h3l t330;
        memset((uint8_t *)&t330 + 17, 0, 7);
        gm_b16_8 t331 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t330 + 0, t331);
        uint8_t t332 = gm_l5;
        GM_ST(uint8_t, (uint8_t *)&t330 + 16, t332);
        gm_tf5srh4mp4b3i t333 = gm_l6;
        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t330 + 24, t333);
        gm_tkn42u7na3t81 t334 = gm_l7;
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t330 + 40, t334);
        gm_ti0kgh3pc3in2 t335;
        GM_ST(uint8_t, (uint8_t *)&t335 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t335 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t335 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t335 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tpautr4br7h3l, ((uint8_t *)&t335 + 8), t330);
        uint8_t t336 = 0;
        F.ret = t335;
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_subst_a2s1mb3 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* map */
    void * gm_l3; /* tys */
    uint32_t gm_l5; /* p */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l8; /* m */
    uint32_t gm_l9; /* id */
    gm_b16_8 gm_l10; /* args */
    gm_th3eueiipivq6 gm_l11; /* shape */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    gm_t88rcucg1b7uk gm_l13; /* x */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_t88rcucg1b7uk gm_l18; /* x */
    uint8_t gm_l21; /* c */
    gm_t88rcucg1b7uk gm_l22; /* x */
    uint8_t gm_l23; /* c */
    gm_t88rcucg1b7uk gm_l24; /* x */
    uint8_t gm_l25; /* c */
    gm_t88rcucg1b7uk gm_l26; /* x */
    uint64_t gm_l27; /* len */
    gm_t88rcucg1b7uk gm_l28; /* elem */
    gm_tnnjeerolos0a gm_l29; /* kind */
    gm_tnnjeerolos0a gm_l30; /* ak */
    gm_t88rcucg1b7uk gm_l31; /* e */
    gm_t88rcucg1b7uk gm_l32; /* len */
    gm_t88rcucg1b7uk gm_l33; /* elem */
    gm_t88rcucg1b7uk gm_l34; /* l */
    gm_t88rcucg1b7uk gm_l35; /* el */
    gm_t428ue2u3hf5g gm_l36; /* key */
    uint64_t gm_l37; /* n */
    gm_t9qkh0rsooc8d gm_l38; /* op */
    gm_t88rcucg1b7uk gm_l39; /* lhs */
    gm_t88rcucg1b7uk gm_l40; /* rhs */
    gm_t88rcucg1b7uk gm_l41; /* a */
    gm_t88rcucg1b7uk gm_l42; /* b */
    gm_t88rcucg1b7uk gm_l43; /* x */
    gm_t88rcucg1b7uk gm_l44; /* x */
    gm_t88rcucg1b7uk gm_l45; /* err */
    gm_t88rcucg1b7uk gm_l46; /* ok */
    gm_t88rcucg1b7uk gm_l47; /* e */
    gm_t88rcucg1b7uk gm_l48; /* o */
    gm_b16_8 gm_l49; /* elems */
    gm_b16_8 gm_l50; /* names */
    gm_b16_8 gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    gm_t88rcucg1b7uk gm_l54; /* e */
    gm_tnpifv20irnph gm_l55; /* set */
    GM_STACK_CHECK(&F, 3175u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) {
            void * t6 = gm_l0;
            void * t7 = (void *)(((uint8_t *)t6) + 8);
            gm_t88rcucg1b7uk t8 = gm_l1;
            uint8_t t9 = gm_f_has_param_u9t0efs(t7, GM_CONV(gm_csguankko4lmhd, t8));
            uint8_t t10 = (uint8_t)!t9;
            t3 = t10;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_t88rcucg1b7uk t12 = gm_l1;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t14 = gm_l0;
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_l3 = t15;
    }
    {
        void * t16 = gm_l3;
        gm_t88rcucg1b7uk t17 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&F.l4, t16, GM_CONV(gm_csguankko4lmhd, t17));
    }
    {
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        switch (t36) { case ((uint8_t)0ULL): goto ma34; case ((uint8_t)1ULL): goto ma34; case ((uint8_t)2ULL): goto ma34; case ((uint8_t)3ULL): goto ma34; case ((uint8_t)4ULL): goto ma34; case ((uint8_t)5ULL): goto ma34; case ((uint8_t)6ULL): goto ma34; case ((uint8_t)7ULL): goto ma34; case ((uint8_t)8ULL): goto ma23; case ((uint8_t)9ULL): goto ma24; case ((uint8_t)10ULL): goto ma25; case ((uint8_t)11ULL): goto ma26; case ((uint8_t)12ULL): goto ma32; case ((uint8_t)13ULL): goto ma29; case ((uint8_t)14ULL): goto ma31; case ((uint8_t)15ULL): goto ma21; case ((uint8_t)16ULL): goto ma22; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma30; case ((uint8_t)19ULL): goto ma19; case ((uint8_t)20ULL): goto ma27; case ((uint8_t)21ULL): goto ma34; case ((uint8_t)22ULL): goto ma28; case ((uint8_t)23ULL): goto ma34; case ((uint8_t)24ULL): goto ma33; case ((uint8_t)25ULL): goto ma34; case ((uint8_t)26ULL): goto ma34; default: goto ma34; }
ma19:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t38 != ((uint8_t)19ULL)) goto mn37;
        uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 8));
        gm_l5 = t39;
        {
            {
                gm_b16_8 t42 = gm_l2;
                gm_l6 = t42;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t44 = gm_l7;
                    uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t46 = (t44 < t45);
                    if (!t46) goto lb43;
                    {
                        uint64_t t49 = gm_l7;
                        uint64_t t50 = (uintptr_t)t49;
                        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        gm_tplpbqf3duvn3 t54 = GM_LD(gm_tplpbqf3duvn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t50 * 8));
                        gm_l8 = t54;
                        {
                            uint32_t t57 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
                            uint32_t t58 = gm_l5;
                            uint8_t t59 = ((t57 == t58));
                            if (!t59) goto ie56;
                            {
                                gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l8) + 4));
                                uint8_t t62 = 0;
                                F.ret = t61;
                                goto gm_fret;
                            }
                            goto ix56;
ie56:;
ix56:;
                        }
                        goto me52;
mn53:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 683, 13);
me52:;
                    }
lc43:;
                    {
                        uint64_t t64 = gm_l7;
                        uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                        uint64_t t67 = r65;
                        gm_l7 = t67;
                    }
                }
lb43:;
            }
        }
        {
            gm_t88rcucg1b7uk t68 = gm_l1;
            uint8_t t69 = 0;
            F.ret = t68;
            goto gm_fret;
        }
        goto me18;
mn37:;
ma20:;
        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t71 != ((uint8_t)17ULL)) goto mn70;
        uint32_t t72 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l9 = t72;
        gm_b16_8 t73 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 8));
        gm_l10 = t73;
        {
            uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
            uint8_t t77 = ((t76 == ((uint64_t)0ULL)));
            if (!t77) goto ie75;
            {
                gm_t88rcucg1b7uk t79 = gm_l1;
                uint8_t t80 = 0;
                F.ret = t79;
                goto gm_fret;
            }
            goto ix75;
ie75:;
ix75:;
        }
        {
            void * t82 = gm_l0;
            void * t83 = gm_l3;
            uint32_t t84 = gm_l9;
            uint32_t t85 = gm_f_template_9o92cvv(t83, t84);
            gm_tbm0n9ujhrlhf t86;
            gm_f_soa_shape_of_ohenvjc((void *)&t86, t82, t85);
            gm_tbm0n9ujhrlhf t87 = t86;
            if (!((((uint8_t *)&t87))[24] != 0)) goto ie81;
            gm_th3eueiipivq6 t88 = GM_LD(gm_th3eueiipivq6, ((uint8_t *)&t87));
            gm_l11 = t88;
            {
                void * t90 = gm_l3;
                gm_t88rcucg1b7uk t91 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t90);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t91;
            }
            {
                uint64_t t93 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                if (t93 >= (uintptr_t)(t94)) gm_trap_index(t93, t94, gm_src3k278vcmqgdr, 693, 32);
                uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t93 * 40) + 0));
                if (t95 != ((uint8_t)0ULL)) goto ie92;
                gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t93 * 40) + 8));
                gm_l13 = t96;
                {
                    void * t98 = gm_l0;
                    gm_t88rcucg1b7uk t99 = gm_l13;
                    gm_b16_8 t100 = gm_l2;
                    gm_t88rcucg1b7uk t101 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t98, t99, t100);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l12 = t101;
                }
                goto ix92;
ie92:;
ix92:;
            }
            {
                void * t102 = gm_l0;
                gm_t88rcucg1b7uk t103 = gm_l12;
                gm_t88rcucg1b7uk t104 = gm_f_subst_soa_2chqte3((void *)NULL, cx, NULL, t102, t103, &(*(gm_th3eueiipivq6 *)((uint8_t *)&gm_l11)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t105 = 0;
                F.ret = t104;
                goto gm_fret;
            }
            goto ix81;
ie81:;
ix81:;
        }
        {
            gm_f_list_nof7vfj((void *)&F.l14, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t107 = gm_l10;
                gm_l15 = t107;
            }
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t109 = gm_l16;
                    uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t111 = (t109 < t110);
                    if (!t111) goto lb108;
                    {
                        uint64_t t114 = gm_l16;
                        uint64_t t115 = (uintptr_t)t114;
                        uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        gm_toa2vhe3l4hfr t119 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t115 * 40));
                        F.l17 = t119;
                        {
                            void * t121 = (void *)((uint8_t *)&F.l14);
                            gm_t88rcucg1b7uk t122;
                            uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                            if (t125 != ((uint8_t)0ULL)) goto mn124;
                            gm_t88rcucg1b7uk t126 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 8));
                            gm_l18 = t126;
                            void * t127 = gm_l0;
                            gm_t88rcucg1b7uk t128 = gm_l18;
                            gm_b16_8 t129 = gm_l2;
                            gm_t88rcucg1b7uk t130 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t127, t128, t129);
                            if (gm_unw.kind) {
                                goto gm_ll112;
                            }
                            t122 = t130;
                            goto me123;
mn124:;
                            uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                            if (t132 != ((uint8_t)1ULL)) goto mn131;
                            void * t133 = gm_l0;
                            void * t134 = (void *)(((uint8_t *)t133) + 8);
                            gm_t88rcucg1b7uk t135 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t134);
                            if (gm_unw.kind) {
                                goto gm_ll112;
                            }
                            t122 = t135;
                            goto me123;
mn131:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 698, 31);
me123:;
                            gm_tftb1odc739t5 t136 = gm_f_push_k3l9nic(t121, GM_CONV(gm_csguankko4lmhd, t122));
                            gm_tftb1odc739t5 t137 = t136;
                            if (((((uint8_t *)&t137))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t137) + 1, gm_src3k278vcmqgdr, 698, 17);
                            gm_b0_1 t138 = GM_LD(gm_b0_1, (((uint8_t *)&t137) + 1));
                        }
                        goto me117;
mn118:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 697, 13);
me117:;
                    }
lc108:;
                    {
                        uint64_t t140 = gm_l16;
                        uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
                        uint64_t t143 = r141;
                        gm_l16 = t143;
                    }
                }
gm_ll112:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb108; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc108; }
                }
                goto gm_lad;
lb108:;
            }
        }
        {
            void * t144 = gm_l0;
            void * t145 = gm_l0;
            void * t146 = (void *)(((uint8_t *)t145) + 8);
            uint32_t t147 = gm_l9;
            uint32_t t148 = gm_f_template_9o92cvv(t146, t147);
            gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
            gm_b16_8 t150 = t149;
            gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
            gm_t88rcucg1b7uk t152 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t144, t148, t151);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t153 = 0;
            F.ret = t152;
            goto gm_fret;
        }
        goto me18;
mn70:;
ma21:;
        uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t155 != ((uint8_t)15ULL)) goto mn154;
        gm_tddtjfggdat0d t156 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&F.l4) + 8));
        F.l19 = t156;
        void * t157 = gm_l0;
        void * t158 = (void *)(((uint8_t *)t157) + 8);
        void * t159 = gm_l0;
        void * t160 = (void *)((uint8_t *)&F.l19);
        gm_b16_8 t161 = gm_l2;
        gm_tddtjfggdat0d t162;
        gm_f_subst_sig_9ngh936((void *)&t162, (void *)NULL, cx, NULL, t159, t160, t161);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t163;
        GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)15ULL));
        GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t163 + 8), t162);
        gm_t88rcucg1b7uk t164 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t158, &t163);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t165 = 0;
        F.ret = t164;
        goto gm_fret;
        goto me18;
mn154:;
ma22:;
        uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t167 != ((uint8_t)16ULL)) goto mn166;
        gm_tddtjfggdat0d t168 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&F.l4) + 8));
        F.l20 = t168;
        void * t169 = gm_l0;
        void * t170 = (void *)(((uint8_t *)t169) + 8);
        void * t171 = gm_l0;
        void * t172 = (void *)((uint8_t *)&F.l20);
        gm_b16_8 t173 = gm_l2;
        gm_tddtjfggdat0d t174;
        gm_f_subst_sig_9ngh936((void *)&t174, (void *)NULL, cx, NULL, t171, t172, t173);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t175;
        GM_ST(uint8_t, (uint8_t *)&t175 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t175 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t175 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)16ULL));
        GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t175 + 8), t174);
        gm_t88rcucg1b7uk t176 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t170, &t175);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t177 = 0;
        F.ret = t176;
        goto gm_fret;
        goto me18;
mn166:;
ma23:;
        uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t179 != ((uint8_t)8ULL)) goto mn178;
        uint8_t t180 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l21 = t180;
        gm_t88rcucg1b7uk t181 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l22 = t181;
        void * t182 = gm_l0;
        void * t183 = (void *)(((uint8_t *)t182) + 8);
        uint8_t t184 = gm_l21;
        void * t185 = gm_l0;
        gm_t88rcucg1b7uk t186 = gm_l22;
        gm_b16_8 t187 = gm_l2;
        gm_t88rcucg1b7uk t188 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t185, t186, t187);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t189 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t183, t184, t188);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t190 = 0;
        F.ret = t189;
        goto gm_fret;
        goto me18;
mn178:;
ma24:;
        uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t192 != ((uint8_t)9ULL)) goto mn191;
        uint8_t t193 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l23 = t193;
        gm_t88rcucg1b7uk t194 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l24 = t194;
        void * t195 = gm_l0;
        void * t196 = (void *)(((uint8_t *)t195) + 8);
        uint8_t t197 = gm_l23;
        void * t198 = gm_l0;
        gm_t88rcucg1b7uk t199 = gm_l24;
        gm_b16_8 t200 = gm_l2;
        gm_t88rcucg1b7uk t201 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t198, t199, t200);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t202 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t196, t197, t201);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t203 = 0;
        F.ret = t202;
        goto gm_fret;
        goto me18;
mn191:;
ma25:;
        uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t205 != ((uint8_t)10ULL)) goto mn204;
        uint8_t t206 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l25 = t206;
        gm_t88rcucg1b7uk t207 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l26 = t207;
        void * t208 = gm_l0;
        void * t209 = (void *)(((uint8_t *)t208) + 8);
        uint8_t t210 = gm_l25;
        void * t211 = gm_l0;
        gm_t88rcucg1b7uk t212 = gm_l26;
        gm_b16_8 t213 = gm_l2;
        gm_t88rcucg1b7uk t214 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t211, t212, t213);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t215 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t209, t210, t214);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t216 = 0;
        F.ret = t215;
        goto gm_fret;
        goto me18;
mn204:;
ma26:;
        uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t218 != ((uint8_t)11ULL)) goto mn217;
        uint64_t t219 = GM_LD(uint64_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l27 = t219;
        gm_t88rcucg1b7uk t220 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 8));
        gm_l28 = t220;
        gm_tnnjeerolos0a t221 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&F.l4) + 8) + 12));
        gm_l29 = t221;
        {
            gm_tnnjeerolos0a t223 = gm_l29;
            gm_l30 = t223;
        }
        {
            uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
            if (t225 != ((uint8_t)1ULL)) goto ie224;
            gm_t88rcucg1b7uk t226 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l29) + 4));
            gm_l31 = t226;
            {
                void * t228 = gm_l0;
                gm_t88rcucg1b7uk t229 = gm_l31;
                gm_b16_8 t230 = gm_l2;
                gm_t88rcucg1b7uk t231 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t228, t229, t230);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnnjeerolos0a t232;
                GM_ST(uint8_t, (uint8_t *)&t232 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t232 + 2, 0);
                GM_ST(uint8_t, ((uint8_t *)&t232 + 0), ((uint8_t)1ULL));
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t232 + 4), t231);
                gm_l30 = t232;
            }
            goto ix224;
ie224:;
ix224:;
        }
        {
            void * t233 = gm_l0;
            void * t234 = (void *)(((uint8_t *)t233) + 8);
            gm_t8nckeqfenl8a t235;
            memset((uint8_t *)&t235 + 20, 0, 4);
            uint64_t t236 = gm_l27;
            GM_ST(uint64_t, (uint8_t *)&t235 + 0, t236);
            void * t237 = gm_l0;
            gm_t88rcucg1b7uk t238 = gm_l28;
            gm_b16_8 t239 = gm_l2;
            gm_t88rcucg1b7uk t240 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t237, t238, t239);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t235 + 8, t240);
            gm_tnnjeerolos0a t241 = gm_l30;
            GM_ST(gm_tnnjeerolos0a, (uint8_t *)&t235 + 12, t241);
            gm_t53at80pfdus3 t242;
            GM_ST(uint8_t, (uint8_t *)&t242 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t242 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t242 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t242 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t242 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t242 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t242 + 0), ((uint8_t)11ULL));
            GM_ST(gm_t8nckeqfenl8a, ((uint8_t *)&t242 + 8), t235);
            gm_t88rcucg1b7uk t243 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t234, &t242);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t244 = 0;
            F.ret = t243;
            goto gm_fret;
        }
        goto me18;
mn217:;
ma27:;
        uint8_t t246 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t246 != ((uint8_t)20ULL)) goto mn245;
        gm_t88rcucg1b7uk t247 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l32 = t247;
        gm_t88rcucg1b7uk t248 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l33 = t248;
        {
            void * t250 = gm_l0;
            gm_t88rcucg1b7uk t251 = gm_l32;
            gm_b16_8 t252 = gm_l2;
            gm_t88rcucg1b7uk t253 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t250, t251, t252);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l34 = t253;
        }
        {
            void * t254 = gm_l0;
            gm_t88rcucg1b7uk t255 = gm_l33;
            gm_b16_8 t256 = gm_l2;
            gm_t88rcucg1b7uk t257 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t254, t255, t256);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l35 = t257;
        }
        {
            void * t258 = gm_l0;
            void * t259 = (void *)(((uint8_t *)t258) + 8);
            gm_t88rcucg1b7uk t260 = gm_l34;
            gm_t53at80pfdus3 t261;
            gm_f_kind_jcnt4hm((void *)&t261, t259, GM_CONV(gm_csguankko4lmhd, t260));
            gm_t53at80pfdus3 t262 = t261;
            uint8_t t268 = GM_LD(uint8_t, (((uint8_t *)&t262) + 0));
            switch (t268) { case ((uint8_t)0ULL): goto ma266; case ((uint8_t)1ULL): goto ma266; case ((uint8_t)2ULL): goto ma266; case ((uint8_t)3ULL): goto ma266; case ((uint8_t)4ULL): goto ma266; case ((uint8_t)5ULL): goto ma266; case ((uint8_t)6ULL): goto ma266; case ((uint8_t)7ULL): goto ma266; case ((uint8_t)8ULL): goto ma266; case ((uint8_t)9ULL): goto ma266; case ((uint8_t)10ULL): goto ma266; case ((uint8_t)11ULL): goto ma266; case ((uint8_t)12ULL): goto ma266; case ((uint8_t)13ULL): goto ma266; case ((uint8_t)14ULL): goto ma266; case ((uint8_t)15ULL): goto ma266; case ((uint8_t)16ULL): goto ma266; case ((uint8_t)17ULL): goto ma266; case ((uint8_t)18ULL): goto ma266; case ((uint8_t)19ULL): goto ma266; case ((uint8_t)20ULL): goto ma266; case ((uint8_t)21ULL): goto ma264; case ((uint8_t)22ULL): goto ma266; case ((uint8_t)23ULL): goto ma266; case ((uint8_t)24ULL): goto ma266; case ((uint8_t)25ULL): goto ma266; case ((uint8_t)26ULL): goto ma265; default: goto ma266; }
ma264:;
            uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&t262) + 0));
            if (t270 != ((uint8_t)21ULL)) goto mn269;
            gm_t428ue2u3hf5g t271 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&t262) + 8));
            gm_l36 = t271;
            {
                gm_tf5srh4mp4b3i t273 = gm_f_key_u64_ljklb56(&(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l36)));
                gm_tf5srh4mp4b3i t274 = t273;
                uint64_t t275;
                if (((((uint8_t *)&t274))[8] != 0)) {
                    uint64_t t276 = GM_LD(uint64_t, (((uint8_t *)&t274) + 0));
                    t275 = t276;
                } else {
                    void * t277 = gm_l0;
                    void * t278 = (void *)(((uint8_t *)t277) + 8);
                    gm_t88rcucg1b7uk t279 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t278);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t280 = 0;
                    F.ret = t279;
                    goto gm_fret;
                }
                gm_l37 = t275;
            }
            {
                void * t281 = gm_l0;
                void * t282 = (void *)(((uint8_t *)t281) + 8);
                uint64_t t283 = gm_l37;
                gm_t88rcucg1b7uk t284 = gm_l35;
                gm_t88rcucg1b7uk t285 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t282, t283, t284);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t286 = 0;
                F.ret = t285;
                goto gm_fret;
            }
            goto me263;
mn269:;
ma265:;
            uint8_t t288 = GM_LD(uint8_t, (((uint8_t *)&t262) + 0));
            if (t288 != ((uint8_t)26ULL)) goto mn287;
            void * t289 = gm_l0;
            void * t290 = (void *)(((uint8_t *)t289) + 8);
            gm_t88rcucg1b7uk t291 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t290);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t292 = 0;
            F.ret = t291;
            goto gm_fret;
            goto me263;
mn287:;
ma266:;
            void * t294 = gm_l0;
            void * t295 = (void *)(((uint8_t *)t294) + 8);
            gm_t88rcucg1b7uk t296 = gm_l34;
            gm_t88rcucg1b7uk t297 = gm_l35;
            gm_tgglga8ro3isc t298;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t298 + 0), t296);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t298 + 4), t297);
            gm_t53at80pfdus3 t299;
            memset(&t299, 0, sizeof t299);
            GM_ST(uint8_t, ((uint8_t *)&t299 + 0), ((uint8_t)20ULL));
            GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t299 + 8), t298);
            gm_t88rcucg1b7uk t300 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t295, &t299);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t301 = 0;
            F.ret = t300;
            goto gm_fret;
            goto me263;
mn293:;
ma267:;
            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 718, 13);
me263:;
        }
        goto me18;
mn245:;
ma28:;
        uint8_t t303 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t303 != ((uint8_t)22ULL)) goto mn302;
        gm_t9qkh0rsooc8d t304 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l38 = t304;
        gm_t88rcucg1b7uk t305 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l39 = t305;
        gm_t88rcucg1b7uk t306 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 8));
        gm_l40 = t306;
        {
            void * t308 = gm_l0;
            gm_t88rcucg1b7uk t309 = gm_l39;
            gm_b16_8 t310 = gm_l2;
            gm_t88rcucg1b7uk t311 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t308, t309, t310);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l41 = t311;
        }
        {
            void * t312 = gm_l0;
            gm_t88rcucg1b7uk t313 = gm_l40;
            gm_b16_8 t314 = gm_l2;
            gm_t88rcucg1b7uk t315 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t312, t313, t314);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l42 = t315;
        }
        {
            void * t316 = gm_l0;
            void * t317 = (void *)(((uint8_t *)t316) + 8);
            gm_t9qkh0rsooc8d t318 = gm_l38;
            gm_t88rcucg1b7uk t319 = gm_l41;
            gm_t88rcucg1b7uk t320 = gm_l42;
            gm_t88rcucg1b7uk t321 = gm_f_len_expr_i134fvg((void *)NULL, cx, NULL, t317, t318, t319, t320);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t322 = 0;
            F.ret = t321;
            goto gm_fret;
        }
        goto me18;
mn302:;
ma29:;
        uint8_t t324 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t324 != ((uint8_t)13ULL)) goto mn323;
        gm_t88rcucg1b7uk t325 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
        gm_l43 = t325;
        void * t326 = gm_l0;
        void * t327 = (void *)(((uint8_t *)t326) + 8);
        void * t328 = gm_l0;
        gm_t88rcucg1b7uk t329 = gm_l43;
        gm_b16_8 t330 = gm_l2;
        gm_t88rcucg1b7uk t331 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t328, t329, t330);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t332 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t327, t331);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t333 = 0;
        F.ret = t332;
        goto gm_fret;
        goto me18;
mn323:;
ma30:;
        uint8_t t335 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t335 != ((uint8_t)18ULL)) goto mn334;
        gm_t88rcucg1b7uk t336 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
        gm_l44 = t336;
        void * t337 = gm_l0;
        void * t338 = (void *)(((uint8_t *)t337) + 8);
        void * t339 = gm_l0;
        gm_t88rcucg1b7uk t340 = gm_l44;
        gm_b16_8 t341 = gm_l2;
        gm_t88rcucg1b7uk t342 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t339, t340, t341);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t343;
        memset(&t343, 0, sizeof t343);
        GM_ST(uint8_t, ((uint8_t *)&t343 + 0), ((uint8_t)18ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t343 + 8), t342);
        gm_t88rcucg1b7uk t344 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t338, &t343);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t345 = 0;
        F.ret = t344;
        goto gm_fret;
        goto me18;
mn334:;
ma31:;
        uint8_t t347 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t347 != ((uint8_t)14ULL)) goto mn346;
        gm_t88rcucg1b7uk t348 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l45 = t348;
        gm_t88rcucg1b7uk t349 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l46 = t349;
        {
            void * t351 = gm_l0;
            gm_t88rcucg1b7uk t352 = gm_l45;
            gm_b16_8 t353 = gm_l2;
            gm_t88rcucg1b7uk t354 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t351, t352, t353);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l47 = t354;
        }
        {
            void * t355 = gm_l0;
            gm_t88rcucg1b7uk t356 = gm_l46;
            gm_b16_8 t357 = gm_l2;
            gm_t88rcucg1b7uk t358 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t355, t356, t357);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l48 = t358;
        }
        {
            void * t359 = gm_l0;
            void * t360 = (void *)(((uint8_t *)t359) + 8);
            gm_t88rcucg1b7uk t361 = gm_l47;
            gm_t88rcucg1b7uk t362 = gm_l48;
            gm_t88rcucg1b7uk t363 = gm_f_result_o9ras10((void *)NULL, cx, NULL, t360, t361, t362);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t364 = 0;
            F.ret = t363;
            goto gm_fret;
        }
        goto me18;
mn346:;
ma32:;
        uint8_t t366 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t366 != ((uint8_t)12ULL)) goto mn365;
        gm_b16_8 t367 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l49 = t367;
        gm_b16_8 t368 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 16));
        gm_l50 = t368;
        {
            gm_f_list_nof7vfj((void *)&F.l51, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t371 = gm_l49;
                gm_l52 = t371;
            }
            {
                gm_l53 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t373 = gm_l53;
                    uint64_t t374 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                    uint8_t t375 = (t373 < t374);
                    if (!t375) goto lb372;
                    {
                        uint64_t t378 = gm_l53;
                        uint64_t t379 = (uintptr_t)t378;
                        uint64_t t380 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                        gm_t88rcucg1b7uk t383 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l52)) + t379 * 4));
                        gm_l54 = t383;
                        {
                            void * t385 = (void *)((uint8_t *)&F.l51);
                            void * t386 = gm_l0;
                            gm_t88rcucg1b7uk t387 = gm_l54;
                            gm_b16_8 t388 = gm_l2;
                            gm_t88rcucg1b7uk t389 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t386, t387, t388);
                            if (gm_unw.kind) {
                                goto gm_ll376;
                            }
                            gm_tftb1odc739t5 t390 = gm_f_push_k3l9nic(t385, GM_CONV(gm_csguankko4lmhd, t389));
                            gm_tftb1odc739t5 t391 = t390;
                            if (((((uint8_t *)&t391))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t391) + 1, gm_src3k278vcmqgdr, 741, 30);
                            gm_b0_1 t392 = GM_LD(gm_b0_1, (((uint8_t *)&t391) + 1));
                        }
                        goto me381;
mn382:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 741, 13);
me381:;
                    }
lc372:;
                    {
                        uint64_t t394 = gm_l53;
                        uint64_t r395; bool o396 = __builtin_add_overflow(t394, ((uint64_t)1ULL), &r395);
                        uint64_t t397 = r395;
                        gm_l53 = t397;
                    }
                }
gm_ll376:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb372; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc372; }
                }
                goto gm_lad;
lb372:;
            }
        }
        {
            void * t398 = gm_l0;
            void * t399 = (void *)(((uint8_t *)t398) + 8);
            gm_t6j6smj7qoctu t400;
            gm_b16_8 t401 = GM_LD(gm_b16_8, (((uint8_t *)&F.l51) + 0));
            gm_b16_8 t402 = t401;
            gm_b16_8 t403 = GM_LD(gm_b16_8, ((uint8_t *)&t402));
            GM_ST(gm_b16_8, (uint8_t *)&t400 + 0, t403);
            gm_b16_8 t404 = gm_l50;
            GM_ST(gm_b16_8, (uint8_t *)&t400 + 16, t404);
            gm_t53at80pfdus3 t405;
            GM_ST(uint8_t, (uint8_t *)&t405 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t405 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t405 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t405 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t405 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t405 + 0), ((uint8_t)12ULL));
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t405 + 8), t400);
            gm_t88rcucg1b7uk t406 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t399, &t405);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t407 = 0;
            F.ret = t406;
            goto gm_fret;
        }
        goto me18;
mn365:;
ma33:;
        uint8_t t409 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t409 != ((uint8_t)24ULL)) goto mn408;
        gm_tnpifv20irnph t410 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l4) + 8));
        gm_l55 = t410;
        void * t411 = gm_l0;
        void * t412 = (void *)(((uint8_t *)t411) + 8);
        void * t413 = gm_l0;
        void * t414 = (void *)((uint8_t *)&gm_l55);
        gm_b16_8 t415 = gm_l2;
        gm_tnpifv20irnph t416 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t413, t414, t415);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t417;
        memset(&t417, 0, sizeof t417);
        GM_ST(uint8_t, ((uint8_t *)&t417 + 0), ((uint8_t)24ULL));
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t417 + 8), t416);
        gm_t88rcucg1b7uk t418 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t412, &t417);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t419 = 0;
        F.ret = t418;
        goto gm_fret;
        goto me18;
mn408:;
ma34:;
        gm_t88rcucg1b7uk t421 = gm_l1;
        uint8_t t422 = 0;
        F.ret = t421;
        goto gm_fret;
        goto me18;
mn420:;
ma35:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 681, 5);
me18:;
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
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_subst_effects_jnlbu5m F;
    void * gm_l0; /* s */
    void * gm_l1; /* set */
    gm_b16_8 gm_l2; /* map */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tiilmfd16v3fc gm_l6; /* e */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t88rcucg1b7uk gm_l11; /* t */
    GM_STACK_CHECK(&F, 744u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_084fcd6((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l4 = t4;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l5;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l5;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_tiilmfd16v3fc t16 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t12 * 24));
                    gm_l6 = t16;
                    {
                        gm_f_list_vmvl81b((void *)&F.l7, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        {
                            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                            gm_l8 = t19;
                        }
                        {
                            gm_l9 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t21 = gm_l9;
                                uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                uint8_t t23 = (t21 < t22);
                                if (!t23) goto lb20;
                                {
                                    uint64_t t26 = gm_l9;
                                    uint64_t t27 = (uintptr_t)t26;
                                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                    gm_toa2vhe3l4hfr t31 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t27 * 40));
                                    F.l10 = t31;
                                    {
                                        void * t33 = (void *)((uint8_t *)&F.l7);
                                        gm_toa2vhe3l4hfr t34;
                                        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                                        if (t37 != ((uint8_t)0ULL)) goto mn36;
                                        gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 8));
                                        gm_l11 = t38;
                                        void * t39 = gm_l0;
                                        gm_t88rcucg1b7uk t40 = gm_l11;
                                        gm_b16_8 t41 = gm_l2;
                                        gm_t88rcucg1b7uk t42 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t39, t40, t41);
                                        if (gm_unw.kind) {
                                            goto gm_ll24;
                                        }
                                        gm_toa2vhe3l4hfr t43;
                                        memset(&t43, 0, sizeof t43);
                                        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)0ULL));
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t43 + 8), t42);
                                        t34 = t43;
                                        goto me35;
mn36:;
                                        gm_toa2vhe3l4hfr t45 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)&F.l10));
                                        F.l12 = t45;
                                        gm_toa2vhe3l4hfr t46 = F.l12;
                                        t34 = t46;
                                        goto me35;
mn44:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 764, 23);
me35:;
                                        gm_tftb1odc739t5 t47 = gm_f_push_s5vr2a2(t33, &t34);
                                        gm_tftb1odc739t5 t48 = t47;
                                        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_src3k278vcmqgdr, 764, 13);
                                        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                                    }
                                    goto me29;
mn30:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 763, 9);
me29:;
                                }
lc20:;
                                {
                                    uint64_t t51 = gm_l9;
                                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                                    uint64_t t54 = r52;
                                    gm_l9 = t54;
                                }
                            }
gm_ll24:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb20; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc20; }
                            }
                            goto gm_ll9;
lb20:;
                        }
                    }
                    {
                        void * t55 = (void *)((uint8_t *)&F.l3);
                        gm_tiilmfd16v3fc t56;
                        memset((uint8_t *)&t56 + 4, 0, 4);
                        uint32_t t57 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                        GM_ST(uint32_t, (uint8_t *)&t56 + 0, t57);
                        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                        gm_b16_8 t59 = t58;
                        gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                        GM_ST(gm_b16_8, (uint8_t *)&t56 + 8, t60);
                        gm_tftb1odc739t5 t61 = gm_f_push_d1r4h19(t55, &t56);
                        gm_tftb1odc739t5 t62 = t61;
                        if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_src3k278vcmqgdr, 769, 9);
                        gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 761, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t65 = gm_l5;
                    uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                    uint64_t t68 = r66;
                    gm_l5 = t68;
                }
            }
gm_ll9:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb5; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc5; }
            }
            goto gm_lad;
lb5:;
        }
    }
    {
        void * t69 = gm_l0;
        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t71 = t70;
        gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
        gm_tnpifv20irnph t73 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t74 = gm_f_flatten_2js7j92((void *)NULL, cx, NULL, t69, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t75 = 0;
        F.ret = t74;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_subst_fields_pqf9lb0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_subst_fields_pqf9lb0 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* fields */
    gm_b16_8 gm_l2; /* map */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 343u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_spqn36o((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t0mglksrkfa5e t15 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 40));
                    F.l6 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l3);
                        gm_t0mglksrkfa5e t18;
                        memset((uint8_t *)&t18 + 21, 0, 3);
                        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t19);
                        void * t20 = gm_l0;
                        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 16));
                        gm_b16_8 t22 = gm_l2;
                        gm_t88rcucg1b7uk t23 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t20, t21, t22);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t18 + 16, t23);
                        gm_t9qkh0rsooc8d t24 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l6) + 20));
                        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t18 + 20, t24);
                        gm_tf5srh4mp4b3i t25 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l6) + 24));
                        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t18 + 24, t25);
                        gm_tftb1odc739t5 t26 = gm_f_push_vrmo7c5(t17, &t18);
                        gm_tftb1odc739t5 t27 = t26;
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_src3k278vcmqgdr, 640, 23);
                        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 640, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t30 = gm_l5;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l5 = t33;
                }
            }
gm_ll8:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb4; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc4; }
            }
            goto gm_lad;
lb4:;
        }
    }
    {
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        uint8_t t35 = 0;
        F.ret = t34;
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
inline __attribute__((aligned(4))) void gm_f_subst_sig_9ngh936(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_subst_sig_9ngh936 F;
    void * gm_l0; /* s */
    void * gm_l1; /* sig */
    gm_b16_8 gm_l2; /* map */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* p */
    gm_t88rcucg1b7uk gm_l7; /* ret */
    gm_tnpifv20irnph gm_l8; /* effects */
    GM_STACK_CHECK(&F, 382u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_nof7vfj((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l4 = t4;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l5;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l5;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t12 * 4));
                    gm_l6 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l3);
                        void * t19 = gm_l0;
                        gm_t88rcucg1b7uk t20 = gm_l6;
                        gm_b16_8 t21 = gm_l2;
                        gm_t88rcucg1b7uk t22 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t19, t20, t21);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tftb1odc739t5 t23 = gm_f_push_k3l9nic(t18, GM_CONV(gm_csguankko4lmhd, t22));
                        gm_tftb1odc739t5 t24 = t23;
                        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_src3k278vcmqgdr, 751, 27);
                        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 751, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t27 = gm_l5;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l5 = t30;
                }
            }
gm_ll9:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb5; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc5; }
            }
            goto gm_lad;
lb5:;
        }
    }
    {
        void * t31 = gm_l0;
        void * t32 = gm_l1;
        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t32) + 16));
        gm_b16_8 t34 = gm_l2;
        gm_t88rcucg1b7uk t35 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t31, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t35;
    }
    {
        void * t36 = gm_l0;
        void * t37 = gm_l1;
        void * t38 = (void *)(((uint8_t *)t37) + 24);
        gm_b16_8 t39 = gm_l2;
        gm_tnpifv20irnph t40 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t36, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t40;
    }
    {
        memset((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 20, 0, 4);
        memset((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 41, 0, 7);
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t42 = t41;
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
        gm_t88rcucg1b7uk t44 = gm_l7;
        gm_tnpifv20irnph t45 = gm_l8;
        void * t46 = gm_l1;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)t46) + 40));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 0, t43);
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 16, t44);
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 24, t45);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 40, t47);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_taken_struct_members_1ltbb18(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_taken_struct_members_1ltbb18 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* members */
    void * gm_l3; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 629u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_tfc3oljlofib6 t15 = GM_LD(gm_tfc3oljlofib6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 80));
                    F.l6 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        gm_t24301fas1b2a t20 = GM_LD(gm_t24301fas1b2a, (((uint8_t *)&F.l6) + 8));
                        F.l7 = t20;
                        void * t21 = gm_l3;
                        gm_tftb1odc739t5 t22 = gm_f_push_ghqse9f(t21, &(*(gm_t24301fas1b2a *)((uint8_t *)&F.l7)));
                        gm_tftb1odc739t5 t23 = t22;
                        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_src3k278vcmqgdr, 518, 26);
                        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
                        goto me17;
mn18:;
                        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t26 != ((uint8_t)1ULL)) goto mn25;
                        gm_t7bjbmukv26e7 t27 = GM_LD(gm_t7bjbmukv26e7, (((uint8_t *)&F.l6) + 8));
                        F.l8 = t27;
                        void * t28 = gm_l0;
                        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
                        uint64_t t30 = gm_l1;
                        void * t31 = GM_LD(void *, (((uint8_t *)&F.l8) + 0));
                        gm_tqs5p4n2tq19v t32 = gm_f_when_cond_9a46a5h((void *)NULL, cx, NULL, t29, t30, t31);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tqs5p4n2tq19v t33 = t32;
                        if (!((((uint8_t *)&t33))[1] != 0)) goto mn35;
                        uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)&t33));
                        if (!(t36 == ((uint8_t)1))) goto mn35;
                        void * t37 = gm_l0;
                        uint64_t t38 = gm_l1;
                        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
                        gm_b16_8 t40 = t39;
                        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                        void * t42 = gm_l3;
                        gm_b0_1 t43 = gm_f_taken_struct_members_1ltbb18((void *)NULL, cx, NULL, t37, t38, t41, t42);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me34;
mn35:;
                        if (!((((uint8_t *)&t33))[1] != 0)) goto mn44;
                        uint8_t t45 = GM_LD(uint8_t, ((uint8_t *)&t33));
                        if (!(t45 == ((uint8_t)0))) goto mn44;
                        void * t46 = gm_l0;
                        uint64_t t47 = gm_l1;
                        gm_th2lqbo77pvsc t48 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l8) + 24));
                        gm_th2lqbo77pvsc t49 = t48;
                        gm_b16_8 t50;
                        if (((((uint8_t *)&t49))[16] != 0)) {
                            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 0));
                            t50 = t51;
                        } else {
                            gm_b16_8 t52 = gm_f_empty_slice_d53jqcq();
                            t50 = t52;
                        }
                        gm_b16_8 t53 = t50;
                        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                        void * t55 = gm_l3;
                        gm_b0_1 t56 = gm_f_taken_struct_members_1ltbb18((void *)NULL, cx, NULL, t46, t47, t54, t55);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me34;
mn44:;
                        if (((((uint8_t *)&t33))[1] != 0)) goto mn57;
                        goto me34;
mn57:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 519, 25);
me34:;
                        goto me17;
mn25:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 517, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 516, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t60 = gm_l5;
                    uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                    uint64_t t63 = r61;
                    gm_l5 = t63;
                }
            }
gm_ll8:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb4; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc4; }
            }
            goto gm_lad;
lb4:;
        }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_taken_union_members_pavth81(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_taken_union_members_pavth81 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* members */
    void * gm_l3; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 613u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t03a3imodqi32 t15 = GM_LD(gm_t03a3imodqi32, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 72));
                    F.l6 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        gm_tnljbma3s3h16 t20 = GM_LD(gm_tnljbma3s3h16, (((uint8_t *)&F.l6) + 8));
                        F.l7 = t20;
                        void * t21 = gm_l3;
                        gm_tftb1odc739t5 t22 = gm_f_push_83inprj(t21, &(*(gm_tnljbma3s3h16 *)((uint8_t *)&F.l7)));
                        gm_tftb1odc739t5 t23 = t22;
                        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_src3k278vcmqgdr, 532, 28);
                        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
                        goto me17;
mn18:;
                        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t26 != ((uint8_t)1ULL)) goto mn25;
                        gm_t7bjbmukv26e7 t27 = GM_LD(gm_t7bjbmukv26e7, (((uint8_t *)&F.l6) + 8));
                        F.l8 = t27;
                        void * t28 = gm_l0;
                        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
                        uint64_t t30 = gm_l1;
                        void * t31 = GM_LD(void *, (((uint8_t *)&F.l8) + 0));
                        gm_tqs5p4n2tq19v t32 = gm_f_when_cond_9a46a5h((void *)NULL, cx, NULL, t29, t30, t31);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tqs5p4n2tq19v t33 = t32;
                        if (!((((uint8_t *)&t33))[1] != 0)) goto mn35;
                        uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)&t33));
                        if (!(t36 == ((uint8_t)1))) goto mn35;
                        void * t37 = gm_l0;
                        uint64_t t38 = gm_l1;
                        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
                        gm_b16_8 t40 = t39;
                        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                        void * t42 = gm_l3;
                        gm_b0_1 t43 = gm_f_taken_union_members_pavth81((void *)NULL, cx, NULL, t37, t38, t41, t42);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me34;
mn35:;
                        if (!((((uint8_t *)&t33))[1] != 0)) goto mn44;
                        uint8_t t45 = GM_LD(uint8_t, ((uint8_t *)&t33));
                        if (!(t45 == ((uint8_t)0))) goto mn44;
                        void * t46 = gm_l0;
                        uint64_t t47 = gm_l1;
                        gm_th2lqbo77pvsc t48 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l8) + 24));
                        gm_th2lqbo77pvsc t49 = t48;
                        gm_b16_8 t50;
                        if (((((uint8_t *)&t49))[16] != 0)) {
                            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 0));
                            t50 = t51;
                        } else {
                            gm_b16_8 t52 = gm_f_empty_slice_u4e74ha();
                            t50 = t52;
                        }
                        gm_b16_8 t53 = t50;
                        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                        void * t55 = gm_l3;
                        gm_b0_1 t56 = gm_f_taken_union_members_pavth81((void *)NULL, cx, NULL, t46, t47, t54, t55);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me34;
mn44:;
                        if (((((uint8_t *)&t33))[1] != 0)) goto mn57;
                        goto me34;
mn57:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 533, 25);
me34:;
                        goto me17;
mn25:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 531, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 530, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t60 = gm_l5;
                    uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                    uint64_t t63 = r61;
                    gm_l5 = t63;
                }
            }
gm_ll8:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb4; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc4; }
            }
            goto gm_lad;
lb4:;
        }
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
__attribute__((aligned(4))) void gm_f_union_kind_j2gcmto(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_union_kind_j2gcmto F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* u */
    gm_b16_8 gm_l3; /* attrs */
    uint8_t gm_l4; /* c_union */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l8; /* other */
    gm_tkn42u7na3t81 gm_l9; /* explicit_tag */
    void * gm_l10; /* t */
    gm_t88rcucg1b7uk gm_l11; /* tag */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_tkn42u7na3t81 gm_l22; /* payload */
    void * gm_l23; /* p */
    void * gm_l25; /* val */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* i */
    gm_t88rcucg1b7uk gm_l33; /* p */
    uint64_t gm_l34; /* n */
    gm_t88rcucg1b7uk gm_l35; /* tag */
    gm_b16_8 gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* i */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* j */
    GM_STACK_CHECK(&F, 4920u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t3 = gm_l3;
            gm_l5 = t3;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l6;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l6;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tkacitepg5fk0 t15 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t11 * 72));
                    F.l7 = t15;
                    {
                        if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l7) + 0) + 0) + 8) == 7) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l7) + 0) + 0)), gm_suuv0qpi4v4e4, 7) == 0)) goto mn18;
                        {
                            gm_l4 = ((uint8_t)1);
                        }
                        goto me17;
mn18:;
                        gm_b16_8 t21 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l7) + 0) + 0));
                        gm_l8 = t21;
                        void * t22 = gm_l0;
                        gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l7) + 56));
                        gm_b16_8 t24 = gm_l8;
                        gm_tnpifv20irnph t25;
                        GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
                        gm_tabb710d9mq2e t26;
                        gm_f_format_45bh632((void *)&t26, (void *)NULL, cx, NULL, t25);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tabb710d9mq2e t27 = t26;
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_src3k278vcmqgdr, 345, 38);
                        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
                        gm_b16_8 t29 = t28;
                        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
                        gm_b0_1 t31 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t22, t23, t30);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me17;
mn20:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 343, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 342, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t33 = gm_l6;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l6 = t36;
                }
            }
gm_ll8:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb4; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc4; }
            }
            goto gm_lad;
lb4:;
        }
    }
    {
        gm_tkn42u7na3t81 t37;
        memset(&t37, 0, sizeof t37);
        gm_l9 = t37;
    }
    {
        void * t39 = gm_l2;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
        void * t41 = t40;
        if (!(GM_LD(void *, ((uint8_t *)&t41)) != 0)) goto ie38;
        void * t42 = GM_LD(void *, ((uint8_t *)&t41));
        gm_l10 = t42;
        {
            void * t44 = gm_l0;
            uint64_t t45 = gm_l1;
            void * t46 = gm_l10;
            gm_t88rcucg1b7uk t47 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t44, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t47;
        }
        {
            void * t49 = gm_l0;
            void * t50 = (void *)(((uint8_t *)t49) + 8);
            gm_t88rcucg1b7uk t51 = gm_l11;
            uint8_t t52 = gm_f_is_integer_8d4f9ek(t50, GM_CONV(gm_csguankko4lmhd, t51));
            uint8_t t53 = (uint8_t)!t52;
            if (!t53) goto ie48;
            void * t54 = gm_l0;
            void * t55 = (void *)(((uint8_t *)t54) + 8);
            gm_t88rcucg1b7uk t56 = gm_l11;
            uint8_t t57 = gm_f_is_error_8k27gd1(t55, GM_CONV(gm_csguankko4lmhd, t56));
            uint8_t t58 = (uint8_t)!t57;
            if (!t58) goto ie48;
            {
                void * t60 = gm_l0;
                void * t61 = gm_l2;
                gm_tl874t2atnlh3 t62 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t61) + 64));
                void * t63 = gm_l0;
                gm_t88rcucg1b7uk t64 = gm_l11;
                gm_b16_8 t65 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t63, t64);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t66;
                GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
                gm_tabb710d9mq2e t67;
                gm_f_format_9kb6o1u((void *)&t67, (void *)NULL, cx, NULL, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t68 = t67;
                if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_src3k278vcmqgdr, 352, 29);
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
                gm_b16_8 t70 = t69;
                gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                gm_b0_1 t72 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t60, t62, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix48;
ie48:;
ix48:;
        }
        {
            gm_tkn42u7na3t81 t73;
            void * t75 = gm_l0;
            void * t76 = (void *)(((uint8_t *)t75) + 8);
            gm_t88rcucg1b7uk t77 = gm_l11;
            uint8_t t78 = gm_f_is_integer_8d4f9ek(t76, GM_CONV(gm_csguankko4lmhd, t77));
            if (!t78) goto ie74;
            gm_t88rcucg1b7uk t80 = gm_l11;
            gm_t88rcucg1b7uk t81 = t80;
            gm_tkn42u7na3t81 t82;
            GM_ST(uint8_t, (uint8_t *)&t82 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t82 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t82), t81);
            ((uint8_t *)&t82)[4] = 1;
            t73 = t82;
            goto ix74;
ie74:;
            void * t84 = gm_l0;
            void * t85 = (void *)(((uint8_t *)t84) + 8);
            gm_t88rcucg1b7uk t86 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t87 = t86;
            gm_tkn42u7na3t81 t88;
            GM_ST(uint8_t, (uint8_t *)&t88 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t88 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t88), t87);
            ((uint8_t *)&t88)[4] = 1;
            t73 = t88;
ix74:;
            gm_l9 = t73;
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        gm_f_list_g6uul2f((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_7s4om10((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_zero_s25l990((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_ja7tv3o((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t89 = gm_l0;
        uint64_t t90 = gm_l1;
        void * t91 = gm_l2;
        gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)t91) + 48));
        gm_b16_8 t93 = t92;
        gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
        void * t95 = (void *)((uint8_t *)&F.l15);
        gm_b0_1 t96 = gm_f_taken_union_members_pavth81((void *)NULL, cx, NULL, t89, t90, t94, t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            gm_l16 = t98;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t100 = gm_l17;
                uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t102 = (t100 < t101);
                if (!t102) goto lb99;
                {
                    uint64_t t105 = gm_l17;
                    uint64_t t106 = (uintptr_t)t105;
                    uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    gm_tnljbma3s3h16 t110 = GM_LD(gm_tnljbma3s3h16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t106 * 64));
                    F.l18 = t110;
                    {
                        {
                            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                            gm_l19 = t113;
                        }
                        {
                            gm_l20 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t115 = gm_l20;
                                uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                uint8_t t117 = (t115 < t116);
                                if (!t117) goto lb114;
                                {
                                    uint64_t t120 = gm_l20;
                                    uint64_t t121 = (uintptr_t)t120;
                                    uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                    gm_tckdobunaulee t125 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t121 * 72));
                                    F.l21 = t125;
                                    {
                                        gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
                                        gm_b16_8 t129 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l18) + 0) + 0));
                                        gm_b16_8 t130 = t128;
                                        gm_b16_8 t131 = t129;
                                        uint8_t t132 = (gm_eq0iqiubfm7e(((uint8_t *)&t130), ((uint8_t *)&t131)));
                                        if (!t132) goto ie127;
                                        {
                                            void * t134 = gm_l0;
                                            gm_tl874t2atnlh3 t135 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l18) + 0) + 16));
                                            gm_b16_8 t136 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l18) + 0) + 0));
                                            gm_tnpifv20irnph t137;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t137 + 0), t136);
                                            gm_tabb710d9mq2e t138;
                                            gm_f_format_72n0e07((void *)&t138, (void *)NULL, cx, NULL, t137);
                                            if (gm_unw.kind) {
                                                goto gm_ll118;
                                            }
                                            gm_tabb710d9mq2e t139 = t138;
                                            if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 8, gm_src3k278vcmqgdr, 365, 38);
                                            gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&t139) + 8));
                                            gm_b16_8 t141 = t140;
                                            gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
                                            gm_b0_1 t143 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t134, t135, t142);
                                            if (gm_unw.kind) {
                                                goto gm_ll118;
                                            }
                                        }
                                        {
                                            goto lb114;
                                        }
                                        goto ix127;
ie127:;
ix127:;
                                    }
                                    goto me123;
mn124:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 363, 9);
me123:;
                                }
lc114:;
                                {
                                    uint64_t t145 = gm_l20;
                                    uint64_t r146; bool o147 = __builtin_add_overflow(t145, ((uint64_t)1ULL), &r146);
                                    uint64_t t148 = r146;
                                    gm_l20 = t148;
                                }
                            }
gm_ll118:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb114; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc114; }
                            }
                            goto gm_ll103;
lb114:;
                        }
                    }
                    {
                        gm_tkn42u7na3t81 t149;
                        memset(&t149, 0, sizeof t149);
                        gm_l22 = t149;
                    }
                    {
                        void * t151 = GM_LD(void *, (((uint8_t *)&F.l18) + 32));
                        void * t152 = t151;
                        if (!(GM_LD(void *, ((uint8_t *)&t152)) != 0)) goto ie150;
                        void * t153 = GM_LD(void *, ((uint8_t *)&t152));
                        gm_l23 = t153;
                        {
                            void * t155 = gm_l0;
                            uint64_t t156 = gm_l1;
                            void * t157 = gm_l23;
                            gm_t88rcucg1b7uk t158 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t155, t156, t157);
                            if (gm_unw.kind) {
                                goto gm_ll103;
                            }
                            gm_tkn42u7na3t81 t159;
                            GM_ST(uint8_t, (uint8_t *)&t159 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t159 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t159), t158);
                            ((uint8_t *)&t159)[4] = 1;
                            gm_l22 = t159;
                        }
                        goto ix150;
ie150:;
ix150:;
                    }
                    {
                        gm_tbci5ospk5nnn t160 = F.l14;
                        F.l24 = t160;
                    }
                    {
                        void * t162 = GM_LD(void *, (((uint8_t *)&F.l18) + 40));
                        void * t163 = t162;
                        if (!(GM_LD(void *, ((uint8_t *)&t163)) != 0)) goto ie161;
                        void * t164 = GM_LD(void *, ((uint8_t *)&t163));
                        gm_l25 = t164;
                        {
                            uint8_t t167;
                            if (((((uint8_t *)&gm_l9))[4] != 0)) goto mn169;
                            t167 = ((uint8_t)1);
                            goto me168;
mn169:;
                            t167 = ((uint8_t)0);
                            goto me168;
mn170:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 373, 16);
me168:;
                            if (!t167) goto ie166;
                            {
                                void * t172 = gm_l0;
                                void * t173 = gm_l25;
                                gm_tl874t2atnlh3 t174 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t173) + 72));
                                gm_b16_8 t175;
                                GM_ST(void *, (uint8_t *)&t175, (void *)(gm_su38t05utsk53));
                                GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(66));
                                gm_b0_1 t176 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t172, t174, t175);
                                if (gm_unw.kind) {
                                    goto gm_ll103;
                                }
                            }
                            goto ix166;
ie166:;
ix166:;
                        }
                        {
                            void * t177 = gm_l0;
                            uint64_t t178 = gm_l1;
                            void * t179 = gm_l25;
                            gm_tkv1vap90vht2 t180;
                            gm_f_try_untyped_fj0h8i5((void *)&t180, (void *)NULL, cx, NULL, t177, t178, t179);
                            if (gm_unw.kind) {
                                goto gm_ll103;
                            }
                            gm_tkv1vap90vht2 t181 = t180;
                            if (!((((uint8_t *)&t181))[208] != 0)) goto mn183;
                            uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&t181) + 0));
                            if (t184 != ((uint8_t)0ULL)) goto mn183;
                            gm_t0ialelq3ql1k t185 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&t181) + 8));
                            F.l26 = t185;
                            {
                                uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                                if (t188 != ((uint8_t)0ULL)) goto ie187;
                                gm_tbci5ospk5nnn t189 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l26) + 8));
                                F.l27 = t189;
                                {
                                    gm_tbci5ospk5nnn t191 = F.l27;
                                    F.l24 = t191;
                                }
                                goto ix187;
ie187:;
                                {
                                    void * t193 = gm_l0;
                                    void * t194 = gm_l25;
                                    gm_tl874t2atnlh3 t195 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t194) + 72));
                                    gm_b16_8 t196;
                                    GM_ST(void *, (uint8_t *)&t196, (void *)(gm_seoeuc6nik756));
                                    GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(39));
                                    gm_b0_1 t197 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t193, t195, t196);
                                    if (gm_unw.kind) {
                                        goto gm_ll103;
                                    }
                                }
ix187:;
                            }
                            goto me182;
mn183:;
                            if (!((((uint8_t *)&t181))[208] != 0)) goto mn198;
                            uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&t181) + 0));
                            if (t199 != ((uint8_t)1ULL)) goto mn198;
                            goto me182;
mn198:;
                            if (((((uint8_t *)&t181))[208] != 0)) goto mn201;
                            void * t202 = gm_l0;
                            void * t203 = gm_l25;
                            gm_tl874t2atnlh3 t204 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t203) + 72));
                            gm_b16_8 t205;
                            GM_ST(void *, (uint8_t *)&t205, (void *)(gm_seoeuc6nik756));
                            GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(39));
                            gm_b0_1 t206 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t202, t204, t205);
                            if (gm_unw.kind) {
                                goto gm_ll103;
                            }
                            goto me182;
mn201:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 374, 13);
me182:;
                        }
                        goto ix161;
ie161:;
ix161:;
                    }
                    {
                        gm_tbci5ospk5nnn t207 = F.l24;
                        gm_tbci5ospk5nnn t208;
                        gm_f_one_08qg1op((void *)&t208, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll103;
                        }
                        gm_f_add_420lj6o((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)), (void *)NULL, cx, NULL, &t207, &t208);
                        if (gm_unw.kind) {
                            goto gm_ll103;
                        }
                    }
                    {
                        void * t209 = (void *)((uint8_t *)&F.l12);
                        gm_tckdobunaulee t210;
                        gm_b16_8 t211 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l18) + 0) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t210 + 0, t211);
                        gm_tkn42u7na3t81 t212 = gm_l22;
                        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t210 + 16, t212);
                        memcpy((uint8_t *)&t210 + 24, ((uint8_t *)&F.l24), 48);
                        gm_tftb1odc739t5 t213 = gm_f_push_1j8vbii(t209, &t210);
                        gm_tftb1odc739t5 t214 = t213;
                        if (((((uint8_t *)&t214))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t214) + 1, gm_src3k278vcmqgdr, 387, 9);
                        gm_b0_1 t215 = GM_LD(gm_b0_1, (((uint8_t *)&t214) + 1));
                    }
                    {
                        void * t216 = (void *)((uint8_t *)&F.l13);
                        gm_tl874t2atnlh3 t217 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 48));
                        gm_tftb1odc739t5 t218 = gm_f_push_0mk7udh(t216, GM_CONV(gm_cseahvg8v7p0vs, t217));
                        gm_tftb1odc739t5 t219 = t218;
                        if (((((uint8_t *)&t219))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t219) + 1, gm_src3k278vcmqgdr, 388, 9);
                        gm_b0_1 t220 = GM_LD(gm_b0_1, (((uint8_t *)&t219) + 1));
                    }
                    goto me108;
mn109:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 362, 5);
me108:;
                }
lc99:;
                {
                    uint64_t t222 = gm_l17;
                    uint64_t r223; bool o224 = __builtin_add_overflow(t222, ((uint64_t)1ULL), &r223);
                    uint64_t t225 = r223;
                    gm_l17 = t225;
                }
            }
gm_ll103:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb99; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc99; }
            }
            goto gm_lad;
lb99:;
        }
    }
    {
        uint8_t t227 = gm_l4;
        if (!t227) goto ie226;
        {
            gm_f_list_spqn36o((void *)&F.l28, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t230 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                gm_b16_8 t231 = t230;
                gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)&t231));
                gm_l29 = t232;
            }
            {
                gm_l30 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t234 = gm_l30;
                    uint64_t t235 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                    uint8_t t236 = (t234 < t235);
                    if (!t236) goto lb233;
                    {
                        gm_t3l8idb2m6c1r t239;
                        uint64_t t240 = gm_l30;
                        GM_ST(uint64_t, (uint8_t *)&t239 + 0, t240);
                        uint64_t t241 = gm_l30;
                        uint64_t t242 = (uintptr_t)t241;
                        uint64_t t243 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                        memcpy((uint8_t *)&t239 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t242 * 72), 72);
                        gm_t3l8idb2m6c1r t244 = t239;
                        uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)&t244) + 0));
                        gm_l31 = t247;
                        gm_tckdobunaulee t248 = GM_LD(gm_tckdobunaulee, (((uint8_t *)&t244) + 8));
                        F.l32 = t248;
                        {
                            if (!(((((uint8_t *)&F.l32) + 16))[4] != 0)) goto ie250;
                            gm_t88rcucg1b7uk t251 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l32) + 16));
                            gm_l33 = t251;
                            {
                                void * t253 = (void *)((uint8_t *)&F.l28);
                                gm_t0mglksrkfa5e t254;
                                memset((uint8_t *)&t254 + 21, 0, 3);
                                gm_b16_8 t255 = GM_LD(gm_b16_8, (((uint8_t *)&F.l32) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t254 + 0, t255);
                                gm_t88rcucg1b7uk t256 = gm_l33;
                                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t254 + 16, t256);
                                gm_t9qkh0rsooc8d t257;
                                GM_ST(uint8_t, ((uint8_t *)&t257 + 0), ((uint8_t)2ULL));
                                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t254 + 20, t257);
                                gm_tf5srh4mp4b3i t258;
                                memset(&t258, 0, sizeof t258);
                                GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t254 + 24, t258);
                                gm_tftb1odc739t5 t259 = gm_f_push_vrmo7c5(t253, &t254);
                                gm_tftb1odc739t5 t260 = t259;
                                if (((((uint8_t *)&t260))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t260) + 1, gm_src3k278vcmqgdr, 394, 17);
                                gm_b0_1 t261 = GM_LD(gm_b0_1, (((uint8_t *)&t260) + 1));
                            }
                            goto ix250;
ie250:;
                            {
                                void * t263 = gm_l0;
                                uint64_t t264 = gm_l31;
                                uint64_t t265 = (uintptr_t)t264;
                                uint64_t t266 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                                if (t265 >= (uintptr_t)(t266)) gm_trap_index(t265, t266, gm_src3k278vcmqgdr, 396, 25);
                                gm_tl874t2atnlh3 t267 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t265 * 12));
                                gm_b16_8 t268;
                                GM_ST(void *, (uint8_t *)&t268, (void *)(gm_s8h39pil8lgh0));
                                GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(40));
                                gm_b0_1 t269 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t263, t267, t268);
                                if (gm_unw.kind) {
                                    goto gm_ll237;
                                }
                            }
ix250:;
                        }
                        goto me245;
mn246:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 392, 9);
me245:;
                    }
lc233:;
                    {
                        uint64_t t271 = gm_l30;
                        uint64_t r272; bool o273 = __builtin_add_overflow(t271, ((uint64_t)1ULL), &r272);
                        uint64_t t274 = r272;
                        gm_l30 = t274;
                    }
                }
gm_ll237:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb233; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc233; }
                }
                goto gm_lad;
lb233:;
            }
        }
        {
            gm_b16_8 t275 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
            gm_ti0kgh3pc3in2 t276;
            memset(&t276, 0, sizeof t276);
            GM_ST(uint8_t, ((uint8_t *)&t276 + 0), ((uint8_t)2ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t276 + 8), t275);
            uint8_t t277 = 0;
            F.ret = t276;
            goto gm_fret;
        }
        goto ix226;
ie226:;
ix226:;
    }
    {
        uint64_t t278 = GM_LD(uintptr_t, (((uint8_t *)&F.l12) + 0) + 8);
        gm_l34 = t278;
    }
    {
        gm_tkn42u7na3t81 t279 = gm_l9;
        gm_tkn42u7na3t81 t280 = t279;
        gm_t88rcucg1b7uk t281;
        if (((((uint8_t *)&t280))[4] != 0)) {
            gm_t88rcucg1b7uk t282 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t280) + 0));
            t281 = t282;
        } else {
            gm_t88rcucg1b7uk t283;
            uint64_t t285 = gm_l34;
            uint8_t t286 = (t285 <= ((uint64_t)256ULL));
            if (!t286) goto ie284;
            void * t288 = gm_l0;
            void * t289 = (void *)(((uint8_t *)t288) + 8);
            gm_t88rcucg1b7uk t290 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t289);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t291 = t290;
            t283 = t291;
            goto ix284;
ie284:;
            gm_t88rcucg1b7uk t292;
            uint64_t t294 = gm_l34;
            uint8_t t295 = (t294 <= ((uint64_t)65536ULL));
            if (!t295) goto ie293;
            void * t297 = gm_l0;
            void * t298 = (void *)(((uint8_t *)t297) + 8);
            gm_t88rcucg1b7uk t299 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t298, ((uint8_t)0), ((uint16_t)16ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t300 = t299;
            t292 = t300;
            goto ix293;
ie293:;
            void * t302 = gm_l0;
            void * t303 = (void *)(((uint8_t *)t302) + 8);
            gm_t88rcucg1b7uk t304 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t303, ((uint8_t)0), ((uint16_t)32ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t305 = t304;
            t292 = t305;
ix293:;
            t283 = t292;
ix284:;
            t281 = t283;
        }
        gm_l35 = t281;
    }
    {
        {
            gm_b16_8 t307 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
            gm_b16_8 t308 = t307;
            gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
            gm_l36 = t309;
        }
        {
            gm_l37 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t311 = gm_l37;
                uint64_t t312 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                uint8_t t313 = (t311 < t312);
                if (!t313) goto lb310;
                {
                    gm_t3l8idb2m6c1r t316;
                    uint64_t t317 = gm_l37;
                    GM_ST(uint64_t, (uint8_t *)&t316 + 0, t317);
                    uint64_t t318 = gm_l37;
                    uint64_t t319 = (uintptr_t)t318;
                    uint64_t t320 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                    memcpy((uint8_t *)&t316 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t319 * 72), 72);
                    gm_t3l8idb2m6c1r t321 = t316;
                    uint64_t t324 = GM_LD(uint64_t, (((uint8_t *)&t321) + 0));
                    gm_l38 = t324;
                    gm_tckdobunaulee t325 = GM_LD(gm_tckdobunaulee, (((uint8_t *)&t321) + 8));
                    F.l39 = t325;
                    {
                        void * t328 = gm_l0;
                        void * t329 = (void *)(((uint8_t *)t328) + 8);
                        gm_t88rcucg1b7uk t330 = gm_l35;
                        uint8_t t331 = gm_f_is_error_8k27gd1(t329, GM_CONV(gm_csguankko4lmhd, t330));
                        uint8_t t332 = (uint8_t)!t331;
                        if (!t332) goto ie327;
                        void * t333 = gm_l0;
                        gm_t88rcucg1b7uk t334 = gm_l35;
                        uint8_t t335 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t333, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l39) + 24)), t334);
                        if (gm_unw.kind) {
                            goto gm_ll314;
                        }
                        uint8_t t336 = (uint8_t)!t335;
                        if (!t336) goto ie327;
                        {
                            void * t338 = gm_l0;
                            uint64_t t339 = gm_l38;
                            uint64_t t340 = (uintptr_t)t339;
                            uint64_t t341 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                            if (t340 >= (uintptr_t)(t341)) gm_trap_index(t340, t341, gm_src3k278vcmqgdr, 405, 21);
                            gm_tl874t2atnlh3 t342 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t340 * 12));
                            gm_tfeab279c16rr t343;
                            gm_b16_8 t344 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l39) + 24)));
                            if (gm_unw.kind) {
                                goto gm_ll314;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t343 + 0, t344);
                            gm_b16_8 t345 = GM_LD(gm_b16_8, (((uint8_t *)&F.l39) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t343 + 16, t345);
                            void * t346 = gm_l0;
                            gm_t88rcucg1b7uk t347 = gm_l35;
                            gm_b16_8 t348 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t346, t347);
                            if (gm_unw.kind) {
                                goto gm_ll314;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t343 + 32, t348);
                            gm_tabb710d9mq2e t349;
                            gm_f_format_nag8m5u((void *)&t349, (void *)NULL, cx, NULL, &t343);
                            if (gm_unw.kind) {
                                goto gm_ll314;
                            }
                            gm_tabb710d9mq2e t350 = t349;
                            if (((((uint8_t *)&t350))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t350) + 8, gm_src3k278vcmqgdr, 405, 37);
                            gm_b16_8 t351 = GM_LD(gm_b16_8, (((uint8_t *)&t350) + 8));
                            gm_b16_8 t352 = t351;
                            gm_b16_8 t353 = GM_LD(gm_b16_8, ((uint8_t *)&t352));
                            gm_b0_1 t354 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t338, t342, t353);
                            if (gm_unw.kind) {
                                goto gm_ll314;
                            }
                        }
                        goto ix327;
ie327:;
ix327:;
                    }
                    {
                        {
                            uint64_t t356 = GM_LD(uintptr_t, (((uint8_t *)&F.l12) + 0) + 8);
                            void * t357 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l12) + 0)));
                            uint64_t t358 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t359 = gm_l38;
                            uint64_t t360 = (uintptr_t)t359;
                            if (t358 > t360 || t360 > (uintptr_t)t356) gm_trap_slice(t358, t360, t356, gm_src3k278vcmqgdr, 407, 31);
                            gm_b16_8 t361;
                            GM_ST(void *, (uint8_t *)&t361, (void *)(((uint8_t *)t357 + t358 * 72)));
                            GM_ST(uintptr_t, (uint8_t *)&t361 + 8, (uintptr_t)(t360 - t358));
                            gm_b16_8 t362 = t361;
                            gm_b16_8 t363 = GM_LD(gm_b16_8, ((uint8_t *)&t362));
                            gm_l40 = t363;
                        }
                        {
                            gm_l41 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t365 = gm_l41;
                                uint64_t t366 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                                uint8_t t367 = (t365 < t366);
                                if (!t367) goto lb364;
                                {
                                    gm_t3l8idb2m6c1r t370;
                                    uint64_t t371 = gm_l41;
                                    GM_ST(uint64_t, (uint8_t *)&t370 + 0, t371);
                                    uint64_t t372 = gm_l41;
                                    uint64_t t373 = (uintptr_t)t372;
                                    uint64_t t374 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                                    memcpy((uint8_t *)&t370 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t373 * 72), 72);
                                    gm_t3l8idb2m6c1r t375 = t370;
                                    uint64_t t378 = GM_LD(uint64_t, (((uint8_t *)&t375) + 0));
                                    gm_l42 = t378;
                                    gm_tckdobunaulee t379 = GM_LD(gm_tckdobunaulee, (((uint8_t *)&t375) + 8));
                                    F.l43 = t379;
                                    {
                                        uint8_t t382 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l43) + 24)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l39) + 24)));
                                        if (!t382) goto ie381;
                                        {
                                            uint64_t t384 = gm_l38;
                                            uint64_t t385 = (uintptr_t)t384;
                                            uint64_t t386 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                                            if (t385 >= (uintptr_t)(t386)) gm_trap_index(t385, t386, gm_src3k278vcmqgdr, 409, 33);
                                            gm_tl874t2atnlh3 t387 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t385 * 12));
                                            gm_tfeab279c16rr t388;
                                            gm_b16_8 t389 = GM_LD(gm_b16_8, (((uint8_t *)&F.l43) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t388 + 0, t389);
                                            gm_b16_8 t390 = GM_LD(gm_b16_8, (((uint8_t *)&F.l39) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t388 + 16, t390);
                                            gm_b16_8 t391 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l39) + 24)));
                                            if (gm_unw.kind) {
                                                goto gm_ll368;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t388 + 32, t391);
                                            gm_tabb710d9mq2e t392;
                                            gm_f_format_uaa99rp((void *)&t392, (void *)NULL, cx, NULL, &t388);
                                            if (gm_unw.kind) {
                                                goto gm_ll368;
                                            }
                                            gm_tabb710d9mq2e t393 = t392;
                                            if (((((uint8_t *)&t393))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t393) + 8, gm_src3k278vcmqgdr, 409, 49);
                                            gm_b16_8 t394 = GM_LD(gm_b16_8, (((uint8_t *)&t393) + 8));
                                            gm_b16_8 t395 = t394;
                                            gm_b16_8 t396 = GM_LD(gm_b16_8, ((uint8_t *)&t395));
                                            gm_f_error_s6hn871((void *)&F.l44, (void *)NULL, cx, NULL, t387, t396);
                                            if (gm_unw.kind) {
                                                goto gm_ll368;
                                            }
                                        }
                                        {
                                            void * t397 = (void *)((uint8_t *)&F.l44);
                                            uint64_t t398 = gm_l42;
                                            uint64_t t399 = (uintptr_t)t398;
                                            uint64_t t400 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                                            if (t399 >= (uintptr_t)(t400)) gm_trap_index(t399, t400, gm_src3k278vcmqgdr, 410, 29);
                                            gm_tl874t2atnlh3 t401 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t399 * 12));
                                            gm_b16_8 t402;
                                            GM_ST(void *, (uint8_t *)&t402, (void *)(gm_sbe7dr7t400bh));
                                            GM_ST(uintptr_t, (uint8_t *)&t402 + 8, (uintptr_t)(15));
                                            gm_f_add_label_50fj7vv(t397, GM_CONV(gm_cseahvg8v7p0vs, t401), t402);
                                        }
                                        {
                                            void * t403 = gm_l0;
                                            gm_b0_1 t404 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t403, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l44)));
                                            if (gm_unw.kind) {
                                                goto gm_ll368;
                                            }
                                        }
                                        {
                                            goto lb364;
                                        }
                                        goto ix381;
ie381:;
ix381:;
                                    }
                                    goto me376;
mn377:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 407, 9);
me376:;
                                }
lc364:;
                                {
                                    uint64_t t406 = gm_l41;
                                    uint64_t r407; bool o408 = __builtin_add_overflow(t406, ((uint64_t)1ULL), &r407);
                                    uint64_t t409 = r407;
                                    gm_l41 = t409;
                                }
                            }
gm_ll368:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb364; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc364; }
                            }
                            goto gm_ll314;
lb364:;
                        }
                    }
                    goto me322;
mn323:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 403, 5);
me322:;
                }
lc310:;
                {
                    uint64_t t411 = gm_l37;
                    uint64_t r412; bool o413 = __builtin_add_overflow(t411, ((uint64_t)1ULL), &r412);
                    uint64_t t414 = r412;
                    gm_l37 = t414;
                }
            }
gm_ll314:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb310; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc310; }
            }
            goto gm_lad;
lb310:;
        }
    }
    {
        gm_tcmkompbl09il t415;
        memset((uint8_t *)&t415 + 4, 0, 4);
        gm_t88rcucg1b7uk t416 = gm_l35;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t415 + 0, t416);
        gm_b16_8 t417 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t415 + 8, t417);
        gm_ti0kgh3pc3in2 t418;
        GM_ST(uint8_t, (uint8_t *)&t418 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t418 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t418 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t418 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t418 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t418 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t418 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t418 + 8), t415);
        uint8_t t419 = 0;
        F.ret = t418;
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
inline __attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_union_variants_a7khaqp F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* id */
    gm_t88rcucg1b7uk gm_l3; /* tag */
    gm_b16_8 gm_l4; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 373u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l2 = t9;
        {
            uint32_t t12 = gm_l2;
            uint64_t t13 = (uint64_t)t12;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 1088) + 0) + 8);
            if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_src3k278vcmqgdr, 510, 30);
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 1088) + 0)) + t14 * 184) + 24) + 0));
            if (t17 != ((uint8_t)1ULL)) goto ie11;
            gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 1088) + 0)) + t14 * 184) + 24) + 8) + 0));
            gm_l3 = t18;
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 1088) + 0)) + t14 * 184) + 24) + 8) + 8));
            gm_l4 = t19;
            {
                gm_tep2drsl9408b t21;
                memset((uint8_t *)&t21 + 20, 0, 4);
                gm_b16_8 t22 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t21 + 0, t22);
                gm_t88rcucg1b7uk t23 = gm_l3;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t21 + 16, t23);
                gm_tdlpv0q69lov1 t24;
                GM_ST(uint8_t, (uint8_t *)&t24 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t24 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t24 + 28, 0);
                GM_ST(gm_tep2drsl9408b, ((uint8_t *)&t24), t21);
                ((uint8_t *)&t24)[24] = 1;
                uint8_t t25 = 0;
                F.ret = t24;
                goto gm_fret;
            }
            goto ix11;
ie11:;
            {
                gm_tdlpv0q69lov1 t27;
                memset(&t27, 0, sizeof t27);
                uint8_t t28 = 0;
                F.ret = t27;
                goto gm_fret;
            }
ix11:;
        }
        goto ix2;
ie2:;
        {
            gm_tdlpv0q69lov1 t30;
            memset(&t30, 0, sizeof t30);
            uint8_t t31 = 0;
            F.ret = t30;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tdlpv0q69lov1 gm_f_union_variants_a7khaqp_c(void * a0, gm_csguankko4lmhd a1) { gm_tdlpv0q69lov1 gm_r; gm_f_union_variants_a7khaqp(&gm_r, a0, a1); return gm_r; }
