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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csff7attl2ehc5 { uint64_t f0; uint32_t f1; } gm_csff7attl2ehc5;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { void * f0; } gm_t3p0qn9o5e53o;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; uint32_t f2; uint8_t pe[4]; } gm_tk20dunvmmkbp;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; void * v1; } f1; } gm_tljvbvj59adij;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csff7attl2ehc5) == 16 && _Alignof(gm_csff7attl2ehc5) == 8 && offsetof(gm_csff7attl2ehc5, f0) == 0 && offsetof(gm_csff7attl2ehc5, f1) == 8, "layout of `WithKey`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t3p0qn9o5e53o) == 8 && _Alignof(gm_t3p0qn9o5e53o) == 8 && offsetof(gm_t3p0qn9o5e53o, f0) == 0, "layout of `Sched`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tn68el8d14vku f1; } gm_t4qfihdig0mro;
typedef struct { gm_tbl8il92l97ut f0; uint8_t f1; uint8_t pe[7]; } gm_t5lf9i3n1r3co;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t7neromnu8t62;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_td7tfovebirdn;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tk20dunvmmkbp) == 24 && _Alignof(gm_tk20dunvmmkbp) == 8 && offsetof(gm_tk20dunvmmkbp, f0) == 0 && offsetof(gm_tk20dunvmmkbp, f1) == 8 && offsetof(gm_tk20dunvmmkbp, f2) == 16, "layout of `(id: u32, env: ?^Frame, cx: u32)`");
typedef struct { uint64_t f0; gm_tf5srh4mp4b3i f1; } gm_tk4qs2jduhfn8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; } f1; } gm_tl2g3fsjpi9op;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tljvbvj59adij) == 16 && _Alignof(gm_tljvbvj59adij) == 8 && offsetof(gm_tljvbvj59adij, f0) == 0 && offsetof(gm_tljvbvj59adij, f1) == 8, "layout of `Outcome`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tnsjse4su5jue;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_b16_8 f2; uint32_t f3; gm_tl874t2atnlh3 f4; } gm_tonn1ba3fjh5g;
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; void * f2; gm_b16_8 f3; } gm_tsbt7g382sgof;
typedef struct { gm_t76123gfet73g f0; uint8_t f1; uint8_t pe[7]; } gm_t07lf02ct4q61;
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; } gm_t16gj5so0pj6j;
_Static_assert(sizeof(gm_t4qfihdig0mro) == 24 && _Alignof(gm_t4qfihdig0mro) == 8 && offsetof(gm_t4qfihdig0mro, f0) == 0 && offsetof(gm_t4qfihdig0mro, f1) == 8, "layout of `(fiber: u32, key: WithKey)`");
_Static_assert(sizeof(gm_t5lf9i3n1r3co) == 24 && _Alignof(gm_t5lf9i3n1r3co) == 8 && offsetof(gm_t5lf9i3n1r3co, f0) == 0 && offsetof(gm_t5lf9i3n1r3co, f1) == 16, "layout of `?SpecCall`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t7neromnu8t62) == 40 && _Alignof(gm_t7neromnu8t62) == 8 && offsetof(gm_t7neromnu8t62, f0) == 0 && offsetof(gm_t7neromnu8t62, f1) == 8 && offsetof(gm_t7neromnu8t62, f2) == 24, "layout of `(u64, string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_td7tfovebirdn) == 40 && _Alignof(gm_td7tfovebirdn) == 8 && offsetof(gm_td7tfovebirdn, f0) == 0 && offsetof(gm_td7tfovebirdn, f1) == 16 && offsetof(gm_td7tfovebirdn, f2) == 32, "layout of `([]u8, string, u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tk4qs2jduhfn8 v1; uint64_t v2; void * v3; } f1; } gm_tdkjirf1g83ud;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tsbt7g382sgof v0; gm_tonn1ba3fjh5g v1; gm_t79rbdv4rafe5 v2; } f1; } gm_th5tat709fgha;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tk4qs2jduhfn8) == 24 && _Alignof(gm_tk4qs2jduhfn8) == 8 && offsetof(gm_tk4qs2jduhfn8, f0) == 0 && offsetof(gm_tk4qs2jduhfn8, f1) == 8, "layout of `(seen: u64, deadline: ?u64)`");
_Static_assert(sizeof(gm_tl2g3fsjpi9op) == 24 && _Alignof(gm_tl2g3fsjpi9op) == 8 && offsetof(gm_tl2g3fsjpi9op, f0) == 0 && offsetof(gm_tl2g3fsjpi9op, f1) == 8, "layout of `FState`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t4qfihdig0mro v1; } f1; } gm_tmr8tdh5g7uj6;
_Static_assert(sizeof(gm_tnsjse4su5jue) == 48 && _Alignof(gm_tnsjse4su5jue) == 8 && offsetof(gm_tnsjse4su5jue, f0) == 0 && offsetof(gm_tnsjse4su5jue, f1) == 4 && offsetof(gm_tnsjse4su5jue, f2) == 8 && offsetof(gm_tnsjse4su5jue, f3) == 16 && offsetof(gm_tnsjse4su5jue, f4) == 32, "layout of `(fiber: u32, node: u32, op: u32, args: []const []u8, span: Span)`");
typedef struct { gm_tn68el8d14vku f0; uint32_t f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; uint32_t f3; uint8_t p4[4]; gm_trqfvmjtt4sij f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_to8g4620flbcm;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; } gm_tolm6p7jp1hb3;
_Static_assert(sizeof(gm_tonn1ba3fjh5g) == 56 && _Alignof(gm_tonn1ba3fjh5g) == 8 && offsetof(gm_tonn1ba3fjh5g, f0) == 0 && offsetof(gm_tonn1ba3fjh5g, f1) == 16 && offsetof(gm_tonn1ba3fjh5g, f2) == 24 && offsetof(gm_tonn1ba3fjh5g, f3) == 40 && offsetof(gm_tonn1ba3fjh5g, f4) == 44, "layout of `(f: []u8, fty: Ty, arg: []u8, cx: u32, span: Span)`");
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsbt7g382sgof) == 40 && _Alignof(gm_tsbt7g382sgof) == 8 && offsetof(gm_tsbt7g382sgof, f0) == 0 && offsetof(gm_tsbt7g382sgof, f1) == 8 && offsetof(gm_tsbt7g382sgof, f2) == 16 && offsetof(gm_tsbt7g382sgof, f3) == 24, "layout of `(fr: ^Frame, cx: u32, body: ^Node, dst: []u8)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tk20dunvmmkbp v0; gm_td7tfovebirdn v1; gm_t9qkh0rsooc8d v2; } f1; } gm_tte9vgf6noic9;
_Static_assert(sizeof(gm_t07lf02ct4q61) == 32 && _Alignof(gm_t07lf02ct4q61) == 8 && offsetof(gm_t07lf02ct4q61, f0) == 0 && offsetof(gm_t07lf02ct4q61, f1) == 24, "layout of `?Flow`");
_Static_assert(sizeof(gm_t16gj5so0pj6j) == 56 && _Alignof(gm_t16gj5so0pj6j) == 8 && offsetof(gm_t16gj5so0pj6j, f0) == 0 && offsetof(gm_t16gj5so0pj6j, f1) == 40 && offsetof(gm_t16gj5so0pj6j, f2) == 48, "layout of `Scratch`");
typedef struct { gm_th5tat709fgha f0; uint8_t f1; uint8_t pe[7]; } gm_t68f8jeqq3b3v;
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
typedef struct { gm_t16gj5so0pj6j f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; uint32_t f3; uint8_t p4[4]; uint64_t f4; uint64_t f5; } gm_td6r3ebeccg25;
_Static_assert(sizeof(gm_tdkjirf1g83ud) == 32 && _Alignof(gm_tdkjirf1g83ud) == 8 && offsetof(gm_tdkjirf1g83ud, f0) == 0 && offsetof(gm_tdkjirf1g83ud, f1) == 8, "layout of `ThreadState`");
_Static_assert(sizeof(gm_th5tat709fgha) == 64 && _Alignof(gm_th5tat709fgha) == 8 && offsetof(gm_th5tat709fgha, f0) == 0 && offsetof(gm_th5tat709fgha, f1) == 8, "layout of `Start`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tiilmfd16v3fc f1; gm_tte9vgf6noic9 f2; uint64_t f3; uint32_t f4; uint8_t p5[4]; void * f5; uint64_t f6; } gm_tjsm4kosd5pev;
_Static_assert(sizeof(gm_tmr8tdh5g7uj6) == 32 && _Alignof(gm_tmr8tdh5g7uj6) == 8 && offsetof(gm_tmr8tdh5g7uj6, f0) == 0 && offsetof(gm_tmr8tdh5g7uj6, f1) == 8, "layout of `Flow!(fiber: u32, key: WithKey)`");
_Static_assert(sizeof(gm_to8g4620flbcm) == 128 && _Alignof(gm_to8g4620flbcm) == 8 && offsetof(gm_to8g4620flbcm, f0) == 0 && offsetof(gm_to8g4620flbcm, f1) == 16 && offsetof(gm_to8g4620flbcm, f2) == 24 && offsetof(gm_to8g4620flbcm, f3) == 64 && offsetof(gm_to8g4620flbcm, f4) == 72 && offsetof(gm_to8g4620flbcm, f5) == 112, "layout of `WithRec`");
_Static_assert(sizeof(gm_tolm6p7jp1hb3) == 72 && _Alignof(gm_tolm6p7jp1hb3) == 8 && offsetof(gm_tolm6p7jp1hb3, f0) == 0 && offsetof(gm_tolm6p7jp1hb3, f1) == 16 && offsetof(gm_tolm6p7jp1hb3, f2) == 32, "layout of `EvalTrap`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t07lf02ct4q61 f1; } gm_tslj4ghflekdk;
_Static_assert(sizeof(gm_tte9vgf6noic9) == 48 && _Alignof(gm_tte9vgf6noic9) == 8 && offsetof(gm_tte9vgf6noic9, f0) == 0 && offsetof(gm_tte9vgf6noic9, f1) == 8, "layout of `Installed`");
_Static_assert(sizeof(gm_t68f8jeqq3b3v) == 72 && _Alignof(gm_t68f8jeqq3b3v) == 8 && offsetof(gm_t68f8jeqq3b3v, f0) == 0 && offsetof(gm_t68f8jeqq3b3v, f1) == 64, "layout of `?Start`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tnsjse4su5jue v1; gm_tslj4ghflekdk v2; } f1; } gm_t9hl37efuvhj8;
_Static_assert(sizeof(gm_td6r3ebeccg25) == 160 && _Alignof(gm_td6r3ebeccg25) == 8 && offsetof(gm_td6r3ebeccg25, f0) == 0 && offsetof(gm_td6r3ebeccg25, f1) == 56 && offsetof(gm_td6r3ebeccg25, f2) == 96 && offsetof(gm_td6r3ebeccg25, f3) == 136 && offsetof(gm_td6r3ebeccg25, f4) == 144 && offsetof(gm_td6r3ebeccg25, f5) == 152, "layout of `Saved`");
_Static_assert(sizeof(gm_tjsm4kosd5pev) == 112 && _Alignof(gm_tjsm4kosd5pev) == 8 && offsetof(gm_tjsm4kosd5pev, f0) == 0 && offsetof(gm_tjsm4kosd5pev, f1) == 8 && offsetof(gm_tjsm4kosd5pev, f2) == 32 && offsetof(gm_tjsm4kosd5pev, f3) == 80 && offsetof(gm_tjsm4kosd5pev, f4) == 88 && offsetof(gm_tjsm4kosd5pev, f5) == 96 && offsetof(gm_tjsm4kosd5pev, f6) == 104, "layout of `CtxNode`");
typedef struct { gm_td6r3ebeccg25 f0; uint8_t f1; uint8_t pe[7]; } gm_tphppq0r5de2b;
_Static_assert(sizeof(gm_tslj4ghflekdk) == 40 && _Alignof(gm_tslj4ghflekdk) == 8 && offsetof(gm_tslj4ghflekdk, f0) == 0 && offsetof(gm_tslj4ghflekdk, f1) == 8, "layout of `(fiber: u32, flow: ?Flow)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_t9hl37efuvhj8 f3; gm_b16_8 f4; gm_trqfvmjtt4sij f5; } gm_t5rqrtiuf7cri;
_Static_assert(sizeof(gm_t9hl37efuvhj8) == 56 && _Alignof(gm_t9hl37efuvhj8) == 8 && offsetof(gm_t9hl37efuvhj8, f0) == 0 && offsetof(gm_t9hl37efuvhj8, f1) == 8, "layout of `Msg`");
typedef struct { gm_th2lqbo77pvsc f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; gm_b16_8 f4; gm_tphppq0r5de2b f5; gm_tn68el8d14vku f6; uint64_t f7; gm_tl2g3fsjpi9op f8; gm_t68f8jeqq3b3v f9; uint64_t f10; uint32_t f11; uint8_t pe[4]; } gm_to1vg29gqhjl9;
_Static_assert(sizeof(gm_tphppq0r5de2b) == 168 && _Alignof(gm_tphppq0r5de2b) == 8 && offsetof(gm_tphppq0r5de2b, f0) == 0 && offsetof(gm_tphppq0r5de2b, f1) == 160, "layout of `?Saved`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t9hl37efuvhj8 v1; } f1; } gm_tv90d8meul1fb;
_Static_assert(sizeof(gm_t5rqrtiuf7cri) == 200 && _Alignof(gm_t5rqrtiuf7cri) == 8 && offsetof(gm_t5rqrtiuf7cri, f0) == 0 && offsetof(gm_t5rqrtiuf7cri, f1) == 8 && offsetof(gm_t5rqrtiuf7cri, f2) == 48 && offsetof(gm_t5rqrtiuf7cri, f3) == 88 && offsetof(gm_t5rqrtiuf7cri, f4) == 144 && offsetof(gm_t5rqrtiuf7cri, f5) == 160, "layout of `Fibers`");
_Static_assert(sizeof(gm_to1vg29gqhjl9) == 360 && _Alignof(gm_to1vg29gqhjl9) == 8 && offsetof(gm_to1vg29gqhjl9, f0) == 0 && offsetof(gm_to1vg29gqhjl9, f1) == 24 && offsetof(gm_to1vg29gqhjl9, f2) == 25 && offsetof(gm_to1vg29gqhjl9, f3) == 32 && offsetof(gm_to1vg29gqhjl9, f4) == 40 && offsetof(gm_to1vg29gqhjl9, f5) == 56 && offsetof(gm_to1vg29gqhjl9, f6) == 224 && offsetof(gm_to1vg29gqhjl9, f7) == 240 && offsetof(gm_to1vg29gqhjl9, f8) == 248 && offsetof(gm_to1vg29gqhjl9, f9) == 272 && offsetof(gm_to1vg29gqhjl9, f10) == 344 && offsetof(gm_to1vg29gqhjl9, f11) == 352, "layout of `CoroRec`");
_Static_assert(sizeof(gm_tv90d8meul1fb) == 64 && _Alignof(gm_tv90d8meul1fb) == 8 && offsetof(gm_tv90d8meul1fb, f0) == 0 && offsetof(gm_tv90d8meul1fb, f1) == 8, "layout of `Flow!Msg`");
struct gm_fr_abort_all_4him68f {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* rest */
    gm_b0_1 ret;
};
struct gm_fr_aborted_ua52qs6 {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_add_coro_k13bkpe {
    gm_frame h;
    gm_th5tat709fgha l1; /* start */
    uint32_t ret;
};
struct gm_fr_cancel__tb288ki {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_cancel_fiber_n4k6buo {
    gm_frame h;
    gm_tslj4ghflekdk l4; /* x */
    gm_tnsjse4su5jue l6; /* x */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_check_stack_eei454t {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_complete_ok_ufvdohe {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_complete_u18qm88 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_consume_u5eqint {
    gm_frame h;
    gm_tmr8tdh5g7uj6 ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_coro_entry_62llbbf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_coro_main_hqntcv4 {
    gm_frame h;
    gm_t9hl37efuvhj8 l3; /* first */
    gm_th5tat709fgha l4; /* start */
    gm_th5tat709fgha l7; /* start */
    gm_tonn1ba3fjh5g l12; /* x */
    gm_t79rbdv4rafe5 l14; /* x */
    gm_b0_1 ret;
};
struct gm_fr_ctl_perform_kobk825 {
    gm_frame h;
    gm_tjsm4kosd5pev l6; /* node */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_drive_i7nr5mu {
    gm_frame h;
    gm_t9hl37efuvhj8 l4; /* msg */
    gm_tnsjse4su5jue l5; /* x */
    gm_trqfvmjtt4sij l9; /* args */
    gm_tslj4ghflekdk l14; /* x */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_empty_slice_2p7d2kv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_54qkesi {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_84b0b46 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8othtri_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fbf7s1e {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_end_thread_j1ah2vh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_exits_past_2b4ebbp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_extend_5tfshb9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fast_23em7sv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fiber_builtin_rpc9l9s {
    gm_frame h;
    gm_th5tat709fgha l14; /* start */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_fibers_i2ha6hs {
    gm_frame h;
    gm_t5rqrtiuf7cri ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_handoff_mtsh8r2 {
    gm_frame h;
    gm_t9hl37efuvhj8 l2; /* msg */
    gm_td6r3ebeccg25 l4; /* s */
    gm_t9hl37efuvhj8 l5; /* r */
    gm_t9hl37efuvhj8 ret;
};
struct gm_fr_host_stack_gpfprtr {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_list_12h90il {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fufbgf7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_j8jr9cr_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_jmo8t51 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_kl0t9m0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_new_coro_06nq12a {
    gm_frame h;
    gm_th5tat709fgha l1; /* start */
    uint32_t ret;
};
struct gm_fr_new_fiber_39221jf {
    gm_frame h;
    gm_th5tat709fgha l2; /* start */
    uint32_t ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rluch9r {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t6u2ggv {
    gm_frame h;
    gm_to8g4620flbcm l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vpv1j2p {
    gm_frame h;
    gm_to1vg29gqhjl9 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_put_cont_vgnbja2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_bslikk9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_f62s8ln {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j41e2v9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resume__mi7hk2f {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_root_host_stack_e3obr0f {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_run_coroutines_0a7hn79 {
    gm_frame h;
    void * l0; /* m */
    gm_closure l1; /* body */
    gm_t3p0qn9o5e53o l2; /* sched */
    gm_b16_8 l3; /* stack */
    gm_tljvbvj59adij l4; /* outcome */
    gm_tljvbvj59adij ret;
    gm_tljvbvj59adij w0;
};
struct gm_fr_same_key_a5oh9pl {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_set_stack_floor_f15muof {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_switch_to_gq9vlt3 {
    gm_frame h;
    gm_t9hl37efuvhj8 l4; /* msg */
    gm_tv90d8meul1fb ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_transfer_0mkrfnm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_with_on_a8m20cg {
    gm_frame h;
    gm_t710lsrq1m8gk l2; /* w */
    gm_to8g4620flbcm l12; /* x */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_with_rec_2eah8tp {
    gm_frame h;
    void * ret;
};
struct gm_pr_coro_main_hqntcv4_190 { gm_node *n; uint32_t a0; };
struct gm_pr_handoff_mtsh8r2_23 { gm_node *n; uint32_t a0; };
struct gm_pr_run_coroutines_0a7hn79_45 { gm_node *n; uint32_t a0; };
gm_b0_1 gm_dt365sua5c99_0(void *env, gm_node *cx, gm_node *inst, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_abort_all_4him68f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tn68el8d14vku a2);
__attribute__((aligned(4))) void gm_f_aborted_ua52qs6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint32_t gm_f_add_coro_k13bkpe(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_call_clause_bjbs6l1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_call_value_fm9m0cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_cancel__tb288ki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_cancel_fiber_n4k6buo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tn68el8d14vku a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_check_stack_eei454t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_complete_ok_ufvdohe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tn68el8d14vku a2);
__attribute__((aligned(4))) void gm_f_complete_u18qm88(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tn68el8d14vku a2, const void * a3, uint8_t a4);
__attribute__((aligned(4))) void gm_f_consume_u5eqint(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_coro_entry_62llbbf(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_coro_main_hqntcv4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_ctl_perform_kobk825(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_deinit_padgogk(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_tq06jtm(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_v2ik7pl(void * a0);
__attribute__((aligned(4))) void gm_f_drive_i7nr5mu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tn68el8d14vku a2, uint32_t a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_54qkesi(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8othtri_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fbf7s1e(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_thread_j1ah2vh(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_ev_25jfdar(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_exits_past_2b4ebbp(const void * a0, gm_csff7attl2ehc5 a1);
__attribute__((aligned(4))) uint8_t gm_f_exits_past_2b4ebbp_c(gm_t76123gfet73g a0, gm_csff7attl2ehc5 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_5tfshb9(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) void gm_f_fiber_builtin_rpc9l9s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2, void * a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_fibers_i2ha6hs(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_format_40t0ers(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_ialr6ut(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_handoff_mtsh8r2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) void gm_f_host_stack_gpfprtr(void *gm_out, uint64_t a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_host_stack_gpfprtr_c(uint64_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_kill_temps_8sgbc55(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_list_12h90il(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_j8jr9cr_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_jmo8t51(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_kl0t9m0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
__attribute__((aligned(4))) void gm_f_make_fill_jpvm0kc(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint8_t a1, const void * a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
inline __attribute__((aligned(4))) uint32_t gm_f_new_coro_06nq12a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint32_t a2);
__attribute__((aligned(4))) uint32_t gm_f_new_fiber_39221jf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tn68el8d14vku a1, const void * a2, const void * a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_jd11mvt(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_mbrm063(void *env, gm_node *cx, gm_node *inst, gm_tbl8il92l97ut a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t6u2ggv(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vpv1j2p(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_put_cont_vgnbja2(void * a0, gm_b16_8 a1, uint32_t a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_put_word_tfn443i(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bslikk9(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_f62s8ln(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j41e2v9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) uint32_t gm_f_remove_10muhi8(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_resume__mi7hk2f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_root_host_stack_e3obr0f(void *gm_out);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_root_host_stack_e3obr0f_c(void);
__attribute__((aligned(4))) gm_tljvbvj59adij gm_f_run_coroutines_0a7hn79(void *env, gm_node *cx, gm_node *inst, void * a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_same_key_a5oh9pl(gm_csff7attl2ehc5 a0, gm_csff7attl2ehc5 a1);
inline __attribute__((aligned(4))) void gm_f_set_stack_floor_f15muof(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_swap_remove_7083s0t(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_swap_remove_vmt3jr9(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_switch_thread_m7aoqko(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_switch_to_gq9vlt3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tn68el8d14vku a2, uint32_t a3, const void * a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_thread_main_n6r943c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_transfer_0mkrfnm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) void gm_f_with_on_a8m20cg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void * gm_f_with_rec_2eah8tp(void * a0, gm_csff7attl2ehc5 a1);
__attribute__((aligned(4))) uint64_t gm_f_word_o2rcrhs(void * a0, gm_b16_8 a1, uint64_t a2);
void gm_fs_transfer_0mkrfnm_98(gm_fiber *fb);
void gm_pt_coro_main_hqntcv4_190(void *req, gm_cont k);
void gm_pt_handoff_mtsh8r2_23(void *req, gm_cont k);
void gm_pt_run_coroutines_0a7hn79_45(void *req, gm_cont k);
void gm_wb_run_coroutines_0a7hn79_0(gm_fiber *fb);
extern void * gm_x_calloc_gae3ijq(uint64_t, uint64_t) __asm__(GM_SYM("calloc"));
extern void gm_x_free_eeuh60c(void *) __asm__(GM_SYM("free"));
extern uint8_t gm_c83hsvhq98b[8] __attribute__((aligned(8)));
extern uint8_t gm_ca9ikdfm7p4[4] __attribute__((aligned(4)));
extern uint8_t gm_cbbu3qvtp3q[8] __attribute__((aligned(8)));
extern uint8_t gm_cbcjb1mvjd8[8] __attribute__((aligned(8)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cfea4sug7gd[8] __attribute__((aligned(8)));
extern uint8_t gm_ci39cltltdi[8] __attribute__((aligned(8)));
extern uint8_t gm_cog1l56ulfv[8] __attribute__((aligned(8)));
extern void *const gm_hd365sua5c99[];
extern uint8_t gm_k1b2sgalr12[40] __attribute__((aligned(8)));
extern uint8_t gm_k2t1c54q9lq[40] __attribute__((aligned(8)));
extern uint8_t gm_k4ha9p0b2mp[40] __attribute__((aligned(8)));
extern uint8_t gm_k62pju45ug1[40] __attribute__((aligned(8)));
extern uint8_t gm_k9epus5iul2[40] __attribute__((aligned(8)));
extern uint8_t gm_kau7m62eplk[40] __attribute__((aligned(8)));
extern uint8_t gm_kd9f1ktn0ro[40] __attribute__((aligned(8)));
extern uint8_t gm_kpbp8co2u50[40] __attribute__((aligned(8)));
extern uint8_t gm_kq0m5gkhfc4[40] __attribute__((aligned(8)));
extern uint8_t gm_kvubgn35fiq[40] __attribute__((aligned(8)));
extern const uint8_t gm_s03pmf867dcsr[2];
extern const uint8_t gm_s0e9nt7u0mvf9[66];
extern const uint8_t gm_s2ef11ime8o01[42];
extern const uint8_t gm_s3hali1afgel3[28];
extern const uint8_t gm_s3mvfcnrk9f21[48];
extern const uint8_t gm_s64eu4ogr34e2[62];
extern const uint8_t gm_s6t0sieqs3tpi[48];
extern const uint8_t gm_s7doqph2gqafo[60];
extern const uint8_t gm_s7m5v91ik4pn5[52];
extern const uint8_t gm_sba0bp6ej30io[30];
extern const uint8_t gm_sdko2shp3619i[4];
extern const uint8_t gm_sgpdmvc9s0d2p[3];
extern const uint8_t gm_sij6bc8bh95o1[59];
extern const uint8_t gm_sjtfkltao8c2o[49];
extern const uint8_t gm_skfrgfcfg8vrf[36];
extern const uint8_t gm_skq2uj9oi1243[41];
extern const uint8_t gm_smovtms5ssgir[88];
extern const uint8_t gm_sp7r202318t40[57];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcp0l171g2m7ho[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srmistf0k19ab[72];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t2 == t3));
        if (!t4) {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_coro_entry_62llbbf(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_coro_entry_62llbbf F;
    void * gm_l0; /* p */
    void * gm_l1; /* arg */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcp0l171g2m7ho, 268, 12);
        void * t3 = t2;
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l1;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = gm_l1;
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t6) + 8));
        gm_b0_1 t8 = gm_f_coro_main_hqntcv4((void *)NULL, cx, NULL, t5, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_trap("reached `@unreachable()`", gm_srcp0l171g2m7ho, 270, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2p7d2kv F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_54qkesi(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_54qkesi F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 128)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_84b0b46 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8othtri_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8othtri_2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 4)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fbf7s1e(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fbf7s1e F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 360)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_5tfshb9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_5tfshb9 F;
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
            gm_tftb1odc739t5 t17 = gm_f_regrow_bslikk9(t14, t15, t16);
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
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 16);
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
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 16)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fast_23em7sv F;
    void * gm_l0; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2 = ((GM_LD(uint8_t, (((uint8_t *)t1) + 88) + 0) == ((uint8_t)3ULL)));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&gm_l0));
        gm_l2 = t2;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l3 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = gm_l3;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l2;
                        uint8_t t19 = gm_l1;
                        GM_ST(uint8_t, ((uint8_t *)t18 + t17 * 1), t19);
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t21 = gm_l4;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l4 = t24;
                }
            }
lb5:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_12h90il(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_12h90il F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fbf7s1e();
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
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fufbgf7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_2p7d2kv();
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
inline __attribute__((aligned(4))) void gm_f_list_j8jr9cr_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_j8jr9cr_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8othtri_2();
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
inline __attribute__((aligned(4))) void gm_f_list_jmo8t51(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_jmo8t51 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_54qkesi();
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
inline __attribute__((aligned(4))) void gm_f_list_kl0t9m0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_kl0t9m0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_84b0b46();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_kpppm1j F;
    void * gm_l0; /* m */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)t1) + 360) + 40));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 360) + 48));
    gm_t2lqu8jsj511d t5;
    GM_ST(uint64_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 8), t4);
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
inline __attribute__((aligned(4))) uint32_t gm_f_new_coro_06nq12a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint32_t a2) {
    struct gm_fr_new_coro_06nq12a F;
    void * gm_l0; /* m */
    uint32_t gm_l2; /* thread */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    void * t1 = gm_l0;
    unsigned __int128 n2 = (unsigned __int128)((uint64_t)20ULL);
    if (n2 >= 64) gm_trap_shift((uint64_t)n2, "usize", gm_srcp0l171g2m7ho, 376, 96);
    uint64_t r3 = (uint64_t)((uint64_t)((uint64_t)8ULL) << (int)n2);
    bool o4 = (uint64_t)(r3 >> (int)n2) != ((uint64_t)8ULL);
    if (o4) gm_trap("integer overflow in `<<`", gm_srcp0l171g2m7ho, 376, 96);
    uint64_t t5 = r3;
    uint32_t t6 = gm_l2;
    uint32_t t7 = gm_f_add_coro_k13bkpe((void *)NULL, cx, NULL, t1, &(*(gm_th5tat709fgha *)((uint8_t *)&F.l1)), t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t7;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
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
            gm_b16_8 t11 = gm_f_empty_slice_d914i5u();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_2onqfca(t10, ((uint64_t)1ULL), t12);
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
        uint32_t t21 = gm_l1;
        GM_ST(uint32_t, ((uint8_t *)t20 + t18 * 4), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 4)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rluch9r F;
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
            gm_b16_8 t11 = gm_f_empty_slice_2p7d2kv();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_bslikk9(t10, ((uint64_t)1ULL), t12);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t6u2ggv(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t6u2ggv F;
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
            gm_b16_8 t11 = gm_f_empty_slice_54qkesi();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_j41e2v9(t10, ((uint64_t)1ULL), t12);
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
        gm_to8g4620flbcm t21 = GM_LD(gm_to8g4620flbcm, ((uint8_t *)a1));
        GM_ST(gm_to8g4620flbcm, ((uint8_t *)t20 + t18 * 128), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 128)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vpv1j2p(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vpv1j2p F;
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
            gm_b16_8 t11 = gm_f_empty_slice_fbf7s1e();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_f62s8ln(t10, ((uint64_t)1ULL), t12);
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
        memmove(((uint8_t *)t20 + t18 * 360), ((uint8_t *)a1), 360);
    }
    {
        void * t21 = gm_l0;
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
        void * t24 = ((uint8_t *)t23);
        uint64_t t25 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t26 = gm_l2;
        uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
        uint64_t t29 = r27;
        uint64_t t30 = (uintptr_t)t29;
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t24 + t25 * 360)));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30 - t25));
        GM_ST(gm_b16_8, (((uint8_t *)t21) + 0), t31);
    }
    {
        gm_b0_1 t32;
        gm_tftb1odc739t5 t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
        ((uint8_t *)&t33)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t33 + 1), t32);
        uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
        F.ret = t33;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_put_cont_vgnbja2(void * a0, gm_b16_8 a1, uint32_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_cont_vgnbja2 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* dst */
    uint32_t gm_l2; /* fiber */
    uint64_t gm_l3; /* epoch */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        uint32_t t4 = gm_l2;
        uint64_t t5 = (uint64_t)t4;
        gm_f_put_word_tfn443i(t2, t3, ((uint64_t)0ULL), t5);
    }
    {
        void * t6 = gm_l0;
        gm_b16_8 t7 = gm_l1;
        uint64_t t8 = gm_l3;
        gm_f_put_word_tfn443i(t6, t7, ((uint64_t)1ULL), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2onqfca F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)4ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bslikk9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_bslikk9 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_f62s8ln(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_f62s8ln F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)360ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j41e2v9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j41e2v9 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)128ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_release_jn5kqdi F;
    void * gm_l0; /* m */
    gm_t2lqu8jsj511d gm_l1; /* mk */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
        GM_ST(uint64_t, ((((uint8_t *)t2) + 360) + 40), t3);
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        GM_ST(uint64_t, ((((uint8_t *)t4) + 360) + 48), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_same_key_a5oh9pl(gm_csff7attl2ehc5 a0, gm_csff7attl2ehc5 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_key_a5oh9pl F;
    gm_tn68el8d14vku gm_l0; /* a */
    gm_tn68el8d14vku gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tn68el8d14vku, a0);
    gm_l1 = GM_CONV(gm_tn68el8d14vku, a1);
    uint8_t t1 = 0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 0));
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t7 = ((t5 == t6));
        t1 = t7;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_set_stack_floor_f15muof(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_set_stack_floor_f15muof F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* size */
    uint8_t gm_l2; /* marker */
    uint64_t gm_l3; /* here */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint8_t)0ULL);
    }
    {
        void * t2 = (void *)((uint8_t *)&gm_l2);
        uint64_t t3 = (uintptr_t)t2;
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = gm_l3;
        uint64_t t6 = gm_l1;
        uint64_t t7 = GM_LD(uint64_t, gm_cfea4sug7gd);
        uint64_t t8 = gm_l1;
        if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcp0l171g2m7ho, 263, 53);
        uint64_t t9 = (uint64_t)(t8 / ((uint64_t)2ULL));
        uint64_t t10 = (t7 < t9 ? t7 : t9);
        uint64_t r11; bool o12 = __builtin_sub_overflow(t6, t10, &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_srcp0l171g2m7ho, 263, 29);
        uint64_t t13 = r11;
        uint64_t r14; bool o15 = __builtin_sub_overflow(t5, t13, &r14);
        if (o15) gm_trap("integer overflow in `-`", gm_srcp0l171g2m7ho, 263, 21);
        uint64_t t16 = r14;
        GM_ST(uint64_t, (((uint8_t *)t4) + 776), t16);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 360);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 40));
        uint64_t t6 = (uintptr_t)t5;
        void * t7 = gm_l2;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 0) + 0) + 8);
        if (t6 >= (uintptr_t)(t8)) gm_trap_index(t6, t8, gm_srcpit8mdn0781q, 386, 14);
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 0) + 0)) + t6 * 16));
        gm_l3 = t9;
    }
    {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t11 = (uintptr_t)t10;
        gm_l4 = t11;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l2;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
        uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t17 = r15;
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t20 = r18;
        uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
        uint64_t t22 = (uint64_t)~t21;
        uint64_t t23 = (uint64_t)(t20 & t22);
        uint64_t t24 = gm_l4;
        uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
        if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 390, 12);
        uint64_t t27 = r25;
        gm_l5 = t27;
    }
    {
        uint64_t t28 = gm_l1;
        uint64_t t29 = gm_f_us_cuv01fp(t28);
        gm_l6 = t29;
    }
    {
        uint8_t t31 = 0;
        uint64_t t32 = gm_l1;
        uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
        if (!t33) {
            uint64_t t34 = gm_l5;
            uint64_t t35 = gm_l6;
            uint64_t r36; bool o37 = __builtin_add_overflow(t34, t35, &r36);
            if (o37) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 392, 18);
            uint64_t t38 = r36;
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t40 = (t38 > t39);
            t31 = t40;
        } else t31 = 1;
        if (!t31) goto ie30;
        {
            void * t42 = gm_l0;
            uint64_t t43 = gm_l1;
            gm_b16_8 t44 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t47 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t48 = gm_l5;
        uint64_t t49 = (uintptr_t)t48;
        uint64_t t50 = gm_l5;
        uint64_t t51 = gm_l6;
        uint64_t r52; bool o53 = __builtin_add_overflow(t50, t51, &r52);
        if (o53) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 21);
        uint64_t t54 = r52;
        uint64_t t55 = (uintptr_t)t54;
        if (t49 > t55 || t55 > (uintptr_t)t46) gm_trap_slice(t49, t55, t46, gm_srcpit8mdn0781q, 393, 10);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t47 + t49 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55 - t49));
        gm_l7 = t56;
    }
    {
        void * t57 = gm_l2;
        uint64_t t58 = gm_l5;
        uint64_t t59 = gm_l6;
        uint64_t r60; bool o61 = __builtin_add_overflow(t58, t59, &r60);
        if (o61) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 14);
        uint64_t t62 = r60;
        GM_ST(uint64_t, (((uint8_t *)t57) + 48), t62);
    }
    {
        gm_b16_8 t63 = gm_l7;
        gm_f_fill_abmf5g5(t63, ((uint8_t)0ULL));
    }
    {
        gm_b16_8 t64 = gm_l7;
        uint8_t t65 = 0;
        F.ret = t64;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_us_cuv01fp F;
    uint64_t gm_l0; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = gm_l0;
    uint64_t t2 = (uint64_t)t1;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
gm_b0_1 gm_dt365sua5c99_0(void *env, gm_node *cx, gm_node *inst, uint32_t a1, gm_b16_8 a2) { return gm_f_transfer_0mkrfnm(NULL, cx, inst, GM_LD(void *, env), a1, a2); }
__attribute__((aligned(4))) gm_b0_1 gm_f_abort_all_4him68f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tn68el8d14vku a2) {
    struct gm_fr_abort_all_4him68f F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tn68el8d14vku gm_l2; /* key */
    void * gm_l3; /* w */
    uint32_t gm_l4; /* id */
    void * gm_l5; /* w */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* id */
    void * gm_l10; /* r */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint32_t gm_l13; /* id */
    GM_STACK_CHECK(&F, 849u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb2;
            {
                void * t5 = gm_l0;
                gm_tn68el8d14vku t6 = gm_l2;
                void * t7 = gm_f_with_rec_2eah8tp(t5, GM_CONV(gm_csff7attl2ehc5, t6));
                void * t8;
                if ((GM_LD(void *, ((uint8_t *)&t7)) != 0)) {
                    void * t9 = GM_LD(void *, (((uint8_t *)&t7) + 0));
                    t8 = t9;
                } else {
                    gm_b0_1 t10;
                    uint8_t t11 = 0;
                    F.ret = t10;
                    goto gm_fret;
                }
                gm_l3 = t8;
            }
            {
                void * t12 = gm_l3;
                void * t13 = (void *)(((uint8_t *)t12) + 24);
                gm_t9805qtonbsag t14 = gm_f_pop_570g79n(t13);
                uint32_t t15;
                if (((((uint8_t *)&t14))[4] != 0)) {
                    uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&t14) + 0));
                    t15 = t16;
                } else {
                    goto lb2;
                }
                gm_l4 = t15;
            }
            {
                {
                    void * t18 = gm_l0;
                    gm_tl874t2atnlh3 t19 = gm_l1;
                    gm_tn68el8d14vku t20 = gm_l2;
                    uint32_t t21 = gm_l4;
                    gm_t9hl37efuvhj8 t22;
                    memset(&t22, 0, sizeof t22);
                    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)3ULL));
                    gm_tv90d8meul1fb t23;
                    gm_f_switch_to_gq9vlt3((void *)&t23, (void *)NULL, cx, NULL, t18, t19, t20, t21, &t22);
                    if (gm_unw.kind) {
                        goto gm_ll3;
                    }
                }
            }
lc2:;
        }
gm_ll3:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
    }
    {
        void * t24 = gm_l0;
        gm_tn68el8d14vku t25 = gm_l2;
        void * t26 = gm_f_with_rec_2eah8tp(t24, GM_CONV(gm_csff7attl2ehc5, t25));
        void * t27;
        if ((GM_LD(void *, ((uint8_t *)&t26)) != 0)) {
            void * t28 = GM_LD(void *, (((uint8_t *)&t26) + 0));
            t27 = t28;
        } else {
            gm_b0_1 t29;
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        gm_l5 = t27;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t32 = gm_l5;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 72) + 0));
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
                    uint32_t t43 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t41 * 4));
                    uint32_t t46 = GM_LD(uint32_t, ((uint8_t *)&t43));
                    gm_l9 = t46;
                    {
                        void * t48 = gm_l0;
                        gm_b16_8 t49 = GM_LD(gm_b16_8, (((((uint8_t *)t48) + 1088) + 8) + 0));
                        gm_b16_8 t50 = t49;
                        uint32_t t51 = gm_l9;
                        uint64_t t52 = (uint64_t)t51;
                        uint64_t t53 = gm_f_us_cuv01fp(t52);
                        uint64_t t54 = (uintptr_t)t53;
                        uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&t50) + 8);
                        if (t54 >= (uintptr_t)(t55)) gm_trap_index(t54, t55, gm_srcp0l171g2m7ho, 505, 15);
                        void * t56 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t50)) + t54 * 360);
                        gm_l10 = t56;
                    }
                    {
                        void * t58 = gm_l10;
                        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)t58) + 24));
                        if (!t59) goto ie57;
                        void * t60 = gm_l10;
                        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)t60) + 25));
                        uint8_t t62 = (uint8_t)!t61;
                        if (!t62) goto ie57;
                        {
                            void * t64 = (void *)((uint8_t *)&F.l6);
                            uint32_t t65 = gm_l9;
                            gm_tftb1odc739t5 t66 = gm_f_push_im9d4ro(t64, t65);
                            if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srcp0l171g2m7ho, 506, 43);
                            gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
                        }
                        goto ix57;
ie57:;
ix57:;
                    }
                    goto me44;
mn45:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 504, 5);
me44:;
                }
lc34:;
                {
                    uint64_t t69 = gm_l8;
                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                    uint64_t t72 = r70;
                    gm_l8 = t72;
                }
            }
lb34:;
        }
    }
    {
        {
            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_l11 = t74;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t76 = gm_l12;
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t78 = (t76 < t77);
                if (!t78) goto lb75;
                {
                    uint64_t t81 = gm_l12;
                    uint64_t t82 = (uintptr_t)t81;
                    uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint32_t t84 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t82 * 4));
                    uint32_t t87 = GM_LD(uint32_t, ((uint8_t *)&t84));
                    gm_l13 = t87;
                    {
                        {
                            void * t90 = gm_l0;
                            gm_tl874t2atnlh3 t91 = gm_l1;
                            gm_tn68el8d14vku t92 = gm_l2;
                            uint32_t t93 = gm_l13;
                            gm_t9hl37efuvhj8 t94;
                            memset(&t94, 0, sizeof t94);
                            GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)3ULL));
                            gm_tv90d8meul1fb t95;
                            gm_f_switch_to_gq9vlt3((void *)&t95, (void *)NULL, cx, NULL, t90, t91, t92, t93, &t94);
                            if (gm_unw.kind) {
                                goto gm_ll79;
                            }
                        }
                    }
                    goto me85;
mn86:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 508, 5);
me85:;
                }
lc75:;
                {
                    uint64_t t97 = gm_l12;
                    uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                    uint64_t t100 = r98;
                    gm_l12 = t100;
                }
            }
gm_ll79:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb75; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc75; }
            }
            goto gm_lad;
lb75:;
        }
    }
    {
        void * t101 = (void *)((uint8_t *)&F.l6);
        gm_f_deinit_padgogk(t101);
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
inline __attribute__((aligned(4))) void gm_f_aborted_ua52qs6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_aborted_ua52qs6 F;
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tolm6p7jp1hb3 t1;
    memset((uint8_t *)&t1 + 28, 0, 4);
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sba0bp6ej30io));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(29));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 16, t3);
    gm_f_list_j8jr9cr_2((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t1 + 32)), (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t4 = gm_f_new_jd11mvt((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kau7m62eplk)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcp0l171g2m7ho, 323, 46);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t76123gfet73g *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_t76123gfet73g *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t76123gfet73g *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t76123gfet73g *)gm_out) + 16, 0);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t76123gfet73g *)gm_out) + 0, ((uint8_t)4ULL));
    GM_ST(void *, (uint8_t *)&(*(gm_t76123gfet73g *)gm_out) + 8, t5);
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
__attribute__((aligned(4))) uint32_t gm_f_add_coro_k13bkpe(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2, uint32_t a3) {
    struct gm_fr_add_coro_k13bkpe F;
    void * gm_l0; /* m */
    uint64_t gm_l2; /* want */
    uint32_t gm_l3; /* thread */
    gm_b16_8 gm_l4; /* host */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint32_t gm_l8; /* id */
    void * gm_l9; /* arg */
    GM_STACK_CHECK(&F, 1174u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_b16_8 t2 = gm_f_empty_slice_ijtp9lo();
        gm_l4 = t2;
    }
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            void * t4 = gm_l0;
            uint64_t t5 = GM_LD(uintptr_t, (((((uint8_t *)t4) + 1088) + 160) + 0) + 8);
            gm_l5 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l6;
                uint64_t t8 = gm_l5;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l6;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l7 = t15;
                    {
                        uint64_t t18 = gm_l7;
                        uint64_t t19 = (uintptr_t)t18;
                        void * t20 = gm_l0;
                        uint64_t t21 = GM_LD(uintptr_t, (((((uint8_t *)t20) + 1088) + 160) + 0) + 8);
                        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srcp0l171g2m7ho, 382, 12);
                        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, (((((uint8_t *)t20) + 1088) + 160) + 0)) + t19 * 16) + 8);
                        uint64_t t23 = gm_l2;
                        uint8_t t24 = (t22 >= t23);
                        if (!t24) goto ie17;
                        {
                            void * t26 = gm_l0;
                            void * t27 = (void *)((((uint8_t *)t26) + 1088) + 160);
                            uint64_t t28 = gm_l7;
                            gm_b16_8 t29 = gm_f_swap_remove_vmt3jr9(t27, t28);
                            gm_l4 = t29;
                        }
                        {
                            goto lb6;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 381, 5);
me13:;
                }
lc6:;
                {
                    uint64_t t31 = gm_l6;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l6 = t34;
                }
            }
lb6:;
        }
    }
    {
        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t37 = ((t36 == ((uint64_t)0ULL)));
        if (!t37) goto ie35;
        {
            uint64_t t39 = gm_l2;
            gm_th2lqbo77pvsc t40;
            gm_f_host_stack_gpfprtr((void *)&t40, t39);
            if (!((((uint8_t *)&t40))[16] != 0)) gm_trap("unwrap of null", gm_srcp0l171g2m7ho, 387, 31);
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
            gm_l4 = t41;
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        void * t42 = gm_l0;
        uint64_t t43 = GM_LD(uintptr_t, (((((uint8_t *)t42) + 1088) + 8) + 0) + 8);
        uint32_t t44 = (uint32_t)t43;
        gm_l8 = t44;
    }
    {
        void * t45 = gm_l0;
        uint32_t t46 = gm_l8;
        gm_tbl8il92l97ut t47;
        GM_ST(uint32_t, (uint8_t *)&t47 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t47 + 0), t45);
        GM_ST(uint32_t, ((uint8_t *)&t47 + 8), t46);
        gm_tq7s98ihonkju t48 = gm_f_new_mbrm063((void *)NULL, cx, NULL, t47, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k62pju45ug1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 8, gm_srcp0l171g2m7ho, 389, 12);
        void * t49 = GM_LD(void *, (((uint8_t *)&t48) + 8));
        gm_l9 = t49;
    }
    {
        void * t50 = gm_l0;
        void * t51 = (void *)((((uint8_t *)t50) + 1088) + 8);
        gm_to1vg29gqhjl9 t52;
        memset((uint8_t *)&t52 + 26, 0, 6);
        memset((uint8_t *)&t52 + 356, 0, 4);
        gm_th2lqbo77pvsc t53;
        memset(&t53, 0, sizeof t53);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t52 + 0, t53);
        GM_ST(uint8_t, (uint8_t *)&t52 + 24, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&t52 + 25, ((uint8_t)0));
        void * t54 = gm_l9;
        void * t55 = t54;
        GM_ST(void *, (uint8_t *)&t52 + 32, t55);
        gm_b16_8 t56 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t52 + 40, t56);
        gm_tphppq0r5de2b t57;
        memset(&t57, 0, sizeof t57);
        GM_ST(gm_tphppq0r5de2b, (uint8_t *)&t52 + 56, t57);
        gm_tn68el8d14vku t58;
        GM_ST(uint32_t, (uint8_t *)&t58 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t58 + 0), ((uint64_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t58 + 8), ((uint32_t)0ULL));
        GM_ST(gm_tn68el8d14vku, (uint8_t *)&t52 + 224, t58);
        GM_ST(uint64_t, (uint8_t *)&t52 + 240, ((uint64_t)1ULL));
        gm_tl2g3fsjpi9op t59;
        GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t59 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t59 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tl2g3fsjpi9op, (uint8_t *)&t52 + 248, t59);
        gm_th5tat709fgha t60 = GM_LD(gm_th5tat709fgha, ((uint8_t *)a1));
        gm_t68f8jeqq3b3v t61;
        GM_ST(uint8_t, (uint8_t *)&t61 + 65, 0);
        GM_ST(uint16_t, (uint8_t *)&t61 + 66, 0);
        GM_ST(uint32_t, (uint8_t *)&t61 + 68, 0);
        GM_ST(gm_th5tat709fgha, ((uint8_t *)&t61), t60);
        ((uint8_t *)&t61)[64] = 1;
        GM_ST(gm_t68f8jeqq3b3v, (uint8_t *)&t52 + 272, t61);
        uint64_t t62 = (uint64_t)~((uint64_t)0ULL);
        GM_ST(uint64_t, (uint8_t *)&t52 + 344, t62);
        uint32_t t63 = gm_l3;
        GM_ST(uint32_t, (uint8_t *)&t52 + 352, t63);
        gm_tftb1odc739t5 t64 = gm_f_push_vpv1j2p(t51, &t52);
        if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_srcp0l171g2m7ho, 390, 5);
        gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
    }
    {
        uint32_t t66 = gm_l8;
        uint8_t t67 = 0;
        F.ret = t66;
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
inline __attribute__((aligned(4))) void gm_f_cancel__tb288ki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3) {
    struct gm_fr_cancel__tb288ki F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* k */
    void * gm_l3; /* e */
    gm_t2lqu8jsj511d gm_l4; /* mk */
    gm_b16_8 gm_l5; /* kb */
    gm_t4qfihdig0mro gm_l6; /* c */
    GM_STACK_CHECK(&F, 540u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            void * t3 = gm_l0;
            gm_t2lqu8jsj511d t4 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t3));
            gm_l4 = t4;
        }
        {
            void * t5 = gm_l0;
            void * t6 = gm_l2;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 104));
            gm_b16_8 t8 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t5, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t8;
        }
        {
            void * t9 = gm_l0;
            gm_tbl8il92l97ut t10 = gm_l1;
            void * t11 = gm_l2;
            gm_b16_8 t12 = gm_l5;
            gm_tcfa6l7reoosn t13;
            gm_f_ev_25jfdar((void *)&t13, (void *)NULL, cx, NULL, t9, t10, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_tcfa6l7reoosn t14;
                GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
                ((uint8_t *)&t14)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t14 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t13) + 8)));
                F.ret = t14;
                goto gm_fret;
            }
            gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 8));
        }
        {
            void * t16 = gm_l0;
            void * t17 = gm_l3;
            gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t17) + 112));
            void * t19 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
            gm_b16_8 t20 = gm_l5;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            gm_tmr8tdh5g7uj6 t22;
            gm_f_consume_u5eqint((void *)&t22, (void *)NULL, cx, NULL, t16, t18, t19, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t22))[0] != 0)) {
                gm_tcfa6l7reoosn t23;
                GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
                ((uint8_t *)&t23)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t23 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t22) + 8)));
                F.ret = t23;
                goto gm_fret;
            }
            gm_l6 = (*(gm_t4qfihdig0mro *)(((uint8_t *)&t22) + 8));
        }
        {
            void * t24 = gm_l0;
            gm_t2lqu8jsj511d t25 = gm_l4;
            gm_f_release_jn5kqdi(t24, GM_CONV(gm_csc7lc6jlkqkuq, t25));
        }
        {
            void * t26 = gm_l0;
            void * t27 = gm_l3;
            gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t27) + 112));
            gm_tn68el8d14vku t29 = GM_LD(gm_tn68el8d14vku, (((uint8_t *)&gm_l6) + 8));
            uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
            gm_tcfa6l7reoosn t31;
            gm_f_cancel_fiber_n4k6buo((void *)&t31, (void *)NULL, cx, NULL, t26, t28, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t31))[0] != 0)) {
                gm_tcfa6l7reoosn t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
                ((uint8_t *)&t32)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t32 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t31) + 8)));
                F.ret = t32;
                goto gm_fret;
            }
            gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 8));
        }
    }
    gm_b0_1 t34;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t34);
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
inline __attribute__((aligned(4))) void gm_f_cancel_fiber_n4k6buo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tn68el8d14vku a2, uint32_t a3) {
    struct gm_fr_cancel_fiber_n4k6buo F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tn68el8d14vku gm_l2; /* key */
    uint32_t gm_l3; /* id */
    gm_t76123gfet73g gm_l5; /* f */
    GM_STACK_CHECK(&F, 481u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            void * t3 = gm_l0;
            gm_tl874t2atnlh3 t4 = gm_l1;
            gm_tn68el8d14vku t5 = gm_l2;
            uint32_t t6 = gm_l3;
            gm_t9hl37efuvhj8 t7;
            memset(&t7, 0, sizeof t7);
            GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)2ULL));
            gm_tv90d8meul1fb t8;
            gm_f_switch_to_gq9vlt3((void *)&t8, (void *)NULL, cx, NULL, t3, t4, t5, t6, &t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t8))[0] != 0)) {
                gm_tcfa6l7reoosn t9;
                GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
                ((uint8_t *)&t9)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t9 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t8) + 8)));
                F.ret = t9;
                goto gm_fret;
            }
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
            switch (t16) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma12; default: goto ma14; }
ma11:;
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
            if (t18 != ((uint8_t)0ULL)) goto mn17;
            goto me10;
mn17:;
ma12:;
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
            if (t21 != ((uint8_t)5ULL)) goto mn20;
            F.l4 = (*(gm_tslj4ghflekdk *)(((uint8_t *)&t8) + 16));
            {
                uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 0));
                uint64_t t24 = (uint64_t)t23;
                uint64_t t25 = (uintptr_t)t24;
                void * t26 = gm_l0;
                uint64_t t27 = GM_LD(uintptr_t, (((((uint8_t *)t26) + 1088) + 8) + 0) + 8);
                if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srcp0l171g2m7ho, 518, 13);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t26) + 1088) + 8) + 0)) + t25 * 360) + 248)) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t26) + 1088) + 8) + 0)) + t25 * 360) + 248)) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t26) + 1088) + 8) + 0)) + t25 * 360) + 248)) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t26) + 1088) + 8) + 0)) + t25 * 360) + 248)) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t26) + 1088) + 8) + 0)) + t25 * 360) + 248)) + 16, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t26) + 1088) + 8) + 0)) + t25 * 360) + 248)) + 0, ((uint8_t)2ULL));
            }
            {
                gm_t07lf02ct4q61 t28 = GM_LD(gm_t07lf02ct4q61, (((uint8_t *)&F.l4) + 8));
                gm_t76123gfet73g t29;
                if (((((uint8_t *)&t28))[24] != 0)) {
                    t29 = (*(gm_t76123gfet73g *)(((uint8_t *)&t28) + 0));
                } else {
                    void * t30 = gm_l0;
                    gm_tl874t2atnlh3 t31 = gm_l1;
                    gm_b16_8 t32;
                    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s7m5v91ik4pn5));
                    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(51));
                    gm_t76123gfet73g t33;
                    gm_f_trap_qh84l9d((void *)&t33, (void *)NULL, cx, NULL, t30, t31, t32);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t33);
                    goto gm_fdone;
                }
                gm_l5 = t29;
            }
            {
                uint8_t t35 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l5) + 0) == ((uint8_t)6ULL)));
                if (!t35) goto ie34;
                {
                    gm_b0_1 t37;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t37);
                    goto gm_fdone;
                }
                goto ix34;
ie34:;
ix34:;
            }
            {
                const uint8_t *t38 = ((uint8_t *)&gm_l5);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t38, 24);
                goto gm_fdone;
            }
            goto me10;
mn20:;
ma13:;
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
            if (t40 != ((uint8_t)4ULL)) goto mn39;
            F.l6 = (*(gm_tnsjse4su5jue *)(((uint8_t *)&t8) + 16));
            void * t41 = gm_l0;
            gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 32));
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s0e9nt7u0mvf9));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(65));
            gm_t76123gfet73g t44;
            gm_f_trap_qh84l9d((void *)&t44, (void *)NULL, cx, NULL, t41, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t44);
            goto gm_fdone;
            goto me10;
mn39:;
ma14:;
            goto me10;
mn45:;
ma15:;
            gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 515, 5);
me10:;
        }
    }
    gm_b0_1 t47;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t47);
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
inline __attribute__((aligned(4))) void gm_f_check_stack_eei454t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_check_stack_eei454t F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* stack */
    uint64_t gm_l3; /* len */
    GM_STACK_CHECK(&F, 251u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = gm_l2;
            gm_t2lqu8jsj511d t5 = gm_f_slice_parts_1se5hu4(t3, t4);
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 8));
            gm_l3 = t6;
        }
        {
            void * t8 = gm_l0;
            uint8_t t9 = gm_f_fast_23em7sv(t8);
            uint8_t t10 = (uint8_t)!t9;
            if (!t10) goto ie7;
            uint64_t t11 = gm_l3;
            uint64_t t12 = GM_LD(uint64_t, gm_c83hsvhq98b);
            uint8_t t13 = (t11 < t12);
            if (!t13) goto ie7;
            {
                void * t15 = gm_l0;
                gm_tl874t2atnlh3 t16 = gm_l1;
                uint64_t t17 = GM_LD(uint64_t, gm_c83hsvhq98b);
                uint64_t t18 = gm_l3;
                gm_t2lqu8jsj511d t19;
                GM_ST(uint64_t, ((uint8_t *)&t19 + 0), t17);
                GM_ST(uint64_t, ((uint8_t *)&t19 + 8), t18);
                gm_tabb710d9mq2e t20;
                gm_f_format_40t0ers((void *)&t20, (void *)NULL, cx, NULL, t19);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_srcp0l171g2m7ho, 396, 64);
                gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
                gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
                gm_t76123gfet73g t23;
                gm_f_trap_qh84l9d((void *)&t23, (void *)NULL, cx, NULL, t15, t16, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t23);
                goto gm_fdone;
            }
            goto ix7;
ie7:;
ix7:;
        }
    }
    gm_b0_1 t24;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t24);
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
inline __attribute__((aligned(4))) void gm_f_complete_ok_ufvdohe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tn68el8d14vku a2) {
    struct gm_fr_complete_ok_ufvdohe F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tn68el8d14vku gm_l2; /* key */
    uint64_t gm_l3; /* n */
    gm_b16_8 gm_l4; /* s */
    GM_STACK_CHECK(&F, 503u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l0;
            gm_tn68el8d14vku t4 = gm_l2;
            void * t5 = gm_f_with_rec_2eah8tp(t3, GM_CONV(gm_csff7attl2ehc5, t4));
            void * t6;
            if ((GM_LD(void *, ((uint8_t *)&t5)) != 0)) {
                void * t7 = GM_LD(void *, (((uint8_t *)&t5) + 0));
                t6 = t7;
            } else {
                gm_b0_1 t8;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t8);
                goto gm_fdone;
            }
            uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 24) + 0) + 8);
            gm_l3 = t9;
        }
        {
            uint64_t t11 = gm_l3;
            uint8_t t12 = (t11 > ((uint64_t)0ULL));
            if (!t12) goto ie10;
            {
                void * t14 = gm_l0;
                gm_tl874t2atnlh3 t15 = gm_l1;
                gm_tn68el8d14vku t16 = gm_l2;
                gm_b0_1 t17 = gm_f_abort_all_4him68f((void *)NULL, cx, NULL, t14, t15, t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t18;
                uint64_t t20 = gm_l3;
                uint8_t t21 = ((t20 == ((uint64_t)1ULL)));
                if (!t21) goto ie19;
                gm_b16_8 t23;
                GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(0));
                gm_b16_8 t24 = t23;
                t18 = t24;
                goto ix19;
ie19:;
                gm_b16_8 t26;
                GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s03pmf867dcsr));
                GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(1));
                gm_b16_8 t27 = t26;
                t18 = t27;
ix19:;
                gm_l4 = t18;
            }
            {
                void * t28 = gm_l0;
                gm_tl874t2atnlh3 t29 = gm_l1;
                gm_t7neromnu8t62 t30;
                uint64_t t31 = gm_l3;
                GM_ST(uint64_t, (uint8_t *)&t30 + 0, t31);
                gm_b16_8 t32 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t30 + 8, t32);
                gm_b16_8 t33;
                uint64_t t35 = gm_l3;
                uint8_t t36 = ((t35 == ((uint64_t)1ULL)));
                if (!t36) goto ie34;
                gm_b16_8 t38;
                GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sgpdmvc9s0d2p));
                GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(2));
                gm_b16_8 t39 = t38;
                t33 = t39;
                goto ix34;
ie34:;
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sdko2shp3619i));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(3));
                gm_b16_8 t42 = t41;
                t33 = t42;
ix34:;
                GM_ST(gm_b16_8, (uint8_t *)&t30 + 24, t33);
                gm_tabb710d9mq2e t43;
                gm_f_format_ialr6ut((void *)&t43, (void *)NULL, cx, NULL, &t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcp0l171g2m7ho, 466, 30);
                gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
                gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                gm_t76123gfet73g t46;
                gm_f_trap_qh84l9d((void *)&t46, (void *)NULL, cx, NULL, t28, t29, t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t46);
                goto gm_fdone;
            }
            goto ix10;
ie10:;
ix10:;
        }
    }
    gm_b0_1 t47;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t47);
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
inline __attribute__((aligned(4))) void gm_f_complete_u18qm88(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tn68el8d14vku a2, const void * a3, uint8_t a4) {
    struct gm_fr_complete_u18qm88 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tn68el8d14vku gm_l2; /* key */
    gm_t76123gfet73g gm_l3; /* f */
    uint8_t gm_l4; /* crossing */
    void * gm_l5; /* w */
    uint32_t gm_l6; /* id */
    gm_t76123gfet73g gm_l7; /* g */
    GM_STACK_CHECK(&F, 458u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    {
        uint8_t t3;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; default: goto ma7; }
ma5:;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        if (t11 != ((uint8_t)4ULL)) goto mn10;
        t3 = ((uint8_t)1);
        goto me4;
mn10:;
ma6:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        if (t13 != ((uint8_t)3ULL)) goto mn12;
        t3 = ((uint8_t)1);
        goto me4;
mn12:;
ma7:;
        t3 = ((uint8_t)0);
        goto me4;
mn14:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 473, 8);
me4:;
        if (!t3) goto ie2;
        {
            void * t16 = gm_l0;
            gm_tl874t2atnlh3 t17 = gm_l1;
            gm_tn68el8d14vku t18 = gm_l2;
            gm_b0_1 t19 = gm_f_abort_all_4him68f((void *)NULL, cx, NULL, t16, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            const uint8_t *t20 = ((uint8_t *)a3);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t20, 24);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t22 = gm_l4;
        if (!t22) goto ie21;
        {
            for (;;) {
                if (!((uint8_t)1)) goto lb24;
                {
                    void * t27 = gm_l0;
                    gm_tn68el8d14vku t28 = gm_l2;
                    void * t29 = gm_f_with_rec_2eah8tp(t27, GM_CONV(gm_csff7attl2ehc5, t28));
                    void * t30;
                    if ((GM_LD(void *, ((uint8_t *)&t29)) != 0)) {
                        void * t31 = GM_LD(void *, (((uint8_t *)&t29) + 0));
                        t30 = t31;
                    } else {
                        goto lb24;
                    }
                    gm_l5 = t30;
                }
                {
                    void * t32 = gm_l5;
                    void * t33 = (void *)(((uint8_t *)t32) + 24);
                    gm_t9805qtonbsag t34 = gm_f_pop_570g79n(t33);
                    uint32_t t35;
                    if (((((uint8_t *)&t34))[4] != 0)) {
                        uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&t34) + 0));
                        t35 = t36;
                    } else {
                        goto lb24;
                    }
                    gm_l6 = t35;
                }
                {
                    void * t37 = gm_l0;
                    gm_tl874t2atnlh3 t38 = gm_l1;
                    gm_tn68el8d14vku t39 = gm_l2;
                    uint32_t t40 = gm_l6;
                    gm_tcfa6l7reoosn t41;
                    gm_f_cancel_fiber_n4k6buo((void *)&t41, (void *)NULL, cx, NULL, t37, t38, t39, t40);
                    if (gm_unw.kind) {
                        goto gm_ll25;
                    }
                    gm_b0_1 t42;
                    if (!((((uint8_t *)&t41))[0] != 0)) {
                        gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 8));
                        t42 = t43;
                    } else {
                        gm_l7 = (*(gm_t76123gfet73g *)(((uint8_t *)&t41) + 8));
                        {
                            void * t45 = gm_l0;
                            gm_tl874t2atnlh3 t46 = gm_l1;
                            gm_tn68el8d14vku t47 = gm_l2;
                            gm_b0_1 t48 = gm_f_abort_all_4him68f((void *)NULL, cx, NULL, t45, t46, t47);
                            if (gm_unw.kind) {
                                goto gm_ll25;
                            }
                        }
                        const uint8_t *t49 = ((uint8_t *)&gm_l7);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t49, 24);
                        goto gm_fdone;
                    }
                }
lc24:;
            }
gm_ll25:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb24; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc24; }
            }
            goto gm_lad;
lb24:;
        }
        {
            const uint8_t *t50 = ((uint8_t *)a3);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t50, 24);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t51 = gm_l0;
        gm_tl874t2atnlh3 t52 = gm_l1;
        gm_tn68el8d14vku t53 = gm_l2;
        gm_tcfa6l7reoosn t54;
        gm_f_complete_ok_ufvdohe((void *)&t54, (void *)NULL, cx, NULL, t51, t52, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t54))[0] != 0)) {
            gm_tcfa6l7reoosn t55;
            GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
            ((uint8_t *)&t55)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t55 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t54) + 8)));
            F.ret = t55;
            goto gm_fret;
        }
        gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 8));
    }
    {
        const uint8_t *t57 = ((uint8_t *)a3);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t57, 24);
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
__attribute__((aligned(4))) void gm_f_consume_u5eqint(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_consume_u5eqint F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* fr */
    gm_b16_8 gm_l3; /* k */
    uint64_t gm_l4; /* id */
    uint64_t gm_l5; /* epoch */
    gm_tbl8il92l97ut gm_l6; /* inst */
    gm_tn68el8d14vku gm_l7; /* clause_with */
    void * gm_l8; /* rec */
    gm_tn68el8d14vku gm_l9; /* w */
    void * gm_l10; /* wr */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    GM_STACK_CHECK(&F, 1123u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l3;
        uint64_t t4 = gm_f_word_o2rcrhs(t2, t3, ((uint64_t)0ULL));
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l0;
        gm_b16_8 t6 = gm_l3;
        uint64_t t7 = gm_f_word_o2rcrhs(t5, t6, ((uint64_t)1ULL));
        gm_l5 = t7;
    }
    {
        void * t8 = gm_l2;
        gm_t5lf9i3n1r3co t9 = GM_LD(gm_t5lf9i3n1r3co, (((uint8_t *)t8) + 216));
        gm_tbl8il92l97ut t10;
        if (((((uint8_t *)&t9))[16] != 0)) {
            gm_tbl8il92l97ut t11 = GM_LD(gm_tbl8il92l97ut, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            void * t12 = gm_l0;
            gm_tl874t2atnlh3 t13 = gm_l1;
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sp7r202318t40));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(56));
            gm_t76123gfet73g t15;
            gm_f_trap_qh84l9d((void *)&t15, (void *)NULL, cx, NULL, t12, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 8, t15);
            goto gm_fdone;
        }
        gm_l6 = t10;
    }
    {
        void * t16 = GM_LD(void *, (((uint8_t *)&gm_l6) + 0));
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 0));
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
        gm_tn68el8d14vku t19;
        GM_ST(uint32_t, (uint8_t *)&t19 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t19 + 0), t17);
        GM_ST(uint32_t, ((uint8_t *)&t19 + 8), t18);
        gm_l7 = t19;
    }
    {
        uint8_t t21 = 0;
        uint64_t t22 = gm_l4;
        uint8_t t23 = ((t22 == ((uint64_t)0ULL)));
        if (!t23) {
            uint64_t t24 = gm_l4;
            void * t25 = gm_l0;
            uint64_t t26 = GM_LD(uintptr_t, (((((uint8_t *)t25) + 1088) + 8) + 0) + 8);
            uint64_t t27 = (uint64_t)t26;
            uint8_t t28 = (t24 >= t27);
            t21 = t28;
        } else t21 = 1;
        if (!t21) goto ie20;
        {
            void * t30 = gm_l0;
            gm_tl874t2atnlh3 t31 = gm_l1;
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_srmistf0k19ab));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(71));
            gm_t76123gfet73g t33;
            gm_f_trap_qh84l9d((void *)&t33, (void *)NULL, cx, NULL, t30, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 8, t33);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t34 = gm_l0;
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((((uint8_t *)t34) + 1088) + 8) + 0));
        gm_b16_8 t36 = t35;
        uint64_t t37 = gm_l4;
        uint64_t t38 = gm_f_us_cuv01fp(t37);
        uint64_t t39 = (uintptr_t)t38;
        uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&t36) + 8);
        if (t39 >= (uintptr_t)(t40)) gm_trap_index(t39, t40, gm_srcp0l171g2m7ho, 573, 13);
        void * t41 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t36)) + t39 * 360);
        gm_l8 = t41;
    }
    {
        void * t43 = gm_l8;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)t43) + 25));
        if (!t44) goto ie42;
        void * t45 = gm_l8;
        uint8_t t46 = ((GM_LD(uint8_t, (((uint8_t *)t45) + 248) + 0) == ((uint8_t)2ULL)));
        if (!t46) goto ie42;
        {
            void * t48 = gm_l0;
            gm_tl874t2atnlh3 t49 = gm_l1;
            gm_b16_8 t50;
            GM_ST(void *, (uint8_t *)&t50, (void *)(gm_srmistf0k19ab));
            GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(71));
            gm_t76123gfet73g t51;
            gm_f_trap_qh84l9d((void *)&t51, (void *)NULL, cx, NULL, t48, t49, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 8, t51);
            goto gm_fdone;
        }
        goto ix42;
ie42:;
ix42:;
    }
    {
        void * t53 = gm_l8;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t53) + 248) + 0));
        if (t54 != ((uint8_t)1ULL)) goto ie52;
        gm_tn68el8d14vku t55 = GM_LD(gm_tn68el8d14vku, ((((uint8_t *)t53) + 248) + 8));
        gm_l9 = t55;
        {
            void * t58 = gm_l8;
            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)t58) + 240));
            uint64_t t60 = gm_l5;
            uint8_t t61 = (!(t59 == t60));
            if (!t61) goto ie57;
            {
                void * t63 = gm_l0;
                gm_tl874t2atnlh3 t64 = gm_l1;
                gm_b16_8 t65;
                GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s6t0sieqs3tpi));
                GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(47));
                gm_t76123gfet73g t66;
                gm_f_trap_qh84l9d((void *)&t66, (void *)NULL, cx, NULL, t63, t64, t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 8, t66);
                goto gm_fdone;
            }
            goto ix57;
ie57:;
ix57:;
        }
        {
            gm_tn68el8d14vku t68 = gm_l9;
            gm_tn68el8d14vku t69 = gm_l7;
            uint8_t t70 = gm_f_same_key_a5oh9pl(GM_CONV(gm_csff7attl2ehc5, t68), GM_CONV(gm_csff7attl2ehc5, t69));
            uint8_t t71 = (uint8_t)!t70;
            if (!t71) goto ie67;
            {
                void * t73 = gm_l0;
                gm_tl874t2atnlh3 t74 = gm_l1;
                gm_b16_8 t75;
                GM_ST(void *, (uint8_t *)&t75, (void *)(gm_smovtms5ssgir));
                GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(87));
                gm_t76123gfet73g t76;
                gm_f_trap_qh84l9d((void *)&t76, (void *)NULL, cx, NULL, t73, t74, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 8, t76);
                goto gm_fdone;
            }
            goto ix67;
ie67:;
ix67:;
        }
        {
            void * t78 = gm_l0;
            gm_tn68el8d14vku t79 = gm_l9;
            void * t80 = gm_f_with_rec_2eah8tp(t78, GM_CONV(gm_csff7attl2ehc5, t79));
            if (!(GM_LD(void *, ((uint8_t *)&t80)) != 0)) goto ie77;
            void * t81 = GM_LD(void *, ((uint8_t *)&t80));
            gm_l10 = t81;
            {
                {
                    gm_l12 = ((uint64_t)0ULL);
                }
                {
                    void * t84 = gm_l10;
                    uint64_t t85 = GM_LD(uintptr_t, ((((uint8_t *)t84) + 24) + 0) + 8);
                    gm_l11 = t85;
                }
                {
                    for (;;) {
                        uint64_t t87 = gm_l12;
                        uint64_t t88 = gm_l11;
                        uint8_t t89 = (t87 < t88);
                        if (!t89) goto lb86;
                        {
                            uint64_t t92 = gm_l12;
                            uint64_t t95 = GM_LD(uint64_t, ((uint8_t *)&t92));
                            gm_l13 = t95;
                            {
                                uint64_t t98 = gm_l13;
                                uint64_t t99 = (uintptr_t)t98;
                                void * t100 = gm_l10;
                                uint64_t t101 = GM_LD(uintptr_t, ((((uint8_t *)t100) + 24) + 0) + 8);
                                if (t99 >= (uintptr_t)(t101)) gm_trap_index(t99, t101, gm_srcp0l171g2m7ho, 580, 16);
                                uint32_t t102 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t100) + 24) + 0)) + t99 * 4));
                                uint64_t t103 = gm_l4;
                                uint32_t t104 = (uint32_t)t103;
                                uint8_t t105 = ((t102 == t104));
                                if (!t105) goto ie97;
                                {
                                    {
                                        void * t108 = gm_l10;
                                        void * t109 = (void *)(((uint8_t *)t108) + 24);
                                        uint64_t t110 = gm_l13;
                                        uint32_t t111 = gm_f_remove_10muhi8(t109, t110);
                                    }
                                }
                                {
                                    goto lb86;
                                }
                                goto ix97;
ie97:;
ix97:;
                            }
                            goto me93;
mn94:;
                            gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 579, 9);
me93:;
                        }
lc86:;
                        {
                            uint64_t t113 = gm_l12;
                            uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                            uint64_t t116 = r114;
                            gm_l12 = t116;
                        }
                    }
lb86:;
                }
            }
            goto ix77;
ie77:;
ix77:;
        }
        {
            void * t117 = gm_l8;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)t117) + 248)) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)t117) + 248)) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)t117) + 248)) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)t117) + 248)) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)t117) + 248)) + 16, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)t117) + 248)) + 0, ((uint8_t)0ULL));
        }
        {
            gm_t4qfihdig0mro t118;
            memset((uint8_t *)&t118 + 4, 0, 4);
            uint64_t t119 = gm_l4;
            uint32_t t120 = (uint32_t)t119;
            GM_ST(uint32_t, (uint8_t *)&t118 + 0, t120);
            gm_tn68el8d14vku t121 = gm_l9;
            GM_ST(gm_tn68el8d14vku, (uint8_t *)&t118 + 8, t121);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out))[0] = 0;
            GM_ST(gm_t4qfihdig0mro, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 8, t118);
            goto gm_fdone;
        }
        goto ix52;
ie52:;
        {
            void * t123 = gm_l0;
            gm_tl874t2atnlh3 t124 = gm_l1;
            gm_b16_8 t125;
            GM_ST(void *, (uint8_t *)&t125, (void *)(gm_s6t0sieqs3tpi));
            GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(47));
            gm_t76123gfet73g t126;
            gm_f_trap_qh84l9d((void *)&t126, (void *)NULL, cx, NULL, t123, t124, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tmr8tdh5g7uj6 *)gm_out) + 8, t126);
            goto gm_fdone;
        }
ix52:;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_coro_entry_62llbbf(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_coro_main_hqntcv4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_coro_main_hqntcv4 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* id */
    void * gm_l2; /* rec */
    gm_t07lf02ct4q61 gm_l5; /* flow */
    gm_b16_8 gm_l6; /* v */
    void * gm_l8; /* fr */
    uint32_t gm_l9; /* cx */
    void * gm_l10; /* body */
    gm_b16_8 gm_l11; /* dst */
    gm_tpmdr2hmdhsqr gm_l13; /* args */
    gm_tcfa6l7reoosn gm_l15; /* r */
    gm_t76123gfet73g gm_l16; /* f */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_b16_8 gm_l19; /* c */
    void * gm_l20; /* driver */
    GM_STACK_CHECK(&F, 1976u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 1088) + 8) + 0));
        gm_b16_8 t4 = t3;
        uint32_t t5 = gm_l1;
        uint64_t t6 = (uint64_t)t5;
        uint64_t t7 = gm_f_us_cuv01fp(t6);
        uint64_t t8 = (uintptr_t)t7;
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srcp0l171g2m7ho, 275, 13);
        void * t10 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t4)) + t8 * 360);
        gm_l2 = t10;
    }
    {
        void * t11 = gm_l0;
        void * t12 = gm_l2;
        uint64_t t13 = GM_LD(uintptr_t, (((uint8_t *)t12) + 40) + 8);
        gm_f_set_stack_floor_f15muof(t11, t13);
    }
    {
        void * t14 = gm_l0;
        uint8_t *t15 = (((uint8_t *)t14) + 360);
        gm_trqfvmjtt4sij t16;
        gm_f_list_fufbgf7((void *)&t16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t16gj5so0pj6j *)t15) + 0, t16);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t16gj5so0pj6j *)t15) + 40, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t16gj5so0pj6j *)t15) + 48, ((uint64_t)0ULL));
    }
    {
        void * t17 = gm_l0;
        void * t18 = (void *)((((uint8_t *)t17) + 360) + 0);
        uint64_t t19 = GM_LD(uint64_t, gm_ci39cltltdi);
        gm_tabb710d9mq2e t20;
        gm_f_make_fill_jpvm0kc((void *)&t20, (void *)NULL, cx, NULL, t19, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2t1c54q9lq)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_srcp0l171g2m7ho, 278, 27);
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
        gm_tftb1odc739t5 t22 = gm_f_push_rluch9r(t18, t21);
        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srcp0l171g2m7ho, 278, 5);
        gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
    }
    {
        void * t24 = gm_l0;
        uint8_t *t25 = (((uint8_t *)t24) + 416);
        gm_f_list_kl0t9m0((void *)&(*(gm_trqfvmjtt4sij *)t25), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t26 = gm_l0;
        uint8_t *t27 = (((uint8_t *)t26) + 552);
        gm_f_list_fufbgf7((void *)&(*(gm_trqfvmjtt4sij *)t27), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t28 = gm_l0;
        GM_ST(uint32_t, (((uint8_t *)t28) + 768), ((uint32_t)0ULL));
    }
    {
        void * t29 = gm_l0;
        void * t30 = gm_l2;
        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 344));
        GM_ST(uint64_t, (((uint8_t *)t29) + 1288), t31);
    }
    {
        void * t32 = gm_l0;
        gm_t9hl37efuvhj8 t33 = GM_LD(gm_t9hl37efuvhj8, ((((uint8_t *)t32) + 1088) + 88));
        F.l3 = t33;
    }
    {
        void * t34 = gm_l0;
        memset(&(*(gm_t9hl37efuvhj8 *)((((uint8_t *)t34) + 1088) + 88)), 0, sizeof (*(gm_t9hl37efuvhj8 *)((((uint8_t *)t34) + 1088) + 88)));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t9hl37efuvhj8 *)((((uint8_t *)t34) + 1088) + 88)) + 0, ((uint8_t)0ULL));
    }
    {
        void * t36 = gm_l2;
        if (!(((((uint8_t *)t36) + 272))[64] != 0)) goto ie35;
        gm_th5tat709fgha t37 = GM_LD(gm_th5tat709fgha, (((uint8_t *)t36) + 272));
        F.l4 = t37;
        uint8_t t38;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t41 != ((uint8_t)2ULL)) goto mn40;
        t38 = ((uint8_t)1);
        goto me39;
mn40:;
        t38 = ((uint8_t)0);
        goto me39;
mn42:;
        gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 285, 31);
me39:;
        if (!t38) goto ie35;
        {
            gm_b16_8 t44 = gm_f_empty_slice_ijtp9lo();
            memset(&(*(gm_t9hl37efuvhj8 *)((uint8_t *)&F.l3)), 0, sizeof (*(gm_t9hl37efuvhj8 *)((uint8_t *)&F.l3)));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t9hl37efuvhj8 *)((uint8_t *)&F.l3)) + 0, ((uint8_t)1ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t9hl37efuvhj8 *)((uint8_t *)&F.l3)) + 8, t44);
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        gm_t07lf02ct4q61 t45;
        memset(&t45, 0, sizeof t45);
        gm_l5 = t45;
    }
    {
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        switch (t51) { case ((uint8_t)0ULL): goto ma49; case ((uint8_t)1ULL): goto ma47; case ((uint8_t)2ULL): goto ma48; case ((uint8_t)3ULL): goto ma49; case ((uint8_t)4ULL): goto ma49; case ((uint8_t)5ULL): goto ma49; default: goto ma49; }
ma47:;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t53 != ((uint8_t)1ULL)) goto mn52;
        gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
        gm_l6 = t54;
        {
            uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            uint8_t t58 = (t57 > ((uint64_t)0ULL));
            if (!t58) goto ie56;
            {
                gm_b16_8 t60 = gm_l6;
                gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                gm_b0_1 t62 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t61, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kq0m5gkhfc4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix56;
ie56:;
ix56:;
        }
        {
            void * t63 = gm_l2;
            gm_t68f8jeqq3b3v t64 = GM_LD(gm_t68f8jeqq3b3v, (((uint8_t *)t63) + 272));
            gm_th5tat709fgha t65;
            if (((((uint8_t *)&t64))[64] != 0)) {
                t65 = (*(gm_th5tat709fgha *)(((uint8_t *)&t64) + 0));
            } else {
                gm_b0_1 t66;
                uint8_t t67 = 0;
                F.ret = t66;
                goto gm_fret;
            }
            F.l7 = t65;
        }
        {
            void * t68 = gm_l2;
            gm_t68f8jeqq3b3v t69;
            memset(&t69, 0, sizeof t69);
            GM_ST(gm_t68f8jeqq3b3v, (((uint8_t *)t68) + 272), t69);
        }
        {
            gm_tcfa6l7reoosn t70;
            uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
            switch (t76) { case ((uint8_t)0ULL): goto ma72; case ((uint8_t)1ULL): goto ma73; case ((uint8_t)2ULL): goto ma74; default: goto ma75; }
ma72:;
            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
            if (t78 != ((uint8_t)0ULL)) goto mn77;
            void * t79 = GM_LD(void *, ((((uint8_t *)&F.l7) + 8) + 0));
            gm_l8 = t79;
            uint32_t t80 = GM_LD(uint32_t, ((((uint8_t *)&F.l7) + 8) + 8));
            gm_l9 = t80;
            void * t81 = GM_LD(void *, ((((uint8_t *)&F.l7) + 8) + 16));
            gm_l10 = t81;
            gm_b16_8 t82 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l7) + 8) + 24));
            gm_l11 = t82;
            void * t83 = gm_l0;
            void * t84 = gm_l8;
            uint32_t t85 = gm_l9;
            gm_tbl8il92l97ut t86;
            GM_ST(uint32_t, (uint8_t *)&t86 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t86 + 0), t84);
            GM_ST(uint32_t, ((uint8_t *)&t86 + 8), t85);
            void * t87 = gm_l10;
            gm_b16_8 t88 = gm_l11;
            gm_tcfa6l7reoosn t89;
            gm_f_ev_25jfdar((void *)&t89, (void *)NULL, cx, NULL, t83, t86, t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t70 = t89;
            goto me71;
mn77:;
ma73:;
            uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
            if (t91 != ((uint8_t)1ULL)) goto mn90;
            gm_tonn1ba3fjh5g t92 = GM_LD(gm_tonn1ba3fjh5g, (((uint8_t *)&F.l7) + 8));
            F.l12 = t92;
            {
                gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 24));
                gm_tpmdr2hmdhsqr t95;
                GM_ST(gm_b16_8, ((uint8_t *)&t95 + 0), t94);
                gm_l13 = t95;
            }
            void * t96 = gm_l0;
            uint32_t t97 = GM_LD(uint32_t, (((uint8_t *)&F.l12) + 40));
            gm_tl874t2atnlh3 t98 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 44));
            gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
            gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
            gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 16));
            void * t102 = ((uint8_t *)&gm_l13);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcp0l171g2m7ho, 296, 60);
            gm_b16_8 t103;
            GM_ST(void *, (uint8_t *)&t103, (void *)(((uint8_t *)t102 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
            gm_b16_8 t105 = gm_f_empty_slice_ijtp9lo();
            gm_tcfa6l7reoosn t106;
            gm_f_call_value_fm9m0cc((void *)&t106, (void *)NULL, cx, NULL, t96, t97, t98, t100, t101, t104, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t107 = t106;
            t70 = t107;
            goto me71;
mn90:;
ma74:;
            uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
            if (t109 != ((uint8_t)2ULL)) goto mn108;
            gm_t79rbdv4rafe5 t110 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&F.l7) + 8));
            F.l14 = t110;
            {
                void * t112 = gm_l0;
                uint32_t t113 = GM_LD(uint32_t, (((uint8_t *)&F.l14) + 0));
                gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 8));
                gm_tl874t2atnlh3 t115 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l14) + 24));
                gm_b0_1 t116 = gm_f_thread_main_n6r943c((void *)NULL, cx, NULL, t112, t113, t114, t115);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t117 = gm_l0;
                uint32_t t118 = gm_l1;
                gm_b0_1 t119 = gm_f_end_thread_j1ah2vh((void *)NULL, cx, NULL, t117, t118);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t120;
            uint8_t t121 = 0;
            F.ret = t120;
            goto gm_fret;
            goto me71;
mn108:;
ma75:;
            gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 292, 18);
me71:;
            gm_l15 = t70;
        }
        {
            gm_tcfa6l7reoosn t122 = gm_l15;
            gm_b0_1 t123;
            if (!((((uint8_t *)&t122))[0] != 0)) {
                gm_b0_1 t124 = GM_LD(gm_b0_1, (((uint8_t *)&t122) + 8));
                t123 = t124;
            } else {
                gm_l16 = (*(gm_t76123gfet73g *)(((uint8_t *)&t122) + 8));
                {
                    const uint8_t *t126 = ((uint8_t *)&gm_l16);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 25, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 26, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 28, 0);
                    ((uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)))[24] = 1;
                    memmove((uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 0, t126, 24);
                }
                t123 = GM_ZERO(gm_b0_1);
            }
        }
        goto me46;
mn52:;
ma48:;
        uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t128 != ((uint8_t)2ULL)) goto mn127;
        {
            gm_t76123gfet73g t130;
            GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t130 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t130 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)6ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 28, 0);
            ((uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)))[24] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 0, t130);
        }
        goto me46;
mn127:;
ma49:;
        {
            gm_t76123gfet73g t133;
            gm_f_aborted_ua52qs6((void *)&t133, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 28, 0);
            ((uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)))[24] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 0, t133);
        }
        goto me46;
mn131:;
ma50:;
        gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 287, 5);
me46:;
    }
    {
        void * t134 = gm_l0;
        gm_b0_1 t135 = gm_f_kill_temps_8sgbc55((void *)NULL, cx, NULL, t134, ((uint64_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t137 = gm_l0;
            gm_b16_8 t138 = GM_LD(gm_b16_8, (((((uint8_t *)t137) + 360) + 0) + 0));
            gm_l17 = t138;
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t140 = gm_l18;
                uint64_t t141 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                uint8_t t142 = (t140 < t141);
                if (!t142) goto lb139;
                {
                    uint64_t t145 = gm_l18;
                    uint64_t t146 = (uintptr_t)t145;
                    uint64_t t147 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t146 * 16));
                    gm_l19 = t150;
                    {
                        gm_b16_8 t152 = gm_l19;
                        gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
                        gm_b0_1 t154 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t153, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kd9f1ktn0ro)));
                        if (gm_unw.kind) {
                            goto gm_ll143;
                        }
                    }
                    goto me148;
mn149:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 310, 5);
me148:;
                }
lc139:;
                {
                    uint64_t t156 = gm_l18;
                    uint64_t r157; bool o158 = __builtin_add_overflow(t156, ((uint64_t)1ULL), &r157);
                    uint64_t t159 = r157;
                    gm_l18 = t159;
                }
            }
gm_ll143:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb139; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc139; }
            }
            goto gm_lad;
lb139:;
        }
    }
    {
        void * t160 = gm_l0;
        void * t161 = (void *)((((uint8_t *)t160) + 360) + 0);
        gm_f_deinit_tq06jtm(t161);
    }
    {
        void * t162 = gm_l0;
        void * t163 = (void *)(((uint8_t *)t162) + 416);
        gm_f_deinit_v2ik7pl(t163);
    }
    {
        void * t164 = gm_l0;
        void * t165 = (void *)(((uint8_t *)t164) + 552);
        gm_f_deinit_tq06jtm(t165);
    }
    {
        void * t166 = gm_l0;
        gm_b16_8 t167 = GM_LD(gm_b16_8, (((((uint8_t *)t166) + 1088) + 8) + 0));
        gm_b16_8 t168 = t167;
        uint32_t t169 = gm_l1;
        uint64_t t170 = (uint64_t)t169;
        uint64_t t171 = gm_f_us_cuv01fp(t170);
        uint64_t t172 = (uintptr_t)t171;
        uint64_t t173 = GM_LD(uintptr_t, ((uint8_t *)&t168) + 8);
        if (t172 >= (uintptr_t)(t173)) gm_trap_index(t172, t173, gm_srcp0l171g2m7ho, 314, 12);
        void * t174 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t168)) + t172 * 360);
        gm_l2 = t174;
    }
    {
        void * t175 = gm_l2;
        GM_ST(uint8_t, (((uint8_t *)t175) + 25), ((uint8_t)1));
    }
    {
        void * t176 = gm_l0;
        void * t177 = gm_l2;
        gm_tn68el8d14vku t178 = GM_LD(gm_tn68el8d14vku, (((uint8_t *)t177) + 224));
        void * t179 = gm_f_with_rec_2eah8tp(t176, GM_CONV(gm_csff7attl2ehc5, t178));
        void * t180;
        if ((GM_LD(void *, ((uint8_t *)&t179)) != 0)) {
            void * t181 = GM_LD(void *, (((uint8_t *)&t179) + 0));
            t180 = t181;
        } else {
            gm_b0_1 t182;
            uint8_t t183 = 0;
            F.ret = t182;
            goto gm_fret;
        }
        gm_l20 = t180;
    }
    {
        void * t184 = gm_l0;
        gm_tslj4ghflekdk t185;
        memset((uint8_t *)&t185 + 4, 0, 4);
        uint32_t t186 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t185 + 0, t186);
        memcpy((uint8_t *)&t185 + 8, ((uint8_t *)&gm_l5), 32);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t9hl37efuvhj8 *)((((uint8_t *)t184) + 1088) + 88)) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t9hl37efuvhj8 *)((((uint8_t *)t184) + 1088) + 88)) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t9hl37efuvhj8 *)((((uint8_t *)t184) + 1088) + 88)) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t9hl37efuvhj8 *)((((uint8_t *)t184) + 1088) + 88)) + 48, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t9hl37efuvhj8 *)((((uint8_t *)t184) + 1088) + 88)) + 0, ((uint8_t)5ULL));
        GM_ST(gm_tslj4ghflekdk, (uint8_t *)&(*(gm_t9hl37efuvhj8 *)((((uint8_t *)t184) + 1088) + 88)) + 8, t185);
    }
    {
        void * t187 = gm_l20;
        uint32_t t188 = GM_LD(uint32_t, (((uint8_t *)t187) + 16));
        gm_node *pn189 = gm_find(cx, 734392322);
        if (!pn189) gm_trap("no handler installed for effect `Coro`", gm_srcp0l171g2m7ho, 318, 5);
        struct gm_pr_coro_main_hqntcv4_190 pr191 = {pn189, t188};
        gm_ctl_perform(pn189->fw, gm_pt_coro_main_hqntcv4_190, &pr191, NULL);
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
__attribute__((aligned(4))) void gm_f_ctl_perform_kobk825(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_ctl_perform_kobk825 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint32_t gm_l2; /* n */
    uint32_t gm_l3; /* op */
    gm_b16_8 gm_l4; /* args */
    gm_b16_8 gm_l5; /* dst */
    gm_tn68el8d14vku gm_l7; /* key */
    void * gm_l8; /* w */
    uint32_t gm_l9; /* id */
    void * gm_l10; /* rec */
    gm_b16_8 gm_l11; /* v */
    GM_STACK_CHECK(&F, 1035u);
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
        {
            uint32_t t3 = gm_l2;
            uint64_t t4 = (uint64_t)t3;
            uint64_t t5 = (uintptr_t)t4;
            void * t6 = gm_l0;
            uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 632) + 0) + 8);
            if (t5 >= (uintptr_t)(t7)) gm_trap_index(t5, t7, gm_srcp0l171g2m7ho, 546, 13);
            gm_tjsm4kosd5pev t8 = GM_LD(gm_tjsm4kosd5pev, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 632) + 0)) + t5 * 112));
            F.l6 = t8;
        }
        {
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 80));
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 88));
            gm_tn68el8d14vku t11;
            GM_ST(uint32_t, (uint8_t *)&t11 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t11 + 0), t9);
            GM_ST(uint32_t, ((uint8_t *)&t11 + 8), t10);
            gm_l7 = t11;
        }
        {
            void * t12 = gm_l0;
            gm_tn68el8d14vku t13 = gm_l7;
            void * t14 = gm_f_with_rec_2eah8tp(t12, GM_CONV(gm_csff7attl2ehc5, t13));
            void * t15;
            if ((GM_LD(void *, ((uint8_t *)&t14)) != 0)) {
                void * t16 = GM_LD(void *, (((uint8_t *)&t14) + 0));
                t15 = t16;
            } else {
                void * t17 = gm_l0;
                gm_tl874t2atnlh3 t18 = gm_l1;
                gm_b16_8 t19;
                GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sij6bc8bh95o1));
                GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(58));
                gm_t76123gfet73g t20;
                gm_f_trap_qh84l9d((void *)&t20, (void *)NULL, cx, NULL, t17, t18, t19);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t20);
                goto gm_fdone;
            }
            gm_l8 = t15;
        }
        {
            void * t21 = gm_l0;
            uint32_t t22 = GM_LD(uint32_t, ((((uint8_t *)t21) + 1088) + 0));
            gm_l9 = t22;
        }
        {
            uint32_t t24 = gm_l9;
            uint8_t t25 = ((t24 == ((uint32_t)0ULL)));
            if (!t25) goto ie23;
            {
                void * t27 = gm_l0;
                gm_tl874t2atnlh3 t28 = gm_l1;
                gm_b16_8 t29;
                GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s7doqph2gqafo));
                GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(59));
                gm_t76123gfet73g t30;
                gm_f_trap_qh84l9d((void *)&t30, (void *)NULL, cx, NULL, t27, t28, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t30);
                goto gm_fdone;
            }
            goto ix23;
ie23:;
ix23:;
        }
        {
            void * t31 = gm_l0;
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((((uint8_t *)t31) + 1088) + 8) + 0));
            gm_b16_8 t33 = t32;
            uint32_t t34 = gm_l9;
            uint64_t t35 = (uint64_t)t34;
            uint64_t t36 = gm_f_us_cuv01fp(t35);
            uint64_t t37 = (uintptr_t)t36;
            uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&t33) + 8);
            if (t37 >= (uintptr_t)(t38)) gm_trap_index(t37, t38, gm_srcp0l171g2m7ho, 551, 13);
            void * t39 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t33)) + t37 * 360);
            gm_l10 = t39;
        }
        {
            void * t40 = gm_l10;
            void * t41 = gm_l10;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 240));
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcp0l171g2m7ho, 552, 5);
            uint64_t t45 = r43;
            GM_ST(uint64_t, (((uint8_t *)t40) + 240), t45);
        }
        {
            void * t46 = gm_l10;
            gm_tn68el8d14vku t47 = gm_l7;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)t46) + 248)) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)t46) + 248)) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)t46) + 248)) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)t46) + 248)) + 0, ((uint8_t)1ULL));
            GM_ST(gm_tn68el8d14vku, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)t46) + 248)) + 8, t47);
        }
        {
            void * t48 = gm_l8;
            void * t49 = (void *)(((uint8_t *)t48) + 24);
            uint32_t t50 = gm_l9;
            gm_tftb1odc739t5 t51 = gm_f_push_im9d4ro(t49, t50);
            if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srcp0l171g2m7ho, 554, 5);
            gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
        }
        {
            void * t53 = gm_l0;
            void * t54 = gm_l8;
            uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)t54) + 16));
            gm_tnsjse4su5jue t56;
            memset((uint8_t *)&t56 + 12, 0, 4);
            memset((uint8_t *)&t56 + 44, 0, 4);
            uint32_t t57 = gm_l9;
            GM_ST(uint32_t, (uint8_t *)&t56 + 0, t57);
            uint32_t t58 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t56 + 4, t58);
            uint32_t t59 = gm_l3;
            GM_ST(uint32_t, (uint8_t *)&t56 + 8, t59);
            gm_b16_8 t60 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t56 + 16, t60);
            gm_tl874t2atnlh3 t61 = gm_l1;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t56 + 32, t61);
            gm_t9hl37efuvhj8 t62;
            GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)4ULL));
            GM_ST(gm_tnsjse4su5jue, ((uint8_t *)&t62 + 8), t56);
            gm_t9hl37efuvhj8 t63;
            gm_f_handoff_mtsh8r2((void *)&t63, (void *)NULL, cx, NULL, t53, t55, &t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
            switch (t69) { case ((uint8_t)0ULL): goto ma67; case ((uint8_t)1ULL): goto ma65; case ((uint8_t)2ULL): goto ma66; case ((uint8_t)3ULL): goto ma67; case ((uint8_t)4ULL): goto ma67; case ((uint8_t)5ULL): goto ma67; default: goto ma67; }
ma65:;
            uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
            if (t71 != ((uint8_t)1ULL)) goto mn70;
            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
            gm_l11 = t72;
            {
                gm_b16_8 t74 = gm_l5;
                uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                void * t76 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)));
                uint64_t t77 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint64_t t79 = (uintptr_t)t78;
                if (t77 > t79 || t79 > (uintptr_t)t75) gm_trap_slice(t77, t79, t75, gm_srcp0l171g2m7ho, 557, 27);
                gm_b16_8 t80;
                GM_ST(void *, (uint8_t *)&t80, (void *)(((uint8_t *)t76 + t77 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(t79 - t77));
                gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
                gm_f_copy_from_f7j6p05(t74, t81);
            }
            {
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t84 = (t83 > ((uint64_t)0ULL));
                if (!t84) goto ie82;
                {
                    gm_b16_8 t86 = gm_l11;
                    gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                    gm_b0_1 t88 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t87, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k4ha9p0b2mp)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix82;
ie82:;
ix82:;
            }
            goto me64;
mn70:;
ma66:;
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
            if (t90 != ((uint8_t)2ULL)) goto mn89;
            gm_t76123gfet73g t91;
            GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t91 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t91 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)6ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t91);
            goto gm_fdone;
            goto me64;
mn89:;
ma67:;
            gm_t76123gfet73g t93;
            gm_f_aborted_ua52qs6((void *)&t93, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t93);
            goto gm_fdone;
            goto me64;
mn92:;
ma68:;
            gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 555, 5);
me64:;
        }
    }
    gm_b0_1 t94;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t94);
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
__attribute__((aligned(4))) void gm_f_drive_i7nr5mu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tn68el8d14vku a2, uint32_t a3) {
    struct gm_fr_drive_i7nr5mu F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tn68el8d14vku gm_l2; /* key */
    uint32_t gm_l3; /* first */
    uint64_t gm_l6; /* epoch */
    gm_t2lqu8jsj511d gm_l7; /* mk */
    gm_b16_8 gm_l8; /* k */
    gm_tcfa6l7reoosn gm_l10; /* r */
    gm_t76123gfet73g gm_l11; /* f */
    uint32_t gm_l12; /* fiber */
    gm_b16_8 gm_l13; /* v */
    gm_t76123gfet73g gm_l15; /* f */
    GM_STACK_CHECK(&F, 1562u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_tn68el8d14vku t4 = gm_l2;
        uint32_t t5 = gm_l3;
        gm_b16_8 t6 = gm_f_empty_slice_ijtp9lo();
        gm_t9hl37efuvhj8 t7;
        memset(&t7, 0, sizeof t7);
        GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)1ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 8), t6);
        gm_tv90d8meul1fb t8;
        gm_f_switch_to_gq9vlt3((void *)&t8, (void *)NULL, cx, NULL, t2, t3, t4, t5, &t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t8))[0] != 0)) {
            gm_tcfa6l7reoosn t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t9 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t8) + 8)));
            F.ret = t9;
            goto gm_fret;
        }
        F.l4 = (*(gm_t9hl37efuvhj8 *)(((uint8_t *)&t8) + 8));
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb10;
            {
                uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                switch (t18) { case ((uint8_t)0ULL): goto ma16; case ((uint8_t)1ULL): goto ma16; case ((uint8_t)2ULL): goto ma16; case ((uint8_t)3ULL): goto ma16; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma15; default: goto ma16; }
ma14:;
                uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                if (t20 != ((uint8_t)4ULL)) goto mn19;
                gm_tnsjse4su5jue t21 = GM_LD(gm_tnsjse4su5jue, (((uint8_t *)&F.l4) + 8));
                F.l5 = t21;
                {
                    uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 0));
                    uint64_t t24 = (uint64_t)t23;
                    uint64_t t25 = (uintptr_t)t24;
                    void * t26 = gm_l0;
                    uint64_t t27 = GM_LD(uintptr_t, (((((uint8_t *)t26) + 1088) + 8) + 0) + 8);
                    if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srcp0l171g2m7ho, 427, 26);
                    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, (((((uint8_t *)t26) + 1088) + 8) + 0)) + t25 * 360) + 240));
                    gm_l6 = t28;
                }
                {
                    void * t29 = gm_l0;
                    gm_t2lqu8jsj511d t30 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t29));
                    gm_l7 = t30;
                }
                {
                    void * t31 = gm_l0;
                    void * t32 = gm_l0;
                    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 176));
                    uint64_t r34; bool o35 = __builtin_mul_overflow(((uint64_t)2ULL), t33, &r34);
                    if (o35) gm_trap("integer overflow in `*`", gm_srcp0l171g2m7ho, 429, 28);
                    uint64_t t36 = r34;
                    gm_b16_8 t37 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t31, t36);
                    if (gm_unw.kind) {
                        goto gm_ll11;
                    }
                    gm_l8 = t37;
                }
                {
                    void * t38 = gm_l0;
                    gm_b16_8 t39 = gm_l8;
                    uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 0));
                    uint64_t t41 = gm_l6;
                    gm_f_put_cont_vgnbja2(t38, t39, t40, t41);
                }
                {
                    gm_f_list_fufbgf7((void *)&F.l9, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_ll11;
                    }
                }
                {
                    void * t42 = (void *)((uint8_t *)&F.l9);
                    gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
                    gm_tftb1odc739t5 t44 = gm_f_extend_5tfshb9(t42, t43);
                    if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 1, gm_srcp0l171g2m7ho, 432, 17);
                    gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 1));
                }
                {
                    void * t46 = (void *)((uint8_t *)&F.l9);
                    gm_b16_8 t47 = gm_l8;
                    gm_tftb1odc739t5 t48 = gm_f_push_rluch9r(t46, t47);
                    if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srcp0l171g2m7ho, 433, 17);
                    gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                }
                {
                    void * t50 = gm_l0;
                    gm_tl874t2atnlh3 t51 = gm_l1;
                    uint32_t t52 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 4));
                    uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 8));
                    gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                    gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                    gm_b16_8 t56 = gm_f_empty_slice_ijtp9lo();
                    gm_f_call_clause_bjbs6l1((void *)&gm_l10, (void *)NULL, cx, NULL, t50, t51, t52, t53, t55, t56);
                    if (gm_unw.kind) {
                        goto gm_ll11;
                    }
                }
                {
                    void * t57 = (void *)((uint8_t *)&F.l9);
                    gm_f_deinit_tq06jtm(t57);
                }
                {
                    void * t58 = gm_l0;
                    gm_t2lqu8jsj511d t59 = gm_l7;
                    gm_f_release_jn5kqdi(t58, GM_CONV(gm_csc7lc6jlkqkuq, t59));
                }
                {
                    gm_tcfa6l7reoosn t60 = gm_l10;
                    gm_b0_1 t61;
                    if (!((((uint8_t *)&t60))[0] != 0)) {
                        gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 8));
                        t61 = t62;
                    } else {
                        gm_l11 = (*(gm_t76123gfet73g *)(((uint8_t *)&t60) + 8));
                        {
                            uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                            if (t65 != ((uint8_t)5ULL)) goto ie64;
                            uint32_t t66 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 8));
                            gm_l12 = t66;
                            {
                                void * t68 = gm_l0;
                                gm_b16_8 t69 = GM_LD(gm_b16_8, ((((uint8_t *)t68) + 1088) + 144));
                                gm_l13 = t69;
                            }
                            {
                                void * t70 = gm_l0;
                                uint8_t *t71 = ((((uint8_t *)t70) + 1088) + 144);
                                gm_b16_8 t72 = gm_f_empty_slice_ijtp9lo();
                                GM_ST(gm_b16_8, t71, t72);
                            }
                            {
                                void * t73 = gm_l0;
                                gm_tl874t2atnlh3 t74 = gm_l1;
                                gm_tn68el8d14vku t75 = gm_l2;
                                uint32_t t76 = gm_l12;
                                gm_b16_8 t77 = gm_l13;
                                gm_t9hl37efuvhj8 t78;
                                memset(&t78, 0, sizeof t78);
                                GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_b16_8, ((uint8_t *)&t78 + 8), t77);
                                gm_tv90d8meul1fb t79;
                                gm_f_switch_to_gq9vlt3((void *)&t79, (void *)NULL, cx, NULL, t73, t74, t75, t76, &t78);
                                if (gm_unw.kind) {
                                    goto gm_ll11;
                                }
                                if (((((uint8_t *)&t79))[0] != 0)) {
                                    gm_tcfa6l7reoosn t80;
                                    GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
                                    ((uint8_t *)&t80)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t80 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t79) + 8)));
                                    F.ret = t80;
                                    goto gm_fret;
                                }
                                F.l4 = (*(gm_t9hl37efuvhj8 *)(((uint8_t *)&t79) + 8));
                            }
                            {
                                goto lc10;
                            }
                            goto ix64;
ie64:;
ix64:;
                        }
                        void * t81 = gm_l0;
                        gm_tl874t2atnlh3 t82 = gm_l1;
                        gm_tn68el8d14vku t83 = gm_l2;
                        gm_t76123gfet73g t84 = gm_l11;
                        gm_tn68el8d14vku t85 = gm_l2;
                        uint8_t t86 = gm_f_exits_past_2b4ebbp(&(*(gm_t76123gfet73g *)((uint8_t *)&gm_l11)), GM_CONV(gm_csff7attl2ehc5, t85));
                        gm_f_complete_u18qm88((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t81, t82, t83, &t84, t86);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        goto gm_fdone;
                    }
                }
                {
                    void * t87 = gm_l0;
                    gm_tl874t2atnlh3 t88 = gm_l1;
                    gm_tn68el8d14vku t89 = gm_l2;
                    void * t90 = gm_l0;
                    gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 32));
                    gm_b16_8 t92;
                    GM_ST(void *, (uint8_t *)&t92, (void *)(gm_sjtfkltao8c2o));
                    GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(48));
                    gm_t76123gfet73g t93;
                    gm_f_trap_qh84l9d((void *)&t93, (void *)NULL, cx, NULL, t90, t91, t92);
                    if (gm_unw.kind) {
                        goto gm_ll11;
                    }
                    gm_f_complete_u18qm88((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t87, t88, t89, &t93, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_ll11;
                    }
                    goto gm_fdone;
                }
                goto me13;
mn19:;
ma15:;
                uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                if (t95 != ((uint8_t)5ULL)) goto mn94;
                gm_tslj4ghflekdk t96 = GM_LD(gm_tslj4ghflekdk, (((uint8_t *)&F.l4) + 8));
                F.l14 = t96;
                {
                    uint32_t t98 = GM_LD(uint32_t, (((uint8_t *)&F.l14) + 0));
                    uint64_t t99 = (uint64_t)t98;
                    uint64_t t100 = (uintptr_t)t99;
                    void * t101 = gm_l0;
                    uint64_t t102 = GM_LD(uintptr_t, (((((uint8_t *)t101) + 1088) + 8) + 0) + 8);
                    if (t100 >= (uintptr_t)(t102)) gm_trap_index(t100, t102, gm_srcp0l171g2m7ho, 450, 17);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t101) + 1088) + 8) + 0)) + t100 * 360) + 248)) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t101) + 1088) + 8) + 0)) + t100 * 360) + 248)) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t101) + 1088) + 8) + 0)) + t100 * 360) + 248)) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t101) + 1088) + 8) + 0)) + t100 * 360) + 248)) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t101) + 1088) + 8) + 0)) + t100 * 360) + 248)) + 16, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl2g3fsjpi9op *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t101) + 1088) + 8) + 0)) + t100 * 360) + 248)) + 0, ((uint8_t)2ULL));
                }
                {
                    if (!(((((uint8_t *)&F.l14) + 8))[24] != 0)) goto ie103;
                    gm_t76123gfet73g t104 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&F.l14) + 8));
                    gm_l15 = t104;
                    {
                        void * t106 = gm_l0;
                        gm_tl874t2atnlh3 t107 = gm_l1;
                        gm_tn68el8d14vku t108 = gm_l2;
                        gm_f_complete_u18qm88((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t106, t107, t108, &(*(gm_t76123gfet73g *)((uint8_t *)&gm_l15)), ((uint8_t)1));
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        goto gm_fdone;
                    }
                    goto ix103;
ie103:;
ix103:;
                }
                {
                    uint32_t t110 = GM_LD(uint32_t, (((uint8_t *)&F.l14) + 0));
                    uint32_t t111 = gm_l3;
                    uint8_t t112 = (!(t110 == t111));
                    if (!t112) goto ie109;
                    {
                        void * t114 = gm_l0;
                        gm_tl874t2atnlh3 t115 = gm_l1;
                        gm_tn68el8d14vku t116 = gm_l2;
                        void * t117 = gm_l0;
                        gm_tl874t2atnlh3 t118 = gm_l1;
                        gm_b16_8 t119;
                        GM_ST(void *, (uint8_t *)&t119, (void *)(gm_s3hali1afgel3));
                        GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(27));
                        gm_t76123gfet73g t120;
                        gm_f_trap_qh84l9d((void *)&t120, (void *)NULL, cx, NULL, t117, t118, t119);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        gm_f_complete_u18qm88((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t114, t115, t116, &t120, ((uint8_t)1));
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        goto gm_fdone;
                    }
                    goto ix109;
ie109:;
ix109:;
                }
                {
                    void * t121 = gm_l0;
                    gm_tl874t2atnlh3 t122 = gm_l1;
                    gm_tn68el8d14vku t123 = gm_l2;
                    gm_f_complete_ok_ufvdohe((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t121, t122, t123);
                    if (gm_unw.kind) {
                        goto gm_ll11;
                    }
                    goto gm_fdone;
                }
                goto me13;
mn94:;
ma16:;
                void * t125 = gm_l0;
                gm_tl874t2atnlh3 t126 = gm_l1;
                gm_b16_8 t127;
                GM_ST(void *, (uint8_t *)&t127, (void *)(gm_skfrgfcfg8vrf));
                GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(35));
                gm_t76123gfet73g t128;
                gm_f_trap_qh84l9d((void *)&t128, (void *)NULL, cx, NULL, t125, t126, t127);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t128);
                goto gm_fdone;
                goto me13;
mn124:;
ma17:;
                gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 425, 9);
me13:;
            }
lc10:;
        }
gm_ll11:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb10; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc10; }
        }
        goto gm_lad;
lb10:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_end_thread_j1ah2vh(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_end_thread_j1ah2vh F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* id */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* c */
    void * gm_l5; /* rec */
    uint32_t gm_l6; /* t */
    GM_STACK_CHECK(&F, 452u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b0_1 t3 = gm_f_kill_temps_8sgbc55((void *)NULL, cx, NULL, t2, ((uint64_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t5 = gm_l0;
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((((uint8_t *)t5) + 360) + 0) + 0));
            gm_l2 = t6;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l3;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint64_t t13 = gm_l3;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t14 * 16));
                    gm_l4 = t18;
                    {
                        gm_b16_8 t20 = gm_l4;
                        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
                        gm_b0_1 t22 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t21, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kpbp8co2u50)));
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 224, 5);
me16:;
                }
lc7:;
                {
                    uint64_t t24 = gm_l3;
                    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                    uint64_t t27 = r25;
                    gm_l3 = t27;
                }
            }
gm_ll11:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb7; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc7; }
            }
            goto gm_lad;
lb7:;
        }
    }
    {
        void * t28 = gm_l0;
        void * t29 = (void *)((((uint8_t *)t28) + 360) + 0);
        gm_f_deinit_tq06jtm(t29);
    }
    {
        void * t30 = gm_l0;
        void * t31 = (void *)(((uint8_t *)t30) + 416);
        gm_f_deinit_v2ik7pl(t31);
    }
    {
        void * t32 = gm_l0;
        void * t33 = (void *)(((uint8_t *)t32) + 552);
        gm_f_deinit_tq06jtm(t33);
    }
    {
        uint32_t t34 = gm_l1;
        uint64_t t35 = (uint64_t)t34;
        uint64_t t36 = (uintptr_t)t35;
        void * t37 = gm_l0;
        uint64_t t38 = GM_LD(uintptr_t, (((((uint8_t *)t37) + 1088) + 8) + 0) + 8);
        if (t36 >= (uintptr_t)(t38)) gm_trap_index(t36, t38, gm_srcp0l171g2m7ho, 228, 13);
        void * t39 = (void *)((uint8_t *)GM_LD(void *, (((((uint8_t *)t37) + 1088) + 8) + 0)) + t36 * 360);
        gm_l5 = t39;
    }
    {
        void * t40 = gm_l5;
        GM_ST(uint8_t, (((uint8_t *)t40) + 25), ((uint8_t)1));
    }
    {
        void * t41 = gm_l5;
        uint32_t t42 = GM_LD(uint32_t, (((uint8_t *)t41) + 352));
        gm_l6 = t42;
    }
    {
        uint32_t t43 = gm_l6;
        uint64_t t44 = (uint64_t)t43;
        uint64_t t45 = (uintptr_t)t44;
        void * t46 = gm_l0;
        uint64_t t47 = GM_LD(uintptr_t, (((((uint8_t *)t46) + 1656) + 0) + 0) + 8);
        if (t45 >= (uintptr_t)(t47)) gm_trap_index(t45, t47, gm_srcp0l171g2m7ho, 231, 5);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t46) + 1656) + 0) + 0)) + t45 * 48) + 8)) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t46) + 1656) + 0) + 0)) + t45 * 48) + 8)) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t46) + 1656) + 0) + 0)) + t45 * 48) + 8)) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t46) + 1656) + 0) + 0)) + t45 * 48) + 8)) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t46) + 1656) + 0) + 0)) + t45 * 48) + 8)) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t46) + 1656) + 0) + 0)) + t45 * 48) + 8)) + 24, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t46) + 1656) + 0) + 0)) + t45 * 48) + 8)) + 0, ((uint8_t)5ULL));
    }
    {
        uint8_t t49 = 0;
        void * t50 = gm_l0;
        void * t51 = GM_LD(void *, ((((uint8_t *)t50) + 1656) + 56));
        uint8_t t52;
        if ((GM_LD(void *, ((uint8_t *)&t51)) != 0)) goto mn54;
        t52 = ((uint8_t)0);
        goto me53;
mn54:;
        t52 = ((uint8_t)1);
        goto me53;
mn55:;
        gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 233, 8);
me53:;
        if (!t52) {
            void * t56 = gm_l0;
            uint8_t t57;
            if (((((((uint8_t *)t56) + 1656) + 64))[1] != 0)) goto mn59;
            t57 = ((uint8_t)0);
            goto me58;
mn59:;
            t57 = ((uint8_t)1);
            goto me58;
mn60:;
            gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 233, 39);
me58:;
            t49 = t57;
        } else t49 = 1;
        if (!t49) goto ie48;
        {
            uint64_t t62 = (uintptr_t)((uint64_t)0ULL);
            void * t63 = gm_l0;
            uint64_t t64 = GM_LD(uintptr_t, (((((uint8_t *)t63) + 1656) + 0) + 0) + 8);
            if (t62 >= (uintptr_t)(t64)) gm_trap_index(t62, t64, gm_srcp0l171g2m7ho, 233, 68);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t63) + 1656) + 0) + 0)) + t62 * 48) + 8)) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t63) + 1656) + 0) + 0)) + t62 * 48) + 8)) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t63) + 1656) + 0) + 0)) + t62 * 48) + 8)) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t63) + 1656) + 0) + 0)) + t62 * 48) + 8)) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t63) + 1656) + 0) + 0)) + t62 * 48) + 8)) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t63) + 1656) + 0) + 0)) + t62 * 48) + 8)) + 24, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t63) + 1656) + 0) + 0)) + t62 * 48) + 8)) + 0, ((uint8_t)0ULL));
        }
        goto ix48;
ie48:;
ix48:;
    }
    {
        void * t65 = gm_l0;
        gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        gm_tcfa6l7reoosn t67;
        gm_f_switch_thread_m7aoqko((void *)&t67, (void *)NULL, cx, NULL, t65, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t68;
        if (!((((uint8_t *)&t67))[0] != 0)) {
            gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t67) + 8));
            t68 = t69;
        } else {
            t68 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_exits_past_2b4ebbp(const void * a0, gm_csff7attl2ehc5 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_exits_past_2b4ebbp F;
    gm_t76123gfet73g gm_l0; /* f */
    gm_tn68el8d14vku gm_l1; /* key */
    uint64_t gm_l2; /* frame */
    uint32_t gm_l3; /* label */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = GM_CONV(gm_tn68el8d14vku, a1);
    uint8_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma5; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma5; case ((uint8_t)6ULL): goto ma4; default: goto ma5; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 0));
    gm_l2 = t10;
    uint32_t t11 = GM_LD(uint32_t, ((((uint8_t *)a0) + 8) + 8));
    gm_l3 = t11;
    uint8_t t12 = 0;
    uint64_t t13 = gm_l2;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
    uint8_t t15 = (!(t13 == t14));
    if (!t15) {
        uint32_t t16 = gm_l3;
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t18 = (!(t16 == t17));
        t12 = t18;
    } else t12 = 1;
    t1 = t12;
    goto me2;
mn8:;
ma4:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t22 != ((uint8_t)1ULL)) goto pa21;
    goto po20;
pa21:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t24 != ((uint8_t)2ULL)) goto pa23;
    goto po20;
pa23:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t26 != ((uint8_t)6ULL)) goto pa25;
    goto po20;
pa25:;
    goto mn19;
po20:;
    t1 = ((uint8_t)1);
    goto me2;
mn19:;
ma5:;
    t1 = ((uint8_t)0);
    goto me2;
mn27:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 644, 52);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_exits_past_2b4ebbp_c(gm_t76123gfet73g a0, gm_csff7attl2ehc5 a1) { return gm_f_exits_past_2b4ebbp(&a0, a1); }
__attribute__((aligned(4))) void gm_f_fiber_builtin_rpc9l9s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2, void * a3, gm_b16_8 a4) {
    struct gm_fr_fiber_builtin_rpc9l9s F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_b16_8 gm_l2; /* args */
    void * gm_l3; /* e */
    gm_b16_8 gm_l4; /* dst */
    gm_t2lqu8jsj511d gm_l5; /* mk */
    gm_b16_8 gm_l6; /* fb */
    gm_b16_8 gm_l7; /* ab */
    gm_b16_8 gm_l8; /* sb */
    gm_tbl8il92l97ut gm_l9; /* inst */
    gm_tn68el8d14vku gm_l10; /* key */
    void * gm_l11; /* w */
    gm_b16_8 gm_l12; /* f */
    gm_b16_8 gm_l13; /* arg */
    uint32_t gm_l15; /* id */
    GM_STACK_CHECK(&F, 1774u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            void * t3 = gm_l0;
            gm_t2lqu8jsj511d t4 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t3));
            gm_l5 = t4;
        }
        {
            void * t5 = gm_l0;
            uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t6 >= (uintptr_t)(t7)) gm_trap_index(t6, t7, gm_srcp0l171g2m7ho, 621, 17);
            void * t8 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t6 * 8));
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 104));
            gm_b16_8 t10 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t5, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t10;
        }
        {
            void * t11 = gm_l0;
            gm_tbl8il92l97ut t12 = gm_l1;
            uint64_t t13 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t13 >= (uintptr_t)(t14)) gm_trap_index(t13, t14, gm_srcp0l171g2m7ho, 622, 17);
            void * t15 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t13 * 8));
            gm_b16_8 t16 = gm_l6;
            gm_tcfa6l7reoosn t17;
            gm_f_ev_25jfdar((void *)&t17, (void *)NULL, cx, NULL, t11, t12, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_tcfa6l7reoosn t18;
                GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
                ((uint8_t *)&t18)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t18 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t17) + 8)));
                F.ret = t18;
                goto gm_fret;
            }
            gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 8));
        }
        {
            void * t20 = gm_l0;
            uint64_t t21 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_srcp0l171g2m7ho, 623, 17);
            void * t23 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t21 * 8));
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 104));
            gm_b16_8 t25 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t20, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t25;
        }
        {
            void * t26 = gm_l0;
            gm_tbl8il92l97ut t27 = gm_l1;
            uint64_t t28 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t28 >= (uintptr_t)(t29)) gm_trap_index(t28, t29, gm_srcp0l171g2m7ho, 624, 17);
            void * t30 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t28 * 8));
            gm_b16_8 t31 = gm_l7;
            gm_tcfa6l7reoosn t32;
            gm_f_ev_25jfdar((void *)&t32, (void *)NULL, cx, NULL, t26, t27, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t32))[0] != 0)) {
                gm_tcfa6l7reoosn t33;
                GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
                ((uint8_t *)&t33)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t33 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t32) + 8)));
                F.ret = t33;
                goto gm_fret;
            }
            gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 8));
        }
        {
            void * t35 = gm_l0;
            uint64_t t36 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t36 >= (uintptr_t)(t37)) gm_trap_index(t36, t37, gm_srcp0l171g2m7ho, 625, 17);
            void * t38 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t36 * 8));
            uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 104));
            gm_b16_8 t40 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t35, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t40;
        }
        {
            void * t41 = gm_l0;
            gm_tbl8il92l97ut t42 = gm_l1;
            uint64_t t43 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t43 >= (uintptr_t)(t44)) gm_trap_index(t43, t44, gm_srcp0l171g2m7ho, 626, 17);
            void * t45 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t43 * 8));
            gm_b16_8 t46 = gm_l8;
            gm_tcfa6l7reoosn t47;
            gm_f_ev_25jfdar((void *)&t47, (void *)NULL, cx, NULL, t41, t42, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t47))[0] != 0)) {
                gm_tcfa6l7reoosn t48;
                GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                ((uint8_t *)&t48)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t48 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t47) + 8)));
                F.ret = t48;
                goto gm_fret;
            }
            gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 8));
        }
        {
            void * t50 = gm_l0;
            void * t51 = gm_l3;
            gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t51) + 112));
            gm_b16_8 t53 = gm_l8;
            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
            gm_tcfa6l7reoosn t55;
            gm_f_check_stack_eei454t((void *)&t55, (void *)NULL, cx, NULL, t50, t52, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t55))[0] != 0)) {
                gm_tcfa6l7reoosn t56;
                GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
                ((uint8_t *)&t56)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t56 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t55) + 8)));
                F.ret = t56;
                goto gm_fret;
            }
            gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 8));
        }
        {
            void * t58 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
            gm_t5lf9i3n1r3co t59 = GM_LD(gm_t5lf9i3n1r3co, (((uint8_t *)t58) + 216));
            gm_tbl8il92l97ut t60;
            if (((((uint8_t *)&t59))[16] != 0)) {
                gm_tbl8il92l97ut t61 = GM_LD(gm_tbl8il92l97ut, (((uint8_t *)&t59) + 0));
                t60 = t61;
            } else {
                void * t62 = gm_l0;
                void * t63 = gm_l3;
                gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 112));
                gm_b16_8 t65;
                GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s2ef11ime8o01));
                GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(41));
                gm_t76123gfet73g t66;
                gm_f_trap_qh84l9d((void *)&t66, (void *)NULL, cx, NULL, t62, t64, t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t66);
                goto gm_fdone;
            }
            gm_l9 = t60;
        }
        {
            void * t67 = GM_LD(void *, (((uint8_t *)&gm_l9) + 0));
            uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)t67) + 0));
            uint32_t t69 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
            gm_tn68el8d14vku t70;
            GM_ST(uint32_t, (uint8_t *)&t70 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t70 + 0), t68);
            GM_ST(uint32_t, ((uint8_t *)&t70 + 8), t69);
            gm_l10 = t70;
        }
        {
            void * t71 = gm_l0;
            gm_tn68el8d14vku t72 = gm_l10;
            void * t73 = gm_f_with_rec_2eah8tp(t71, GM_CONV(gm_csff7attl2ehc5, t72));
            void * t74;
            if ((GM_LD(void *, ((uint8_t *)&t73)) != 0)) {
                void * t75 = GM_LD(void *, (((uint8_t *)&t73) + 0));
                t74 = t75;
            } else {
                void * t76 = gm_l0;
                void * t77 = gm_l3;
                gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t77) + 112));
                gm_b16_8 t79;
                GM_ST(void *, (uint8_t *)&t79, (void *)(gm_s64eu4ogr34e2));
                GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(61));
                gm_t76123gfet73g t80;
                gm_f_trap_qh84l9d((void *)&t80, (void *)NULL, cx, NULL, t76, t78, t79);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t80);
                goto gm_fdone;
            }
            gm_l11 = t74;
        }
        {
            uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            gm_tabb710d9mq2e t82;
            gm_f_make_2ip78bj((void *)&t82, (void *)NULL, cx, NULL, t81, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kvubgn35fiq)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 8, gm_srcp0l171g2m7ho, 631, 10);
            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 8));
            gm_l12 = t83;
        }
        {
            gm_b16_8 t84 = gm_l12;
            gm_b16_8 t85 = gm_l6;
            gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
            gm_f_copy_from_f7j6p05(t84, t86);
        }
        {
            uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            uint64_t t88 = (t87 > ((uint64_t)1ULL) ? t87 : ((uint64_t)1ULL));
            gm_tabb710d9mq2e t89;
            gm_f_make_2ip78bj((void *)&t89, (void *)NULL, cx, NULL, t88, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k9epus5iul2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t89))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t89) + 8, gm_srcp0l171g2m7ho, 633, 12);
            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&t89) + 8));
            gm_l13 = t90;
        }
        {
            uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
            void * t92 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)));
            uint64_t t93 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            uint64_t t95 = (uintptr_t)t94;
            if (t93 > t95 || t95 > (uintptr_t)t91) gm_trap_slice(t93, t95, t91, gm_srcp0l171g2m7ho, 634, 5);
            gm_b16_8 t96;
            GM_ST(void *, (uint8_t *)&t96, (void *)(((uint8_t *)t92 + t93 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(t95 - t93));
            gm_b16_8 t97 = gm_l7;
            gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
            gm_f_copy_from_f7j6p05(t96, t98);
        }
        {
            GM_ST(uint8_t, (uint8_t *)&F.l14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&F.l14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&F.l14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&F.l14 + 0, ((uint8_t)1ULL));
            memset((uint8_t *)&(*(gm_tonn1ba3fjh5g *)((uint8_t *)&F.l14 + 8)) + 20, 0, 4);
            gm_b16_8 t99 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tonn1ba3fjh5g *)((uint8_t *)&F.l14 + 8)) + 0, t99);
            uint64_t t100 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t100 >= (uintptr_t)(t101)) gm_trap_index(t100, t101, gm_srcp0l171g2m7ho, 635, 28);
            void * t102 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t100 * 8));
            gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t102) + 96));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_tonn1ba3fjh5g *)((uint8_t *)&F.l14 + 8)) + 16, t103);
            uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
            void * t105 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)));
            uint64_t t106 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            uint64_t t108 = (uintptr_t)t107;
            if (t106 > t108 || t108 > (uintptr_t)t104) gm_trap_slice(t106, t108, t104, gm_srcp0l171g2m7ho, 635, 40);
            gm_b16_8 t109;
            GM_ST(void *, (uint8_t *)&t109, (void *)(((uint8_t *)t105 + t106 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(t108 - t106));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tonn1ba3fjh5g *)((uint8_t *)&F.l14 + 8)) + 24, t109);
            void * t110 = gm_l11;
            uint32_t t111 = GM_LD(uint32_t, (((uint8_t *)t110) + 64));
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tonn1ba3fjh5g *)((uint8_t *)&F.l14 + 8)) + 40, t111);
            void * t112 = gm_l11;
            gm_tl874t2atnlh3 t113 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t112) + 112));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tonn1ba3fjh5g *)((uint8_t *)&F.l14 + 8)) + 44, t113);
        }
        {
            void * t114 = gm_l0;
            gm_tn68el8d14vku t115 = gm_l10;
            gm_tn68el8d14vku t116 = gm_l10;
            gm_tl2g3fsjpi9op t117;
            GM_ST(uint8_t, (uint8_t *)&t117 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t117 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t117 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tn68el8d14vku, ((uint8_t *)&t117 + 8), t116);
            gm_b16_8 t118 = gm_l8;
            gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
            uint32_t t120 = gm_f_new_fiber_39221jf((void *)NULL, cx, NULL, t114, t115, &(*(gm_th5tat709fgha *)((uint8_t *)&F.l14)), &t117, t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t120;
        }
        {
            void * t121 = gm_l0;
            gm_tn68el8d14vku t122 = gm_l10;
            void * t123 = gm_f_with_rec_2eah8tp(t121, GM_CONV(gm_csff7attl2ehc5, t122));
            void * t124;
            if ((GM_LD(void *, ((uint8_t *)&t123)) != 0)) {
                void * t125 = GM_LD(void *, (((uint8_t *)&t123) + 0));
                t124 = t125;
            } else {
                gm_b0_1 t126;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t126);
                goto gm_fdone;
            }
            void * t127 = (void *)(((uint8_t *)t124) + 24);
            uint32_t t128 = gm_l15;
            gm_tftb1odc739t5 t129 = gm_f_push_im9d4ro(t127, t128);
            if (((((uint8_t *)&t129))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t129) + 1, gm_srcp0l171g2m7ho, 637, 5);
            gm_b0_1 t130 = GM_LD(gm_b0_1, (((uint8_t *)&t129) + 1));
        }
        {
            void * t131 = gm_l0;
            gm_b16_8 t132 = gm_l4;
            uint32_t t133 = gm_l15;
            gm_f_put_cont_vgnbja2(t131, t132, t133, ((uint64_t)1ULL));
        }
        {
            void * t134 = gm_l0;
            gm_t2lqu8jsj511d t135 = gm_l5;
            gm_f_release_jn5kqdi(t134, GM_CONV(gm_csc7lc6jlkqkuq, t135));
        }
    }
    gm_b0_1 t136;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t136);
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
inline __attribute__((aligned(4))) void gm_f_fibers_i2ha6hs(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fibers_i2ha6hs F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_t5rqrtiuf7cri *)gm_out) + 4, 0, 4);
    gm_trqfvmjtt4sij t1;
    gm_f_list_12h90il((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t2;
    gm_f_list_jmo8t51((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t9hl37efuvhj8 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0ULL));
    gm_b16_8 t4 = gm_f_empty_slice_ijtp9lo();
    gm_trqfvmjtt4sij t5;
    gm_f_list_fufbgf7((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5rqrtiuf7cri *)gm_out) + 0, ((uint32_t)0ULL));
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t5rqrtiuf7cri *)gm_out) + 8, t1);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t5rqrtiuf7cri *)gm_out) + 48, t2);
    GM_ST(gm_t9hl37efuvhj8, (uint8_t *)&(*(gm_t5rqrtiuf7cri *)gm_out) + 88, t3);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5rqrtiuf7cri *)gm_out) + 144, t4);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t5rqrtiuf7cri *)gm_out) + 160, t5);
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
inline __attribute__((aligned(4))) void gm_f_handoff_mtsh8r2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2) {
    struct gm_fr_handoff_mtsh8r2 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* to */
    uint32_t gm_l3; /* me */
    GM_STACK_CHECK(&F, 1204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, ((((uint8_t *)t2) + 1088) + 0));
        gm_l3 = t3;
    }
    {
        uint32_t t4 = gm_l3;
        uint64_t t5 = (uint64_t)t4;
        uint64_t t6 = (uintptr_t)t5;
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uintptr_t, (((((uint8_t *)t7) + 1088) + 8) + 0) + 8);
        if (t6 >= (uintptr_t)(t8)) gm_trap_index(t6, t8, gm_srcp0l171g2m7ho, 328, 5);
        gm_td6r3ebeccg25 t9;
        memset((uint8_t *)&t9 + 140, 0, 4);
        void * t10 = gm_l0;
        memcpy((uint8_t *)&t9 + 0, (((uint8_t *)t10) + 360), 56);
        void * t11 = gm_l0;
        memcpy((uint8_t *)&t9 + 56, (((uint8_t *)t11) + 416), 40);
        void * t12 = gm_l0;
        memcpy((uint8_t *)&t9 + 96, (((uint8_t *)t12) + 552), 40);
        void * t13 = gm_l0;
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t13) + 768));
        GM_ST(uint32_t, (uint8_t *)&t9 + 136, t14);
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 1288));
        GM_ST(uint64_t, (uint8_t *)&t9 + 144, t16);
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 776));
        GM_ST(uint64_t, (uint8_t *)&t9 + 152, t18);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tphppq0r5de2b *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t7) + 1088) + 8) + 0)) + t6 * 360) + 56)) + 161, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tphppq0r5de2b *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t7) + 1088) + 8) + 0)) + t6 * 360) + 56)) + 162, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tphppq0r5de2b *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t7) + 1088) + 8) + 0)) + t6 * 360) + 56)) + 164, 0);
        ((uint8_t *)&(*(gm_tphppq0r5de2b *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t7) + 1088) + 8) + 0)) + t6 * 360) + 56)))[160] = 1;
        GM_ST(gm_td6r3ebeccg25, (uint8_t *)&(*(gm_tphppq0r5de2b *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t7) + 1088) + 8) + 0)) + t6 * 360) + 56)) + 0, t9);
    }
    {
        void * t19 = gm_l0;
        gm_t9hl37efuvhj8 t20 = F.l2;
        GM_ST(gm_t9hl37efuvhj8, ((((uint8_t *)t19) + 1088) + 88), t20);
    }
    {
        uint32_t t21 = gm_l1;
        gm_node *pn22 = gm_find(cx, 734392322);
        if (!pn22) gm_trap("no handler installed for effect `Coro`", gm_srcp0l171g2m7ho, 330, 5);
        struct gm_pr_handoff_mtsh8r2_23 pr24 = {pn22, t21};
        gm_ctl_perform(pn22->fw, gm_pt_handoff_mtsh8r2_23, &pr24, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t25 = gm_l3;
        uint64_t t26 = (uint64_t)t25;
        uint64_t t27 = (uintptr_t)t26;
        void * t28 = gm_l0;
        uint64_t t29 = GM_LD(uintptr_t, (((((uint8_t *)t28) + 1088) + 8) + 0) + 8);
        if (t27 >= (uintptr_t)(t29)) gm_trap_index(t27, t29, gm_srcp0l171g2m7ho, 331, 10);
        gm_tphppq0r5de2b t30 = GM_LD(gm_tphppq0r5de2b, (((uint8_t *)GM_LD(void *, (((((uint8_t *)t28) + 1088) + 8) + 0)) + t27 * 360) + 56));
        gm_td6r3ebeccg25 t31;
        if (((((uint8_t *)&t30))[160] != 0)) {
            t31 = (*(gm_td6r3ebeccg25 *)(((uint8_t *)&t30) + 0));
        } else {
            memset(&(*(gm_t9hl37efuvhj8 *)gm_out), 0, sizeof (*(gm_t9hl37efuvhj8 *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t9hl37efuvhj8 *)gm_out) + 0, ((uint8_t)0ULL));
            goto gm_fdone;
        }
        F.l4 = t31;
    }
    {
        uint32_t t32 = gm_l3;
        uint64_t t33 = (uint64_t)t32;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, (((((uint8_t *)t35) + 1088) + 8) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcp0l171g2m7ho, 332, 5);
        gm_tphppq0r5de2b t37;
        memset(&t37, 0, sizeof t37);
        GM_ST(gm_tphppq0r5de2b, (((uint8_t *)GM_LD(void *, (((((uint8_t *)t35) + 1088) + 8) + 0)) + t34 * 360) + 56), t37);
    }
    {
        void * t38 = gm_l0;
        gm_t16gj5so0pj6j t39 = GM_LD(gm_t16gj5so0pj6j, (((uint8_t *)&F.l4) + 0));
        GM_ST(gm_t16gj5so0pj6j, (((uint8_t *)t38) + 360), t39);
    }
    {
        void * t40 = gm_l0;
        gm_trqfvmjtt4sij t41 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&F.l4) + 56));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t40) + 416), t41);
    }
    {
        void * t42 = gm_l0;
        gm_trqfvmjtt4sij t43 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&F.l4) + 96));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t42) + 552), t43);
    }
    {
        void * t44 = gm_l0;
        uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 136));
        GM_ST(uint32_t, (((uint8_t *)t44) + 768), t45);
    }
    {
        void * t46 = gm_l0;
        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 144));
        GM_ST(uint64_t, (((uint8_t *)t46) + 1288), t47);
    }
    {
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 152));
        GM_ST(uint64_t, (((uint8_t *)t48) + 776), t49);
    }
    {
        void * t50 = gm_l0;
        gm_t9hl37efuvhj8 t51 = GM_LD(gm_t9hl37efuvhj8, ((((uint8_t *)t50) + 1088) + 88));
        F.l5 = t51;
    }
    {
        void * t52 = gm_l0;
        memset(&(*(gm_t9hl37efuvhj8 *)((((uint8_t *)t52) + 1088) + 88)), 0, sizeof (*(gm_t9hl37efuvhj8 *)((((uint8_t *)t52) + 1088) + 88)));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t9hl37efuvhj8 *)((((uint8_t *)t52) + 1088) + 88)) + 0, ((uint8_t)0ULL));
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l5), 56);
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
inline __attribute__((aligned(4))) void gm_f_host_stack_gpfprtr(void *gm_out, uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_host_stack_gpfprtr F;
    uint64_t gm_l0; /* n */
    void * gm_l1; /* p */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t2 = gm_l0;
        void * t3 = gm_x_calloc_gae3ijq(((uint64_t)1ULL), t2);
        void * t4;
        if ((GM_LD(void *, ((uint8_t *)&t3)) != 0)) {
            void * t5 = GM_LD(void *, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_th2lqbo77pvsc t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        void * t8 = gm_l1;
        void * t9 = ((uint8_t *)t8);
        uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t11 = gm_l0;
        uint64_t t12 = (uintptr_t)t11;
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t9 + t10 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12 - t10));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t13);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_host_stack_gpfprtr_c(uint64_t a0) { gm_th2lqbo77pvsc gm_r; gm_f_host_stack_gpfprtr(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) uint32_t gm_f_new_coro_06nq12a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint32_t a2);
inline __attribute__((aligned(4))) uint32_t gm_f_new_fiber_39221jf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tn68el8d14vku a1, const void * a2, const void * a3, gm_b16_8 a4) {
    struct gm_fr_new_fiber_39221jf F;
    void * gm_l0; /* m */
    gm_tn68el8d14vku gm_l1; /* owner */
    gm_tl2g3fsjpi9op gm_l3; /* state */
    gm_b16_8 gm_l4; /* stack */
    uint64_t gm_l5; /* size */
    uint64_t gm_l6; /* budget */
    uint32_t gm_l7; /* id */
    void * gm_l8; /* rec */
    void * gm_l9; /* w */
    GM_STACK_CHECK(&F, 548u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    memcpy(&gm_l3, a3, sizeof gm_l3);
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l4;
        gm_t2lqu8jsj511d t4 = gm_f_slice_parts_1se5hu4(t2, t3);
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 8));
        gm_l5 = t5;
    }
    {
        uint64_t t6;
        uint64_t t8 = gm_l5;
        uint64_t t9 = GM_LD(uint64_t, gm_cbbu3qvtp3q);
        uint8_t t10 = (t8 > t9);
        if (!t10) goto ie7;
        uint64_t t12 = gm_l5;
        uint64_t t13 = GM_LD(uint64_t, gm_cbbu3qvtp3q);
        uint64_t r14; bool o15 = __builtin_sub_overflow(t12, t13, &r14);
        if (o15) gm_trap("integer overflow in `-`", gm_srcp0l171g2m7ho, 363, 41);
        uint64_t t16 = r14;
        uint64_t t17 = t16;
        t6 = t17;
        goto ix7;
ie7:;
        uint64_t t19 = ((uint64_t)0ULL);
        t6 = t19;
ix7:;
        gm_l6 = t6;
    }
    {
        void * t20 = gm_l0;
        gm_th5tat709fgha t21 = F.l2;
        uint64_t t22 = gm_l5;
        unsigned __int128 n23 = (unsigned __int128)((uint64_t)20ULL);
        if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcp0l171g2m7ho, 366, 42);
        uint64_t r24 = (uint64_t)((uint64_t)((uint64_t)8ULL) << (int)n23);
        bool o25 = (uint64_t)(r24 >> (int)n23) != ((uint64_t)8ULL);
        if (o25) gm_trap("integer overflow in `<<`", gm_srcp0l171g2m7ho, 366, 42);
        uint64_t t26 = r24;
        uint64_t t27 = (t22 < t26 ? t22 : t26);
        uint64_t t28 = gm_f_us_cuv01fp(t27);
        uint64_t r29; bool o30 = __builtin_mul_overflow(t28, ((uint64_t)32ULL), &r29);
        if (o30) gm_trap("integer overflow in `*`", gm_srcp0l171g2m7ho, 366, 30);
        uint64_t t31 = r29;
        unsigned __int128 n32 = (unsigned __int128)((uint64_t)20ULL);
        if (n32 >= 64) gm_trap_shift((uint64_t)n32, "usize", gm_srcp0l171g2m7ho, 366, 62);
        uint64_t r33 = (uint64_t)((uint64_t)((uint64_t)4ULL) << (int)n32);
        bool o34 = (uint64_t)(r33 >> (int)n32) != ((uint64_t)4ULL);
        if (o34) gm_trap("integer overflow in `<<`", gm_srcp0l171g2m7ho, 366, 62);
        uint64_t t35 = r33;
        uint64_t t36 = (t31 > t35 ? t31 : t35);
        void * t37 = gm_l0;
        uint32_t t38 = GM_LD(uint32_t, ((((uint8_t *)t37) + 1656) + 40));
        uint32_t t39 = gm_f_add_coro_k13bkpe((void *)NULL, cx, NULL, t20, &t21, t36, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t39;
    }
    {
        uint32_t t40 = gm_l7;
        uint64_t t41 = (uint64_t)t40;
        uint64_t t42 = (uintptr_t)t41;
        void * t43 = gm_l0;
        uint64_t t44 = GM_LD(uintptr_t, (((((uint8_t *)t43) + 1088) + 8) + 0) + 8);
        if (t42 >= (uintptr_t)(t44)) gm_trap_index(t42, t44, gm_srcp0l171g2m7ho, 367, 13);
        void * t45 = (void *)((uint8_t *)GM_LD(void *, (((((uint8_t *)t43) + 1088) + 8) + 0)) + t42 * 360);
        gm_l8 = t45;
    }
    {
        void * t46 = gm_l8;
        gm_tn68el8d14vku t47 = gm_l1;
        GM_ST(gm_tn68el8d14vku, (((uint8_t *)t46) + 224), t47);
    }
    {
        void * t48 = gm_l8;
        gm_tl2g3fsjpi9op t49 = gm_l3;
        GM_ST(gm_tl2g3fsjpi9op, (((uint8_t *)t48) + 248), t49);
    }
    {
        void * t50 = gm_l8;
        uint64_t t51 = gm_l6;
        GM_ST(uint64_t, (((uint8_t *)t50) + 344), t51);
    }
    {
        void * t53 = gm_l0;
        gm_tn68el8d14vku t54 = gm_l1;
        void * t55 = gm_f_with_rec_2eah8tp(t53, GM_CONV(gm_csff7attl2ehc5, t54));
        if (!(GM_LD(void *, ((uint8_t *)&t55)) != 0)) goto ie52;
        void * t56 = GM_LD(void *, ((uint8_t *)&t55));
        gm_l9 = t56;
        {
            void * t58 = gm_l9;
            void * t59 = (void *)(((uint8_t *)t58) + 72);
            uint32_t t60 = gm_l7;
            gm_tftb1odc739t5 t61 = gm_f_push_im9d4ro(t59, t60);
            if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_srcp0l171g2m7ho, 371, 33);
            gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
        }
        goto ix52;
ie52:;
ix52:;
    }
    {
        uint32_t t63 = gm_l7;
        uint8_t t64 = 0;
        F.ret = t63;
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
extern __attribute__((aligned(4))) void gm_f_put_cont_vgnbja2(void * a0, gm_b16_8 a1, uint32_t a2, uint64_t a3);
inline __attribute__((aligned(4))) void gm_f_resume__mi7hk2f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3, void * a4) {
    struct gm_fr_resume__mi7hk2f F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* k */
    void * gm_l3; /* v */
    void * gm_l4; /* e */
    gm_t2lqu8jsj511d gm_l5; /* mk */
    gm_b16_8 gm_l6; /* kb */
    gm_b16_8 gm_l7; /* vb */
    gm_t4qfihdig0mro gm_l8; /* c */
    gm_b16_8 gm_l9; /* saved */
    GM_STACK_CHECK(&F, 793u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        gm_t2lqu8jsj511d t3 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t2));
        gm_l5 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l2;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 104));
        gm_b16_8 t7 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t4, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t7;
    }
    {
        void * t8 = gm_l0;
        gm_tbl8il92l97ut t9 = gm_l1;
        void * t10 = gm_l2;
        gm_b16_8 t11 = gm_l6;
        gm_tcfa6l7reoosn t12;
        gm_f_ev_25jfdar((void *)&t12, (void *)NULL, cx, NULL, t8, t9, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t12))[0] != 0)) {
            gm_tcfa6l7reoosn t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t13 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t12) + 8)));
            F.ret = t13;
            goto gm_fret;
        }
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 8));
    }
    {
        void * t15 = gm_l0;
        void * t16 = gm_l3;
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 104));
        gm_b16_8 t18 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t15, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t18;
    }
    {
        void * t19 = gm_l0;
        gm_tbl8il92l97ut t20 = gm_l1;
        void * t21 = gm_l3;
        gm_b16_8 t22 = gm_l7;
        gm_tcfa6l7reoosn t23;
        gm_f_ev_25jfdar((void *)&t23, (void *)NULL, cx, NULL, t19, t20, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t23))[0] != 0)) {
            gm_tcfa6l7reoosn t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
            ((uint8_t *)&t24)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t24 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t23) + 8)));
            F.ret = t24;
            goto gm_fret;
        }
        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 8));
    }
    {
        void * t26 = gm_l0;
        void * t27 = gm_l4;
        gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t27) + 112));
        void * t29 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
        gm_b16_8 t30 = gm_l6;
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
        gm_tmr8tdh5g7uj6 t32;
        gm_f_consume_u5eqint((void *)&t32, (void *)NULL, cx, NULL, t26, t28, t29, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t32))[0] != 0)) {
            gm_tcfa6l7reoosn t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t33 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t32) + 8)));
            F.ret = t33;
            goto gm_fret;
        }
        gm_l8 = (*(gm_t4qfihdig0mro *)(((uint8_t *)&t32) + 8));
    }
    {
        gm_b16_8 t34 = gm_f_empty_slice_ijtp9lo();
        gm_l9 = t34;
    }
    {
        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
        uint8_t t37 = (t36 > ((uint64_t)0ULL));
        if (!t37) goto ie35;
        {
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            gm_tabb710d9mq2e t40;
            gm_f_make_2ip78bj((void *)&t40, (void *)NULL, cx, NULL, t39, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k1b2sgalr12)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_srcp0l171g2m7ho, 600, 17);
            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
            gm_l9 = t41;
        }
        {
            gm_b16_8 t42 = gm_l9;
            gm_b16_8 t43 = gm_l7;
            gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
            gm_f_copy_from_f7j6p05(t42, t44);
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        void * t45 = gm_l0;
        gm_b16_8 t46 = gm_l9;
        GM_ST(gm_b16_8, ((((uint8_t *)t45) + 1088) + 144), t46);
    }
    {
        void * t47 = gm_l0;
        gm_t2lqu8jsj511d t48 = gm_l5;
        gm_f_release_jn5kqdi(t47, GM_CONV(gm_csc7lc6jlkqkuq, t48));
    }
    {
        uint32_t t49 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
        gm_t76123gfet73g t50;
        GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t50 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t50 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)5ULL));
        GM_ST(uint32_t, ((uint8_t *)&t50 + 8), t49);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t50);
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
inline __attribute__((aligned(4))) void gm_f_root_host_stack_e3obr0f(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_root_host_stack_e3obr0f F;
    uint64_t gm_l0; /* n */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK(&F, 227u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint64_t t2;
        uint64_t t4 = (uint64_t)((uint64_t)18446744073709551615ULL);
        uint64_t t5 = GM_LD(uint64_t, gm_cbcjb1mvjd8);
        uint8_t t6 = (t4 > t5);
        if (!t6) goto ie3;
        uint64_t t8 = GM_LD(uint64_t, gm_cbcjb1mvjd8);
        uint64_t t9 = (uint64_t)t8;
        uint64_t t10 = t9;
        t2 = t10;
        goto ix3;
ie3:;
        uint64_t t12 = GM_LD(uint64_t, gm_cog1l56ulfv);
        uint64_t t13 = (uint64_t)t12;
        uint64_t t14 = t13;
        t2 = t14;
ix3:;
        gm_l0 = t2;
    }
    {
        for (;;) {
            uint64_t t16 = gm_l0;
            uint64_t t17 = (uint64_t)t16;
            uint64_t t18 = GM_LD(uint64_t, gm_cog1l56ulfv);
            uint8_t t19 = (t17 >= t18);
            if (!t19) goto lb15;
            {
                uint64_t t23 = gm_l0;
                gm_th2lqbo77pvsc t24;
                gm_f_host_stack_gpfprtr((void *)&t24, t23);
                if (!((((uint8_t *)&t24))[16] != 0)) goto ie22;
                gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
                gm_l1 = t25;
                {
                    gm_b16_8 t27 = gm_l1;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t27);
                    goto gm_fdone;
                }
                goto ix22;
ie22:;
ix22:;
            }
            {
                uint64_t t28 = gm_l0;
                if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcp0l171g2m7ho, 94, 9);
                uint64_t t29 = (uint64_t)(t28 / ((uint64_t)2ULL));
                gm_l0 = t29;
            }
lc15:;
        }
lb15:;
    }
    {
        gm_th2lqbo77pvsc t30;
        memset(&t30, 0, sizeof t30);
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_root_host_stack_e3obr0f_c(void) { gm_th2lqbo77pvsc gm_r; gm_f_root_host_stack_e3obr0f(&gm_r); return gm_r; }
inline __attribute__((aligned(4))) gm_tljvbvj59adij gm_f_run_coroutines_0a7hn79(void *env, gm_node *cx, gm_node *inst, void * a0, gm_closure a1) {
    struct gm_fr_run_coroutines_0a7hn79 F;
    GM_STACK_CHECK(&F, 999u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    {
        void * t2 = F.l0;
        void * t3 = (void *)((((uint8_t *)t2) + 1088) + 8);
        gm_to1vg29gqhjl9 t4;
        memset((uint8_t *)&t4 + 26, 0, 6);
        memset((uint8_t *)&t4 + 356, 0, 4);
        gm_th2lqbo77pvsc t5;
        memset(&t5, 0, sizeof t5);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t4 + 0, t5);
        GM_ST(uint8_t, (uint8_t *)&t4 + 24, ((uint8_t)1));
        GM_ST(uint8_t, (uint8_t *)&t4 + 25, ((uint8_t)0));
        void * t6 = F.l0;
        void * t7 = t6;
        GM_ST(void *, (uint8_t *)&t4 + 32, t7);
        gm_b16_8 t8 = gm_f_empty_slice_ijtp9lo();
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 40, t8);
        gm_tphppq0r5de2b t9;
        memset(&t9, 0, sizeof t9);
        GM_ST(gm_tphppq0r5de2b, (uint8_t *)&t4 + 56, t9);
        gm_tn68el8d14vku t10;
        GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t10 + 0), ((uint64_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t10 + 8), ((uint32_t)0ULL));
        GM_ST(gm_tn68el8d14vku, (uint8_t *)&t4 + 224, t10);
        GM_ST(uint64_t, (uint8_t *)&t4 + 240, ((uint64_t)0ULL));
        gm_tl2g3fsjpi9op t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t11 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t11 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tl2g3fsjpi9op, (uint8_t *)&t4 + 248, t11);
        gm_t68f8jeqq3b3v t12;
        memset(&t12, 0, sizeof t12);
        GM_ST(gm_t68f8jeqq3b3v, (uint8_t *)&t4 + 272, t12);
        GM_ST(uint64_t, (uint8_t *)&t4 + 344, ((uint64_t)0ULL));
        GM_ST(uint32_t, (uint8_t *)&t4 + 352, ((uint32_t)0ULL));
        gm_tftb1odc739t5 t13 = gm_f_push_vpv1j2p(t3, &t4);
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcp0l171g2m7ho, 244, 5);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        void * t15 = F.l0;
        gm_t3p0qn9o5e53o t16;
        GM_ST(void *, ((uint8_t *)&t16 + 0), t15);
        F.l2 = t16;
    }
    {
        gm_th2lqbo77pvsc t17;
        gm_f_root_host_stack_e3obr0f((void *)&t17);
        if (!((((uint8_t *)&t17))[16] != 0)) gm_trap("unwrap of null", gm_srcp0l171g2m7ho, 246, 14);
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
        F.l3 = t18;
    }
    {
    }
    {
        gm_with gw20; memset(&gw20, 0, sizeof gw20);
        void * t21 = (void *)((uint8_t *)&F.l2);
        void * t22 = t21;
        gm_node wn23 = {.eff = 734392322, .kind = GM_DECL, .clauses = gm_hd365sua5c99, .env = (void *)&t22, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = &gw20, .next = cx};
        gm_b16_8 t24 = F.l3;
        gw20.body_cx = (&wn23);
        gm_fiber *fb49 = gm_fiber_new(&gw20, (uint8_t *)GM_LD(void *, ((uint8_t *)&t24)), GM_LD(uintptr_t, ((uint8_t *)&t24) + 8), gm_wb_run_coroutines_0a7hn79_0, 0, gm_srcp0l171g2m7ho, 248, 12);
        fb49->frame = &F; fb49->env = env; fb49->inst = inst; fb49->cx = (&wn23);
        gm_with_go(&gw20, fb49);
        for (;;) {
            if (gw20.event == GM_EV_PERFORM) {
                gm_with_clause(&gw20);
                if (gm_unw.kind == GM_RESUME) { gm_unw.kind = GM_NONE; gm_with_go(&gw20, gw20.next); continue; }
                gm_with_end(&gw20, !(gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0), gm_srcp0l171g2m7ho, 248, 12);
            } else if (gw20.event_fiber == fb49 && !gm_unw.kind) {
                gm_with_end(&gw20, false, gm_srcp0l171g2m7ho, 248, 12);
                goto we50;
            } else {
                if (!gm_unw.kind) gm_trap("a fiber's function returned", gm_srcp0l171g2m7ho, 248, 12);
                gm_with_end(&gw20, true, gm_srcp0l171g2m7ho, 248, 12);
            }
            if (!gm_unw.kind) gm_trap("internal error: a `ctl` clause finished normally", gm_srcp0l171g2m7ho, 248, 12);
            goto gm_ll51;
gm_ll51:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we50; }
            goto gm_ld1_1;
        }
we50:;
        gm_tljvbvj59adij t52 = F.w0;
        uint8_t t53 = 0;
        F.ret = t52;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t53;
        goto gm_ld1_1;
    }
    {
        void * t54 = GM_LD(void *, ((uint8_t *)&F.l3));
        gm_x_free_eeuh60c(t54);
    }
    goto gm_ls55;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t56 = GM_LD(void *, ((uint8_t *)&F.l3));
        gm_x_free_eeuh60c(t56);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls55:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_same_key_a5oh9pl(gm_csff7attl2ehc5 a0, gm_csff7attl2ehc5 a1);
extern __attribute__((aligned(4))) void gm_f_set_stack_floor_f15muof(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_switch_to_gq9vlt3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tn68el8d14vku a2, uint32_t a3, const void * a4) {
    struct gm_fr_switch_to_gq9vlt3 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tn68el8d14vku gm_l2; /* key */
    uint32_t gm_l3; /* fiber */
    void * gm_l5; /* rec */
    uint8_t gm_l6; /* resuming */
    GM_STACK_CHECK(&F, 549u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&F.l4, a4, sizeof F.l4);
    {
        void * t3 = gm_l0;
        gm_tn68el8d14vku t4 = gm_l2;
        void * t5 = gm_f_with_rec_2eah8tp(t3, GM_CONV(gm_csff7attl2ehc5, t4));
        uint8_t t6;
        if ((GM_LD(void *, ((uint8_t *)&t5)) != 0)) goto mn8;
        t6 = ((uint8_t)1);
        goto me7;
mn8:;
        t6 = ((uint8_t)0);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 531, 8);
me7:;
        if (!t6) goto ie2;
        {
            void * t11 = gm_l0;
            gm_tl874t2atnlh3 t12 = gm_l1;
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_skq2uj9oi1243));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(40));
            gm_t76123gfet73g t14;
            gm_f_trap_qh84l9d((void *)&t14, (void *)NULL, cx, NULL, t11, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            memset(&(*(gm_tv90d8meul1fb *)gm_out), 0, sizeof (*(gm_tv90d8meul1fb *)gm_out));
            ((uint8_t *)&(*(gm_tv90d8meul1fb *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tv90d8meul1fb *)gm_out) + 8, t14);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t15 = gm_l3;
        uint64_t t16 = (uint64_t)t15;
        uint64_t t17 = (uintptr_t)t16;
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uintptr_t, (((((uint8_t *)t18) + 1088) + 8) + 0) + 8);
        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcp0l171g2m7ho, 532, 13);
        void * t20 = (void *)((uint8_t *)GM_LD(void *, (((((uint8_t *)t18) + 1088) + 8) + 0)) + t17 * 360);
        gm_l5 = t20;
    }
    {
        uint8_t t21;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t24 != ((uint8_t)1ULL)) goto mn23;
        t21 = ((uint8_t)1);
        goto me22;
mn23:;
        t21 = ((uint8_t)0);
        goto me22;
mn25:;
        gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 533, 17);
me22:;
        gm_l6 = t21;
    }
    {
        void * t26 = gm_l5;
        uint8_t *t27 = (((uint8_t *)t26) + 248);
        gm_tl2g3fsjpi9op t28;
        uint8_t t30 = gm_l6;
        if (!t30) goto ie29;
        gm_tl2g3fsjpi9op t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t32 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t32 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
        gm_tl2g3fsjpi9op t33 = t32;
        t28 = t33;
        goto ix29;
ie29:;
        gm_tl2g3fsjpi9op t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t35 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)2ULL));
        gm_tl2g3fsjpi9op t36 = t35;
        t28 = t36;
ix29:;
        GM_ST(gm_tl2g3fsjpi9op, t27, t28);
    }
    {
        void * t38 = gm_l5;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t38) + 24));
        uint8_t t40 = (uint8_t)!t39;
        if (!t40) goto ie37;
        uint8_t t41 = gm_l6;
        uint8_t t42 = (uint8_t)!t41;
        if (!t42) goto ie37;
        {
            void * t44 = gm_l5;
            gm_t68f8jeqq3b3v t45;
            memset(&t45, 0, sizeof t45);
            GM_ST(gm_t68f8jeqq3b3v, (((uint8_t *)t44) + 272), t45);
        }
        {
            void * t46 = gm_l5;
            GM_ST(uint8_t, (((uint8_t *)t46) + 25), ((uint8_t)1));
        }
        {
            gm_t9hl37efuvhj8 t47;
            memset(&t47, 0, sizeof t47);
            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tv90d8meul1fb *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tv90d8meul1fb *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tv90d8meul1fb *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tv90d8meul1fb *)gm_out))[0] = 0;
            GM_ST(gm_t9hl37efuvhj8, (uint8_t *)&(*(gm_tv90d8meul1fb *)gm_out) + 8, t47);
            goto gm_fdone;
        }
        goto ix37;
ie37:;
ix37:;
    }
    {
        void * t48 = gm_l0;
        uint32_t t49 = gm_l3;
        gm_t9hl37efuvhj8 t50;
        gm_f_handoff_mtsh8r2((void *)&t50, (void *)NULL, cx, NULL, t48, t49, &(*(gm_t9hl37efuvhj8 *)((uint8_t *)&F.l4)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tv90d8meul1fb *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tv90d8meul1fb *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tv90d8meul1fb *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tv90d8meul1fb *)gm_out))[0] = 0;
        GM_ST(gm_t9hl37efuvhj8, (uint8_t *)&(*(gm_tv90d8meul1fb *)gm_out) + 8, t50);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_transfer_0mkrfnm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_transfer_0mkrfnm F;
    void * gm_l0; /* self */
    uint32_t gm_l1; /* to */
    gm_b16_8 gm_l2; /* k */
    void * gm_l3; /* f */
    void * gm_l4; /* cur */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    gm_b16_8 gm_l8; /* x */
    gm_b16_8 gm_l9; /* r */
    void * gm_l10; /* target */
    gm_b16_8 gm_l11; /* r */
    GM_STACK_CHECK(&F, 927u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 1088);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l3;
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)t5) + 0));
        uint64_t t7 = (uint64_t)t6;
        uint64_t t8 = (uintptr_t)t7;
        void * t9 = gm_l3;
        uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 8) + 0) + 8);
        if (t8 >= (uintptr_t)(t10)) gm_trap_index(t8, t10, gm_srcp0l171g2m7ho, 32, 17);
        void * t11 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t9) + 8) + 0)) + t8 * 360);
        gm_l4 = t11;
    }
    {
        void * t13 = gm_l4;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t13) + 25));
        if (!t14) goto ie12;
        {
            gm_b16_8 t16 = gm_l2;
            gm_cancel_cont(inst->fw, ((uint8_t *)&t16), gm_srcp0l171g2m7ho, 36, 13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t17 = gm_l3;
            void * t18 = (void *)(((uint8_t *)t17) + 160);
            void * t19 = gm_l4;
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t19) + 40));
            gm_tftb1odc739t5 t21 = gm_f_push_rluch9r(t18, t20);
            if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srcp0l171g2m7ho, 37, 13);
            gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
        }
        {
            void * t23 = gm_l4;
            uint8_t *t24 = (((uint8_t *)t23) + 40);
            gm_b16_8 t25 = gm_f_empty_slice_ijtp9lo();
            GM_ST(gm_b16_8, t24, t25);
        }
        goto ix12;
ie12:;
        {
            void * t27 = gm_l4;
            gm_b16_8 t28 = gm_l2;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t27) + 0)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t27) + 0)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t27) + 0)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t27) + 0)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t27) + 0)) + 0, t28);
        }
ix12:;
    }
    {
        void * t29 = gm_l3;
        uint32_t t30 = gm_l1;
        GM_ST(uint32_t, (((uint8_t *)t29) + 0), t30);
    }
    {
        uint32_t t32 = gm_l1;
        uint32_t t33 = GM_LD(uint32_t, gm_ca9ikdfm7p4);
        uint8_t t34 = ((t32 == t33));
        if (!t34) goto ie31;
        {
            {
                gm_l6 = ((uint64_t)1ULL);
            }
            {
                void * t37 = gm_l3;
                uint64_t t38 = GM_LD(uintptr_t, ((((uint8_t *)t37) + 8) + 0) + 8);
                gm_l5 = t38;
            }
            {
                for (;;) {
                    uint64_t t40 = gm_l6;
                    uint64_t t41 = gm_l5;
                    uint8_t t42 = (t40 < t41);
                    if (!t42) goto lb39;
                    {
                        uint64_t t45 = gm_l6;
                        uint64_t t48 = GM_LD(uint64_t, ((uint8_t *)&t45));
                        gm_l7 = t48;
                        {
                            uint64_t t51 = gm_l7;
                            uint64_t t52 = (uintptr_t)t51;
                            void * t53 = gm_l3;
                            uint64_t t54 = GM_LD(uintptr_t, ((((uint8_t *)t53) + 8) + 0) + 8);
                            if (t52 >= (uintptr_t)(t54)) gm_trap_index(t52, t54, gm_srcp0l171g2m7ho, 47, 25);
                            if (!(((((uint8_t *)GM_LD(void *, ((((uint8_t *)t53) + 8) + 0)) + t52 * 360) + 0))[16] != 0)) goto ie50;
                            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t53) + 8) + 0)) + t52 * 360) + 0));
                            gm_l8 = t55;
                            {
                                uint64_t t57 = gm_l7;
                                uint64_t t58 = (uintptr_t)t57;
                                void * t59 = gm_l3;
                                uint64_t t60 = GM_LD(uintptr_t, ((((uint8_t *)t59) + 8) + 0) + 8);
                                if (t58 >= (uintptr_t)(t60)) gm_trap_index(t58, t60, gm_srcp0l171g2m7ho, 48, 21);
                                gm_th2lqbo77pvsc t61;
                                memset(&t61, 0, sizeof t61);
                                GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t59) + 8) + 0)) + t58 * 360) + 0), t61);
                            }
                            {
                                gm_b16_8 t62 = gm_l8;
                                gm_cancel_cont(inst->fw, ((uint8_t *)&t62), gm_srcp0l171g2m7ho, 49, 21);
                                if (gm_unw.kind) {
                                    goto gm_ll43;
                                }
                            }
                            goto ix50;
ie50:;
ix50:;
                        }
                        goto me46;
mn47:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 46, 13);
me46:;
                    }
lc39:;
                    {
                        uint64_t t64 = gm_l6;
                        uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                        uint64_t t67 = r65;
                        gm_l6 = t67;
                    }
                }
gm_ll43:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb39; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc39; }
                }
                goto gm_lad;
lb39:;
            }
        }
        {
            void * t68 = gm_l3;
            GM_ST(uint32_t, (((uint8_t *)t68) + 0), ((uint32_t)0ULL));
        }
        {
            uint64_t t69 = (uintptr_t)((uint64_t)0ULL);
            void * t70 = gm_l3;
            uint64_t t71 = GM_LD(uintptr_t, ((((uint8_t *)t70) + 8) + 0) + 8);
            if (t69 >= (uintptr_t)(t71)) gm_trap_index(t69, t71, gm_srcp0l171g2m7ho, 53, 18);
            gm_th2lqbo77pvsc t72 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t70) + 8) + 0)) + t69 * 360) + 0));
            gm_b16_8 t73;
            if (((((uint8_t *)&t72))[16] != 0)) {
                gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 0));
                t73 = t74;
            } else {
                gm_trap("reached `@unreachable()`", gm_srcp0l171g2m7ho, 53, 42);
            }
            gm_l9 = t73;
        }
        {
            uint64_t t75 = (uintptr_t)((uint64_t)0ULL);
            void * t76 = gm_l3;
            uint64_t t77 = GM_LD(uintptr_t, ((((uint8_t *)t76) + 8) + 0) + 8);
            if (t75 >= (uintptr_t)(t77)) gm_trap_index(t75, t77, gm_srcp0l171g2m7ho, 54, 13);
            gm_th2lqbo77pvsc t78;
            memset(&t78, 0, sizeof t78);
            GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t76) + 8) + 0)) + t75 * 360) + 0), t78);
        }
        {
            gm_b16_8 t79 = gm_l9;
            gm_b0_1 t80;
            void *rp81 = gm_resume(inst->fw, ((uint8_t *)&t79), gm_srcp0l171g2m7ho, 55, 13);
            (void)rp81;
            gm_unw.kind = GM_RESUME; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_lad;
        }
        goto ix31;
ie31:;
ix31:;
    }
    {
        uint32_t t82 = gm_l1;
        uint64_t t83 = (uint64_t)t82;
        uint64_t t84 = (uintptr_t)t83;
        void * t85 = gm_l3;
        uint64_t t86 = GM_LD(uintptr_t, ((((uint8_t *)t85) + 8) + 0) + 8);
        if (t84 >= (uintptr_t)(t86)) gm_trap_index(t84, t86, gm_srcp0l171g2m7ho, 57, 20);
        void * t87 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t85) + 8) + 0)) + t84 * 360);
        gm_l10 = t87;
    }
    {
        void * t89 = gm_l10;
        uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)t89) + 24));
        uint8_t t91 = (uint8_t)!t90;
        if (!t91) goto ie88;
        {
            void * t93 = gm_l10;
            GM_ST(uint8_t, (((uint8_t *)t93) + 24), ((uint8_t)1));
        }
        {
            void * t94 = gm_l10;
            void * t95 = GM_LD(void *, (((uint8_t *)t94) + 32));
            void * t96 = gm_l10;
            gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)t96) + 40));
            gm_fiber *nf101 = gm_fiber_new(inst->fw, (uint8_t *)GM_LD(void *, ((uint8_t *)&t97)), GM_LD(uintptr_t, ((uint8_t *)&t97) + 8), gm_fs_transfer_0mkrfnm_98, 8, gm_srcp0l171g2m7ho, 60, 20);
            nf101->fn = (void *)((void *)gm_f_coro_entry_62llbbf); nf101->cx = inst->fw->body_cx;
            GM_ST(void *, nf101->arg, t95);
            gm_cont kc102 = gm_fiber_cont(inst->fw, nf101);
            gm_b16_8 t103 = GM_LD(gm_b16_8, &kc102);
            gm_b0_1 t104;
            void *rp105 = gm_resume(inst->fw, ((uint8_t *)&t103), gm_srcp0l171g2m7ho, 60, 13);
            (void)rp105;
            gm_unw.kind = GM_RESUME; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_lad;
        }
        goto ix88;
ie88:;
ix88:;
    }
    {
        void * t106 = gm_l10;
        gm_th2lqbo77pvsc t107 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t106) + 0));
        gm_b16_8 t108;
        if (((((uint8_t *)&t107))[16] != 0)) {
            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&t107) + 0));
            t108 = t109;
        } else {
            gm_trap("reached `@unreachable()`", gm_srcp0l171g2m7ho, 62, 28);
        }
        gm_l11 = t108;
    }
    {
        void * t110 = gm_l10;
        gm_th2lqbo77pvsc t111;
        memset(&t111, 0, sizeof t111);
        GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)t110) + 0), t111);
    }
    gm_b16_8 t112 = gm_l11;
    gm_b0_1 t113;
    void *rp114 = gm_resume(inst->fw, ((uint8_t *)&t112), gm_srcp0l171g2m7ho, 64, 9);
    (void)rp114;
    gm_unw.kind = GM_RESUME; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
    goto gm_lad;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_with_on_a8m20cg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_with_on_a8m20cg F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_b16_8 gm_l3; /* stack */
    void * gm_l4; /* e */
    gm_b16_8 gm_l5; /* dst */
    gm_tn68el8d14vku gm_l6; /* key */
    uint32_t gm_l7; /* first */
    gm_tcfa6l7reoosn gm_l8; /* r */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    GM_STACK_CHECK(&F, 1085u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l4;
        gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t3) + 112));
        gm_b16_8 t5 = gm_l3;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_tcfa6l7reoosn t7;
        gm_f_check_stack_eei454t((void *)&t7, (void *)NULL, cx, NULL, t2, t4, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t7))[0] != 0)) {
            gm_tcfa6l7reoosn t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t8 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t7) + 8)));
            F.ret = t8;
            goto gm_fret;
        }
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 8));
    }
    {
        void * t10 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 0));
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)a2) + 0));
        gm_tn68el8d14vku t13;
        GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t13 + 0), t11);
        GM_ST(uint32_t, ((uint8_t *)&t13 + 8), t12);
        gm_l6 = t13;
    }
    {
        void * t15 = gm_l0;
        gm_tn68el8d14vku t16 = gm_l6;
        void * t17 = gm_f_with_rec_2eah8tp(t15, GM_CONV(gm_csff7attl2ehc5, t16));
        uint8_t t18;
        if ((GM_LD(void *, ((uint8_t *)&t17)) != 0)) goto mn20;
        t18 = ((uint8_t)0);
        goto me19;
mn20:;
        t18 = ((uint8_t)1);
        goto me19;
mn21:;
        gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 404, 8);
me19:;
        if (!t18) goto ie14;
        {
            void * t23 = gm_l0;
            void * t24 = gm_l4;
            gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t24) + 112));
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s3mvfcnrk9f21));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(47));
            gm_t76123gfet73g t27;
            gm_f_trap_qh84l9d((void *)&t27, (void *)NULL, cx, NULL, t23, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t27);
            goto gm_fdone;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t28 = gm_l0;
        void * t29 = (void *)((((uint8_t *)t28) + 1088) + 48);
        gm_to8g4620flbcm t30;
        memset((uint8_t *)&t30 + 20, 0, 4);
        memset((uint8_t *)&t30 + 68, 0, 4);
        memset((uint8_t *)&t30 + 124, 0, 4);
        gm_tn68el8d14vku t31 = gm_l6;
        GM_ST(gm_tn68el8d14vku, (uint8_t *)&t30 + 0, t31);
        void * t32 = gm_l0;
        uint32_t t33 = GM_LD(uint32_t, ((((uint8_t *)t32) + 1088) + 0));
        GM_ST(uint32_t, (uint8_t *)&t30 + 16, t33);
        gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t30 + 24)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
        GM_ST(uint32_t, (uint8_t *)&t30 + 64, t34);
        gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t30 + 72)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t35 = gm_l4;
        gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t35) + 112));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t30 + 112, t36);
        gm_tftb1odc739t5 t37 = gm_f_push_t6u2ggv(t29, &t30);
        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_srcp0l171g2m7ho, 405, 5);
        gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
    }
    {
        void * t39 = gm_l0;
        gm_tn68el8d14vku t40 = gm_l6;
        gm_tsbt7g382sgof t41;
        memset((uint8_t *)&t41 + 12, 0, 4);
        void * t42 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
        GM_ST(void *, (uint8_t *)&t41 + 0, t42);
        uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
        GM_ST(uint32_t, (uint8_t *)&t41 + 8, t43);
        void * t44 = GM_LD(void *, (((uint8_t *)a2) + 32));
        GM_ST(void *, (uint8_t *)&t41 + 16, t44);
        gm_b16_8 t45 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t41 + 24, t45);
        gm_th5tat709fgha t46;
        GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t46 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t46 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tsbt7g382sgof, ((uint8_t *)&t46 + 8), t41);
        gm_tl2g3fsjpi9op t47;
        GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t47 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t47 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
        gm_b16_8 t48 = gm_l3;
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
        uint32_t t50 = gm_f_new_fiber_39221jf((void *)NULL, cx, NULL, t39, t40, &t46, &t47, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t50;
    }
    {
        void * t51 = gm_l0;
        void * t52 = gm_l4;
        gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t52) + 112));
        gm_tn68el8d14vku t54 = gm_l6;
        uint32_t t55 = gm_l7;
        gm_f_drive_i7nr5mu((void *)&gm_l8, (void *)NULL, cx, NULL, t51, t53, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t56 = gm_l0;
        void * t57 = gm_l4;
        gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t57) + 112));
        gm_tn68el8d14vku t59 = gm_l6;
        gm_b0_1 t60 = gm_f_abort_all_4him68f((void *)NULL, cx, NULL, t56, t58, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            void * t62 = gm_l0;
            uint64_t t63 = GM_LD(uintptr_t, (((((uint8_t *)t62) + 1088) + 48) + 0) + 8);
            gm_l9 = t63;
        }
        {
            for (;;) {
                uint64_t t65 = gm_l10;
                uint64_t t66 = gm_l9;
                uint8_t t67 = (t65 < t66);
                if (!t67) goto lb64;
                {
                    uint64_t t70 = gm_l10;
                    uint64_t t73 = GM_LD(uint64_t, ((uint8_t *)&t70));
                    gm_l11 = t73;
                    {
                        uint64_t t76 = gm_l11;
                        uint64_t t77 = (uintptr_t)t76;
                        void * t78 = gm_l0;
                        uint64_t t79 = GM_LD(uintptr_t, (((((uint8_t *)t78) + 1088) + 48) + 0) + 8);
                        if (t77 >= (uintptr_t)(t79)) gm_trap_index(t77, t79, gm_srcp0l171g2m7ho, 411, 21);
                        gm_tn68el8d14vku t80 = GM_LD(gm_tn68el8d14vku, (((uint8_t *)GM_LD(void *, (((((uint8_t *)t78) + 1088) + 48) + 0)) + t77 * 128) + 0));
                        gm_tn68el8d14vku t81 = gm_l6;
                        uint8_t t82 = gm_f_same_key_a5oh9pl(GM_CONV(gm_csff7attl2ehc5, t80), GM_CONV(gm_csff7attl2ehc5, t81));
                        if (!t82) goto ie75;
                        {
                            void * t84 = gm_l0;
                            void * t85 = (void *)((((uint8_t *)t84) + 1088) + 48);
                            uint64_t t86 = gm_l11;
                            gm_f_swap_remove_7083s0t((void *)&F.l12, t85, t86);
                        }
                        {
                            void * t87 = (void *)(((uint8_t *)&F.l12) + 24);
                            gm_f_deinit_padgogk(t87);
                        }
                        {
                            void * t88 = (void *)(((uint8_t *)&F.l12) + 72);
                            gm_f_deinit_padgogk(t88);
                        }
                        {
                            goto lb64;
                        }
                        goto ix75;
ie75:;
ix75:;
                    }
                    goto me71;
mn72:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 410, 5);
me71:;
                }
lc64:;
                {
                    uint64_t t90 = gm_l10;
                    uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                    uint64_t t93 = r91;
                    gm_l10 = t93;
                }
            }
lb64:;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l8), 32);
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
inline __attribute__((aligned(4))) void * gm_f_with_rec_2eah8tp(void * a0, gm_csff7attl2ehc5 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_with_rec_2eah8tp F;
    void * gm_l0; /* m */
    gm_tn68el8d14vku gm_l1; /* key */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l5; /* w */
    GM_STACK_CHECK_SHALLOW(&F, 229u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tn68el8d14vku, a1);
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, (((((uint8_t *)t3) + 1088) + 48) + 0) + 8);
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l4 = t14;
                    {
                        uint64_t t16 = gm_l4;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uintptr_t, (((((uint8_t *)t18) + 1088) + 48) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcp0l171g2m7ho, 346, 15);
                        void * t20 = (void *)((uint8_t *)GM_LD(void *, (((((uint8_t *)t18) + 1088) + 48) + 0)) + t17 * 128);
                        gm_l5 = t20;
                    }
                    {
                        void * t22 = gm_l5;
                        uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t22) + 0) + 0));
                        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t25 = ((t23 == t24));
                        if (!t25) goto ie21;
                        void * t26 = gm_l5;
                        uint32_t t27 = GM_LD(uint32_t, ((((uint8_t *)t26) + 0) + 8));
                        uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
                        uint8_t t29 = ((t27 == t28));
                        if (!t29) goto ie21;
                        {
                            void * t31 = gm_l5;
                            uint8_t t32 = 0;
                            F.ret = t31;
                            goto gm_fret;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp0l171g2m7ho, 345, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t34 = gm_l3;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l3 = t37;
                }
            }
lb5:;
        }
    }
    {
        uint8_t t38 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
void gm_fs_transfer_0mkrfnm_98(gm_fiber *fb) {
    void * t99 = GM_LD(void *, fb->arg);
    void * t100 = fb->fn;
    if (GM_PTR_TAGGED(t100)) {
        ((void (*)(void *))GM_PTR_UNTAG(t100))(t99);
    } else {
        ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t100)(NULL, fb->cx, NULL, t99);
    }
}
void gm_pt_coro_main_hqntcv4_190(void *req, gm_cont k) { struct gm_pr_coro_main_hqntcv4_190 *r = (struct gm_pr_coro_main_hqntcv4_190 *)req; gm_node *n = r->n; gm_b16_8 kb; memcpy(&kb, &k, sizeof kb); ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint32_t, gm_b16_8))n->clauses[0])(n->env, n->hctx, n, r->a0, kb); }
void gm_pt_handoff_mtsh8r2_23(void *req, gm_cont k) { struct gm_pr_handoff_mtsh8r2_23 *r = (struct gm_pr_handoff_mtsh8r2_23 *)req; gm_node *n = r->n; gm_b16_8 kb; memcpy(&kb, &k, sizeof kb); ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint32_t, gm_b16_8))n->clauses[0])(n->env, n->hctx, n, r->a0, kb); }
void gm_pt_run_coroutines_0a7hn79_45(void *req, gm_cont k) { struct gm_pr_run_coroutines_0a7hn79_45 *r = (struct gm_pr_run_coroutines_0a7hn79_45 *)req; gm_node *n = r->n; gm_b16_8 kb; memcpy(&kb, &k, sizeof kb); ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint32_t, gm_b16_8))n->clauses[0])(n->env, n->hctx, n, r->a0, kb); }
void gm_wb_run_coroutines_0a7hn79_0(gm_fiber *fb) {
    struct gm_fr_run_coroutines_0a7hn79 *gm_Fp = (struct gm_fr_run_coroutines_0a7hn79 *)fb->frame;
    void *env = fb->env; gm_node *cx = fb->cx; gm_node *inst = fb->inst;
    (void)env; (void)cx; (void)inst;
#define F (*gm_Fp)
    {
        void * t26 = F.l0;
        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&F.l3) + 8);
        gm_f_set_stack_floor_f15muof(t26, t27);
    }
    {
        gm_closure t28 = F.l1;
        gm_closure t29 = t28;
        gm_node *ccx30 = t29.cx;
        gm_node cn31; { gm_node *f = gm_find(cx, 515083575); if (f) { cn31 = *f; cn31.next = ccx30; ccx30 = &cn31; } }
        gm_node cn32; { gm_node *f = gm_find(cx, 2063121243); if (f) { cn32 = *f; cn32.next = ccx30; ccx30 = &cn32; } }
        gm_node cn33; { gm_node *f = gm_find(cx, 621690230); if (f) { cn33 = *f; cn33.next = ccx30; ccx30 = &cn33; } }
        gm_node cn34; { gm_node *f = gm_find(cx, 587034433); if (f) { cn34 = *f; cn34.next = ccx30; ccx30 = &cn34; } }
        gm_node cn35; { gm_node *f = gm_find(cx, 1119945002); if (f) { cn35 = *f; cn35.next = ccx30; ccx30 = &cn35; } }
        gm_node cn36; { gm_node *f = gm_find(cx, 1579066750); if (f) { cn36 = *f; cn36.next = ccx30; ccx30 = &cn36; } }
        gm_node cn37; { gm_node *f = gm_find(cx, 1087599154); if (f) { cn37 = *f; cn37.next = ccx30; ccx30 = &cn37; } }
        gm_node cn38; { gm_node *f = gm_find(cx, 2126215853); if (f) { cn38 = *f; cn38.next = ccx30; ccx30 = &cn38; } }
        gm_node cn39; { gm_node *f = gm_find(cx, 2119230); if (f) { cn39 = *f; cn39.next = ccx30; ccx30 = &cn39; } }
        gm_node cn40; { gm_node *f = gm_find(cx, 734392322); if (f) { cn40 = *f; cn40.next = ccx30; ccx30 = &cn40; } }
        void * t41 = t29.code;
        gm_tljvbvj59adij t42;
        if (GM_PTR_TAGGED(t41)) {
            t42 = ((gm_tljvbvj59adij (*)(void))GM_PTR_UNTAG(t41))();
        } else {
            t42 = ((gm_tljvbvj59adij (*)(void *, gm_node *, gm_node *))t41)(t29.env, ccx30, NULL);
        }
        if (gm_unw.kind) {
            goto gm_fprop;
        }
        F.l4 = t42;
    }
    {
        uint32_t t43 = GM_LD(uint32_t, gm_ca9ikdfm7p4);
        gm_node *pn44 = gm_find(cx, 734392322);
        if (!pn44) gm_trap("no handler installed for effect `Coro`", gm_srcp0l171g2m7ho, 253, 9);
        struct gm_pr_run_coroutines_0a7hn79_45 pr46 = {pn44, t43};
        gm_ctl_perform(pn44->fw, gm_pt_run_coroutines_0a7hn79_45, &pr46, NULL);
        if (gm_unw.kind) {
            goto gm_fprop;
        }
    }
    gm_tljvbvj59adij t47 = F.l4;
    gm_tljvbvj59adij t48 = t47;
    F.w0 = t48;
gm_fprop:
    gm_fiber_finish(fb);
#undef F
}
