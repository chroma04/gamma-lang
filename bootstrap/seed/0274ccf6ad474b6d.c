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
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; } f1; } gm_t8c5h7u09n10a;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct gm_csedogq9e4rp5r { uint32_t f0; gm_cseahvg8v7p0vs f1; } gm_csedogq9e4rp5r;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_t88rcucg1b7uk f0; gm_t9qkh0rsooc8d f1; uint8_t pe[3]; } gm_t3jsf43p1pths;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 a[12]; } gm_t61mja4eihh2b;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t8c5h7u09n10a) == 16 && _Alignof(gm_t8c5h7u09n10a) == 8 && offsetof(gm_t8c5h7u09n10a, f0) == 0 && offsetof(gm_t8c5h7u09n10a, f1) == 8, "layout of `DeclState`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; } gm_t9d4sf0vqn7dt;
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tammvh1iespi3;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_tf5srh4mp4b3i f0; } gm_tba9024q2up98;
typedef struct { uint32_t f0; gm_tqs5p4n2tq19v f1; uint8_t f2; uint8_t f3; } gm_tbcgm40aljf9h;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_tl874t2atnlh3 f1; } gm_tbvif938ns15l;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
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
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_tl874t2atnlh3 f0; gm_t88rcucg1b7uk f1; } gm_trlabqsjjp0ds;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_csedogq9e4rp5r) == 16 && _Alignof(gm_csedogq9e4rp5r) == 4 && offsetof(gm_csedogq9e4rp5r, f0) == 0 && offsetof(gm_csedogq9e4rp5r, f1) == 4, "layout of `TraceCall`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3jsf43p1pths) == 8 && _Alignof(gm_t3jsf43p1pths) == 4 && offsetof(gm_t3jsf43p1pths, f0) == 0 && offsetof(gm_t3jsf43p1pths, f1) == 4, "layout of `Mention`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t61mja4eihh2b) == 192 && _Alignof(gm_t61mja4eihh2b) == 8 && offsetof(gm_t61mja4eihh2b, a) == 0, "layout of `[12]string`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
typedef struct { gm_tbvif938ns15l f0; uint8_t f1; uint8_t pe[3]; } gm_t84dv0cg48dut;
typedef struct { gm_tba9024q2up98 f0; uint8_t f1; uint8_t pe[7]; } gm_t89h15e2c5o3p;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9d4sf0vqn7dt) == 16 && _Alignof(gm_t9d4sf0vqn7dt) == 4 && offsetof(gm_t9d4sf0vqn7dt, f0) == 0 && offsetof(gm_t9d4sf0vqn7dt, f1) == 4, "layout of `TraceCall`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tammvh1iespi3) == 56 && _Alignof(gm_tammvh1iespi3) == 8 && offsetof(gm_tammvh1iespi3, f0) == 0 && offsetof(gm_tammvh1iespi3, f1) == 16 && offsetof(gm_tammvh1iespi3, f2) == 24 && offsetof(gm_tammvh1iespi3, f3) == 40, "layout of `EffectInfo`");
_Static_assert(sizeof(gm_tba9024q2up98) == 16 && _Alignof(gm_tba9024q2up98) == 8 && offsetof(gm_tba9024q2up98, f0) == 0, "layout of `AliasTarget`");
_Static_assert(sizeof(gm_tbcgm40aljf9h) == 8 && _Alignof(gm_tbcgm40aljf9h) == 4 && offsetof(gm_tbcgm40aljf9h, f0) == 0 && offsetof(gm_tbcgm40aljf9h, f1) == 4 && offsetof(gm_tbcgm40aljf9h, f2) == 6 && offsetof(gm_tbcgm40aljf9h, f3) == 7, "layout of `FnHint`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tbvif938ns15l) == 16 && _Alignof(gm_tbvif938ns15l) == 4 && offsetof(gm_tbvif938ns15l, f0) == 0 && offsetof(gm_tbvif938ns15l, f1) == 4, "layout of `(win64: bool, span: Span)`");
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
typedef struct { gm_tdgfduqjheqdm f0; uint8_t f1; uint8_t pe[7]; } gm_tdn4erebgflsv;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
typedef struct { gm_tiilmfd16v3fc a[1]; } gm_tfe2ufaqq8re9;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tbngattl1fo51 f0; uint8_t f1; uint8_t pe[3]; } gm_thetuhtouve7f;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; uint64_t f3; void * f4; gm_tkn42u7na3t81 f5; uint64_t f6; void * f7; gm_t9qkh0rsooc8d f8; uint8_t p9[7]; void * f9; } gm_tjcptv9sagfq8;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_t4c7ipjj9s3fp f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; } gm_tpcj7kul6hlib;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_t3jsf43p1pths f0; uint8_t f1; uint8_t pe[3]; } gm_trcagkhgm1qdu;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trlabqsjjp0ds) == 16 && _Alignof(gm_trlabqsjjp0ds) == 4 && offsetof(gm_trlabqsjjp0ds, f0) == 0 && offsetof(gm_trlabqsjjp0ds, f1) == 12, "layout of `(Span, Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t84dv0cg48dut) == 20 && _Alignof(gm_t84dv0cg48dut) == 4 && offsetof(gm_t84dv0cg48dut, f0) == 0 && offsetof(gm_t84dv0cg48dut, f1) == 16, "layout of `?(win64: bool, span: Span)`");
_Static_assert(sizeof(gm_t89h15e2c5o3p) == 24 && _Alignof(gm_t89h15e2c5o3p) == 8 && offsetof(gm_t89h15e2c5o3p, f0) == 0 && offsetof(gm_t89h15e2c5o3p, f1) == 16, "layout of `?AliasTarget`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
typedef struct { void * f0; gm_tpcj7kul6hlib f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; uint8_t f9; uint8_t p10[7]; gm_trqfvmjtt4sij f10; gm_trqfvmjtt4sij f11; uint8_t f12; uint8_t p13[7]; gm_trqfvmjtt4sij f13; gm_trqfvmjtt4sij f14; gm_trqfvmjtt4sij f15; gm_trqfvmjtt4sij f16; gm_trqfvmjtt4sij f17; gm_trqfvmjtt4sij f18; gm_trqfvmjtt4sij f19; gm_t9805qtonbsag f20; gm_trqfvmjtt4sij f21; gm_trqfvmjtt4sij f22; gm_trqfvmjtt4sij f23; gm_trqfvmjtt4sij f24; gm_trqfvmjtt4sij f25; gm_trqfvmjtt4sij f26; gm_t4c7ipjj9s3fp f27; gm_t4c7ipjj9s3fp f28; gm_trqfvmjtt4sij f29; uint32_t f30; uint8_t f31; uint8_t p32[3]; gm_trqfvmjtt4sij f32; gm_trqfvmjtt4sij f33; gm_trqfvmjtt4sij f34; gm_trqfvmjtt4sij f35; gm_trqfvmjtt4sij f36; gm_trqfvmjtt4sij f37; gm_trqfvmjtt4sij f38; gm_trqfvmjtt4sij f39; gm_trqfvmjtt4sij f40; gm_trqfvmjtt4sij f41; uint32_t f42; uint8_t p43[4]; gm_trqfvmjtt4sij f43; uint32_t f44; uint8_t p45[4]; gm_trqfvmjtt4sij f45; uint8_t f46; uint8_t p47[7]; gm_trqfvmjtt4sij f47; gm_trqfvmjtt4sij f48; uint32_t f49; gm_t9805qtonbsag f50; uint8_t p51[4]; gm_trqfvmjtt4sij f51; gm_t4c7ipjj9s3fp f52; gm_t4c7ipjj9s3fp f53; gm_trqfvmjtt4sij f54; uint32_t f55; uint8_t p56[4]; void * f56; void * f57; void * f58; gm_trqfvmjtt4sij f59; gm_trqfvmjtt4sij f60; gm_trqfvmjtt4sij f61; uint64_t f62; gm_t4c7ipjj9s3fp f63; gm_trqfvmjtt4sij f64; gm_trqfvmjtt4sij f65; gm_trqfvmjtt4sij f66; gm_trqfvmjtt4sij f67; uint8_t f68; uint8_t p69[7]; gm_trqfvmjtt4sij f69; gm_trqfvmjtt4sij f70; gm_t4c7ipjj9s3fp f71; gm_t4c7ipjj9s3fp f72; gm_t4c7ipjj9s3fp f73; } gm_tcrr6nmqqhjbj;
typedef struct { gm_tkacitepg5fk0 f0; uint8_t f1; uint8_t pe[7]; } gm_tcus0chk5r9uq;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdn4erebgflsv) == 72 && _Alignof(gm_tdn4erebgflsv) == 8 && offsetof(gm_tdn4erebgflsv, f0) == 0 && offsetof(gm_tdn4erebgflsv, f1) == 64, "layout of `?FnDef`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_tfe2ufaqq8re9) == 24 && _Alignof(gm_tfe2ufaqq8re9) == 8 && offsetof(gm_tfe2ufaqq8re9, a) == 0, "layout of `[1]EffectRef`");
_Static_assert(sizeof(gm_thetuhtouve7f) == 12 && _Alignof(gm_thetuhtouve7f) == 4 && offsetof(gm_thetuhtouve7f, f0) == 0 && offsetof(gm_thetuhtouve7f, f1) == 8, "layout of `?(inclusive: bool, elem: Ty)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tjcptv9sagfq8) == 88 && _Alignof(gm_tjcptv9sagfq8) == 8 && offsetof(gm_tjcptv9sagfq8, f0) == 0 && offsetof(gm_tjcptv9sagfq8, f1) == 16 && offsetof(gm_tjcptv9sagfq8, f2) == 28 && offsetof(gm_tjcptv9sagfq8, f3) == 32 && offsetof(gm_tjcptv9sagfq8, f4) == 40 && offsetof(gm_tjcptv9sagfq8, f5) == 48 && offsetof(gm_tjcptv9sagfq8, f6) == 56 && offsetof(gm_tjcptv9sagfq8, f7) == 64 && offsetof(gm_tjcptv9sagfq8, f8) == 72 && offsetof(gm_tjcptv9sagfq8, f9) == 80, "layout of `ConstDef`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpcj7kul6hlib) == 296 && _Alignof(gm_tpcj7kul6hlib) == 8 && offsetof(gm_tpcj7kul6hlib, f0) == 0 && offsetof(gm_tpcj7kul6hlib, f1) == 40 && offsetof(gm_tpcj7kul6hlib, f2) == 80 && offsetof(gm_tpcj7kul6hlib, f3) == 136 && offsetof(gm_tpcj7kul6hlib, f4) == 176 && offsetof(gm_tpcj7kul6hlib, f5) == 216 && offsetof(gm_tpcj7kul6hlib, f6) == 256, "layout of `Types`");
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trcagkhgm1qdu) == 12 && _Alignof(gm_trcagkhgm1qdu) == 4 && offsetof(gm_trcagkhgm1qdu, f0) == 0 && offsetof(gm_trcagkhgm1qdu, f1) == 8, "layout of `?(ty: Ty, vis: Visibility)`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tcrr6nmqqhjbj) == 2792 && _Alignof(gm_tcrr6nmqqhjbj) == 8 && offsetof(gm_tcrr6nmqqhjbj, f0) == 0 && offsetof(gm_tcrr6nmqqhjbj, f1) == 8 && offsetof(gm_tcrr6nmqqhjbj, f2) == 304 && offsetof(gm_tcrr6nmqqhjbj, f3) == 344 && offsetof(gm_tcrr6nmqqhjbj, f4) == 384 && offsetof(gm_tcrr6nmqqhjbj, f5) == 424 && offsetof(gm_tcrr6nmqqhjbj, f6) == 464 && offsetof(gm_tcrr6nmqqhjbj, f7) == 504 && offsetof(gm_tcrr6nmqqhjbj, f8) == 544 && offsetof(gm_tcrr6nmqqhjbj, f9) == 584 && offsetof(gm_tcrr6nmqqhjbj, f10) == 592 && offsetof(gm_tcrr6nmqqhjbj, f11) == 632 && offsetof(gm_tcrr6nmqqhjbj, f12) == 672 && offsetof(gm_tcrr6nmqqhjbj, f13) == 680 && offsetof(gm_tcrr6nmqqhjbj, f14) == 720 && offsetof(gm_tcrr6nmqqhjbj, f15) == 760 && offsetof(gm_tcrr6nmqqhjbj, f16) == 800 && offsetof(gm_tcrr6nmqqhjbj, f17) == 840 && offsetof(gm_tcrr6nmqqhjbj, f18) == 880 && offsetof(gm_tcrr6nmqqhjbj, f19) == 920 && offsetof(gm_tcrr6nmqqhjbj, f20) == 960 && offsetof(gm_tcrr6nmqqhjbj, f21) == 968 && offsetof(gm_tcrr6nmqqhjbj, f22) == 1008 && offsetof(gm_tcrr6nmqqhjbj, f23) == 1048 && offsetof(gm_tcrr6nmqqhjbj, f24) == 1088 && offsetof(gm_tcrr6nmqqhjbj, f25) == 1128 && offsetof(gm_tcrr6nmqqhjbj, f26) == 1168 && offsetof(gm_tcrr6nmqqhjbj, f27) == 1208 && offsetof(gm_tcrr6nmqqhjbj, f28) == 1264 && offsetof(gm_tcrr6nmqqhjbj, f29) == 1320 && offsetof(gm_tcrr6nmqqhjbj, f30) == 1360 && offsetof(gm_tcrr6nmqqhjbj, f31) == 1364 && offsetof(gm_tcrr6nmqqhjbj, f32) == 1368 && offsetof(gm_tcrr6nmqqhjbj, f33) == 1408 && offsetof(gm_tcrr6nmqqhjbj, f34) == 1448 && offsetof(gm_tcrr6nmqqhjbj, f35) == 1488 && offsetof(gm_tcrr6nmqqhjbj, f36) == 1528 && offsetof(gm_tcrr6nmqqhjbj, f37) == 1568 && offsetof(gm_tcrr6nmqqhjbj, f38) == 1608 && offsetof(gm_tcrr6nmqqhjbj, f39) == 1648 && offsetof(gm_tcrr6nmqqhjbj, f40) == 1688 && offsetof(gm_tcrr6nmqqhjbj, f41) == 1728 && offsetof(gm_tcrr6nmqqhjbj, f42) == 1768 && offsetof(gm_tcrr6nmqqhjbj, f43) == 1776 && offsetof(gm_tcrr6nmqqhjbj, f44) == 1816 && offsetof(gm_tcrr6nmqqhjbj, f45) == 1824 && offsetof(gm_tcrr6nmqqhjbj, f46) == 1864 && offsetof(gm_tcrr6nmqqhjbj, f47) == 1872 && offsetof(gm_tcrr6nmqqhjbj, f48) == 1912 && offsetof(gm_tcrr6nmqqhjbj, f49) == 1952 && offsetof(gm_tcrr6nmqqhjbj, f50) == 1956 && offsetof(gm_tcrr6nmqqhjbj, f51) == 1968 && offsetof(gm_tcrr6nmqqhjbj, f52) == 2008 && offsetof(gm_tcrr6nmqqhjbj, f53) == 2064 && offsetof(gm_tcrr6nmqqhjbj, f54) == 2120 && offsetof(gm_tcrr6nmqqhjbj, f55) == 2160 && offsetof(gm_tcrr6nmqqhjbj, f56) == 2168 && offsetof(gm_tcrr6nmqqhjbj, f57) == 2176 && offsetof(gm_tcrr6nmqqhjbj, f58) == 2184 && offsetof(gm_tcrr6nmqqhjbj, f59) == 2192 && offsetof(gm_tcrr6nmqqhjbj, f60) == 2232 && offsetof(gm_tcrr6nmqqhjbj, f61) == 2272 && offsetof(gm_tcrr6nmqqhjbj, f62) == 2312 && offsetof(gm_tcrr6nmqqhjbj, f63) == 2320 && offsetof(gm_tcrr6nmqqhjbj, f64) == 2376 && offsetof(gm_tcrr6nmqqhjbj, f65) == 2416 && offsetof(gm_tcrr6nmqqhjbj, f66) == 2456 && offsetof(gm_tcrr6nmqqhjbj, f67) == 2496 && offsetof(gm_tcrr6nmqqhjbj, f68) == 2536 && offsetof(gm_tcrr6nmqqhjbj, f69) == 2544 && offsetof(gm_tcrr6nmqqhjbj, f70) == 2584 && offsetof(gm_tcrr6nmqqhjbj, f71) == 2624 && offsetof(gm_tcrr6nmqqhjbj, f72) == 2680 && offsetof(gm_tcrr6nmqqhjbj, f73) == 2736, "layout of `Sema`");
_Static_assert(sizeof(gm_tcus0chk5r9uq) == 80 && _Alignof(gm_tcus0chk5r9uq) == 8 && offsetof(gm_tcus0chk5r9uq, f0) == 0 && offsetof(gm_tcus0chk5r9uq, f1) == 72, "layout of `?Attribute`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t08olq97rscn3 f0; uint64_t f1; gm_tvpnktv5fo8nl f2; gm_t8c5h7u09n10a f3; } gm_t52j636u3vgk3;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t52j636u3vgk3) == 216 && _Alignof(gm_t52j636u3vgk3) == 8 && offsetof(gm_t52j636u3vgk3, f0) == 0 && offsetof(gm_t52j636u3vgk3, f1) == 32 && offsetof(gm_t52j636u3vgk3, f2) == 40 && offsetof(gm_t52j636u3vgk3, f3) == 200, "layout of `Decl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
struct gm_fr_add_const_8u8b1ja {
    gm_frame h;
    gm_tjcptv9sagfq8 l1; /* c */
    uint32_t ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_adt_field_tys_oggq5ji {
    gm_frame h;
    gm_tpautr4br7h3l l4; /* def */
    gm_tckdobunaulee l9; /* v */
    gm_b0_1 ret;
};
struct gm_fr_adt_name_h7mjik7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_aliased_decl_jv6ilmn {
    gm_frame h;
    gm_t89h15e2c5o3p ret;
};
struct gm_fr_check_allow_attr_3321gm7 {
    gm_frame h;
    gm_tc3oht4ksth9d l5; /* a */
    gm_b0_1 ret;
};
struct gm_fr_check_method_candidates_k08rvu6 {
    gm_frame h;
    gm_t4c7ipjj9s3fp l1; /* seen */
    gm_t79rbdv4rafe5 l4; /* c */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_b0_1 ret;
};
struct gm_fr_check_pub_signatures_plkds6r {
    gm_frame h;
    gm_t6qooom8qa2do l6; /* v */
    gm_trqfvmjtt4sij l7; /* tys */
    gm_t5v0c8ia6a5mo l12; /* p */
    gm_tv0k2vg3054a6 l16; /* op */
    gm_t5v0c8ia6a5mo l19; /* p */
    gm_b0_1 ret;
};
struct gm_fr_clear_ufjejrf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_complex_of_kh32tib {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_compute_binding_cl3ogob {
    gm_frame h;
    gm_t4gut2c9fj1kt l5; /* e */
    gm_t5vbkdq81qfs7 l17; /* r */
    gm_t0ialelq3ql1k l18; /* v */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_compute_decl_gcf8mq9 {
    gm_frame h;
    gm_t52j636u3vgk3 l2; /* decl */
    gm_tvpnktv5fo8nl l3; /* item */
    gm_tv2uf4leil5hq l6; /* b */
    gm_tcus0chk5r9uq l10; /* hints */
    gm_tcus0chk5r9uq l13; /* align_attr */
    gm_t1s75i08qtptd l14; /* export */
    gm_tkacitepg5fk0 l19; /* attr */
    gm_t5vbkdq81qfs7 l22; /* r */
    gm_tbci5ospk5nnn l23; /* i */
    gm_tkacitepg5fk0 l34; /* a */
    gm_t6qooom8qa2do l35; /* v */
    gm_tkacitepg5fk0 l40; /* h */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_const_id_2mlh0og {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_declare_adt_iefkbkk {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_declare_const_or_type_4jaj4k6 {
    gm_frame h;
    gm_t6qooom8qa2do l7; /* v */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_declare_method_candidate_e20bo1i {
    gm_frame h;
    gm_tpautr4br7h3l l7; /* def */
    gm_t0mglksrkfa5e l10; /* f */
    gm_b0_1 ret;
};
struct gm_fr_def_of_m512sbu {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_effect_arg_tys_2a7r8hh {
    gm_frame h;
    gm_toa2vhe3l4hfr l8; /* a */
    gm_b0_1 ret;
};
struct gm_fr_effect_terms_bphj7th {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1fu8oa9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1mrcnd6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1scem7m {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_2k4643t {
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
struct gm_fr_empty_slice_7adh53f {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7od16il {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8nf04m8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_95isii0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_a6or8fm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_c3qnk0a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_cp9r5v5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d0lhrvs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d3uqusp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dq2isu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_e7jr93v {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_eoluluh {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f1ngfj0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f6g91fe {
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
struct gm_fr_empty_slice_g6fvoq5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hii1vte {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hp0e9gl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ipqblac {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_iqgue92 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_j0utaj4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_j6dmic1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_j98q5u1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jpu5akn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kgfehvr {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lh8ov0e {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lp0ck0u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lpero57 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_mbkocse {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_nqsj3l3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oqjc4ke {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_osbc35j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ot5ttb7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p6mn4us {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_pu15hdv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_q870hn3 {
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
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s6l2ctu {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_sc53cu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_shtdeku {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tslvjbf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uq9fi1n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_v0dtga2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_field_default_expr_tqfuiff {
    gm_frame h;
    void * ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_bfunt71 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_hcjjkrm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_repdalk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_frame_up_h0ddp1d {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_generic_of_1861hm9 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_0vd239u {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_1so3e0v {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_84luf3l {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_8lre197 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_gsp2k0o {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_iibum07 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_iug9p19 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_ve01i1t {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hidden_part_is5hgf9 {
    gm_frame h;
    void * l0; /* s */
    gm_t9qkh0rsooc8d l2; /* need */
    gm_trcagkhgm1qdu l3; /* found */
    gm_trcagkhgm1qdu ret;
};
struct gm_fr_in_generic_scope_rvb92ab {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ambient_m41pl1e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_positive_su660ab {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_structural_alias_qpmv4mu {
    gm_frame h;
    gm_tv2uf4leil5hq l3; /* b */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_0bg6tcp {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0bj26n7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0npefvp {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1jabh22 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1teief7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_2343nn9 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_406coeg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_4kh12ib {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5pborc0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_8r7pelg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_9q64p2g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_atkrn3m {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_au01ire {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_c02589n {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_dql1gah {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_dvbol2p {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_dvp9pla {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_eb5ua5r {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fam8ktt {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_h93u1dq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_i6iqfhd {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ib5k4gb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_je8tl46 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_kk91efj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_l3jkoh2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_m7es0sh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_mvhhv5u {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qs7fohu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qv7bq94 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_sajcold {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_t29l5ca {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_tn37ahu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_tqcq99u {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ue2t2bm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uun92du {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v3cpbrb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vhqj1o4 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_non_constant_part_itk7b0v {
    gm_frame h;
    void * l0; /* s */
    gm_tkn42u7na3t81 l2; /* bad */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_pop_j4rpvm5 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_prelude_effect_gilrmm5 {
    gm_frame h;
    gm_tqu3m2l3n8mnc ret;
};
struct gm_fr_prelude_instance_arg_kh32dmf {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_prelude_template_7s9nsp0 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_0fe09r3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_4p3ivmb {
    gm_frame h;
    gm_tq7qjpkmqmc5s l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_89d0k90 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_8cr0sgp {
    gm_frame h;
    gm_tjcptv9sagfq8 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_field_tys_v4ep4t3 {
    gm_frame h;
    gm_t0mglksrkfa5e l5; /* f */
    gm_b0_1 ret;
};
struct gm_fr_push_fomabnf {
    gm_frame h;
    gm_t79rbdv4rafe5 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_h37silv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kpjuj6c {
    gm_frame h;
    gm_tagrkbqh054h7 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_puil3ql {
    gm_frame h;
    gm_tammvh1iespi3 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_range_of_dmmo768 {
    gm_frame h;
    gm_thetuhtouve7f ret;
};
struct gm_fr_range_template_dbt7218 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_15b7omr {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4s59dpv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5jtvbud {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8fvt97b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_c536n99 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_iloqstk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4lehgc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_tk5tca7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_cycle_mauubd4 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* names */
    gm_t4gut2c9fj1kt l9; /* d */
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_ambient_6orl0vo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_decl_pus042v {
    gm_frame h;
    gm_t6qooom8qa2do l2; /* v */
    gm_t6qooom8qa2do l3; /* v */
    gm_trqfvmjtt4sij l6; /* type_params */
    gm_trqfvmjtt4sij l7; /* value_scope */
    gm_trqfvmjtt4sij l8; /* local_consts */
    gm_trqfvmjtt4sij l9; /* local_fns */
    gm_t6qooom8qa2do l10; /* v */
    gm_t6qooom8qa2do l11; /* r */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_resolve_declarations_vrpg1fl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sema_r1nlnnm {
    gm_frame h;
    gm_tcrr6nmqqhjbj l1; /* s */
    gm_tcrr6nmqqhjbj ret;
};
struct gm_fr_set_inst_def_tcpabg0 {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_b0_1 ret;
};
struct gm_fr_set_state_3j300fi {
    gm_frame h;
    gm_tagrkbqh054h7 l2; /* v */
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
struct gm_fr_state_a5bnsjb {
    gm_frame h;
    gm_tagrkbqh054h7 ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
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
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unsupported_t7lgvp9 {
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
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b);
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint32_t gm_f_add_const_8u8b1ja(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_adt_field_tys_oggq5ji(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2, void * a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_aliased_decl_jv6ilmn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_apply_callconv_197l87p(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint8_t a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_attr_power_of_two_gj02n2t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_callconv_attr_dd08ovc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_allow_attr_3321gm7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_import_cycles_2u7oqo0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_method_candidates_k08rvu6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_pub_signatures_plkds6r(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_ufjejrf(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_terms_n8okg7m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_compute_binding_cl3ogob(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_compute_decl_gcf8mq9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_check_d48mnu7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_type_7rijs95(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_in_74tov03(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4, uint8_t a5);
__attribute__((aligned(4))) void gm_f_declare_const_or_type_4jaj4k6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_declare_effect_9n12hc8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_declare_fn_6piolnr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_declare_foreign_6s86jje(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_declare_global_729nttf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_method_candidate_e20bo1i(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tdgfduqjheqdm gm_f_def_of_m512sbu_c(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_deinit_2l7c12h(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_arg_tys_2a7r8hh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_terms_bphj7th(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1mrcnd6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1scem7m(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2k4643t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7adh53f(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7od16il(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8nf04m8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_95isii0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_a6or8fm(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3qnk0a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cp9r5v5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d0lhrvs(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dq2isu2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e7jr93v(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_eoluluh(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f1ngfj0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g6fvoq5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hii1vte(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hp0e9gl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ipqblac(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_iqgue92(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j0utaj4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j6dmic1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j98q5u1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jpu5akn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kgfehvr(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lh8ov0e(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lp0ck0u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpero57(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mbkocse(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nqsj3l3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oqjc4ke(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_osbc35j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ot5ttb7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p6mn4us(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pu15hdv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q870hn3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s6l2ctu(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_sc53cu2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_shtdeku(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tslvjbf(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uq9fi1n(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_v0dtga2(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_export_fn_8hcuasc(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_field_default_expr_tqfuiff(void * a0, uint32_t a1, uint64_t a2);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfunt71(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_hcjjkrm(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_repdalk(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_for_each_component_o5cuh7c(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_0dvj28f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_28p84ds(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ck7vger(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_j4sks15(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_slm5ovr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t5fmuah(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_u2mas3v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_v0s3sc7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_frame_up_h0ddp1d(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_mk00hfd(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pk8ncss(void * a0, gm_trlabqsjjp0ds a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_get_qjsv97t(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_splice_80pgqas(gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_0vd239u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_1so3e0v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_84luf3l(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_8lre197(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_gsp2k0o(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_iibum07(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_iug9p19(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_ve01i1t(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_trcagkhgm1qdu gm_f_hidden_part_is5hgf9(void * a0, gm_csguankko4lmhd a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) uint8_t gm_f_in_generic_scope_rvb92ab(void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_s9trr09(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_ambient_m41pl1e(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_effect_sum_dd9le16(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_positive_su660ab(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_structural_alias_qpmv4mu(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_0bg6tcp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0npefvp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1teief7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_2343nn9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_406coeg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_4kh12ib(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5pborc0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_8r7pelg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_9q64p2g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_atkrn3m(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_au01ire(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_c02589n(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_dql1gah(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_dvbol2p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_dvp9pla(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_eb5ua5r(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fam8ktt(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_h93u1dq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_i6iqfhd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ib5k4gb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_je8tl46(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_kk91efj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_l3jkoh2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_m7es0sh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_mvhhv5u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qs7fohu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qv7bq94(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_sajcold(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_t29l5ca(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_tn37ahu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_tqcq99u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ue2t2bm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uun92du(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v3cpbrb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vhqj1o4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_non_constant_part_itk7b0v(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0);
__attribute__((aligned(4))) void gm_f_prelude_effect_gilrmm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_prelude_instance_arg_kh32dmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_prelude_template_7s9nsp0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0fe09r3(void * a0, gm_tbcgm40aljf9h a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_4p3ivmb(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_89d0k90(void * a0, gm_csedogq9e4rp5r a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_8cr0sgp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_field_tys_v4ep4t3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_fomabnf(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h37silv(void * a0, gm_t3jsf43p1pths a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kpjuj6c(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_puil3ql(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3pvr70r(void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_pjhbiaq(void * a0, const void * a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qrf6f79(void * a0, gm_trlabqsjjp0ds a1, uint32_t a2);
__attribute__((aligned(4))) gm_thetuhtouve7f gm_f_range_of_dmmo768(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_range_template_dbt7218(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_15b7omr(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4s59dpv(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5jtvbud(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8fvt97b(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_c536n99(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_iloqstk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4lehgc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_tk5tca7(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint64_t gm_f_remove_om8q0q2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_cycle_mauubd4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_ambient_6orl0vo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_declarations_vrpg1fl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_resolve_effect_ref_bggirs2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_set_o7rc5od(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_import_944uqa9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_sema_r1nlnnm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_trap_hook_fn_5c0a4ds(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_types_8a61ga3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_utf8_valid_a8oc1t8(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2);
extern uint8_t gm_c0u09tvsmdv[1] __attribute__((aligned(1)));
extern uint8_t gm_c1n574q3spi[8] __attribute__((aligned(8)));
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_cbbjgr0cpr9[8] __attribute__((aligned(8)));
extern uint8_t gm_cp0okt01b9q[192] __attribute__((aligned(8)));
extern const uint8_t gm_s0rphlcbno91v[8];
extern const uint8_t gm_s1ft2nrvnhcbp[10];
extern const uint8_t gm_s2ph9f020upd2[7];
extern const uint8_t gm_s2urait735ofq[5];
extern const uint8_t gm_s43cq4krigs2b[10];
extern const uint8_t gm_s4eilu97sooh1[10];
extern const uint8_t gm_s523ivho5m3dq[13];
extern const uint8_t gm_s68uiiatavvqh[18];
extern const uint8_t gm_s6ibuqseh9tt7[152];
extern const uint8_t gm_s6lnp3h2ojl1f[77];
extern const uint8_t gm_s6vvtrmtnt3j1[5];
extern const uint8_t gm_s7f3pdsji9g3a[9];
extern const uint8_t gm_s80p0ld1j6rfg[10];
extern const uint8_t gm_s8n5rdam5o1sm[69];
extern const uint8_t gm_s8ro53eq54s60[44];
extern const uint8_t gm_s9e3360r5ee0o[7];
extern const uint8_t gm_s9ocqto6ckbos[58];
extern const uint8_t gm_sbqv06vnui8sl[88];
extern const uint8_t gm_sbrnv8n0dpinb[8];
extern const uint8_t gm_scacg345bm166[69];
extern const uint8_t gm_scllp0lpbljde[15];
extern const uint8_t gm_sdbvaa960ee0f[5];
extern const uint8_t gm_ses7tdrdt8a6u[6];
extern const uint8_t gm_seue7mq07v0oc[6];
extern const uint8_t gm_sg7i6m1frjk5n[63];
extern const uint8_t gm_shge5n743ua4h[3];
extern const uint8_t gm_shofa8pdml9fb[13];
extern const uint8_t gm_shud0j6jb8ucc[37];
extern const uint8_t gm_sinoph5a0op58[10];
extern const uint8_t gm_sj1k4t2de5144[6];
extern const uint8_t gm_sj2f7h0km68ut[32];
extern const uint8_t gm_sj3lfuqd4f1np[37];
extern const uint8_t gm_sjqs7q3ostqbm[4];
extern const uint8_t gm_slb6cj7ndgvdm[6];
extern const uint8_t gm_smo0niiqr9onn[11];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_spe8t3e79vnal[7];
extern const uint8_t gm_sppcsr92ukfol[64];
extern const uint8_t gm_spq5ja4c7ciga[11];
extern const uint8_t gm_sptnm8c6c52dr[7];
extern const uint8_t gm_spvg0rt0dloah[9];
extern const uint8_t gm_sq4sf3euttrc8[5];
extern const uint8_t gm_srb1158jlk9u7[75];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srjo6vku5gka5[20];
extern const uint8_t gm_srsiesc4al3v4[50];
extern const uint8_t gm_ss6g2koqn63bf[5];
extern const uint8_t gm_st027n235d4us[45];
extern const uint8_t gm_st6dd0k5nckh3[58];
extern const uint8_t gm_stjei6jfiba56[5];
extern const uint8_t gm_str26682vod5l[38];
extern const uint8_t gm_suuv0qpi4v4e4[8];
extern const uint8_t gm_sv48vll3qortv[40];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b) {
    if (a[8] != b[8]) return false;
    return !a[8] || gm_eq6groq37ram(a, b);
}
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 114u);
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
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcgfglnv8p059r, 92, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_name_h7mjik7 F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 136) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 268, 59);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 136) + 0)) + t3 * 16));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_clear_ufjejrf(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_ufjejrf F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        void * t5 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t3) + 0)));
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 140, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_complex_of_kh32tib F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0rphlcbno91v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_tkn42u7na3t81 t4 = gm_f_prelude_instance_arg_kh32dmf((void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_const_id_2mlh0og F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 2008);
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_trlabqsjjp0ds t5;
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 12), t4);
    gm_t9805qtonbsag t6 = gm_f_get_pk8ncss(t2, t5);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_def_of_m512sbu F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 2064);
        uint32_t t5 = gm_l1;
        gm_tdn4erebgflsv t6;
        gm_f_get_mk00hfd((void *)&t6, t4, t5);
        if (!((((uint8_t *)&t6))[64] != 0)) goto ie2;
        F.l2 = (*(gm_tdgfduqjheqdm *)(((uint8_t *)&t6) + 0));
        {
            memmove(gm_out, ((uint8_t *)&F.l2), 64);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t8 = gm_l1;
        uint64_t t9 = (uint64_t)t8;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 384) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srclv0dcbbfhves, 581, 12);
        memmove(gm_out, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t11) + 384) + 0)) + t10 * 64), 64);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_terms_bphj7th(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_effect_terms_bphj7th F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_qs7fohu((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t4 = gm_f_collect_terms_n8okg7m((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t6 = 0;
        F.ret = t5;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1fu8oa9 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1mrcnd6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1mrcnd6 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1scem7m(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1scem7m F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2k4643t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2k4643t F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7adh53f(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7adh53f F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7od16il(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7od16il F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 96)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8nf04m8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8nf04m8 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_95isii0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_95isii0 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_a6or8fm(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_a6or8fm F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 20)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3qnk0a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c3qnk0a F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cp9r5v5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_cp9r5v5 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d0lhrvs(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d0lhrvs F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d3uqusp F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 28)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dq2isu2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dq2isu2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e7jr93v(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_e7jr93v F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_eoluluh(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_eoluluh F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f1ngfj0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f1ngfj0 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f6g91fe F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 224)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g6fvoq5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g6fvoq5 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hii1vte(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hii1vte F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hp0e9gl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hp0e9gl F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ipqblac(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ipqblac F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 216)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_iqgue92(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_iqgue92 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j0utaj4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_j0utaj4 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j6dmic1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_j6dmic1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j98q5u1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_j98q5u1 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jbu9fo3 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jpu5akn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jpu5akn F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kgfehvr(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kgfehvr F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lh8ov0e(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lh8ov0e F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 72)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lp0ck0u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lp0ck0u F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpero57(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lpero57 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mbkocse(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_mbkocse F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nqsj3l3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_nqsj3l3 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 104)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oqjc4ke(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oqjc4ke F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 184)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_osbc35j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_osbc35j F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ot5ttb7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ot5ttb7 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 136)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p6mn4us(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p6mn4us F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pu15hdv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_pu15hdv F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 20)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q870hn3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_q870hn3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qmcc4fe F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 12)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s6l2ctu(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s6l2ctu F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_sc53cu2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_sc53cu2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 12)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_shtdeku(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_shtdeku F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tslvjbf(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tslvjbf F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uq9fi1n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_uq9fi1n F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_v0dtga2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_v0dtga2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfunt71(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_bfunt71 F;
    gm_t88rcucg1b7uk gm_l0; /* c */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_walk_v858co7 *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t3 = gm_l0;
        gm_closure t4 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_walk_v858co7 *)((&F.h)->up))->l2));
        gm_f_walk_v858co7(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
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
inline __attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_generic_of_1861hm9 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tiilmfd16v3fc gm_l2; /* inst */
    GM_STACK_CHECK_SHALLOW(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t3 = gm_l1;
        uint64_t t4 = (uint64_t)t3;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 344) + 0) + 8);
        if (t5 >= (uintptr_t)(t7)) gm_trap_index(t5, t7, gm_srclv0dcbbfhves, 590, 16);
        if (!(((((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t5 * 136) + 96))[24] != 0)) goto ie2;
        gm_tiilmfd16v3fc t8 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t5 * 136) + 96));
        gm_l2 = t8;
        {
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t12 = gm_l1;
        uint8_t t13 = 0;
        F.ret = t12;
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
inline __attribute__((aligned(4))) void gm_f_hash_map_0vd239u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_0vd239u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_j98q5u1();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_1so3e0v(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_1so3e0v F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_95isii0();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_84luf3l(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_84luf3l F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_jpu5akn();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_8lre197(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_8lre197 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_sc53cu2();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_gsp2k0o(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_gsp2k0o F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_7adh53f();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_iibum07(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_iibum07 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_lh8ov0e();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_iug9p19(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_iug9p19 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_mbkocse();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_ve01i1t(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_ve01i1t F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_pu15hdv();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) gm_trcagkhgm1qdu gm_f_hidden_part_is5hgf9(void * a0, gm_csguankko4lmhd a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hidden_part_is5hgf9 F;
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    F.l2 = a2;
    {
        gm_trcagkhgm1qdu t2;
        memset(&t2, 0, sizeof t2);
        F.l3 = t2;
    }
    {
        void * t3 = F.l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_closure t5 = ((gm_closure){(void *)gm_f_fn_repdalk, (void *)&F.h, cx});
        gm_f_walk_v858co7(t3, GM_CONV(gm_csguankko4lmhd, t4), t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_trcagkhgm1qdu t6 = F.l3;
        uint8_t t7 = 0;
        F.ret = t6;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)26ULL)));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_positive_su660ab(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_positive_su660ab F;
    GM_STACK_CHECK_SHALLOW(&F, 14u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)a0) + 8) + 0) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        t1 = t5;
    } else t1 = 0;
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
inline __attribute__((aligned(4))) void gm_f_list_0bg6tcp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0bg6tcp F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_kgfehvr();
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
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0bj26n7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_qmcc4fe();
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
inline __attribute__((aligned(4))) void gm_f_list_0npefvp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0npefvp F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_dq2isu2();
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
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1jabh22 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_f6g91fe();
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
inline __attribute__((aligned(4))) void gm_f_list_1teief7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1teief7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1mrcnd6();
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
inline __attribute__((aligned(4))) void gm_f_list_2343nn9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_2343nn9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_eoluluh();
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
inline __attribute__((aligned(4))) void gm_f_list_406coeg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_406coeg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_j0utaj4();
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
inline __attribute__((aligned(4))) void gm_f_list_4kh12ib(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_4kh12ib F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s6l2ctu();
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
inline __attribute__((aligned(4))) void gm_f_list_5pborc0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5pborc0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_nqsj3l3();
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
inline __attribute__((aligned(4))) void gm_f_list_8r7pelg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_8r7pelg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_a6or8fm();
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
inline __attribute__((aligned(4))) void gm_f_list_9q64p2g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_9q64p2g F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_v0dtga2();
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
inline __attribute__((aligned(4))) void gm_f_list_atkrn3m(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_atkrn3m F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_g6fvoq5();
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
inline __attribute__((aligned(4))) void gm_f_list_c02589n(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_c02589n F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_e7jr93v();
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
inline __attribute__((aligned(4))) void gm_f_list_dql1gah(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_dql1gah F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_cp9r5v5();
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
inline __attribute__((aligned(4))) void gm_f_list_dvbol2p(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_dvbol2p F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1fu8oa9();
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
inline __attribute__((aligned(4))) void gm_f_list_dvp9pla(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_dvp9pla F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ot5ttb7();
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
inline __attribute__((aligned(4))) void gm_f_list_eb5ua5r(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_eb5ua5r F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ipqblac();
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
inline __attribute__((aligned(4))) void gm_f_list_fam8ktt(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fam8ktt F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_iqgue92();
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
inline __attribute__((aligned(4))) void gm_f_list_h93u1dq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_h93u1dq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_oqjc4ke();
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
inline __attribute__((aligned(4))) void gm_f_list_i6iqfhd(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_i6iqfhd F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lpero57();
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
inline __attribute__((aligned(4))) void gm_f_list_ib5k4gb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ib5k4gb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_d3uqusp();
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
inline __attribute__((aligned(4))) void gm_f_list_je8tl46(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_je8tl46 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_shtdeku();
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
inline __attribute__((aligned(4))) void gm_f_list_kk91efj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_kk91efj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_hp0e9gl();
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
inline __attribute__((aligned(4))) void gm_f_list_l3jkoh2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_l3jkoh2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8nf04m8();
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
inline __attribute__((aligned(4))) void gm_f_list_m7es0sh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_m7es0sh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_c3qnk0a();
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
inline __attribute__((aligned(4))) void gm_f_list_mvhhv5u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_mvhhv5u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_osbc35j();
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
inline __attribute__((aligned(4))) void gm_f_list_qs7fohu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qs7fohu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lp0ck0u();
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
inline __attribute__((aligned(4))) void gm_f_list_qv7bq94(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qv7bq94 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_hii1vte();
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
inline __attribute__((aligned(4))) void gm_f_list_sajcold(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_sajcold F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_tslvjbf();
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
inline __attribute__((aligned(4))) void gm_f_list_t29l5ca(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_t29l5ca F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_2k4643t();
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
inline __attribute__((aligned(4))) void gm_f_list_tn37ahu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_tn37ahu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_q870hn3();
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
inline __attribute__((aligned(4))) void gm_f_list_tqcq99u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_tqcq99u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_j6dmic1();
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
inline __attribute__((aligned(4))) void gm_f_list_ue2t2bm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ue2t2bm F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_d0lhrvs();
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
inline __attribute__((aligned(4))) void gm_f_list_uun92du(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uun92du F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_uq9fi1n();
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
inline __attribute__((aligned(4))) void gm_f_list_v3cpbrb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v3cpbrb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1scem7m();
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
inline __attribute__((aligned(4))) void gm_f_list_vhqj1o4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vhqj1o4 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_7od16il();
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_non_constant_part_itk7b0v(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_non_constant_part_itk7b0v F;
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        gm_tkn42u7na3t81 t2;
        memset(&t2, 0, sizeof t2);
        F.l2 = t2;
    }
    {
        void * t3 = F.l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_closure t5 = ((gm_closure){(void *)gm_f_fn_hcjjkrm, (void *)&F.h, cx});
        gm_f_walk_v858co7(t3, GM_CONV(gm_csguankko4lmhd, t4), t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tkn42u7na3t81 t6 = F.l2;
        uint8_t t7 = 0;
        F.ret = t6;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_j4rpvm5 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* x */
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
            gm_tf5srh4mp4b3i t8;
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
        uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 8));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        uint64_t t29 = gm_l2;
        gm_tf5srh4mp4b3i t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[8] = 1;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0fe09r3(void * a0, gm_tbcgm40aljf9h a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0fe09r3 F;
    void * gm_l0; /* l */
    gm_tbcgm40aljf9h gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_uq9fi1n();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_c536n99(t10, ((uint64_t)1ULL), t12);
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
        gm_tbcgm40aljf9h t21 = gm_l1;
        GM_ST(gm_tbcgm40aljf9h, ((uint8_t *)t20 + t18 * 8), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_4p3ivmb(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_4p3ivmb F;
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
            gm_b16_8 t11 = gm_f_empty_slice_nqsj3l3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_tk5tca7(t10, ((uint64_t)1ULL), t12);
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
        gm_tq7qjpkmqmc5s t21 = GM_LD(gm_tq7qjpkmqmc5s, ((uint8_t *)a1));
        GM_ST(gm_tq7qjpkmqmc5s, ((uint8_t *)t20 + t18 * 104), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 104)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_89d0k90(void * a0, gm_csedogq9e4rp5r a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_89d0k90 F;
    void * gm_l0; /* l */
    gm_t9d4sf0vqn7dt gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t9d4sf0vqn7dt, a1);
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
            gm_b16_8 t11 = gm_f_empty_slice_j6dmic1();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_5jtvbud(t10, ((uint64_t)1ULL), t12);
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
        gm_t9d4sf0vqn7dt t21 = gm_l1;
        GM_ST(gm_t9d4sf0vqn7dt, ((uint8_t *)t20 + t18 * 16), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_8cr0sgp(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_8cr0sgp F;
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
            gm_b16_8 t11 = gm_f_empty_slice_8nf04m8();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_15b7omr(t10, ((uint64_t)1ULL), t12);
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
        gm_tjcptv9sagfq8 t21 = GM_LD(gm_tjcptv9sagfq8, ((uint8_t *)a1));
        GM_ST(gm_tjcptv9sagfq8, ((uint8_t *)t20 + t18 * 88), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 88)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_fomabnf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_fomabnf F;
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
            gm_b16_8 t11 = gm_f_empty_slice_1scem7m();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_4s59dpv(t10, ((uint64_t)1ULL), t12);
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
        gm_t79rbdv4rafe5 t21 = GM_LD(gm_t79rbdv4rafe5, ((uint8_t *)a1));
        GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)t20 + t18 * 40), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 40)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h37silv(void * a0, gm_t3jsf43p1pths a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_h37silv F;
    void * gm_l0; /* l */
    gm_t3jsf43p1pths gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_iqgue92();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_8fvt97b(t10, ((uint64_t)1ULL), t12);
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
        gm_t3jsf43p1pths t21 = gm_l1;
        GM_ST(gm_t3jsf43p1pths, ((uint8_t *)t20 + t18 * 8), t21);
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
            gm_b16_8 t11 = gm_f_empty_slice_jbu9fo3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_8skjmj4(t10, ((uint64_t)1ULL), t12);
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
        gm_tbgd14esf0jvi t21 = GM_LD(gm_tbgd14esf0jvi, ((uint8_t *)a1));
        GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)t20 + t18 * 32), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 32)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kpjuj6c(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kpjuj6c F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_ipqblac();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_j4lehgc(t10, ((uint64_t)1ULL), t12);
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
        gm_tagrkbqh054h7 t21 = F.l1;
        GM_ST(gm_tagrkbqh054h7, ((uint8_t *)t20 + t18 * 216), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 216)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_puil3ql(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_puil3ql F;
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
            gm_b16_8 t11 = gm_f_empty_slice_eoluluh();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_iloqstk(t10, ((uint64_t)1ULL), t12);
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
        gm_tammvh1iespi3 t21 = GM_LD(gm_tammvh1iespi3, ((uint8_t *)a1));
        GM_ST(gm_tammvh1iespi3, ((uint8_t *)t20 + t18 * 56), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 56)));
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_range_template_dbt7218(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_range_template_dbt7218 F;
    void * gm_l0; /* s */
    uint8_t gm_l1; /* inclusive */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    uint8_t t4 = gm_l1;
    if (!t4) goto ie3;
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s43cq4krigs2b));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(9));
    gm_b16_8 t7 = t6;
    t2 = t7;
    goto ix3;
ie3:;
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_seue7mq07v0oc));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(5));
    gm_b16_8 t10 = t9;
    t2 = t10;
ix3:;
    gm_t9805qtonbsag t11 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t1, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t11;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_15b7omr(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_15b7omr F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)88ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4s59dpv(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4s59dpv F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5jtvbud(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5jtvbud F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8fvt97b(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8fvt97b F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_c536n99(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_c536n99 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_iloqstk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_iloqstk F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4lehgc(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j4lehgc F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)216ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_tk5tca7(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_tk5tca7 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)104ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2) {
    struct gm_fr_set_inst_def_tcpabg0 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 2064);
        uint32_t t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_put_3pvr70r(t3, t4, &(*(gm_tdgfduqjheqdm *)((uint8_t *)a2)));
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srclv0dcbbfhves, 607, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
    GM_STACK_CHECK_SHALLOW(&F, 202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9p38b040grms t2;
        gm_f_to_u128_haka99r((void *)&t2, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        unsigned __int128 t3;
        if (((((uint8_t *)&t2))[16] != 0)) {
            unsigned __int128 t4 = GM_LD(unsigned __int128, (((uint8_t *)&t2) + 0));
            t3 = t4;
        } else {
            gm_tf5srh4mp4b3i t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        gm_l1 = t3;
    }
    {
        unsigned __int128 t7 = gm_l1;
        gm_tf5srh4mp4b3i t8;
        uint64_t t9 = (uint64_t)t7;
        gm_tf5srh4mp4b3i t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t10), t9);
        ((uint8_t *)&t10)[8] = 1;
        gm_tf5srh4mp4b3i t11;
        memset(&t11, 0, sizeof t11);
        t8 = ((unsigned __int128)(t7) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t10 : t11;
        uint8_t t12 = 0;
        F.ret = t8;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_t6j6smj7qoctu t4;
        gm_b16_8 t5 = GM_LD(gm_b16_8, gm_c3t36dlh4g5);
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tabb710d9mq2e t7;
        gm_f_format_e79cvtl((void *)&t7, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srclv0dcbbfhves, 635, 19);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_b0_1 t10 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t9);
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
inline __attribute__((aligned(4))) uint32_t gm_f_add_const_8u8b1ja(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_add_const_8u8b1ja F;
    void * gm_l0; /* s */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK(&F, 117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 1968) + 0) + 8);
        uint32_t t4 = (uint32_t)t3;
        gm_l2 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 1968);
        gm_tftb1odc739t5 t7 = gm_f_push_8cr0sgp(t6, &(*(gm_tjcptv9sagfq8 *)((uint8_t *)a1)));
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srclv0dcbbfhves, 616, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 2008);
        gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 28));
        gm_trlabqsjjp0ds t13;
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t13 + 0), t11);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t13 + 12), t12);
        uint32_t t14 = gm_l2;
        gm_tftb1odc739t5 t15 = gm_f_put_qrf6f79(t10, t13, t14);
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srclv0dcbbfhves, 617, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_adt_field_tys_oggq5ji(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2, void * a3) {
    struct gm_fr_adt_field_tys_oggq5ji F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    gm_t9qkh0rsooc8d gm_l2; /* vis */
    void * gm_l3; /* tys */
    gm_b16_8 gm_l5; /* fields */
    gm_b16_8 gm_l6; /* variants */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t88rcucg1b7uk gm_l10; /* p */
    GM_STACK_CHECK(&F, 455u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1088) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srclv0dcbbfhves, 1257, 11);
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma9; default: goto ma11; }
ma8:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24) + 0));
        if (t14 != ((uint8_t)0ULL)) goto mn13;
        gm_tpautr4br7h3l t15 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24) + 8));
        F.l4 = t15;
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        gm_t9qkh0rsooc8d t18 = gm_l2;
        void * t19 = gm_l3;
        gm_b0_1 t20 = gm_f_push_field_tys_v4ep4t3((void *)NULL, cx, NULL, t17, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me7;
mn13:;
ma9:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24) + 0));
        if (t22 != ((uint8_t)2ULL)) goto mn21;
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24) + 8));
        gm_l5 = t23;
        gm_b16_8 t24 = gm_l5;
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
        gm_t9qkh0rsooc8d t26 = gm_l2;
        void * t27 = gm_l3;
        gm_b0_1 t28 = gm_f_push_field_tys_v4ep4t3((void *)NULL, cx, NULL, t25, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me7;
mn21:;
ma10:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24) + 0));
        if (t30 != ((uint8_t)1ULL)) goto mn29;
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24) + 8) + 8));
        gm_l6 = t31;
        {
            gm_b16_8 t33 = gm_l6;
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
                    gm_tckdobunaulee t45 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t41 * 72));
                    F.l9 = t45;
                    {
                        if (!(((((uint8_t *)&F.l9) + 16))[4] != 0)) goto ie47;
                        gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                        gm_l10 = t48;
                        {
                            void * t50 = gm_l3;
                            gm_t88rcucg1b7uk t51 = gm_l10;
                            gm_t9qkh0rsooc8d t52 = gm_l2;
                            gm_t3jsf43p1pths t53;
                            GM_ST(uint8_t, (uint8_t *)&t53 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t53 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t53 + 0), t51);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t53 + 4), t52);
                            gm_tftb1odc739t5 t54 = gm_f_push_h37silv(t50, t53);
                            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srclv0dcbbfhves, 1261, 33);
                            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
                        }
                        goto ix47;
ie47:;
ix47:;
                    }
                    goto me43;
mn44:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1260, 32);
me43:;
                }
lc34:;
                {
                    uint64_t t57 = gm_l8;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l8 = t60;
                }
            }
lb34:;
        }
        goto me7;
mn29:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1257, 5);
me7:;
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
inline __attribute__((aligned(4))) void gm_f_aliased_decl_jv6ilmn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_aliased_decl_jv6ilmn F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint64_t gm_l4; /* d */
    void * gm_l5; /* inner */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    uint64_t gm_l8; /* m */
    GM_STACK_CHECK(&F, 527u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma7; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma7; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma7; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; default: goto ma7; }
ma4:;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t11 != ((uint8_t)6ULL)) goto mn10;
        gm_t08olq97rscn3 t12 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t12;
        {
            void * t14 = gm_l0;
            uint64_t t15 = gm_l1;
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_tf5srh4mp4b3i t17 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t14, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t18;
            if (((((uint8_t *)&t17))[8] != 0)) {
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t17) + 0));
                t18 = t19;
            } else {
                gm_t89h15e2c5o3p t20;
                memset(&t20, 0, sizeof t20);
                uint8_t t21 = 0;
                F.ret = t20;
                goto gm_fret;
            }
            gm_l4 = t18;
        }
        {
            uint64_t t22 = gm_l4;
            gm_tf5srh4mp4b3i t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t23 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t23), t22);
            ((uint8_t *)&t23)[8] = 1;
            gm_tba9024q2up98 t24;
            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t24 + 0), t23);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out))[16] = 1;
            GM_ST(gm_tba9024q2up98, (uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out) + 0, t24);
            goto gm_fdone;
        }
        goto me3;
mn10:;
ma5:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t26 != ((uint8_t)9ULL)) goto mn25;
        void * t27 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l5 = t27;
        void * t28 = gm_l0;
        uint64_t t29 = gm_l1;
        void * t30 = gm_l5;
        gm_f_aliased_decl_jv6ilmn((void *)&(*(gm_t89h15e2c5o3p *)gm_out), (void *)NULL, cx, NULL, t28, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn25:;
ma6:;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t32 != ((uint8_t)22ULL)) goto mn31;
        void * t33 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l6 = t33;
        gm_t08olq97rscn3 t34 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l7 = t34;
        {
            void * t36 = gm_l0;
            uint64_t t37 = gm_l1;
            void * t38 = gm_l6;
            gm_tf5srh4mp4b3i t39 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t36, t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t40;
            if (((((uint8_t *)&t39))[8] != 0)) {
                uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&t39) + 0));
                t40 = t41;
            } else {
                gm_t89h15e2c5o3p t42;
                memset(&t42, 0, sizeof t42);
                uint8_t t43 = 0;
                F.ret = t42;
                goto gm_fret;
            }
            gm_l8 = t40;
        }
        {
            void * t44 = gm_l0;
            uint64_t t45 = gm_l8;
            uint64_t t46 = gm_l1;
            gm_tf5srh4mp4b3i t47 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t44, t45, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)), t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tba9024q2up98 t48;
            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t48 + 0), t47);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out))[16] = 1;
            GM_ST(gm_tba9024q2up98, (uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out) + 0, t48);
            goto gm_fdone;
        }
        goto me3;
mn31:;
ma7:;
        gm_t89h15e2c5o3p t50;
        memset(&t50, 0, sizeof t50);
        uint8_t t51 = 0;
        F.ret = t50;
        goto gm_fret;
        goto me3;
mn49:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1102, 5);
me3:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_allow_attr_3321gm7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_check_allow_attr_3321gm7 F;
    void * gm_l0; /* s */
    void * gm_l1; /* attr */
    gm_b16_8 gm_l2; /* args */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 388u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_th2lqbo77pvsc t3 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t2) + 32));
        gm_b16_8 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_b16_8 t6 = gm_f_empty_slice_g3tddrb();
            t4 = t6;
        }
        gm_l2 = t4;
    }
    {
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t9 = ((t8 == ((uint64_t)0ULL)));
        if (!t9) goto ie7;
        {
            void * t11 = gm_l0;
            void * t12 = gm_l1;
            gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t12) + 56));
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s8n5rdam5o1sm));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(68));
            gm_b0_1 t15 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t11, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        {
            gm_b16_8 t17 = gm_l2;
            gm_l3 = t17;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t19 = gm_l4;
                uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t21 = (t19 < t20);
                if (!t21) goto lb18;
                {
                    uint64_t t24 = gm_l4;
                    uint64_t t25 = (uintptr_t)t24;
                    uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tc3oht4ksth9d t29 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t25 * 88));
                    F.l5 = t29;
                    {
                        uint8_t t32;
                        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 8) + 0));
                        if (t35 != ((uint8_t)2ULL)) goto mn34;
                        t32 = ((uint8_t)0);
                        goto me33;
mn34:;
                        t32 = ((uint8_t)1);
                        goto me33;
mn36:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 973, 12);
me33:;
                        if (!t32) goto ie31;
                        {
                            void * t38 = gm_l0;
                            gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 72));
                            gm_b16_8 t40;
                            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_st6dd0k5nckh3));
                            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(57));
                            gm_b0_1 t41 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t38, t39, t40);
                            if (gm_unw.kind) {
                                goto gm_ll22;
                            }
                        }
                        goto ix31;
ie31:;
ix31:;
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 972, 5);
me27:;
                }
lc18:;
                {
                    uint64_t t43 = gm_l4;
                    uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                    uint64_t t46 = r44;
                    gm_l4 = t46;
                }
            }
gm_ll22:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb18; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc18; }
            }
            goto gm_lad;
lb18:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_method_candidates_k08rvu6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_method_candidates_k08rvu6 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tl874t2atnlh3 gm_l5; /* f */
    gm_tl874t2atnlh3 gm_l6; /* at */
    gm_tl874t2atnlh3 gm_l7; /* other */
    GM_STACK_CHECK(&F, 676u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_hash_map_gsp2k0o((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        {
            void * t4 = gm_l0;
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 1872) + 0));
            gm_l2 = t5;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l3;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l3;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_t79rbdv4rafe5 t17 = GM_LD(gm_t79rbdv4rafe5, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t13 * 40));
                    F.l4 = t17;
                    {
                        void * t19 = (void *)((uint8_t *)&F.l1);
                        gm_tiilmfd16v3fc t20;
                        memset((uint8_t *)&t20 + 4, 0, 4);
                        uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 0));
                        GM_ST(uint32_t, (uint8_t *)&t20 + 0, t21);
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                        GM_ST(gm_b16_8, (uint8_t *)&t20 + 8, t22);
                        gm_toceffb697p0u t23 = gm_f_get_qjsv97t(t19, &t20);
                        gm_tl874t2atnlh3 t24;
                        if (((((uint8_t *)&t23))[12] != 0)) {
                            gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t23) + 0));
                            t24 = t25;
                        } else {
                            {
                                void * t27 = (void *)((uint8_t *)&F.l1);
                                gm_tiilmfd16v3fc t28;
                                memset((uint8_t *)&t28 + 4, 0, 4);
                                uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 0));
                                GM_ST(uint32_t, (uint8_t *)&t28 + 0, t29);
                                gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                                GM_ST(gm_b16_8, (uint8_t *)&t28 + 8, t30);
                                gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                                gm_tftb1odc739t5 t32 = gm_f_put_pjhbiaq(t27, &t28, GM_CONV(gm_cseahvg8v7p0vs, t31));
                                if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srclv0dcbbfhves, 1169, 13);
                                gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
                            }
                            goto lc6;
                        }
                        gm_l5 = t24;
                    }
                    {
                        gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                        gm_l6 = t34;
                    }
                    {
                        gm_tl874t2atnlh3 t35 = gm_l5;
                        gm_l7 = t35;
                    }
                    {
                        uint8_t t37 = 0;
                        uint32_t t38 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 24) + 0));
                        uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 0));
                        uint8_t t40 = (t38 < t39);
                        if (!t40) {
                            uint8_t t41 = 0;
                            uint32_t t42 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 24) + 0));
                            uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 0));
                            uint8_t t44 = ((t42 == t43));
                            if (t44) {
                                uint32_t t45 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 24) + 4));
                                uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                                uint8_t t47 = (t45 < t46);
                                t41 = t47;
                            } else t41 = 0;
                            t37 = t41;
                        } else t37 = 1;
                        if (!t37) goto ie36;
                        {
                            gm_tl874t2atnlh3 t49 = gm_l5;
                            gm_l6 = t49;
                        }
                        {
                            gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                            gm_l7 = t50;
                        }
                        goto ix36;
ie36:;
ix36:;
                    }
                    {
                        gm_tl874t2atnlh3 t51 = gm_l6;
                        gm_t6j6smj7qoctu t52;
                        uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 0));
                        uint64_t t54 = (uint64_t)t53;
                        uint64_t t55 = (uintptr_t)t54;
                        void * t56 = gm_l0;
                        uint64_t t57 = GM_LD(uintptr_t, ((((uint8_t *)t56) + 1088) + 0) + 8);
                        if (t55 >= (uintptr_t)(t57)) gm_trap_index(t55, t57, gm_srclv0dcbbfhves, 1179, 127);
                        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t56) + 1088) + 0)) + t55 * 184) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t52 + 0, t58);
                        gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                        GM_ST(gm_b16_8, (uint8_t *)&t52 + 16, t59);
                        gm_tabb710d9mq2e t60;
                        gm_f_format_ck7vger((void *)&t60, (void *)NULL, cx, NULL, &t52);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 8, gm_srclv0dcbbfhves, 1179, 29);
                        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 8));
                        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                        gm_f_error_s6hn871((void *)&F.l8, (void *)NULL, cx, NULL, t51, t62);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                    }
                    {
                        void * t63 = (void *)((uint8_t *)&F.l8);
                        gm_tl874t2atnlh3 t64 = gm_l7;
                        gm_b16_8 t65;
                        GM_ST(void *, (uint8_t *)&t65, (void *)(gm_srjo6vku5gka5));
                        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(19));
                        gm_f_add_label_50fj7vv(t63, GM_CONV(gm_cseahvg8v7p0vs, t64), t65);
                    }
                    {
                        void * t66 = gm_l0;
                        gm_b0_1 t67 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t66, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l8)));
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1167, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t69 = gm_l3;
                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                    uint64_t t72 = r70;
                    gm_l3 = t72;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_ld1_1;
lb6:;
        }
    }
    {
        void * t73 = gm_l0;
        void * t74 = (void *)(((uint8_t *)t73) + 1872);
        gm_f_clear_ufjejrf(t74);
    }
    {
        void * t75 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_2l7c12h(t75);
    }
    goto gm_ls76;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t77 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_2l7c12h(t77);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls76:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_pub_signatures_plkds6r(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_pub_signatures_plkds6r F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    void * gm_l4; /* d */
    gm_t9qkh0rsooc8d gm_l5; /* vis */
    uint32_t gm_l8; /* f */
    void * gm_l9; /* sig */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint32_t gm_l13; /* e */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_tiilmfd16v3fc gm_l20; /* r */
    gm_tfe2ufaqq8re9 gm_l21; /* <temporary array> */
    gm_tnpifv20irnph gm_l22; /* set */
    gm_t88rcucg1b7uk gm_l23; /* t */
    uint32_t gm_l24; /* id */
    uint32_t gm_l25; /* id */
    gm_t88rcucg1b7uk gm_l26; /* t */
    gm_t88rcucg1b7uk gm_l27; /* t */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_t3jsf43p1pths gm_l30; /* m */
    gm_t3jsf43p1pths gm_l31; /* h */
    gm_b16_8 gm_l32; /* what */
    gm_b16_8 gm_l33; /* kind */
    GM_STACK_CHECK(&F, 2297u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
            uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 240) + 0) + 8);
            gm_l1 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l2;
                uint64_t t8 = gm_l1;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l2;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l3 = t15;
                    {
                        uint64_t t17 = gm_l3;
                        uint64_t t18 = (uintptr_t)t17;
                        void * t19 = gm_l0;
                        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
                        uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 240) + 0) + 8);
                        if (t18 >= (uintptr_t)(t21)) gm_trap_index(t18, t21, gm_srclv0dcbbfhves, 1190, 15);
                        void * t22 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 240) + 0)) + t18 * 216);
                        gm_l4 = t22;
                    }
                    {
                        void * t23 = gm_l4;
                        gm_t9qkh0rsooc8d t24 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t23) + 40) + 56));
                        gm_l5 = t24;
                    }
                    {
                        uint8_t t26 = 0;
                        uint8_t t27 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l5) + 0) == ((uint8_t)0ULL)));
                        if (!t27) {
                            void * t28 = gm_l4;
                            uint8_t t29;
                            uint8_t t32 = GM_LD(uint8_t, (((((uint8_t *)t28) + 40) + 64) + 0));
                            if (t32 != ((uint8_t)0ULL)) goto mn31;
                            t29 = ((uint8_t)0);
                            goto me30;
mn31:;
                            t29 = ((uint8_t)1);
                            goto me30;
mn33:;
                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1192, 31);
me30:;
                            t26 = t29;
                        } else t26 = 1;
                        if (!t26) goto ie25;
                        {
                            goto lc6;
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    {
                        uint64_t t36 = gm_l3;
                        void * t37 = gm_l0;
                        uint64_t t38 = GM_LD(uintptr_t, ((((uint8_t *)t37) + 304) + 0) + 8);
                        uint8_t t39 = (t36 >= t38);
                        if (!t39) goto ie35;
                        {
                            goto lc6;
                        }
                        goto ix35;
ie35:;
ix35:;
                    }
                    {
                        uint64_t t42 = gm_l3;
                        uint64_t t43 = (uintptr_t)t42;
                        void * t44 = gm_l0;
                        uint64_t t45 = GM_LD(uintptr_t, ((((uint8_t *)t44) + 304) + 0) + 8);
                        if (t43 >= (uintptr_t)(t45)) gm_trap_index(t43, t45, gm_srclv0dcbbfhves, 1194, 21);
                        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t44) + 304) + 0)) + t43 * 216) + 0));
                        if (t46 != ((uint8_t)2ULL)) goto ie41;
                        gm_t6qooom8qa2do t47 = GM_LD(gm_t6qooom8qa2do, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t44) + 304) + 0)) + t43 * 216) + 8));
                        F.l6 = t47;
                        {
                            gm_f_list_fam8ktt((void *)&F.l7, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                        }
                        {
                        }
                        {
                            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            switch (t59) { case ((uint8_t)0ULL): goto ma54; case ((uint8_t)1ULL): goto ma55; case ((uint8_t)2ULL): goto ma51; case ((uint8_t)3ULL): goto ma52; case ((uint8_t)4ULL): goto ma53; case ((uint8_t)5ULL): goto ma50; case ((uint8_t)6ULL): goto ma57; case ((uint8_t)7ULL): goto ma56; case ((uint8_t)8ULL): goto ma57; case ((uint8_t)9ULL): goto ma57; case ((uint8_t)10ULL): goto ma56; case ((uint8_t)11ULL): goto ma57; default: goto ma57; }
ma50:;
                            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t61 != ((uint8_t)5ULL)) goto mn60;
                            uint32_t t62 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 8));
                            gm_l8 = t62;
                            {
                                uint32_t t64 = gm_l8;
                                uint64_t t65 = (uint64_t)t64;
                                uint64_t t66 = (uintptr_t)t65;
                                void * t67 = gm_l0;
                                uint64_t t68 = GM_LD(uintptr_t, ((((uint8_t *)t67) + 344) + 0) + 8);
                                if (t66 >= (uintptr_t)(t68)) gm_trap_index(t66, t68, gm_srclv0dcbbfhves, 1199, 25);
                                void * t69 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t67) + 344) + 0)) + t66 * 136);
                                gm_l9 = t69;
                            }
                            {
                                {
                                    void * t71 = gm_l9;
                                    gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)t71) + 40));
                                    gm_l10 = t72;
                                }
                                {
                                    gm_l11 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t74 = gm_l11;
                                        uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                        uint8_t t76 = (t74 < t75);
                                        if (!t76) goto lb73;
                                        {
                                            uint64_t t79 = gm_l11;
                                            uint64_t t80 = (uintptr_t)t79;
                                            uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                            gm_t5v0c8ia6a5mo t84 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t80 * 56));
                                            F.l12 = t84;
                                            {
                                                uint8_t t87;
                                                if ((((((uint8_t *)&F.l12) + 40))[4] != 0)) goto mn89;
                                                t87 = ((uint8_t)1);
                                                goto me88;
mn89:;
                                                t87 = ((uint8_t)0);
                                                goto me88;
mn90:;
                                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1201, 24);
me88:;
                                                if (!t87) goto ie86;
                                                {
                                                    void * t92 = (void *)((uint8_t *)&F.l7);
                                                    gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 16));
                                                    gm_t9qkh0rsooc8d t94 = gm_l5;
                                                    gm_t3jsf43p1pths t95;
                                                    GM_ST(uint8_t, (uint8_t *)&t95 + 5, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t95 + 6, 0);
                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t95 + 0), t93);
                                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t95 + 4), t94);
                                                    gm_tftb1odc739t5 t96 = gm_f_push_h37silv(t92, t95);
                                                    if (((((uint8_t *)&t96))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t96) + 1, gm_srclv0dcbbfhves, 1201, 44);
                                                    gm_b0_1 t97 = GM_LD(gm_b0_1, (((uint8_t *)&t96) + 1));
                                                }
                                                goto ix86;
ie86:;
ix86:;
                                            }
                                            goto me82;
mn83:;
                                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1200, 17);
me82:;
                                        }
lc73:;
                                        {
                                            uint64_t t99 = gm_l11;
                                            uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                                            uint64_t t102 = r100;
                                            gm_l11 = t102;
                                        }
                                    }
lb73:;
                                }
                            }
                            {
                                void * t103 = (void *)((uint8_t *)&F.l7);
                                void * t104 = gm_l9;
                                gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t104) + 56));
                                gm_t9qkh0rsooc8d t106 = gm_l5;
                                gm_t3jsf43p1pths t107;
                                GM_ST(uint8_t, (uint8_t *)&t107 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t107 + 6, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t107 + 0), t105);
                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t107 + 4), t106);
                                gm_tftb1odc739t5 t108 = gm_f_push_h37silv(t103, t107);
                                if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_srclv0dcbbfhves, 1203, 17);
                                gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
                            }
                            {
                                void * t110 = gm_l9;
                                gm_b16_8 t111 = GM_LD(gm_b16_8, ((((uint8_t *)t110) + 64) + 0));
                                gm_t9qkh0rsooc8d t112 = gm_l5;
                                void * t113 = (void *)((uint8_t *)&F.l7);
                                gm_b0_1 t114 = gm_f_effect_arg_tys_2a7r8hh((void *)NULL, cx, NULL, t111, t112, t113);
                                if (gm_unw.kind) {
                                    goto gm_ll10;
                                }
                            }
                            goto me49;
mn60:;
ma51:;
                            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t116 != ((uint8_t)2ULL)) goto mn115;
                            uint32_t t117 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 8));
                            gm_l13 = t117;
                            {
                                {
                                    uint32_t t120 = gm_l13;
                                    uint64_t t121 = (uint64_t)t120;
                                    uint64_t t122 = (uintptr_t)t121;
                                    void * t123 = gm_l0;
                                    uint64_t t124 = GM_LD(uintptr_t, ((((uint8_t *)t123) + 1048) + 0) + 8);
                                    if (t122 >= (uintptr_t)(t124)) gm_trap_index(t122, t124, gm_srclv0dcbbfhves, 1207, 27);
                                    gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t123) + 1048) + 0)) + t122 * 56) + 40));
                                    gm_l14 = t125;
                                }
                                {
                                    gm_l15 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t127 = gm_l15;
                                        uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                        uint8_t t129 = (t127 < t128);
                                        if (!t129) goto lb126;
                                        {
                                            uint64_t t132 = gm_l15;
                                            uint64_t t133 = (uintptr_t)t132;
                                            uint64_t t134 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                            gm_tv0k2vg3054a6 t137 = GM_LD(gm_tv0k2vg3054a6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t133 * 48));
                                            F.l16 = t137;
                                            {
                                                {
                                                    gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 24));
                                                    gm_l17 = t140;
                                                }
                                                {
                                                    gm_l18 = ((uint64_t)0ULL);
                                                }
                                                {
                                                    for (;;) {
                                                        uint64_t t142 = gm_l18;
                                                        uint64_t t143 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                                        uint8_t t144 = (t142 < t143);
                                                        if (!t144) goto lb141;
                                                        {
                                                            uint64_t t147 = gm_l18;
                                                            uint64_t t148 = (uintptr_t)t147;
                                                            uint64_t t149 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                                            gm_t5v0c8ia6a5mo t152 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t148 * 56));
                                                            F.l19 = t152;
                                                            {
                                                                uint8_t t155;
                                                                if ((((((uint8_t *)&F.l19) + 40))[4] != 0)) goto mn157;
                                                                t155 = ((uint8_t)1);
                                                                goto me156;
mn157:;
                                                                t155 = ((uint8_t)0);
                                                                goto me156;
mn158:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1209, 28);
me156:;
                                                                if (!t155) goto ie154;
                                                                {
                                                                    void * t160 = (void *)((uint8_t *)&F.l7);
                                                                    gm_t88rcucg1b7uk t161 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 16));
                                                                    gm_t9qkh0rsooc8d t162 = gm_l5;
                                                                    gm_t3jsf43p1pths t163;
                                                                    GM_ST(uint8_t, (uint8_t *)&t163 + 5, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t163 + 6, 0);
                                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t163 + 0), t161);
                                                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t163 + 4), t162);
                                                                    gm_tftb1odc739t5 t164 = gm_f_push_h37silv(t160, t163);
                                                                    if (((((uint8_t *)&t164))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t164) + 1, gm_srclv0dcbbfhves, 1209, 48);
                                                                    gm_b0_1 t165 = GM_LD(gm_b0_1, (((uint8_t *)&t164) + 1));
                                                                }
                                                                goto ix154;
ie154:;
ix154:;
                                                            }
                                                            goto me150;
mn151:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1208, 21);
me150:;
                                                        }
lc141:;
                                                        {
                                                            uint64_t t167 = gm_l18;
                                                            uint64_t r168; bool o169 = __builtin_add_overflow(t167, ((uint64_t)1ULL), &r168);
                                                            uint64_t t170 = r168;
                                                            gm_l18 = t170;
                                                        }
                                                    }
lb141:;
                                                }
                                            }
                                            {
                                                void * t171 = (void *)((uint8_t *)&F.l7);
                                                gm_t88rcucg1b7uk t172 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 40));
                                                gm_t9qkh0rsooc8d t173 = gm_l5;
                                                gm_t3jsf43p1pths t174;
                                                GM_ST(uint8_t, (uint8_t *)&t174 + 5, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t174 + 6, 0);
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t174 + 0), t172);
                                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t174 + 4), t173);
                                                gm_tftb1odc739t5 t175 = gm_f_push_h37silv(t171, t174);
                                                if (((((uint8_t *)&t175))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t175) + 1, gm_srclv0dcbbfhves, 1211, 21);
                                                gm_b0_1 t176 = GM_LD(gm_b0_1, (((uint8_t *)&t175) + 1));
                                            }
                                            goto me135;
mn136:;
                                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1207, 17);
me135:;
                                        }
lc126:;
                                        {
                                            uint64_t t178 = gm_l15;
                                            uint64_t r179; bool o180 = __builtin_add_overflow(t178, ((uint64_t)1ULL), &r179);
                                            uint64_t t181 = r179;
                                            gm_l15 = t181;
                                        }
                                    }
lb126:;
                                }
                            }
                            goto me49;
mn115:;
ma52:;
                            uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t183 != ((uint8_t)3ULL)) goto mn182;
                            gm_tiilmfd16v3fc t184 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&F.l6) + 8));
                            gm_l20 = t184;
                            {
                                gm_tiilmfd16v3fc t186 = gm_l20;
                                GM_ST(gm_tiilmfd16v3fc, (uint8_t *)&(*(gm_tfe2ufaqq8re9 *)((uint8_t *)&gm_l21)) + 0, t186);
                            }
                            void * t187 = ((uint8_t *)&gm_l21);
                            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srclv0dcbbfhves, 1214, 46);
                            gm_b16_8 t188;
                            GM_ST(void *, (uint8_t *)&t188, (void *)(((uint8_t *)t187 + (uintptr_t)0 * 24)));
                            GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                            gm_b16_8 t189 = t188;
                            gm_t9qkh0rsooc8d t190 = gm_l5;
                            void * t191 = (void *)((uint8_t *)&F.l7);
                            gm_b0_1 t192 = gm_f_effect_arg_tys_2a7r8hh((void *)NULL, cx, NULL, t189, t190, t191);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            goto me49;
mn182:;
ma53:;
                            uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t194 != ((uint8_t)4ULL)) goto mn193;
                            gm_tnpifv20irnph t195 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l6) + 8));
                            gm_l22 = t195;
                            gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 0));
                            gm_t9qkh0rsooc8d t197 = gm_l5;
                            void * t198 = (void *)((uint8_t *)&F.l7);
                            gm_b0_1 t199 = gm_f_effect_arg_tys_2a7r8hh((void *)NULL, cx, NULL, t196, t197, t198);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            goto me49;
mn193:;
ma54:;
                            uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t201 != ((uint8_t)0ULL)) goto mn200;
                            gm_t88rcucg1b7uk t202 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                            gm_l23 = t202;
                            {
                                void * t205 = gm_l0;
                                void * t206 = (void *)(((uint8_t *)t205) + 8);
                                gm_t88rcucg1b7uk t207 = gm_l23;
                                gm_t53at80pfdus3 t208;
                                gm_f_kind_jcnt4hm((void *)&t208, t206, GM_CONV(gm_csguankko4lmhd, t207));
                                uint8_t t209 = GM_LD(uint8_t, (((uint8_t *)&t208) + 0));
                                if (t209 != ((uint8_t)17ULL)) goto ie204;
                                uint32_t t210 = GM_LD(uint32_t, ((((uint8_t *)&t208) + 8) + 0));
                                gm_l24 = t210;
                                uint32_t t211 = gm_l24;
                                uint64_t t212 = (uint64_t)t211;
                                uint64_t t213 = (uintptr_t)t212;
                                void * t214 = gm_l0;
                                uint64_t t215 = GM_LD(uintptr_t, ((((uint8_t *)t214) + 1088) + 0) + 8);
                                if (t213 >= (uintptr_t)(t215)) gm_trap_index(t213, t215, gm_srclv0dcbbfhves, 1217, 53);
                                gm_tf5srh4mp4b3i t216 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t214) + 1088) + 0)) + t213 * 184) + 168));
                                uint64_t t217 = gm_l3;
                                gm_tf5srh4mp4b3i t218;
                                GM_ST(uint8_t, (uint8_t *)&t218 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t218 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t218 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t218), t217);
                                ((uint8_t *)&t218)[8] = 1;
                                uint8_t t219 = (gm_eq7mri15hk78(((uint8_t *)&t216), ((uint8_t *)&t218)));
                                if (!t219) goto ie204;
                                {
                                    void * t221 = gm_l0;
                                    uint32_t t222 = gm_l24;
                                    gm_t9qkh0rsooc8d t223 = gm_l5;
                                    void * t224 = (void *)((uint8_t *)&F.l7);
                                    gm_b0_1 t225 = gm_f_adt_field_tys_oggq5ji((void *)NULL, cx, NULL, t221, t222, t223, t224);
                                    if (gm_unw.kind) {
                                        goto gm_ll10;
                                    }
                                }
                                goto ix204;
ie204:;
                                {
                                    void * t227 = (void *)((uint8_t *)&F.l7);
                                    gm_t88rcucg1b7uk t228 = gm_l23;
                                    gm_t9qkh0rsooc8d t229 = gm_l5;
                                    gm_t3jsf43p1pths t230;
                                    GM_ST(uint8_t, (uint8_t *)&t230 + 5, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t230 + 6, 0);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t230 + 0), t228);
                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t230 + 4), t229);
                                    gm_tftb1odc739t5 t231 = gm_f_push_h37silv(t227, t230);
                                    if (((((uint8_t *)&t231))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t231) + 1, gm_srclv0dcbbfhves, 1220, 21);
                                    gm_b0_1 t232 = GM_LD(gm_b0_1, (((uint8_t *)&t231) + 1));
                                }
ix204:;
                            }
                            goto me49;
mn200:;
ma55:;
                            uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t234 != ((uint8_t)1ULL)) goto mn233;
                            uint32_t t235 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 8));
                            gm_l25 = t235;
                            {
                                uint32_t t238 = gm_l25;
                                uint64_t t239 = (uint64_t)t238;
                                uint64_t t240 = (uintptr_t)t239;
                                void * t241 = gm_l0;
                                uint64_t t242 = GM_LD(uintptr_t, ((((uint8_t *)t241) + 1088) + 0) + 8);
                                if (t240 >= (uintptr_t)(t242)) gm_trap_index(t240, t242, gm_srclv0dcbbfhves, 1224, 20);
                                gm_tf5srh4mp4b3i t243 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t241) + 1088) + 0)) + t240 * 184) + 168));
                                uint64_t t244 = gm_l3;
                                gm_tf5srh4mp4b3i t245;
                                GM_ST(uint8_t, (uint8_t *)&t245 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t245 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t245 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t245), t244);
                                ((uint8_t *)&t245)[8] = 1;
                                uint8_t t246 = (gm_eq7mri15hk78(((uint8_t *)&t243), ((uint8_t *)&t245)));
                                if (!t246) goto ie237;
                                {
                                    void * t248 = gm_l0;
                                    uint32_t t249 = gm_l25;
                                    gm_t9qkh0rsooc8d t250 = gm_l5;
                                    void * t251 = (void *)((uint8_t *)&F.l7);
                                    gm_b0_1 t252 = gm_f_adt_field_tys_oggq5ji((void *)NULL, cx, NULL, t248, t249, t250, t251);
                                    if (gm_unw.kind) {
                                        goto gm_ll10;
                                    }
                                }
                                goto ix237;
ie237:;
ix237:;
                            }
                            goto me49;
mn233:;
ma56:;
                            uint8_t t256 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t256 != ((uint8_t)7ULL)) goto pa255;
                            gm_t88rcucg1b7uk t257 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                            gm_l26 = t257;
                            goto po254;
pa255:;
                            uint8_t t259 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t259 != ((uint8_t)10ULL)) goto pa258;
                            gm_t88rcucg1b7uk t260 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                            gm_l26 = t260;
                            goto po254;
pa258:;
                            goto mn253;
po254:;
                            void * t261 = (void *)((uint8_t *)&F.l7);
                            gm_t88rcucg1b7uk t262 = gm_l26;
                            gm_t9qkh0rsooc8d t263 = gm_l5;
                            gm_t3jsf43p1pths t264;
                            GM_ST(uint8_t, (uint8_t *)&t264 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t264 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t264 + 0), t262);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t264 + 4), t263);
                            gm_tftb1odc739t5 t265 = gm_f_push_h37silv(t261, t264);
                            if (((((uint8_t *)&t265))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t265) + 1, gm_srclv0dcbbfhves, 1226, 38);
                            gm_b0_1 t266 = GM_LD(gm_b0_1, (((uint8_t *)&t265) + 1));
                            goto me49;
mn253:;
ma57:;
                            goto me49;
mn267:;
ma58:;
                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1197, 9);
me49:;
                        }
                        {
                            {
                                gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                                gm_l28 = t270;
                            }
                            {
                                gm_l29 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t272 = gm_l29;
                                    uint64_t t273 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                    uint8_t t274 = (t272 < t273);
                                    if (!t274) goto lb271;
                                    {
                                        uint64_t t277 = gm_l29;
                                        uint64_t t278 = (uintptr_t)t277;
                                        uint64_t t279 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                        gm_t3jsf43p1pths t282 = GM_LD(gm_t3jsf43p1pths, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t278 * 8));
                                        gm_l30 = t282;
                                        {
                                            void * t285 = gm_l0;
                                            gm_t88rcucg1b7uk t286 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l30) + 0));
                                            gm_t9qkh0rsooc8d t287 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l30) + 4));
                                            gm_trcagkhgm1qdu t288 = gm_f_hidden_part_is5hgf9(t285, GM_CONV(gm_csguankko4lmhd, t286), t287);
                                            if (!((((uint8_t *)&t288))[8] != 0)) goto ie284;
                                            gm_t3jsf43p1pths t289 = GM_LD(gm_t3jsf43p1pths, ((uint8_t *)&t288));
                                            gm_l31 = t289;
                                            {
                                                gm_b16_8 t291;
                                                uint8_t t293 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l5) + 0) == ((uint8_t)1ULL)));
                                                if (!t293) goto ie292;
                                                gm_b16_8 t295;
                                                GM_ST(void *, (uint8_t *)&t295, (void *)(gm_shofa8pdml9fb));
                                                GM_ST(uintptr_t, (uint8_t *)&t295 + 8, (uintptr_t)(12));
                                                gm_b16_8 t296 = t295;
                                                t291 = t296;
                                                goto ix292;
ie292:;
                                                gm_b16_8 t298;
                                                GM_ST(void *, (uint8_t *)&t298, (void *)(gm_sjqs7q3ostqbm));
                                                GM_ST(uintptr_t, (uint8_t *)&t298 + 8, (uintptr_t)(3));
                                                gm_b16_8 t299 = t298;
                                                t291 = t299;
ix292:;
                                                gm_l32 = t291;
                                            }
                                            {
                                                gm_b16_8 t300;
                                                uint8_t t302 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 4) + 0) == ((uint8_t)1ULL)));
                                                if (!t302) goto ie301;
                                                gm_b16_8 t304;
                                                GM_ST(void *, (uint8_t *)&t304, (void *)(gm_scllp0lpbljde));
                                                GM_ST(uintptr_t, (uint8_t *)&t304 + 8, (uintptr_t)(14));
                                                gm_b16_8 t305 = t304;
                                                t300 = t305;
                                                goto ix301;
ie301:;
                                                gm_b16_8 t307;
                                                GM_ST(void *, (uint8_t *)&t307, (void *)(gm_sbrnv8n0dpinb));
                                                GM_ST(uintptr_t, (uint8_t *)&t307 + 8, (uintptr_t)(7));
                                                gm_b16_8 t308 = t307;
                                                t300 = t308;
ix301:;
                                                gm_l33 = t300;
                                            }
                                            {
                                                void * t309 = gm_l0;
                                                void * t310 = gm_l4;
                                                gm_tl874t2atnlh3 t311 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t310) + 0) + 16));
                                                gm_t0uuhaljsf410 t312;
                                                gm_b16_8 t313 = gm_l32;
                                                GM_ST(gm_b16_8, (uint8_t *)&t312 + 0, t313);
                                                void * t314 = gm_l4;
                                                gm_b16_8 t315 = GM_LD(gm_b16_8, ((((uint8_t *)t314) + 0) + 0));
                                                GM_ST(gm_b16_8, (uint8_t *)&t312 + 16, t315);
                                                gm_b16_8 t316 = gm_l33;
                                                GM_ST(gm_b16_8, (uint8_t *)&t312 + 32, t316);
                                                void * t317 = gm_l0;
                                                gm_t88rcucg1b7uk t318 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l31) + 0));
                                                gm_b16_8 t319 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t317, t318);
                                                if (gm_unw.kind) {
                                                    goto gm_ll275;
                                                }
                                                GM_ST(gm_b16_8, (uint8_t *)&t312 + 48, t319);
                                                gm_tabb710d9mq2e t320;
                                                gm_f_format_t5fmuah((void *)&t320, (void *)NULL, cx, NULL, &t312);
                                                if (gm_unw.kind) {
                                                    goto gm_ll275;
                                                }
                                                if (((((uint8_t *)&t320))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t320) + 8, gm_srclv0dcbbfhves, 1233, 38);
                                                gm_b16_8 t321 = GM_LD(gm_b16_8, (((uint8_t *)&t320) + 8));
                                                gm_b16_8 t322 = GM_LD(gm_b16_8, ((uint8_t *)&t321));
                                                gm_b0_1 t323 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t309, t311, t322);
                                                if (gm_unw.kind) {
                                                    goto gm_ll275;
                                                }
                                            }
                                            {
                                                goto lb271;
                                            }
                                            goto ix284;
ie284:;
ix284:;
                                        }
                                        goto me280;
mn281:;
                                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1229, 9);
me280:;
                                    }
lc271:;
                                    {
                                        uint64_t t325 = gm_l29;
                                        uint64_t r326; bool o327 = __builtin_add_overflow(t325, ((uint64_t)1ULL), &r326);
                                        uint64_t t328 = r326;
                                        gm_l29 = t328;
                                    }
                                }
gm_ll275:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb271; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc271; }
                                }
                                goto gm_ll10;
lb271:;
                            }
                        }
                        goto ix41;
ie41:;
                        {
                            goto lc6;
                        }
ix41:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1189, 5);
me13:;
                }
lc6:;
                {
                    uint64_t t331 = gm_l2;
                    uint64_t r332; bool o333 = __builtin_add_overflow(t331, ((uint64_t)1ULL), &r332);
                    uint64_t t334 = r332;
                    gm_l2 = t334;
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
extern __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_compute_binding_cl3ogob(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, void * a4) {
    struct gm_fr_compute_binding_cl3ogob F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    uint64_t gm_l2; /* module */
    gm_t08olq97rscn3 gm_l3; /* name */
    void * gm_l4; /* b */
    gm_tl874t2atnlh3 gm_l6; /* between */
    gm_b16_8 gm_l7; /* text */
    void * gm_l8; /* init */
    uint64_t gm_l9; /* m */
    void * gm_l10; /* blk */
    void * gm_l11; /* lit */
    void * gm_l12; /* lit */
    gm_tnpifv20irnph gm_l13; /* set */
    gm_t88rcucg1b7uk gm_l14; /* ty */
    void * gm_l15; /* inner */
    gm_t88rcucg1b7uk gm_l16; /* ty */
    GM_STACK_CHECK(&F, 5295u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    {
        void * t3 = gm_l4;
        uint8_t t4 = ((GM_LD(uint8_t, (((uint8_t *)t3) + 32) + 0) == ((uint8_t)1ULL)));
        if (!t4) goto ie2;
        {
            void * t7 = gm_l4;
            void * t8 = GM_LD(void *, (((uint8_t *)t7) + 40));
            uint8_t t9;
            uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            if (t12 != ((uint8_t)38ULL)) goto mn11;
            t9 = ((uint8_t)1);
            goto me10;
mn11:;
            t9 = ((uint8_t)0);
            goto me10;
mn13:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 979, 12);
me10:;
            if (!t9) goto ie6;
            {
                void * t15 = gm_l0;
                void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
                uint64_t t17 = gm_l1;
                gm_b0_1 t18 = gm_f_resolve_import_944uqa9((void *)NULL, cx, NULL, t16, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)11ULL));
                goto gm_fdone;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            void * t19 = gm_l4;
            gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t19) + 48));
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s6lnp3h2ojl1f));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(76));
            gm_f_error_s6hn871((void *)&F.l5, (void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t22 = gm_l4;
            uint32_t t23 = GM_LD(uint32_t, ((((uint8_t *)t22) + 48) + 0));
            uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)a3) + 16) + 8));
            void * t25 = gm_l4;
            void * t26 = GM_LD(void *, (((uint8_t *)t25) + 40));
            uint32_t t27 = GM_LD(uint32_t, ((((uint8_t *)t26) + 72) + 4));
            gm_tl874t2atnlh3 t28;
            GM_ST(uint32_t, ((uint8_t *)&t28 + 0), t23);
            GM_ST(uint32_t, ((uint8_t *)&t28 + 4), t24);
            GM_ST(uint32_t, ((uint8_t *)&t28 + 8), t27);
            gm_l6 = t28;
        }
        {
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
            void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
            uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
            void * t33 = gm_f_file_o15fqku(t31, t32);
            uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 16) + 8);
            void * t35 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t33) + 16)));
            uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
            uint64_t t37 = (uint64_t)t36;
            uint64_t t38 = (uintptr_t)t37;
            uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
            uint64_t t40 = (uint64_t)t39;
            uint64_t t41 = (uintptr_t)t40;
            if (t38 > t41 || t41 > (uintptr_t)t34) gm_trap_slice(t38, t41, t34, gm_srclv0dcbbfhves, 987, 17);
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t35 + t38 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(t41 - t38));
            gm_l7 = t42;
        }
        {
            void * t44 = gm_l4;
            void * t45 = GM_LD(void *, (((uint8_t *)t44) + 24));
            uint8_t t46;
            if ((GM_LD(void *, ((uint8_t *)&t45)) != 0)) goto mn48;
            t46 = ((uint8_t)1);
            goto me47;
mn48:;
            t46 = ((uint8_t)0);
            goto me47;
mn49:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 988, 12);
me47:;
            if (!t46) goto ie43;
            gm_b16_8 t50 = gm_l7;
            gm_b16_8 t51 = gm_f_trim_fdrfe6i(t50);
            uint8_t t52 = (((GM_LD(uintptr_t, ((uint8_t *)&t51) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&t51)), gm_shge5n743ua4h, 2) == 0));
            if (!t52) goto ie43;
            {
                void * t54 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t55;
                GM_ST(void *, (uint8_t *)&t55, (void *)(gm_spq5ja4c7ciga));
                GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(10));
                gm_tl874t2atnlh3 t56 = gm_l6;
                gm_b16_8 t57;
                GM_ST(void *, (uint8_t *)&t57, (void *)(gm_s2urait735ofq));
                GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(4));
                gm_b0_1 t58 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t54, t55, t56, t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix43;
ie43:;
ix43:;
        }
        {
            void * t59 = gm_l0;
            gm_b0_1 t60 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t59, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l5)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)11ULL));
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t61 = gm_l4;
        void * t62 = GM_LD(void *, (((uint8_t *)t61) + 40));
        gm_l8 = t62;
    }
    {
        void * t63 = gm_l8;
        uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
        switch (t75) { case ((uint8_t)0ULL): goto ma73; case ((uint8_t)1ULL): goto ma73; case ((uint8_t)2ULL): goto ma73; case ((uint8_t)3ULL): goto ma73; case ((uint8_t)4ULL): goto ma73; case ((uint8_t)5ULL): goto ma73; case ((uint8_t)6ULL): goto ma73; case ((uint8_t)7ULL): goto ma73; case ((uint8_t)8ULL): goto ma73; case ((uint8_t)9ULL): goto ma73; case ((uint8_t)10ULL): goto ma73; case ((uint8_t)11ULL): goto ma73; case ((uint8_t)12ULL): goto ma73; case ((uint8_t)13ULL): goto ma69; case ((uint8_t)14ULL): goto ma73; case ((uint8_t)15ULL): goto ma73; case ((uint8_t)16ULL): goto ma73; case ((uint8_t)17ULL): goto ma73; case ((uint8_t)18ULL): goto ma73; case ((uint8_t)19ULL): goto ma73; case ((uint8_t)20ULL): goto ma73; case ((uint8_t)21ULL): goto ma73; case ((uint8_t)22ULL): goto ma73; case ((uint8_t)23ULL): goto ma73; case ((uint8_t)24ULL): goto ma73; case ((uint8_t)25ULL): goto ma73; case ((uint8_t)26ULL): goto ma73; case ((uint8_t)27ULL): goto ma73; case ((uint8_t)28ULL): goto ma73; case ((uint8_t)29ULL): goto ma73; case ((uint8_t)30ULL): goto ma73; case ((uint8_t)31ULL): goto ma73; case ((uint8_t)32ULL): goto ma71; case ((uint8_t)33ULL): goto ma71; case ((uint8_t)34ULL): goto ma68; case ((uint8_t)35ULL): goto ma72; case ((uint8_t)36ULL): goto ma67; case ((uint8_t)37ULL): goto ma70; case ((uint8_t)38ULL): goto ma65; case ((uint8_t)39ULL): goto ma66; case ((uint8_t)40ULL): goto ma73; case ((uint8_t)41ULL): goto ma73; case ((uint8_t)42ULL): goto ma73; case ((uint8_t)43ULL): goto ma73; case ((uint8_t)44ULL): goto ma73; case ((uint8_t)45ULL): goto ma73; case ((uint8_t)46ULL): goto ma73; case ((uint8_t)47ULL): goto ma73; case ((uint8_t)48ULL): goto ma73; case ((uint8_t)49ULL): goto ma73; case ((uint8_t)50ULL): goto ma73; default: goto ma73; }
ma65:;
        uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
        if (t77 != ((uint8_t)38ULL)) goto mn76;
        {
            void * t79 = gm_l0;
            void * t80 = GM_LD(void *, (((uint8_t *)t79) + 0));
            uint64_t t81 = gm_l1;
            gm_b0_1 t82 = gm_f_resolve_import_944uqa9((void *)NULL, cx, NULL, t80, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint64_t t83 = gm_l1;
            uint64_t t84 = (uintptr_t)t83;
            void * t85 = gm_l0;
            void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
            uint64_t t87 = GM_LD(uintptr_t, ((((uint8_t *)t86) + 240) + 0) + 8);
            if (t84 >= (uintptr_t)(t87)) gm_trap_index(t84, t87, gm_srclv0dcbbfhves, 996, 26);
            gm_t6qooom8qa2do t88;
            uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t86) + 240) + 0)) + t84 * 216) + 200) + 0));
            if (t91 != ((uint8_t)1ULL)) goto mn90;
            uint64_t t92 = GM_LD(uint64_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t86) + 240) + 0)) + t84 * 216) + 200) + 8));
            gm_l9 = t92;
            uint64_t t93 = gm_l9;
            gm_t6qooom8qa2do t94;
            memset(&t94, 0, sizeof t94);
            GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)9ULL));
            GM_ST(uint64_t, ((uint8_t *)&t94 + 8), t93);
            t88 = t94;
            goto me89;
mn90:;
            gm_t6qooom8qa2do t96;
            memset(&t96, 0, sizeof t96);
            GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)11ULL));
            t88 = t96;
            goto me89;
mn95:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 996, 20);
me89:;
            uint8_t t97 = 0;
            F.ret = t88;
            goto gm_fret;
        }
        goto me64;
mn76:;
ma66:;
        uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
        if (t99 != ((uint8_t)39ULL)) goto mn98;
        void * t100 = GM_LD(void *, ((((uint8_t *)t63) + 8) + 8));
        gm_l10 = t100;
        void * t102 = gm_l4;
        void * t103 = GM_LD(void *, (((uint8_t *)t102) + 24));
        uint8_t t104;
        if ((GM_LD(void *, ((uint8_t *)&t103)) != 0)) goto mn106;
        t104 = ((uint8_t)1);
        goto me105;
mn106:;
        t104 = ((uint8_t)0);
        goto me105;
mn107:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1001, 29);
me105:;
        if (!t104) goto ie101;
        {
            void * t109 = gm_l0;
            uint64_t t110 = gm_l2;
            void * t111 = gm_l10;
            gm_f_declare_foreign_6s86jje((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t109, t110, &(*(gm_t08olq97rscn3 *)((uint8_t *)a3)), t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix101;
ie101:;
ix101:;
        goto me64;
mn98:;
ma67:;
        uint8_t t113 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
        if (t113 != ((uint8_t)36ULL)) goto mn112;
        void * t114 = GM_LD(void *, ((((uint8_t *)t63) + 8) + 8));
        gm_l11 = t114;
        void * t116 = gm_l4;
        void * t117 = GM_LD(void *, (((uint8_t *)t116) + 24));
        uint8_t t118;
        if ((GM_LD(void *, ((uint8_t *)&t117)) != 0)) goto mn120;
        t118 = ((uint8_t)1);
        goto me119;
mn120:;
        t118 = ((uint8_t)0);
        goto me119;
mn121:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1002, 24);
me119:;
        if (!t118) goto ie115;
        {
            void * t123 = gm_l0;
            uint64_t t124 = gm_l2;
            void * t125 = gm_l11;
            gm_f_declare_fn_6piolnr((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t123, t124, &(*(gm_t08olq97rscn3 *)((uint8_t *)a3)), t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix115;
ie115:;
ix115:;
        goto me64;
mn112:;
ma68:;
        uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
        if (t127 != ((uint8_t)34ULL)) goto mn126;
        void * t128 = GM_LD(void *, ((((uint8_t *)t63) + 8) + 8));
        gm_l12 = t128;
        void * t130 = gm_l4;
        void * t131 = GM_LD(void *, (((uint8_t *)t130) + 24));
        uint8_t t132;
        if ((GM_LD(void *, ((uint8_t *)&t131)) != 0)) goto mn134;
        t132 = ((uint8_t)1);
        goto me133;
mn134:;
        t132 = ((uint8_t)0);
        goto me133;
mn135:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1003, 28);
me133:;
        if (!t132) goto ie129;
        {
            void * t137 = gm_l0;
            uint64_t t138 = gm_l2;
            uint64_t t139 = gm_l1;
            void * t140 = gm_l12;
            gm_f_declare_effect_9n12hc8((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t137, t138, t139, &(*(gm_t08olq97rscn3 *)((uint8_t *)a3)), t140);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix129;
ie129:;
ix129:;
        goto me64;
mn126:;
ma69:;
        uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
        if (t142 != ((uint8_t)13ULL)) goto mn141;
        uint8_t t143 = GM_LD(uint8_t, ((((((uint8_t *)t63) + 8) + 8) + 0) + 0));
        if (t143 != ((uint8_t)0ULL)) goto mn141;
        uint8_t t144 = GM_LD(uint8_t, (((((((uint8_t *)t63) + 8) + 8) + 0) + 1) + 0));
        if (t144 != ((uint8_t)0ULL)) goto mn141;
        {
            void * t147 = gm_l4;
            void * t148 = GM_LD(void *, (((uint8_t *)t147) + 24));
            uint8_t t149;
            if ((GM_LD(void *, ((uint8_t *)&t148)) != 0)) goto mn151;
            t149 = ((uint8_t)1);
            goto me150;
mn151:;
            t149 = ((uint8_t)0);
            goto me150;
mn152:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1006, 16);
me150:;
            if (!t149) goto ie146;
            void * t153 = gm_l0;
            uint64_t t154 = gm_l2;
            void * t155 = gm_l8;
            uint8_t t156 = gm_f_is_effect_sum_dd9le16((void *)NULL, cx, NULL, t153, t154, t155);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t156) goto ie146;
            {
                void * t158 = gm_l0;
                uint64_t t159 = gm_l2;
                void * t160 = gm_l8;
                gm_b16_8 t161 = gm_f_effect_terms_bphj7th((void *)NULL, cx, NULL, t160);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
                gm_tnpifv20irnph t163 = gm_f_resolve_effect_set_o7rc5od((void *)NULL, cx, NULL, t158, t159, t162);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t163;
            }
            {
                gm_tnpifv20irnph t165 = gm_l13;
                uint8_t t166 = gm_f_has_splice_80pgqas(t165);
                if (!t166) goto ie164;
                {
                    void * t168 = gm_l0;
                    void * t169 = gm_l8;
                    gm_tl874t2atnlh3 t170 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t169) + 72));
                    gm_b16_8 t171;
                    GM_ST(void *, (uint8_t *)&t171, (void *)(gm_s9ocqto6ckbos));
                    GM_ST(uintptr_t, (uint8_t *)&t171 + 8, (uintptr_t)(57));
                    gm_b0_1 t172 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t168, t170, t171);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)11ULL));
                    goto gm_fdone;
                }
                goto ix164;
ie164:;
ix164:;
            }
            {
                gm_tnpifv20irnph t173 = gm_l13;
                memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)4ULL));
                GM_ST(gm_tnpifv20irnph, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 8, t173);
                goto gm_fdone;
            }
            goto ix146;
ie146:;
ix146:;
        }
        goto me64;
mn141:;
ma70:;
        uint8_t t175 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
        if (t175 != ((uint8_t)37ULL)) goto mn174;
        {
            void * t177 = gm_l0;
            void * t178 = gm_l8;
            gm_tl874t2atnlh3 t179 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t178) + 72));
            gm_b16_8 t180;
            GM_ST(void *, (uint8_t *)&t180, (void *)(gm_s6ibuqseh9tt7));
            GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(151));
            gm_b0_1 t181 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t177, t179, t180);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)11ULL));
            goto gm_fdone;
        }
        goto me64;
mn174:;
ma71:;
        uint8_t t185 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
        if (t185 != ((uint8_t)32ULL)) goto pa184;
        goto po183;
pa184:;
        uint8_t t187 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
        if (t187 != ((uint8_t)33ULL)) goto pa186;
        goto po183;
pa186:;
        goto mn182;
po183:;
        {
            void * t190 = gm_l4;
            void * t191 = GM_LD(void *, (((uint8_t *)t190) + 24));
            uint8_t t192;
            if ((GM_LD(void *, ((uint8_t *)&t191)) != 0)) goto mn194;
            t192 = ((uint8_t)1);
            goto me193;
mn194:;
            t192 = ((uint8_t)0);
            goto me193;
mn195:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1020, 16);
me193:;
            if (!t192) goto ie189;
            {
                void * t197 = gm_l0;
                uint64_t t198 = gm_l2;
                uint64_t t199 = gm_l1;
                gm_tf5srh4mp4b3i t200;
                GM_ST(uint8_t, (uint8_t *)&t200 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t200 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t200 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t200), t199);
                ((uint8_t *)&t200)[8] = 1;
                gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
                void * t202 = gm_l8;
                gm_t88rcucg1b7uk t203 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t197, t198, t200, t201, t202);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t203;
            }
            {
                gm_t6qooom8qa2do t204;
                void * t206 = gm_l0;
                void * t207 = (void *)(((uint8_t *)t206) + 8);
                gm_t88rcucg1b7uk t208 = gm_l14;
                uint8_t t209 = gm_f_is_error_8k27gd1(t207, GM_CONV(gm_csguankko4lmhd, t208));
                if (!t209) goto ie205;
                gm_t6qooom8qa2do t211;
                memset(&t211, 0, sizeof t211);
                GM_ST(uint8_t, ((uint8_t *)&t211 + 0), ((uint8_t)11ULL));
                gm_t6qooom8qa2do t212 = t211;
                t204 = t212;
                goto ix205;
ie205:;
                gm_t88rcucg1b7uk t214 = gm_l14;
                gm_t6qooom8qa2do t215;
                memset(&t215, 0, sizeof t215);
                GM_ST(uint8_t, ((uint8_t *)&t215 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t215 + 8), t214);
                gm_t6qooom8qa2do t216 = t215;
                t204 = t216;
ix205:;
                uint8_t t217 = 0;
                F.ret = t204;
                goto gm_fret;
            }
            goto ix189;
ie189:;
ix189:;
        }
        goto me64;
mn182:;
ma72:;
        uint8_t t219 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
        if (t219 != ((uint8_t)35ULL)) goto mn218;
        void * t220 = GM_LD(void *, (((((uint8_t *)t63) + 8) + 8) + 16));
        gm_l15 = t220;
        {
            void * t223 = gm_l4;
            void * t224 = GM_LD(void *, (((uint8_t *)t223) + 24));
            uint8_t t225;
            if ((GM_LD(void *, ((uint8_t *)&t224)) != 0)) goto mn227;
            t225 = ((uint8_t)1);
            goto me226;
mn227:;
            t225 = ((uint8_t)0);
            goto me226;
mn228:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1026, 16);
me226:;
            if (!t225) goto ie222;
            void * t229 = gm_l15;
            uint8_t t230;
            uint8_t t236 = GM_LD(uint8_t, ((((uint8_t *)t229) + 8) + 0));
            switch (t236) { case ((uint8_t)0ULL): goto ma234; case ((uint8_t)1ULL): goto ma234; case ((uint8_t)2ULL): goto ma234; case ((uint8_t)3ULL): goto ma234; case ((uint8_t)4ULL): goto ma234; case ((uint8_t)5ULL): goto ma234; case ((uint8_t)6ULL): goto ma234; case ((uint8_t)7ULL): goto ma234; case ((uint8_t)8ULL): goto ma234; case ((uint8_t)9ULL): goto ma234; case ((uint8_t)10ULL): goto ma234; case ((uint8_t)11ULL): goto ma234; case ((uint8_t)12ULL): goto ma234; case ((uint8_t)13ULL): goto ma234; case ((uint8_t)14ULL): goto ma234; case ((uint8_t)15ULL): goto ma234; case ((uint8_t)16ULL): goto ma234; case ((uint8_t)17ULL): goto ma234; case ((uint8_t)18ULL): goto ma234; case ((uint8_t)19ULL): goto ma234; case ((uint8_t)20ULL): goto ma234; case ((uint8_t)21ULL): goto ma234; case ((uint8_t)22ULL): goto ma234; case ((uint8_t)23ULL): goto ma234; case ((uint8_t)24ULL): goto ma234; case ((uint8_t)25ULL): goto ma234; case ((uint8_t)26ULL): goto ma234; case ((uint8_t)27ULL): goto ma234; case ((uint8_t)28ULL): goto ma234; case ((uint8_t)29ULL): goto ma234; case ((uint8_t)30ULL): goto ma234; case ((uint8_t)31ULL): goto ma234; case ((uint8_t)32ULL): goto ma232; case ((uint8_t)33ULL): goto ma233; case ((uint8_t)34ULL): goto ma234; case ((uint8_t)35ULL): goto ma234; case ((uint8_t)36ULL): goto ma234; case ((uint8_t)37ULL): goto ma234; case ((uint8_t)38ULL): goto ma234; case ((uint8_t)39ULL): goto ma234; case ((uint8_t)40ULL): goto ma234; case ((uint8_t)41ULL): goto ma234; case ((uint8_t)42ULL): goto ma234; case ((uint8_t)43ULL): goto ma234; case ((uint8_t)44ULL): goto ma234; case ((uint8_t)45ULL): goto ma234; case ((uint8_t)46ULL): goto ma234; case ((uint8_t)47ULL): goto ma234; case ((uint8_t)48ULL): goto ma234; case ((uint8_t)49ULL): goto ma234; case ((uint8_t)50ULL): goto ma234; default: goto ma234; }
ma232:;
            uint8_t t238 = GM_LD(uint8_t, ((((uint8_t *)t229) + 8) + 0));
            if (t238 != ((uint8_t)32ULL)) goto mn237;
            t230 = ((uint8_t)1);
            goto me231;
mn237:;
ma233:;
            uint8_t t240 = GM_LD(uint8_t, ((((uint8_t *)t229) + 8) + 0));
            if (t240 != ((uint8_t)33ULL)) goto mn239;
            t230 = ((uint8_t)1);
            goto me231;
mn239:;
ma234:;
            t230 = ((uint8_t)0);
            goto me231;
mn241:;
ma235:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1026, 33);
me231:;
            if (!t230) goto ie222;
            {
                void * t243 = gm_l0;
                uint64_t t244 = gm_l2;
                uint64_t t245 = gm_l1;
                gm_tf5srh4mp4b3i t246;
                GM_ST(uint8_t, (uint8_t *)&t246 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t246 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t246 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t246), t245);
                ((uint8_t *)&t246)[8] = 1;
                gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
                void * t248 = gm_l8;
                gm_t88rcucg1b7uk t249 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t243, t244, t246, t247, t248);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t249;
            }
            {
                gm_t6qooom8qa2do t250;
                void * t252 = gm_l0;
                void * t253 = (void *)(((uint8_t *)t252) + 8);
                gm_t88rcucg1b7uk t254 = gm_l16;
                uint8_t t255 = gm_f_is_error_8k27gd1(t253, GM_CONV(gm_csguankko4lmhd, t254));
                if (!t255) goto ie251;
                gm_t6qooom8qa2do t257;
                memset(&t257, 0, sizeof t257);
                GM_ST(uint8_t, ((uint8_t *)&t257 + 0), ((uint8_t)11ULL));
                gm_t6qooom8qa2do t258 = t257;
                t250 = t258;
                goto ix251;
ie251:;
                gm_t88rcucg1b7uk t260 = gm_l16;
                gm_t6qooom8qa2do t261;
                memset(&t261, 0, sizeof t261);
                GM_ST(uint8_t, ((uint8_t *)&t261 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t261 + 8), t260);
                gm_t6qooom8qa2do t262 = t261;
                t250 = t262;
ix251:;
                uint8_t t263 = 0;
                F.ret = t250;
                goto gm_fret;
            }
            goto ix222;
ie222:;
ix222:;
        }
        goto me64;
mn218:;
ma73:;
        goto me64;
mn264:;
ma74:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 993, 5);
me64:;
    }
    {
        void * t267 = gm_l4;
        void * t268 = GM_LD(void *, (((uint8_t *)t267) + 24));
        uint8_t t269;
        if ((GM_LD(void *, ((uint8_t *)&t268)) != 0)) goto mn271;
        t269 = ((uint8_t)1);
        goto me270;
mn271:;
        t269 = ((uint8_t)0);
        goto me270;
mn272:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1034, 8);
me270:;
        if (!t269) goto ie266;
        {
            void * t275 = gm_l0;
            uint64_t t276 = gm_l2;
            void * t277 = gm_l8;
            gm_tkv1vap90vht2 t278;
            gm_f_try_untyped_fj0h8i5((void *)&t278, (void *)NULL, cx, NULL, t275, t276, t277);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t278))[208] != 0)) goto ie274;
            F.l17 = (*(gm_t5vbkdq81qfs7 *)(((uint8_t *)&t278) + 0));
            {
                gm_t6qooom8qa2do t280;
                uint8_t t283 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                if (t283 != ((uint8_t)0ULL)) goto mn282;
                gm_t0ialelq3ql1k t284 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l17) + 8));
                F.l18 = t284;
                gm_t0ialelq3ql1k t285 = F.l18;
                gm_t6qooom8qa2do t286;
                GM_ST(uint8_t, (uint8_t *)&t286 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t286 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t286 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t286 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t286 + 8), t285);
                t280 = t286;
                goto me281;
mn282:;
                uint8_t t288 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                if (t288 != ((uint8_t)1ULL)) goto mn287;
                gm_t6qooom8qa2do t289;
                memset(&t289, 0, sizeof t289);
                GM_ST(uint8_t, ((uint8_t *)&t289 + 0), ((uint8_t)11ULL));
                t280 = t289;
                goto me281;
mn287:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1036, 20);
me281:;
                uint8_t t290 = 0;
                F.ret = t280;
                goto gm_fret;
            }
            goto ix274;
ie274:;
ix274:;
        }
        goto ix266;
ie266:;
ix266:;
    }
    {
        void * t291 = gm_l0;
        uint64_t t292 = gm_l2;
        void * t293 = gm_l4;
        void * t294 = GM_LD(void *, (((uint8_t *)t293) + 24));
        void * t295 = gm_l8;
        gm_f_declare_const_or_type_4jaj4k6((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t291, t292, &(*(gm_t08olq97rscn3 *)((uint8_t *)a3)), t294, t295);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) void gm_f_compute_decl_gcf8mq9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_compute_decl_gcf8mq9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    uint64_t gm_l4; /* module */
    gm_t08olq97rscn3 gm_l5; /* name */
    uint8_t gm_l7; /* global */
    uint8_t gm_l8; /* thread_local */
    gm_tf5srh4mp4b3i gm_l9; /* fuel */
    uint8_t gm_l11; /* inline */
    gm_tbcgm40aljf9h gm_l12; /* hint_set */
    gm_t84dv0cg48dut gm_l15; /* callconv */
    gm_toceffb697p0u gm_l16; /* trap_hook */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint8_t gm_l20; /* ok */
    gm_b16_8 gm_l21; /* args */
    uint64_t gm_l24; /* n */
    uint8_t gm_l25; /* ok */
    gm_b16_8 gm_l26; /* args */
    gm_tibupt4dmcqai gm_l27; /* lit */
    gm_b16_8 gm_l28; /* args */
    uint8_t gm_l29; /* w */
    uint8_t gm_l30; /* ok */
    gm_b16_8 gm_l31; /* args */
    gm_b16_8 gm_l32; /* other */
    gm_tf5srh4mp4b3i gm_l33; /* global_align */
    uint64_t gm_l36; /* saved_fuel */
    gm_t88rcucg1b7uk gm_l37; /* ty */
    gm_tbvif938ns15l gm_l38; /* c */
    uint32_t gm_l39; /* f */
    uint32_t gm_l41; /* f */
    uint32_t gm_l42; /* f */
    gm_tl874t2atnlh3 gm_l43; /* span */
    uint32_t gm_l44; /* f */
    gm_t08olq97rscn3 gm_l45; /* e */
    uint32_t gm_l46; /* f */
    GM_STACK_CHECK(&F, 3784u);
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
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srclv0dcbbfhves, 829, 13);
        gm_t52j636u3vgk3 t7 = GM_LD(gm_t52j636u3vgk3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t3 * 216));
        F.l2 = t7;
    }
    {
        gm_tvpnktv5fo8nl t8 = GM_LD(gm_tvpnktv5fo8nl, (((uint8_t *)&F.l2) + 40));
        F.l3 = t8;
    }
    {
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 32));
        gm_l4 = t9;
    }
    {
        gm_t08olq97rscn3 t10 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l2) + 0));
        gm_l5 = t10;
    }
    {
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 64) + 0));
        if (t12 != ((uint8_t)0ULL)) goto ie11;
        gm_tv2uf4leil5hq t13 = GM_LD(gm_tv2uf4leil5hq, ((((uint8_t *)&F.l3) + 64) + 8));
        F.l6 = t13;
        {
            gm_l7 = ((uint8_t)0);
        }
        {
            gm_l8 = ((uint8_t)0);
        }
        {
            gm_tf5srh4mp4b3i t15;
            memset(&t15, 0, sizeof t15);
            gm_l9 = t15;
        }
        {
            gm_tcus0chk5r9uq t16;
            memset(&t16, 0, sizeof t16);
            F.l10 = t16;
        }
        {
            gm_l11 = ((uint8_t)0);
        }
        {
            gm_tqs5p4n2tq19v t17;
            memset(&t17, 0, sizeof t17);
            gm_tbcgm40aljf9h t18;
            GM_ST(uint32_t, ((uint8_t *)&t18 + 0), ((uint32_t)0ULL));
            GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t18 + 4), t17);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 6), ((uint8_t)0));
            GM_ST(uint8_t, ((uint8_t *)&t18 + 7), ((uint8_t)0));
            gm_l12 = t18;
        }
        {
            gm_tcus0chk5r9uq t19;
            memset(&t19, 0, sizeof t19);
            F.l13 = t19;
        }
        {
            gm_t1s75i08qtptd t20;
            memset(&t20, 0, sizeof t20);
            F.l14 = t20;
        }
        {
            gm_t84dv0cg48dut t21;
            memset(&t21, 0, sizeof t21);
            gm_l15 = t21;
        }
        {
            gm_toceffb697p0u t22;
            memset(&t22, 0, sizeof t22);
            gm_l16 = t22;
        }
        {
            {
                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
                gm_l17 = t24;
            }
            {
                gm_l18 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t26 = gm_l18;
                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    uint8_t t28 = (t26 < t27);
                    if (!t28) goto lb25;
                    {
                        uint64_t t31 = gm_l18;
                        uint64_t t32 = (uintptr_t)t31;
                        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                        gm_tkacitepg5fk0 t36 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t32 * 72));
                        F.l19 = t36;
                        {
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 6) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_sptnm8c6c52dr, 6) == 0)) goto mn39;
                            {
                                gm_l7 = ((uint8_t)1);
                            }
                            goto me38;
mn39:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 5) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_sj1k4t2de5144, 5) == 0)) goto mn41;
                            {
                                uint8_t t44;
                                if (((((uint8_t *)&F.l13))[72] != 0)) goto mn46;
                                t44 = ((uint8_t)1);
                                goto me45;
mn46:;
                                t44 = ((uint8_t)0);
                                goto me45;
mn47:;
                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 849, 20);
me45:;
                                if (!t44) goto ie43;
                                {
                                    const uint8_t *t49 = ((uint8_t *)&F.l19);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcus0chk5r9uq *)((uint8_t *)&F.l13)) + 73, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcus0chk5r9uq *)((uint8_t *)&F.l13)) + 74, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcus0chk5r9uq *)((uint8_t *)&F.l13)) + 76, 0);
                                    ((uint8_t *)&(*(gm_tcus0chk5r9uq *)((uint8_t *)&F.l13)))[72] = 1;
                                    memmove((uint8_t *)&(*(gm_tcus0chk5r9uq *)((uint8_t *)&F.l13)) + 0, t49, 72);
                                }
                                goto ix43;
ie43:;
                                {
                                    void * t51 = gm_l0;
                                    gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    gm_b16_8 t53;
                                    GM_ST(void *, (uint8_t *)&t53, (void *)(gm_scacg345bm166));
                                    GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(68));
                                    gm_b0_1 t54 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t51, t52, t53);
                                    if (gm_unw.kind) {
                                        goto gm_ll29;
                                    }
                                }
ix43:;
                            }
                            goto me38;
mn41:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 12) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s523ivho5m3dq, 12) == 0)) goto mn55;
                            {
                                gm_l8 = ((uint8_t)1);
                            }
                            goto me38;
mn55:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_stjei6jfiba56, 4) == 0)) goto mn57;
                            {
                                gm_l20 = ((uint8_t)0);
                            }
                            {
                                if (!(((((uint8_t *)&F.l19) + 32))[16] != 0)) goto ie59;
                                gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 32));
                                gm_l21 = t60;
                                uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                uint8_t t62 = ((t61 == ((uint64_t)1ULL)));
                                if (!t62) goto ie59;
                                {
                                    void * t65 = gm_l0;
                                    uint64_t t66 = gm_l4;
                                    uint64_t t67 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                    if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_srclv0dcbbfhves, 860, 52);
                                    void * t69 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t67 * 88);
                                    gm_tkv1vap90vht2 t70;
                                    gm_f_try_untyped_fj0h8i5((void *)&t70, (void *)NULL, cx, NULL, t65, t66, t69);
                                    if (gm_unw.kind) {
                                        goto gm_ll29;
                                    }
                                    if (!((((uint8_t *)&t70))[208] != 0)) goto ie64;
                                    F.l22 = (*(gm_t5vbkdq81qfs7 *)(((uint8_t *)&t70) + 0));
                                    uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
                                    if (t71 != ((uint8_t)0ULL)) goto ie64;
                                    uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 0));
                                    if (t72 != ((uint8_t)0ULL)) goto ie64;
                                    gm_tbci5ospk5nnn t73 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l22) + 8) + 8));
                                    F.l23 = t73;
                                    uint8_t t74 = gm_f_is_positive_su660ab(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l23)));
                                    if (!t74) goto ie64;
                                    {
                                        gm_tf5srh4mp4b3i t77 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l23)));
                                        if (!((((uint8_t *)&t77))[8] != 0)) goto ie76;
                                        uint64_t t78 = GM_LD(uint64_t, ((uint8_t *)&t77));
                                        gm_l24 = t78;
                                        {
                                            uint64_t t80 = gm_l24;
                                            gm_tf5srh4mp4b3i t81;
                                            GM_ST(uint8_t, (uint8_t *)&t81 + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t81 + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t81 + 12, 0);
                                            GM_ST(uint64_t, ((uint8_t *)&t81), t80);
                                            ((uint8_t *)&t81)[8] = 1;
                                            gm_l9 = t81;
                                        }
                                        {
                                            gm_l20 = ((uint8_t)1);
                                        }
                                        goto ix76;
ie76:;
ix76:;
                                    }
                                    goto ix64;
ie64:;
ix64:;
                                }
                                goto ix59;
ie59:;
ix59:;
                            }
                            {
                                uint8_t t83 = gm_l20;
                                uint8_t t84 = (uint8_t)!t83;
                                if (!t84) goto ie82;
                                {
                                    void * t86 = gm_l0;
                                    gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    gm_b16_8 t88;
                                    GM_ST(void *, (uint8_t *)&t88, (void *)(gm_st027n235d4us));
                                    GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(44));
                                    gm_b0_1 t89 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t86, t87, t88);
                                    if (gm_unw.kind) {
                                        goto gm_ll29;
                                    }
                                }
                                goto ix82;
ie82:;
ix82:;
                            }
                            goto me38;
mn57:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 6) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_spe8t3e79vnal, 6) == 0)) goto mn90;
                            {
                                gm_l25 = ((uint8_t)0);
                            }
                            {
                                if (!(((((uint8_t *)&F.l19) + 32))[16] != 0)) goto ie92;
                                gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 32));
                                gm_l26 = t93;
                                uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                                uint8_t t95 = ((t94 == ((uint64_t)1ULL)));
                                if (!t95) goto ie92;
                                uint64_t t96 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                                if (t96 >= (uintptr_t)(t97)) gm_trap_index(t96, t97, gm_srclv0dcbbfhves, 871, 73);
                                uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t96 * 88) + 8) + 0));
                                if (t98 != ((uint8_t)2ULL)) goto ie92;
                                gm_tibupt4dmcqai t99 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t96 * 88) + 8) + 8));
                                gm_l27 = t99;
                                {
                                    gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 0));
                                    gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
                                    uint8_t t104 = gm_f_utf8_valid_a8oc1t8(t103);
                                    uint8_t t105 = (uint8_t)!t104;
                                    if (!t105) goto ie101;
                                    {
                                        void * t107 = gm_l0;
                                        uint64_t t108 = (uintptr_t)((uint64_t)0ULL);
                                        uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                                        if (t108 >= (uintptr_t)(t109)) gm_trap_index(t108, t109, gm_srclv0dcbbfhves, 872, 67);
                                        gm_tl874t2atnlh3 t110 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t108 * 88) + 72));
                                        gm_b16_8 t111;
                                        GM_ST(void *, (uint8_t *)&t111, (void *)(gm_shud0j6jb8ucc));
                                        GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(36));
                                        gm_b0_1 t112 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t107, t110, t111);
                                        if (gm_unw.kind) {
                                            goto gm_ll29;
                                        }
                                    }
                                    goto ix101;
ie101:;
ix101:;
                                }
                                {
                                    gm_t08olq97rscn3 t113;
                                    memset((uint8_t *)&t113 + 28, 0, 4);
                                    gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 0));
                                    gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
                                    GM_ST(gm_b16_8, (uint8_t *)&t113 + 0, t115);
                                    gm_tl874t2atnlh3 t116 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t113 + 16, t116);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l14)) + 33, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l14)) + 34, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l14)) + 36, 0);
                                    ((uint8_t *)&(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l14)))[32] = 1;
                                    GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l14)) + 0, t113);
                                }
                                {
                                    gm_l25 = ((uint8_t)1);
                                }
                                goto ix92;
ie92:;
ix92:;
                            }
                            {
                                uint8_t t118 = gm_l25;
                                uint8_t t119 = (uint8_t)!t118;
                                if (!t119) goto ie117;
                                {
                                    void * t121 = gm_l0;
                                    gm_tl874t2atnlh3 t122 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    gm_b16_8 t123;
                                    GM_ST(void *, (uint8_t *)&t123, (void *)(gm_srsiesc4al3v4));
                                    GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(49));
                                    gm_b0_1 t124 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t121, t122, t123);
                                    if (gm_unw.kind) {
                                        goto gm_ll29;
                                    }
                                }
                                goto ix117;
ie117:;
ix117:;
                            }
                            goto me38;
mn90:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 9) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s1ft2nrvnhcbp, 9) == 0)) goto mn125;
                            {
                                if (!(((((uint8_t *)&F.l19) + 32))[16] != 0)) goto ie127;
                                gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 32));
                                gm_l28 = t128;
                                uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                uint8_t t130 = (t129 > ((uint64_t)0ULL));
                                if (!t130) goto ie127;
                                {
                                    void * t132 = gm_l0;
                                    gm_tl874t2atnlh3 t133 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    gm_b16_8 t134;
                                    GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sj2f7h0km68ut));
                                    GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(31));
                                    gm_b0_1 t135 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t132, t133, t134);
                                    if (gm_unw.kind) {
                                        goto gm_ll29;
                                    }
                                }
                                goto ix127;
ie127:;
ix127:;
                            }
                            {
                                gm_tl874t2atnlh3 t136 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                gm_toceffb697p0u t137;
                                GM_ST(uint8_t, (uint8_t *)&t137 + 13, 0);
                                GM_ST(uint16_t, (uint8_t *)&t137 + 14, 0);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t137), t136);
                                ((uint8_t *)&t137)[12] = 1;
                                gm_l16 = t137;
                            }
                            goto me38;
mn125:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 8) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_spvg0rt0dloah, 8) == 0)) goto mn138;
                            {
                                uint8_t t141;
                                if (((((uint8_t *)&gm_l15))[16] != 0)) goto mn143;
                                t141 = ((uint8_t)0);
                                goto me142;
mn143:;
                                t141 = ((uint8_t)1);
                                goto me142;
mn144:;
                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 883, 20);
me142:;
                                if (!t141) goto ie140;
                                {
                                    void * t146 = gm_l0;
                                    gm_tl874t2atnlh3 t147 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    gm_b16_8 t148;
                                    GM_ST(void *, (uint8_t *)&t148, (void *)(gm_str26682vod5l));
                                    GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(37));
                                    gm_b0_1 t149 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t146, t147, t148);
                                    if (gm_unw.kind) {
                                        goto gm_ll29;
                                    }
                                }
                                goto ix140;
ie140:;
ix140:;
                            }
                            {
                                gm_t84dv0cg48dut t150;
                                memset(&t150, 0, sizeof t150);
                                gm_l15 = t150;
                            }
                            {
                                void * t152 = gm_l0;
                                void * t153 = (void *)((uint8_t *)&F.l19);
                                gm_tqs5p4n2tq19v t154 = gm_f_callconv_attr_dd08ovc((void *)NULL, cx, NULL, t152, t153);
                                if (gm_unw.kind) {
                                    goto gm_ll29;
                                }
                                if (!((((uint8_t *)&t154))[1] != 0)) goto ie151;
                                uint8_t t155 = GM_LD(uint8_t, ((uint8_t *)&t154));
                                gm_l29 = t155;
                                {
                                    uint8_t t157 = gm_l29;
                                    gm_tl874t2atnlh3 t158 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    gm_tbvif938ns15l t159;
                                    GM_ST(uint8_t, (uint8_t *)&t159 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t159 + 2, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t159 + 0), t157);
                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t159 + 4), t158);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t84dv0cg48dut *)((uint8_t *)&gm_l15)) + 17, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t84dv0cg48dut *)((uint8_t *)&gm_l15)) + 18, 0);
                                    ((uint8_t *)&(*(gm_t84dv0cg48dut *)((uint8_t *)&gm_l15)))[16] = 1;
                                    GM_ST(gm_tbvif938ns15l, (uint8_t *)&(*(gm_t84dv0cg48dut *)((uint8_t *)&gm_l15)) + 0, t159);
                                }
                                goto ix151;
ie151:;
ix151:;
                            }
                            goto me38;
mn138:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 6) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s9e3360r5ee0o, 6) == 0)) goto pa162;
                            goto po161;
pa162:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 8) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s7f3pdsji9g3a, 8) == 0)) goto pa163;
                            goto po161;
pa163:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_sq4sf3euttrc8, 4) == 0)) goto pa164;
                            goto po161;
pa164:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 9) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_sinoph5a0op58, 9) == 0)) goto pa165;
                            goto po161;
pa165:;
                            goto mn160;
po161:;
                            {
                                gm_b16_8 t168 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l19) + 0) + 0));
                                gm_t6j6smj7qoctu t169;
                                gm_b16_8 t170;
                                GM_ST(void *, (uint8_t *)&t170, (void *)(gm_s9e3360r5ee0o));
                                GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(6));
                                GM_ST(gm_b16_8, (uint8_t *)&t169 + 0, t170);
                                gm_b16_8 t171;
                                GM_ST(void *, (uint8_t *)&t171, (void *)(gm_s7f3pdsji9g3a));
                                GM_ST(uintptr_t, (uint8_t *)&t171 + 8, (uintptr_t)(8));
                                GM_ST(gm_b16_8, (uint8_t *)&t169 + 16, t171);
                                uint8_t t172 = 0;
                                if (gm_eq0iqiubfm7e(((uint8_t *)&t169) + 0, ((uint8_t *)&t168))) t172 = 1;
                                if (gm_eq0iqiubfm7e(((uint8_t *)&t169) + 16, ((uint8_t *)&t168))) t172 = 1;
                                uint8_t t173 = (uint8_t)(t172);
                                if (!t173) goto ie167;
                                {
                                    uint8_t t175 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 6) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s9e3360r5ee0o, 6) == 0));
                                    gm_l11 = t175;
                                }
                                {
                                    uint8_t t176 = gm_l11;
                                    gm_tqs5p4n2tq19v t177;
                                    GM_ST(uint8_t, ((uint8_t *)&t177), t176);
                                    ((uint8_t *)&t177)[1] = 1;
                                    GM_ST(gm_tqs5p4n2tq19v, (((uint8_t *)&gm_l12) + 4), t177);
                                }
                                goto ix167;
ie167:;
                                uint8_t t179 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_sq4sf3euttrc8, 4) == 0));
                                if (!t179) goto ie178;
                                {
                                    GM_ST(uint8_t, (((uint8_t *)&gm_l12) + 6), ((uint8_t)1));
                                }
                                goto ix178;
ie178:;
                                {
                                    GM_ST(uint8_t, (((uint8_t *)&gm_l12) + 7), ((uint8_t)1));
                                }
ix178:;
ix167:;
                            }
                            {
                                const uint8_t *t182 = ((uint8_t *)&F.l19);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcus0chk5r9uq *)((uint8_t *)&F.l10)) + 73, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcus0chk5r9uq *)((uint8_t *)&F.l10)) + 74, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcus0chk5r9uq *)((uint8_t *)&F.l10)) + 76, 0);
                                ((uint8_t *)&(*(gm_tcus0chk5r9uq *)((uint8_t *)&F.l10)))[72] = 1;
                                memmove((uint8_t *)&(*(gm_tcus0chk5r9uq *)((uint8_t *)&F.l10)) + 0, t182, 72);
                            }
                            goto me38;
mn160:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 5) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_slb6cj7ndgvdm, 5) == 0)) goto mn183;
                            void * t184 = gm_l0;
                            void * t185 = (void *)((uint8_t *)&F.l19);
                            gm_b0_1 t186 = gm_f_check_allow_attr_3321gm7((void *)NULL, cx, NULL, t184, t185);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            goto me38;
mn183:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 10) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_smo0niiqr9onn, 10) == 0)) goto mn187;
                            {
                                gm_l30 = ((uint8_t)1);
                            }
                            {
                                if (!(((((uint8_t *)&F.l19) + 32))[16] != 0)) goto ie189;
                                gm_b16_8 t190 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 32));
                                gm_l31 = t190;
                                {
                                    uint8_t t192 = 0;
                                    uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                                    uint8_t t194 = ((t193 == ((uint64_t)1ULL)));
                                    if (t194) {
                                        uint64_t t195 = (uintptr_t)((uint64_t)0ULL);
                                        uint64_t t196 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                                        if (t195 >= (uintptr_t)(t196)) gm_trap_index(t195, t196, gm_srclv0dcbbfhves, 905, 44);
                                        uint8_t t197;
                                        uint8_t t200 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l31)) + t195 * 88) + 8) + 0));
                                        if (t200 != ((uint8_t)2ULL)) goto mn199;
                                        t197 = ((uint8_t)1);
                                        goto me198;
mn199:;
                                        t197 = ((uint8_t)0);
                                        goto me198;
mn201:;
                                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 905, 44);
me198:;
                                        t192 = t197;
                                    } else t192 = 0;
                                    gm_l30 = t192;
                                }
                                goto ix189;
ie189:;
ix189:;
                            }
                            {
                                uint8_t t203 = gm_l30;
                                uint8_t t204 = (uint8_t)!t203;
                                if (!t204) goto ie202;
                                {
                                    void * t206 = gm_l0;
                                    gm_tl874t2atnlh3 t207 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    gm_b16_8 t208;
                                    GM_ST(void *, (uint8_t *)&t208, (void *)(gm_sppcsr92ukfol));
                                    GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(63));
                                    gm_b0_1 t209 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t206, t207, t208);
                                    if (gm_unw.kind) {
                                        goto gm_ll29;
                                    }
                                }
                                goto ix202;
ie202:;
ix202:;
                            }
                            goto me38;
mn187:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 6) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s2ph9f020upd2, 6) == 0)) goto pa212;
                            goto po211;
pa212:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s6vvtrmtnt3j1, 4) == 0)) goto pa213;
                            goto po211;
pa213:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 7) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_suuv0qpi4v4e4, 7) == 0)) goto pa214;
                            goto po211;
pa214:;
                            goto mn210;
po211:;
                            void * t215 = gm_l0;
                            gm_tl874t2atnlh3 t216 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                            gm_b16_8 t217 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l19) + 0) + 0));
                            gm_tnpifv20irnph t218;
                            GM_ST(gm_b16_8, ((uint8_t *)&t218 + 0), t217);
                            gm_tabb710d9mq2e t219;
                            gm_f_format_0dvj28f((void *)&t219, (void *)NULL, cx, NULL, t218);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            if (((((uint8_t *)&t219))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t219) + 8, gm_srclv0dcbbfhves, 909, 63);
                            gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)&t219) + 8));
                            gm_b16_8 t221 = GM_LD(gm_b16_8, ((uint8_t *)&t220));
                            gm_b0_1 t222 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t215, t216, t221);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            goto me38;
mn210:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 9) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s80p0ld1j6rfg, 9) == 0)) goto mn223;
                            void * t224 = gm_l0;
                            gm_tl874t2atnlh3 t225 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                            gm_b16_8 t226;
                            GM_ST(void *, (uint8_t *)&t226, (void *)(gm_sg7i6m1frjk5n));
                            GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(62));
                            gm_b0_1 t227 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t224, t225, t226);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            goto me38;
mn223:;
                            gm_b16_8 t229 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l19) + 0) + 0));
                            gm_l32 = t229;
                            void * t230 = gm_l0;
                            gm_tl874t2atnlh3 t231 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                            gm_b16_8 t232 = gm_l32;
                            gm_tnpifv20irnph t233;
                            GM_ST(gm_b16_8, ((uint8_t *)&t233 + 0), t232);
                            gm_tabb710d9mq2e t234;
                            gm_f_format_u2mas3v((void *)&t234, (void *)NULL, cx, NULL, t233);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            if (((((uint8_t *)&t234))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t234) + 8, gm_srclv0dcbbfhves, 911, 41);
                            gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&t234) + 8));
                            gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
                            gm_b0_1 t237 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t230, t231, t236);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            goto me38;
mn228:;
                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 845, 9);
me38:;
                        }
                        goto me34;
mn35:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 844, 5);
me34:;
                    }
lc25:;
                    {
                        uint64_t t239 = gm_l18;
                        uint64_t r240; bool o241 = __builtin_add_overflow(t239, ((uint64_t)1ULL), &r240);
                        uint64_t t242 = r240;
                        gm_l18 = t242;
                    }
                }
gm_ll29:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb25; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc25; }
                }
                goto gm_lad;
lb25:;
            }
        }
        {
            uint8_t t244 = gm_l8;
            if (!t244) goto ie243;
            uint8_t t245 = gm_l7;
            uint8_t t246 = (uint8_t)!t245;
            if (!t246) goto ie243;
            {
                void * t248 = gm_l0;
                gm_tl874t2atnlh3 t249 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 16));
                gm_b16_8 t250;
                GM_ST(void *, (uint8_t *)&t250, (void *)(gm_s8ro53eq54s60));
                GM_ST(uintptr_t, (uint8_t *)&t250 + 8, (uintptr_t)(43));
                gm_b0_1 t251 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t248, t249, t250);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix243;
ie243:;
ix243:;
        }
        {
            gm_tf5srh4mp4b3i t252;
            memset(&t252, 0, sizeof t252);
            gm_l33 = t252;
        }
        {
            if (!((((uint8_t *)&F.l13))[72] != 0)) goto ie253;
            gm_tkacitepg5fk0 t254 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)&F.l13));
            F.l34 = t254;
            {
                uint8_t t257 = gm_l7;
                if (!t257) goto ie256;
                {
                    void * t259 = gm_l0;
                    uint64_t t260 = gm_l4;
                    void * t261 = (void *)((uint8_t *)&F.l34);
                    gm_tf5srh4mp4b3i t262 = gm_f_attr_power_of_two_gj02n2t((void *)NULL, cx, NULL, t259, t260, t261);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l33 = t262;
                }
                goto ix256;
ie256:;
                {
                    void * t264 = gm_l0;
                    gm_tl874t2atnlh3 t265 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l34) + 56));
                    gm_b16_8 t266;
                    GM_ST(void *, (uint8_t *)&t266, (void *)(gm_sbqv06vnui8sl));
                    GM_ST(uintptr_t, (uint8_t *)&t266 + 8, (uintptr_t)(87));
                    gm_b0_1 t267 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t264, t265, t266);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix256:;
            }
            goto ix253;
ie253:;
ix253:;
        }
        {
            memset(&F.l35, 0, sizeof F.l35);
            GM_ST(uint8_t, (uint8_t *)&F.l35 + 0, ((uint8_t)11ULL));
        }
        {
            void * t268 = gm_l0;
            uint64_t t269 = GM_LD(uint64_t, (((uint8_t *)t268) + 2312));
            gm_l36 = t269;
        }
        {
            void * t270 = gm_l0;
            gm_tf5srh4mp4b3i t271 = gm_l9;
            uint64_t t272;
            if (((((uint8_t *)&t271))[8] != 0)) {
                uint64_t t273 = GM_LD(uint64_t, (((uint8_t *)&t271) + 0));
                t272 = t273;
            } else {
                uint64_t t274 = GM_LD(uint64_t, gm_c1n574q3spi);
                t272 = t274;
            }
            GM_ST(uint64_t, (((uint8_t *)t270) + 2312), t272);
        }
        {
            uint8_t t276 = gm_l7;
            if (!t276) goto ie275;
            {
                void * t278 = gm_l0;
                uint64_t t279 = gm_l4;
                void * t280 = (void *)((uint8_t *)&F.l6);
                gm_f_declare_global_729nttf((void *)&(*(gm_t6qooom8qa2do *)((uint8_t *)&F.l35)), (void *)NULL, cx, NULL, t278, t279, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l5)), t280);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint8_t t282 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t282 != ((uint8_t)10ULL)) goto ie281;
                gm_t88rcucg1b7uk t283 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l35) + 8));
                gm_l37 = t283;
                {
                    void * t285 = gm_l0;
                    void * t286 = (void *)(((uint8_t *)t285) + 1448);
                    gm_tq7qjpkmqmc5s t287;
                    memset((uint8_t *)&t287 + 28, 0, 4);
                    memset((uint8_t *)&t287 + 49, 0, 7);
                    uint64_t t288 = gm_l1;
                    GM_ST(uint64_t, (uint8_t *)&t287 + 0, t288);
                    gm_b16_8 t289 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t287 + 8, t289);
                    gm_t88rcucg1b7uk t290 = gm_l37;
                    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t287 + 24, t290);
                    uint64_t t291 = gm_l4;
                    GM_ST(uint64_t, (uint8_t *)&t287 + 32, t291);
                    void * t292 = GM_LD(void *, (((uint8_t *)&F.l6) + 40));
                    GM_ST(void *, (uint8_t *)&t287 + 40, t292);
                    uint8_t t293 = gm_l8;
                    GM_ST(uint8_t, (uint8_t *)&t287 + 48, t293);
                    gm_th2lqbo77pvsc t294;
                    memset(&t294, 0, sizeof t294);
                    GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t287 + 56, t294);
                    gm_tf5srh4mp4b3i t295 = gm_l33;
                    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t287 + 80, t295);
                    GM_ST(void *, (uint8_t *)&t287 + 96, ((void *)0));
                    gm_tftb1odc739t5 t296 = gm_f_push_4p3ivmb(t286, &t287);
                    if (((((uint8_t *)&t296))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t296) + 1, gm_srclv0dcbbfhves, 929, 13);
                    gm_b0_1 t297 = GM_LD(gm_b0_1, (((uint8_t *)&t296) + 1));
                }
                goto ix281;
ie281:;
ix281:;
            }
            goto ix275;
ie275:;
            {
                void * t299 = gm_l0;
                uint64_t t300 = gm_l1;
                uint64_t t301 = gm_l4;
                void * t302 = (void *)((uint8_t *)&F.l6);
                gm_f_compute_binding_cl3ogob((void *)&(*(gm_t6qooom8qa2do *)((uint8_t *)&F.l35)), (void *)NULL, cx, NULL, t299, t300, t301, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l5)), t302);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix275:;
        }
        {
            void * t303 = gm_l0;
            uint64_t t304 = gm_l36;
            GM_ST(uint64_t, (((uint8_t *)t303) + 2312), t304);
        }
        {
            if (!((((uint8_t *)&gm_l15))[16] != 0)) goto ie305;
            gm_tbvif938ns15l t306 = GM_LD(gm_tbvif938ns15l, ((uint8_t *)&gm_l15));
            gm_l38 = t306;
            {
                uint8_t t313 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                switch (t313) { case ((uint8_t)0ULL): goto ma311; case ((uint8_t)1ULL): goto ma311; case ((uint8_t)2ULL): goto ma311; case ((uint8_t)3ULL): goto ma311; case ((uint8_t)4ULL): goto ma311; case ((uint8_t)5ULL): goto ma309; case ((uint8_t)6ULL): goto ma311; case ((uint8_t)7ULL): goto ma311; case ((uint8_t)8ULL): goto ma311; case ((uint8_t)9ULL): goto ma311; case ((uint8_t)10ULL): goto ma311; case ((uint8_t)11ULL): goto ma310; default: goto ma311; }
ma309:;
                uint8_t t315 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t315 != ((uint8_t)5ULL)) goto mn314;
                uint32_t t316 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 8));
                gm_l39 = t316;
                void * t317 = gm_l0;
                uint32_t t318 = gm_l39;
                uint8_t t319 = GM_LD(uint8_t, (((uint8_t *)&gm_l38) + 0));
                gm_tl874t2atnlh3 t320 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l38) + 4));
                gm_b0_1 t321 = gm_f_apply_callconv_197l87p((void *)NULL, cx, NULL, t317, t318, t319, t320);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me308;
mn314:;
ma310:;
                uint8_t t323 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t323 != ((uint8_t)11ULL)) goto mn322;
                goto me308;
mn322:;
ma311:;
                void * t326 = gm_l0;
                gm_tl874t2atnlh3 t327 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l38) + 4));
                gm_b16_8 t328;
                GM_ST(void *, (uint8_t *)&t328, (void *)(gm_srb1158jlk9u7));
                GM_ST(uintptr_t, (uint8_t *)&t328 + 8, (uintptr_t)(74));
                gm_b0_1 t329 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t326, t327, t328);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me308;
mn325:;
ma312:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 936, 9);
me308:;
            }
            goto ix305;
ie305:;
ix305:;
        }
        {
            if (!((((uint8_t *)&F.l10))[72] != 0)) goto ie330;
            gm_tkacitepg5fk0 t331 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)&F.l10));
            F.l40 = t331;
            {
                uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t334 != ((uint8_t)5ULL)) goto ie333;
                uint32_t t335 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 8));
                gm_l41 = t335;
                uint8_t t336 = gm_l11;
                if (!t336) goto ie333;
                {
                    void * t338 = gm_l0;
                    void * t339 = (void *)(((uint8_t *)t338) + 800);
                    uint32_t t340 = gm_l41;
                    gm_tl874t2atnlh3 t341 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l40) + 56));
                    gm_t9d4sf0vqn7dt t342;
                    GM_ST(uint32_t, ((uint8_t *)&t342 + 0), t340);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t342 + 4), t341);
                    gm_tftb1odc739t5 t343 = gm_f_push_89d0k90(t339, GM_CONV(gm_csedogq9e4rp5r, t342));
                    if (((((uint8_t *)&t343))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t343) + 1, gm_srclv0dcbbfhves, 943, 37);
                    gm_b0_1 t344 = GM_LD(gm_b0_1, (((uint8_t *)&t343) + 1));
                }
                goto ix333;
ie333:;
ix333:;
            }
            {
                uint8_t t346 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t346 != ((uint8_t)5ULL)) goto ie345;
                uint32_t t347 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 8));
                gm_l42 = t347;
                {
                    uint32_t t349 = gm_l42;
                    GM_ST(uint32_t, (((uint8_t *)&gm_l12) + 0), t349);
                }
                {
                    void * t350 = gm_l0;
                    void * t351 = (void *)(((uint8_t *)t350) + 840);
                    gm_tbcgm40aljf9h t352 = gm_l12;
                    gm_tftb1odc739t5 t353 = gm_f_push_0fe09r3(t351, t352);
                    if (((((uint8_t *)&t353))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t353) + 1, gm_srclv0dcbbfhves, 946, 13);
                    gm_b0_1 t354 = GM_LD(gm_b0_1, (((uint8_t *)&t353) + 1));
                }
                goto ix345;
ie345:;
ix345:;
            }
            {
                uint8_t t356;
                uint8_t t359 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t359 != ((uint8_t)5ULL)) goto mn358;
                t356 = ((uint8_t)0);
                goto me357;
mn358:;
                t356 = ((uint8_t)1);
                goto me357;
mn360:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 948, 12);
me357:;
                if (!t356) goto ie355;
                uint8_t t361 = (!(GM_LD(uint8_t, ((uint8_t *)&F.l35) + 0) == ((uint8_t)11ULL)));
                if (!t361) goto ie355;
                {
                    void * t363 = gm_l0;
                    gm_tl874t2atnlh3 t364 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l40) + 56));
                    gm_b16_8 t365 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l40) + 0) + 0));
                    gm_tnpifv20irnph t366;
                    GM_ST(gm_b16_8, ((uint8_t *)&t366 + 0), t365);
                    gm_tabb710d9mq2e t367;
                    gm_f_format_28p84ds((void *)&t367, (void *)NULL, cx, NULL, t366);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t367))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t367) + 8, gm_srclv0dcbbfhves, 949, 29);
                    gm_b16_8 t368 = GM_LD(gm_b16_8, (((uint8_t *)&t367) + 8));
                    gm_b16_8 t369 = GM_LD(gm_b16_8, ((uint8_t *)&t368));
                    gm_b0_1 t370 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t363, t364, t369);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix355;
ie355:;
ix355:;
            }
            goto ix330;
ie330:;
ix330:;
        }
        {
            if (!((((uint8_t *)&gm_l16))[12] != 0)) goto ie371;
            gm_tl874t2atnlh3 t372 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l16));
            gm_l43 = t372;
            {
                uint8_t t379 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                switch (t379) { case ((uint8_t)0ULL): goto ma377; case ((uint8_t)1ULL): goto ma377; case ((uint8_t)2ULL): goto ma377; case ((uint8_t)3ULL): goto ma377; case ((uint8_t)4ULL): goto ma377; case ((uint8_t)5ULL): goto ma375; case ((uint8_t)6ULL): goto ma377; case ((uint8_t)7ULL): goto ma377; case ((uint8_t)8ULL): goto ma377; case ((uint8_t)9ULL): goto ma377; case ((uint8_t)10ULL): goto ma377; case ((uint8_t)11ULL): goto ma376; default: goto ma377; }
ma375:;
                uint8_t t381 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t381 != ((uint8_t)5ULL)) goto mn380;
                uint32_t t382 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 8));
                gm_l44 = t382;
                void * t383 = gm_l0;
                uint32_t t384 = gm_l44;
                gm_tl874t2atnlh3 t385 = gm_l43;
                gm_b0_1 t386 = gm_f_trap_hook_fn_5c0a4ds((void *)NULL, cx, NULL, t383, t384, t385);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me374;
mn380:;
ma376:;
                uint8_t t388 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t388 != ((uint8_t)11ULL)) goto mn387;
                goto me374;
mn387:;
ma377:;
                void * t391 = gm_l0;
                gm_tl874t2atnlh3 t392 = gm_l43;
                gm_b16_8 t393;
                GM_ST(void *, (uint8_t *)&t393, (void *)(gm_sv48vll3qortv));
                GM_ST(uintptr_t, (uint8_t *)&t393 + 8, (uintptr_t)(39));
                gm_b0_1 t394 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t391, t392, t393);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me374;
mn390:;
ma378:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 953, 9);
me374:;
            }
            goto ix371;
ie371:;
ix371:;
        }
        {
            if (!((((uint8_t *)&F.l14))[32] != 0)) goto ie395;
            gm_t08olq97rscn3 t396 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&F.l14));
            gm_l45 = t396;
            {
                uint8_t t403 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                switch (t403) { case ((uint8_t)0ULL): goto ma401; case ((uint8_t)1ULL): goto ma401; case ((uint8_t)2ULL): goto ma401; case ((uint8_t)3ULL): goto ma401; case ((uint8_t)4ULL): goto ma401; case ((uint8_t)5ULL): goto ma399; case ((uint8_t)6ULL): goto ma401; case ((uint8_t)7ULL): goto ma401; case ((uint8_t)8ULL): goto ma401; case ((uint8_t)9ULL): goto ma401; case ((uint8_t)10ULL): goto ma401; case ((uint8_t)11ULL): goto ma400; default: goto ma401; }
ma399:;
                uint8_t t405 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t405 != ((uint8_t)5ULL)) goto mn404;
                uint32_t t406 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 8));
                gm_l46 = t406;
                void * t407 = gm_l0;
                uint32_t t408 = gm_l46;
                gm_b16_8 t409 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 0));
                gm_tl874t2atnlh3 t410 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l45) + 16));
                gm_b0_1 t411 = gm_f_export_fn_8hcuasc((void *)NULL, cx, NULL, t407, t408, t409, t410);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me398;
mn404:;
ma400:;
                uint8_t t413 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t413 != ((uint8_t)11ULL)) goto mn412;
                goto me398;
mn412:;
ma401:;
                void * t416 = gm_l0;
                gm_tl874t2atnlh3 t417 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l45) + 16));
                gm_b16_8 t418;
                GM_ST(void *, (uint8_t *)&t418, (void *)(gm_sj3lfuqd4f1np));
                GM_ST(uintptr_t, (uint8_t *)&t418 + 8, (uintptr_t)(36));
                gm_b0_1 t419 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t416, t417, t418);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me398;
mn415:;
ma402:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 960, 9);
me398:;
            }
            goto ix395;
ie395:;
ix395:;
        }
        {
            memmove(gm_out, ((uint8_t *)&F.l35), 208);
            goto gm_fdone;
        }
        goto ix11;
ie11:;
        {
            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)11ULL));
            goto gm_fdone;
        }
ix11:;
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
extern __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_declare_const_or_type_4jaj4k6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3, void * a4) {
    struct gm_fr_declare_const_or_type_4jaj4k6 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* annot */
    void * gm_l4; /* init */
    gm_tba9024q2up98 gm_l5; /* found */
    uint64_t gm_l6; /* d */
    void * gm_l8; /* callee */
    gm_tba9024q2up98 gm_l9; /* found */
    uint64_t gm_l10; /* d */
    gm_tiilmfd16v3fc gm_l11; /* r */
    gm_t88rcucg1b7uk gm_l12; /* ty */
    gm_tkn42u7na3t81 gm_l13; /* expected */
    void * gm_l14; /* a */
    gm_t88rcucg1b7uk gm_l15; /* t */
    void * gm_l16; /* hook */
    uint32_t gm_l17; /* id */
    gm_t88rcucg1b7uk gm_l18; /* ty */
    gm_t88rcucg1b7uk gm_l19; /* bad */
    GM_STACK_CHECK(&F, 2574u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t3 = gm_l3;
        uint8_t t4;
        if ((GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto mn6;
        t4 = ((uint8_t)1);
        goto me5;
mn6:;
        t4 = ((uint8_t)0);
        goto me5;
mn7:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1047, 8);
me5:;
        if (!t4) goto ie2;
        {
            void * t10 = gm_l0;
            uint64_t t11 = gm_l1;
            void * t12 = gm_l4;
            gm_t89h15e2c5o3p t13;
            gm_f_aliased_decl_jv6ilmn((void *)&t13, (void *)NULL, cx, NULL, t10, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t13))[16] != 0)) goto ie9;
            gm_tba9024q2up98 t14 = GM_LD(gm_tba9024q2up98, ((uint8_t *)&t13));
            gm_l5 = t14;
            {
                gm_tf5srh4mp4b3i t16 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l5) + 0));
                uint64_t t17;
                if (((((uint8_t *)&t16))[8] != 0)) {
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                    t17 = t18;
                } else {
                    memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)11ULL));
                    goto gm_fdone;
                }
                gm_l6 = t17;
            }
            {
                void * t19 = gm_l0;
                uint64_t t20 = gm_l6;
                gm_f_resolve_decl_pus042v((void *)&F.l7, (void *)NULL, cx, NULL, t19, t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                if (t25 != ((uint8_t)7ULL)) goto pa24;
                goto po23;
pa24:;
                uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                if (t27 != ((uint8_t)10ULL)) goto pa26;
                goto po23;
pa26:;
                goto mn22;
po23:;
                goto me21;
mn22:;
                memmove(gm_out, ((uint8_t *)&F.l7), 208);
                goto gm_fdone;
                goto me21;
mn29:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1052, 13);
me21:;
            }
            goto ix9;
ie9:;
ix9:;
        }
        {
            void * t31 = gm_l4;
            uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t31) + 8) + 0));
            if (t32 != ((uint8_t)19ULL)) goto ie30;
            void * t33 = GM_LD(void *, (((((uint8_t *)t31) + 8) + 8) + 0));
            gm_l8 = t33;
            {
                void * t36 = gm_l0;
                uint64_t t37 = gm_l1;
                void * t38 = gm_l8;
                gm_t89h15e2c5o3p t39;
                gm_f_aliased_decl_jv6ilmn((void *)&t39, (void *)NULL, cx, NULL, t36, t37, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t39))[16] != 0)) goto ie35;
                gm_tba9024q2up98 t40 = GM_LD(gm_tba9024q2up98, ((uint8_t *)&t39));
                gm_l9 = t40;
                {
                    if (!(((((uint8_t *)&gm_l9) + 0))[8] != 0)) goto ie42;
                    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 0));
                    gm_l10 = t43;
                    {
                        void * t46 = gm_l0;
                        uint64_t t47 = gm_l10;
                        gm_t6qooom8qa2do t48;
                        gm_f_resolve_decl_pus042v((void *)&t48, (void *)NULL, cx, NULL, t46, t47);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t49;
                        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
                        if (t52 != ((uint8_t)2ULL)) goto mn51;
                        t49 = ((uint8_t)1);
                        goto me50;
mn51:;
                        t49 = ((uint8_t)0);
                        goto me50;
mn53:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1061, 24);
me50:;
                        if (!t49) goto ie45;
                        {
                            void * t55 = gm_l0;
                            uint64_t t56 = gm_l1;
                            void * t57 = gm_l4;
                            gm_tqu3m2l3n8mnc t58;
                            gm_f_resolve_effect_ref_bggirs2((void *)&t58, (void *)NULL, cx, NULL, t55, t56, t57);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tiilmfd16v3fc t59;
                            if (((((uint8_t *)&t58))[24] != 0)) {
                                t59 = (*(gm_tiilmfd16v3fc *)(((uint8_t *)&t58) + 0));
                            } else {
                                memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)11ULL));
                                goto gm_fdone;
                            }
                            gm_l11 = t59;
                        }
                        {
                            const uint8_t *t60 = ((uint8_t *)&gm_l11);
                            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)3ULL));
                            memmove((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 8, t60, 24);
                            goto gm_fdone;
                        }
                        goto ix45;
ie45:;
ix45:;
                    }
                    goto ix42;
ie42:;
ix42:;
                }
                goto ix35;
ie35:;
ix35:;
            }
            goto ix30;
ie30:;
ix30:;
        }
        {
            void * t62 = gm_l0;
            uint64_t t63 = gm_l1;
            void * t64 = gm_l4;
            uint8_t t65 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t62, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t65) goto ie61;
            {
                void * t67 = gm_l0;
                uint64_t t68 = gm_l1;
                void * t69 = gm_l4;
                gm_t88rcucg1b7uk t70 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t67, t68, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t70;
            }
            {
                gm_t6qooom8qa2do t71;
                void * t73 = gm_l0;
                void * t74 = (void *)(((uint8_t *)t73) + 8);
                gm_t88rcucg1b7uk t75 = gm_l12;
                uint8_t t76 = gm_f_is_error_8k27gd1(t74, GM_CONV(gm_csguankko4lmhd, t75));
                if (!t76) goto ie72;
                gm_t6qooom8qa2do t78;
                memset(&t78, 0, sizeof t78);
                GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)11ULL));
                gm_t6qooom8qa2do t79 = t78;
                t71 = t79;
                goto ix72;
ie72:;
                gm_t88rcucg1b7uk t81 = gm_l12;
                gm_t6qooom8qa2do t82;
                memset(&t82, 0, sizeof t82);
                GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t82 + 8), t81);
                gm_t6qooom8qa2do t83 = t82;
                t71 = t83;
ix72:;
                uint8_t t84 = 0;
                F.ret = t71;
                goto gm_fret;
            }
            goto ix61;
ie61:;
ix61:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_tkn42u7na3t81 t85;
        memset(&t85, 0, sizeof t85);
        gm_l13 = t85;
    }
    {
        void * t87 = gm_l3;
        if (!(GM_LD(void *, ((uint8_t *)&t87)) != 0)) goto ie86;
        void * t88 = GM_LD(void *, ((uint8_t *)&t87));
        gm_l14 = t88;
        {
            void * t90 = gm_l0;
            uint64_t t91 = gm_l1;
            void * t92 = gm_l14;
            gm_t88rcucg1b7uk t93 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t90, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t94;
            GM_ST(uint8_t, (uint8_t *)&t94 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t94 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t94), t93);
            ((uint8_t *)&t94)[4] = 1;
            gm_l13 = t94;
        }
        goto ix86;
ie86:;
ix86:;
    }
    {
        if (!((((uint8_t *)&gm_l13))[4] != 0)) goto ie95;
        gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l13));
        gm_l15 = t96;
        void * t97 = gm_l0;
        void * t98 = (void *)(((uint8_t *)t97) + 8);
        gm_t88rcucg1b7uk t99 = gm_l15;
        uint8_t t100 = gm_f_is_error_8k27gd1(t98, GM_CONV(gm_csguankko4lmhd, t99));
        if (!t100) goto ie95;
        {
            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)11ULL));
            goto gm_fdone;
        }
        goto ix95;
ie95:;
ix95:;
    }
    {
        void * t103 = gm_l0;
        void * t104 = GM_LD(void *, (((uint8_t *)t103) + 2168));
        if (!(GM_LD(void *, ((uint8_t *)&t104)) != 0)) goto ie102;
        void * t105 = GM_LD(void *, ((uint8_t *)&t104));
        gm_l16 = t105;
        void * t106 = gm_l0;
        uint64_t t107 = gm_l1;
        void * t108 = gm_l4;
        gm_tkn42u7na3t81 t109 = gm_l13;
        gm_tkn42u7na3t81 t110 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t106, t107, t108, t109);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t111;
        if (((((uint8_t *)&t110))[4] != 0)) goto mn113;
        t111 = ((uint8_t)1);
        goto me112;
mn113:;
        t111 = ((uint8_t)0);
        goto me112;
mn114:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1078, 32);
me112:;
        if (!t111) goto ie102;
        {
            void * t116 = gm_l16;
            void * t117 = gm_l0;
            uint64_t t118 = gm_l1;
            void * t119 = gm_l4;
            gm_tkn42u7na3t81 t120 = gm_l13;
            gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            gm_tl874t2atnlh3 t122 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
            void * t123 = t116;
            gm_t9805qtonbsag t124;
            if (GM_PTR_TAGGED(t123)) {
                t124 = ((gm_t9805qtonbsag (*)(void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t123))(t117, t118, t119, t120, t121, GM_CONV(gm_cseahvg8v7p0vs, t122));
            } else {
                t124 = ((gm_t9805qtonbsag (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_tl874t2atnlh3))t123)(NULL, cx, NULL, t117, t118, t119, t120, t121, t122);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t125;
            if (((((uint8_t *)&t124))[4] != 0)) {
                uint32_t t126 = GM_LD(uint32_t, (((uint8_t *)&t124) + 0));
                t125 = t126;
            } else {
                memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)11ULL));
                goto gm_fdone;
            }
            gm_l17 = t125;
        }
        {
            uint32_t t127 = gm_l17;
            uint64_t t128 = (uint64_t)t127;
            uint64_t t129 = (uintptr_t)t128;
            void * t130 = gm_l0;
            uint64_t t131 = GM_LD(uintptr_t, ((((uint8_t *)t130) + 1968) + 0) + 8);
            if (t129 >= (uintptr_t)(t131)) gm_trap_index(t129, t131, gm_srclv0dcbbfhves, 1080, 23);
            gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t130) + 1968) + 0)) + t129 * 88) + 28));
            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)7ULL));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 8, t132);
            goto gm_fdone;
        }
        goto ix102;
ie102:;
ix102:;
    }
    {
        void * t133 = gm_l0;
        uint64_t t134 = gm_l1;
        void * t135 = gm_l4;
        gm_tkn42u7na3t81 t136 = gm_l13;
        gm_tkn42u7na3t81 t137 = gm_f_const_type_7rijs95((void *)NULL, cx, NULL, t133, t134, t135, t136);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t138;
        if (((((uint8_t *)&t137))[4] != 0)) {
            gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t137) + 0));
            t138 = t139;
        } else {
            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)11ULL));
            goto gm_fdone;
        }
        gm_l18 = t138;
    }
    {
        void * t141 = gm_l0;
        void * t142 = GM_LD(void *, (((uint8_t *)t141) + 2168));
        uint8_t t143;
        if ((GM_LD(void *, ((uint8_t *)&t142)) != 0)) goto mn145;
        t143 = ((uint8_t)0);
        goto me144;
mn145:;
        t143 = ((uint8_t)1);
        goto me144;
mn146:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1083, 8);
me144:;
        if (!t143) goto ie140;
        {
            {
                void * t149 = gm_l0;
                gm_tjcptv9sagfq8 t150;
                memset((uint8_t *)&t150 + 73, 0, 7);
                gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t150 + 0, t151);
                gm_tl874t2atnlh3 t152 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t150 + 16, t152);
                gm_t88rcucg1b7uk t153 = gm_l18;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t150 + 28, t153);
                uint64_t t154 = gm_l1;
                GM_ST(uint64_t, (uint8_t *)&t150 + 32, t154);
                void * t155 = gm_l4;
                GM_ST(void *, (uint8_t *)&t150 + 40, t155);
                gm_tkn42u7na3t81 t156 = gm_l13;
                GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t150 + 48, t156);
                void * t157 = gm_l0;
                uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)t157) + 2312));
                GM_ST(uint64_t, (uint8_t *)&t150 + 56, t158);
                GM_ST(void *, (uint8_t *)&t150 + 64, ((void *)0));
                gm_t9qkh0rsooc8d t159 = GM_LD(gm_t9qkh0rsooc8d, gm_c0u09tvsmdv);
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t150 + 72, t159);
                GM_ST(void *, (uint8_t *)&t150 + 80, ((void *)0));
                uint32_t t160 = gm_f_add_const_8u8b1ja((void *)NULL, cx, NULL, t149, &t150);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        goto ix140;
ie140:;
ix140:;
    }
    {
        void * t162 = gm_l0;
        void * t163 = (void *)(((uint8_t *)t162) + 8);
        gm_t88rcucg1b7uk t164 = gm_l18;
        uint8_t t165 = gm_f_is_error_8k27gd1(t163, GM_CONV(gm_csguankko4lmhd, t164));
        if (!t165) goto ie161;
        {
            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)11ULL));
            goto gm_fdone;
        }
        goto ix161;
ie161:;
ix161:;
    }
    {
        void * t168 = gm_l0;
        gm_t88rcucg1b7uk t169 = gm_l18;
        gm_tkn42u7na3t81 t170 = gm_f_non_constant_part_itk7b0v(t168, GM_CONV(gm_csguankko4lmhd, t169));
        if (!((((uint8_t *)&t170))[4] != 0)) goto ie167;
        gm_t88rcucg1b7uk t171 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t170));
        gm_l19 = t171;
        {
            void * t173 = gm_l0;
            void * t174 = gm_l4;
            gm_tl874t2atnlh3 t175 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t174) + 72));
            gm_t6j6smj7qoctu t176;
            void * t177 = gm_l0;
            gm_t88rcucg1b7uk t178 = gm_l18;
            gm_b16_8 t179 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t177, t178);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t176 + 0, t179);
            void * t180 = gm_l0;
            gm_t88rcucg1b7uk t181 = gm_l19;
            gm_b16_8 t182 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t180, t181);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t176 + 16, t182);
            gm_tabb710d9mq2e t183;
            gm_f_format_j4sks15((void *)&t183, (void *)NULL, cx, NULL, &t176);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t183))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t183) + 8, gm_srclv0dcbbfhves, 1088, 28);
            gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&t183) + 8));
            gm_b16_8 t185 = GM_LD(gm_b16_8, ((uint8_t *)&t184));
            gm_b0_1 t186 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t173, t175, t185);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)11ULL));
            goto gm_fdone;
        }
        goto ix167;
ie167:;
ix167:;
    }
    {
        gm_t88rcucg1b7uk t187 = gm_l18;
        memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)7ULL));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 8, t187);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_method_candidate_e20bo1i(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_declare_method_candidate_e20bo1i F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_t88rcucg1b7uk gm_l3; /* first */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_t88rcucg1b7uk gm_l5; /* p */
    uint32_t gm_l6; /* adt */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 806u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        gm_t88rcucg1b7uk t2 = gm_l3;
        gm_l4 = t2;
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l3;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)8ULL)) goto ie3;
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l5 = t9;
        {
            gm_t88rcucg1b7uk t11 = gm_l5;
            gm_l4 = t11;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 8);
        gm_t88rcucg1b7uk t15 = gm_l4;
        gm_t53at80pfdus3 t16;
        gm_f_kind_jcnt4hm((void *)&t16, t14, GM_CONV(gm_csguankko4lmhd, t15));
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t17 != ((uint8_t)17ULL)) goto ie12;
        uint32_t t18 = GM_LD(uint32_t, ((((uint8_t *)&t16) + 8) + 0));
        gm_l6 = t18;
        {
            uint32_t t21 = gm_l6;
            uint64_t t22 = (uint64_t)t21;
            uint64_t t23 = (uintptr_t)t22;
            void * t24 = gm_l0;
            uint64_t t25 = GM_LD(uintptr_t, ((((uint8_t *)t24) + 1088) + 0) + 8);
            if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_srclv0dcbbfhves, 1150, 8);
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t24) + 1088) + 0)) + t23 * 184) + 16));
            uint64_t t27 = gm_l1;
            uint8_t t28 = (!(t26 == t27));
            if (!t28) goto ie20;
            {
                gm_b0_1 t30;
                uint8_t t31 = 0;
                F.ret = t30;
                goto gm_fret;
            }
            goto ix20;
ie20:;
ix20:;
        }
        {
            uint32_t t33 = gm_l6;
            uint64_t t34 = (uint64_t)t33;
            uint64_t t35 = (uintptr_t)t34;
            void * t36 = gm_l0;
            uint64_t t37 = GM_LD(uintptr_t, ((((uint8_t *)t36) + 1088) + 0) + 8);
            if (t35 >= (uintptr_t)(t37)) gm_trap_index(t35, t37, gm_srclv0dcbbfhves, 1151, 24);
            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t36) + 1088) + 0)) + t35 * 184) + 24) + 0));
            if (t38 != ((uint8_t)0ULL)) goto ie32;
            gm_tpautr4br7h3l t39 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t36) + 1088) + 0)) + t35 * 184) + 24) + 8));
            F.l7 = t39;
            {
                {
                    gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                    gm_l8 = t42;
                }
                {
                    gm_l9 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t44 = gm_l9;
                        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        uint8_t t46 = (t44 < t45);
                        if (!t46) goto lb43;
                        {
                            uint64_t t49 = gm_l9;
                            uint64_t t50 = (uintptr_t)t49;
                            uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                            gm_t0mglksrkfa5e t54 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t50 * 40));
                            F.l10 = t54;
                            {
                                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                                gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                                uint8_t t59 = (gm_eq0iqiubfm7e(((uint8_t *)&t57), ((uint8_t *)&t58)));
                                if (!t59) goto ie56;
                                {
                                    void * t61 = gm_l0;
                                    gm_tl874t2atnlh3 t62 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                                    gm_t6j6smj7qoctu t63;
                                    void * t64 = gm_l0;
                                    gm_t88rcucg1b7uk t65 = gm_l4;
                                    gm_b16_8 t66 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t64, t65);
                                    if (gm_unw.kind) {
                                        goto gm_ll47;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t63 + 0, t66);
                                    gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                                    GM_ST(gm_b16_8, (uint8_t *)&t63 + 16, t67);
                                    gm_tabb710d9mq2e t68;
                                    gm_f_format_v0s3sc7((void *)&t68, (void *)NULL, cx, NULL, &t63);
                                    if (gm_unw.kind) {
                                        goto gm_ll47;
                                    }
                                    if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_srclv0dcbbfhves, 1154, 36);
                                    gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
                                    gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                                    gm_b0_1 t71 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t61, t62, t70);
                                    if (gm_unw.kind) {
                                        goto gm_ll47;
                                    }
                                }
                                {
                                    goto lb43;
                                }
                                goto ix56;
ie56:;
ix56:;
                            }
                            goto me52;
mn53:;
                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1152, 9);
me52:;
                        }
lc43:;
                        {
                            uint64_t t73 = gm_l9;
                            uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                            uint64_t t76 = r74;
                            gm_l9 = t76;
                        }
                    }
gm_ll47:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb43; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc43; }
                    }
                    goto gm_lad;
lb43:;
                }
            }
            goto ix32;
ie32:;
ix32:;
        }
        {
            void * t77 = gm_l0;
            void * t78 = (void *)(((uint8_t *)t77) + 1872);
            gm_t79rbdv4rafe5 t79;
            memset((uint8_t *)&t79 + 4, 0, 4);
            memset((uint8_t *)&t79 + 36, 0, 4);
            void * t80 = gm_l0;
            void * t81 = (void *)(((uint8_t *)t80) + 8);
            uint32_t t82 = gm_l6;
            uint32_t t83 = gm_f_template_9o92cvv(t81, t82);
            GM_ST(uint32_t, (uint8_t *)&t79 + 0, t83);
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t79 + 8, t84);
            gm_tl874t2atnlh3 t85 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t79 + 24, t85);
            gm_tftb1odc739t5 t86 = gm_f_push_fomabnf(t78, &t79);
            if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srclv0dcbbfhves, 1159, 5);
            gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
        }
        goto ix12;
ie12:;
        {
            gm_b0_1 t89;
            uint8_t t90 = 0;
            F.ret = t89;
            goto gm_fret;
        }
ix12:;
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
extern __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tdgfduqjheqdm gm_f_def_of_m512sbu_c(void * a0, uint32_t a1) { gm_tdgfduqjheqdm gm_r; gm_f_def_of_m512sbu(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_effect_arg_tys_2a7r8hh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2) {
    struct gm_fr_effect_arg_tys_2a7r8hh F;
    gm_b16_8 gm_l0; /* refs */
    gm_t9qkh0rsooc8d gm_l1; /* need */
    void * gm_l2; /* tys */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tiilmfd16v3fc gm_l5; /* r */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l9; /* t */
    GM_STACK_CHECK(&F, 339u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l3 = t3;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tiilmfd16v3fc t15 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 24));
                    gm_l5 = t15;
                    {
                        {
                            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
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
                                    gm_toa2vhe3l4hfr t30 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t26 * 40));
                                    F.l8 = t30;
                                    {
                                        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                                        if (t33 != ((uint8_t)0ULL)) goto ie32;
                                        gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 8));
                                        gm_l9 = t34;
                                        {
                                            void * t36 = gm_l2;
                                            gm_t88rcucg1b7uk t37 = gm_l9;
                                            gm_t9qkh0rsooc8d t38 = gm_l1;
                                            gm_t3jsf43p1pths t39;
                                            GM_ST(uint8_t, (uint8_t *)&t39 + 5, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t39 + 6, 0);
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t39 + 0), t37);
                                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t39 + 4), t38);
                                            gm_tftb1odc739t5 t40 = gm_f_push_h37silv(t36, t39);
                                            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srclv0dcbbfhves, 1249, 32);
                                            gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                                        }
                                        goto ix32;
ie32:;
ix32:;
                                    }
                                    goto me28;
mn29:;
                                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1248, 9);
me28:;
                                }
lc19:;
                                {
                                    uint64_t t43 = gm_l7;
                                    uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                                    uint64_t t46 = r44;
                                    gm_l7 = t46;
                                }
                            }
lb19:;
                        }
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1247, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t48 = gm_l4;
                    uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                    uint64_t t51 = r49;
                    gm_l4 = t51;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_field_default_expr_tqfuiff(void * a0, uint32_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_field_default_expr_tqfuiff F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* exprs */
    GM_STACK_CHECK_SHALLOW(&F, 155u);
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
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1088) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srclv0dcbbfhves, 708, 14);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 96));
        gm_l3 = t7;
    }
    {
        uint64_t t9 = gm_l2;
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t11 = (t9 >= t10);
        if (!t11) goto ie8;
        {
            uint8_t t13 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        uint64_t t14 = gm_l2;
        uint64_t t15 = (uintptr_t)t14;
        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_srclv0dcbbfhves, 710, 12);
        void * t17 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t15 * 8));
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfunt71(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hcjjkrm(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_hcjjkrm F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    uint8_t gm_l1; /* c */
    uint8_t gm_l2; /* c */
    uint8_t gm_l3; /* c */
    uint32_t gm_l4; /* id */
    uint8_t gm_l5; /* is_bad */
    GM_STACK_CHECK_SHALLOW(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_non_constant_part_itk7b0v *)((&F.h)->up))->l0));
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l0;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma8:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t17 != ((uint8_t)8ULL)) goto pa16;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l1 = t18;
        goto po15;
pa16:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t20 != ((uint8_t)9ULL)) goto pa19;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l1 = t21;
        goto po15;
pa19:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t23 != ((uint8_t)10ULL)) goto pa22;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l1 = t24;
        goto po15;
pa22:;
        goto mn14;
po15:;
        uint8_t t25 = gm_l1;
        uint8_t t26 = (uint8_t)!t25;
        t6 = t26;
        goto me7;
mn14:;
ma9:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t30 != ((uint8_t)7ULL)) goto pa29;
        goto po28;
pa29:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t32 != ((uint8_t)18ULL)) goto pa31;
        goto po28;
pa31:;
        goto mn27;
po28:;
        t6 = ((uint8_t)1);
        goto me7;
mn27:;
ma10:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t34 != ((uint8_t)17ULL)) goto mn33;
        uint32_t t35 = GM_LD(uint32_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l4 = t35;
        void * t36 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_non_constant_part_itk7b0v *)((&F.h)->up))->l0));
        void * t37 = (void *)(((uint8_t *)t36) + 8);
        uint32_t t38 = gm_l4;
        gm_b16_8 t39 = gm_f_adt_name_h7mjik7(t37, t38);
        uint8_t t40 = (((GM_LD(uintptr_t, ((uint8_t *)&t39) + 8) == 9) && memcmp(GM_LD(void *, ((uint8_t *)&t39)), gm_s4eilu97sooh1, 9) == 0));
        t6 = t40;
        goto me7;
mn33:;
ma11:;
        t6 = ((uint8_t)0);
        goto me7;
mn41:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1121, 19);
me7:;
        gm_l5 = t6;
    }
    {
        uint8_t t43 = gm_l5;
        if (!t43) goto ie42;
        uint8_t t44;
        if (((((uint8_t *)&((struct gm_fr_non_constant_part_itk7b0v *)((&F.h)->up))->l2))[4] != 0)) goto mn46;
        t44 = ((uint8_t)1);
        goto me45;
mn46:;
        t44 = ((uint8_t)0);
        goto me45;
mn47:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1127, 23);
me45:;
        if (!t44) goto ie42;
        {
            gm_t88rcucg1b7uk t49 = gm_l0;
            gm_tkn42u7na3t81 t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t50), t49);
            ((uint8_t *)&t50)[4] = 1;
            GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&((struct gm_fr_non_constant_part_itk7b0v *)((&F.h)->up))->l2), t50);
        }
        goto ix42;
ie42:;
ix42:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_repdalk(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_repdalk F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    uint32_t gm_l1; /* id */
    uint64_t gm_l2; /* d */
    gm_t9qkh0rsooc8d gm_l3; /* vis */
    GM_STACK_CHECK_SHALLOW(&F, 250u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3;
        if (((((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l3))[8] != 0)) goto mn5;
        t3 = ((uint8_t)1);
        goto me4;
mn5:;
        t3 = ((uint8_t)0);
        goto me4;
mn6:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1279, 12);
me4:;
        if (!t3) goto ie2;
        {
            void * t9 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l0));
            void * t10 = (void *)(((uint8_t *)t9) + 8);
            gm_t88rcucg1b7uk t11 = gm_l0;
            gm_t53at80pfdus3 t12;
            gm_f_kind_jcnt4hm((void *)&t12, t10, GM_CONV(gm_csguankko4lmhd, t11));
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
            if (t13 != ((uint8_t)17ULL)) goto ie8;
            uint32_t t14 = GM_LD(uint32_t, ((((uint8_t *)&t12) + 8) + 0));
            gm_l1 = t14;
            {
                void * t17 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l0));
                gm_b16_8 t18 = GM_LD(gm_b16_8, ((((uint8_t *)t17) + 1088) + 0));
                gm_b16_8 t19 = t18;
                void * t20 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l0));
                void * t21 = (void *)(((uint8_t *)t20) + 8);
                uint32_t t22 = gm_l1;
                uint32_t t23 = gm_f_template_9o92cvv(t21, t22);
                uint64_t t24 = (uint64_t)t23;
                uint64_t t25 = (uintptr_t)t24;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&t19) + 8);
                if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srclv0dcbbfhves, 1281, 25);
                if (!(((((uint8_t *)GM_LD(void *, ((uint8_t *)&t19)) + t25 * 184) + 168))[8] != 0)) goto ie16;
                uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t19)) + t25 * 184) + 168));
                gm_l2 = t27;
                {
                    uint64_t t29 = gm_l2;
                    uint64_t t30 = (uintptr_t)t29;
                    void * t31 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l0));
                    void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
                    uint64_t t33 = GM_LD(uintptr_t, ((((uint8_t *)t32) + 240) + 0) + 8);
                    if (t30 >= (uintptr_t)(t33)) gm_trap_index(t30, t33, gm_srclv0dcbbfhves, 1282, 28);
                    gm_t9qkh0rsooc8d t34 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t32) + 240) + 0)) + t30 * 216) + 40) + 56));
                    gm_l3 = t34;
                }
                {
                    gm_t9qkh0rsooc8d t36 = gm_l3;
                    uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)&t36));
                    gm_t9qkh0rsooc8d t38 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l2));
                    uint8_t t39 = GM_LD(uint8_t, ((uint8_t *)&t38));
                    uint8_t t40 = (t37 < t39);
                    if (!t40) goto ie35;
                    {
                        gm_t88rcucg1b7uk t42 = gm_l0;
                        gm_t9qkh0rsooc8d t43 = gm_l3;
                        gm_t3jsf43p1pths t44;
                        GM_ST(uint8_t, (uint8_t *)&t44 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t44 + 6, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t44 + 0), t42);
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t44 + 4), t43);
                        gm_trcagkhgm1qdu t45;
                        GM_ST(uint8_t, (uint8_t *)&t45 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t45 + 10, 0);
                        GM_ST(gm_t3jsf43p1pths, ((uint8_t *)&t45), t44);
                        ((uint8_t *)&t45)[8] = 1;
                        GM_ST(gm_trcagkhgm1qdu, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l3), t45);
                    }
                    goto ix35;
ie35:;
ix35:;
                }
                goto ix16;
ie16:;
ix16:;
            }
            goto ix8;
ie8:;
ix8:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_frame_up_h0ddp1d(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_frame_up_h0ddp1d F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    uint32_t gm_l2; /* levels */
    uint32_t gm_l3; /* cur */
    uint32_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l1;
        gm_l3 = t2;
    }
    {
        {
            gm_l5 = ((uint32_t)0ULL);
        }
        {
            uint32_t t4 = gm_l2;
            gm_l4 = t4;
        }
        {
            for (;;) {
                uint32_t t6 = gm_l5;
                uint32_t t7 = gm_l4;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint32_t t11 = gm_l5;
                    {
                        uint32_t t16 = gm_l3;
                        uint64_t t17 = (uint64_t)t16;
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 384) + 0) + 8);
                        uint8_t t20 = (t17 >= t19);
                        if (!t20) goto ie15;
                        {
                            gm_t9805qtonbsag t22;
                            memset(&t22, 0, sizeof t22);
                            uint8_t t23 = 0;
                            F.ret = t22;
                            goto gm_fret;
                        }
                        goto ix15;
ie15:;
ix15:;
                    }
                    {
                        uint32_t t24 = gm_l3;
                        uint64_t t25 = (uint64_t)t24;
                        uint64_t t26 = (uintptr_t)t25;
                        void * t27 = gm_l0;
                        uint64_t t28 = GM_LD(uintptr_t, ((((uint8_t *)t27) + 384) + 0) + 8);
                        if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_srclv0dcbbfhves, 600, 15);
                        gm_t9805qtonbsag t29 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t27) + 384) + 0)) + t26 * 64) + 48));
                        uint32_t t30;
                        if (((((uint8_t *)&t29))[4] != 0)) {
                            uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&t29) + 0));
                            t30 = t31;
                        } else {
                            gm_t9805qtonbsag t32;
                            memset(&t32, 0, sizeof t32);
                            uint8_t t33 = 0;
                            F.ret = t32;
                            goto gm_fret;
                        }
                        gm_l3 = t30;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 598, 5);
me12:;
                }
lc5:;
                {
                    uint32_t t35 = gm_l5;
                    uint32_t r36; bool o37 = __builtin_add_overflow(t35, ((uint32_t)1ULL), &r36);
                    uint32_t t38 = r36;
                    gm_l5 = t38;
                }
            }
lb5:;
        }
    }
    {
        uint32_t t39 = gm_l3;
        gm_t9805qtonbsag t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t40), t39);
        ((uint8_t *)&t40)[4] = 1;
        uint8_t t41 = 0;
        F.ret = t40;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1);
extern __attribute__((aligned(4))) gm_trcagkhgm1qdu gm_f_hidden_part_is5hgf9(void * a0, gm_csguankko4lmhd a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) uint8_t gm_f_in_generic_scope_rvb92ab(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_in_generic_scope_rvb92ab F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_tep2drsl9408b gm_l3; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 1528) + 0) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t9 = gm_l0;
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((((uint8_t *)t9) + 1488) + 0));
            gm_l1 = t10;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t12 = gm_l2;
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    uint64_t t17 = gm_l2;
                    uint64_t t18 = (uintptr_t)t17;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    gm_tep2drsl9408b t22 = GM_LD(gm_tep2drsl9408b, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t18 * 24));
                    gm_l3 = t22;
                    {
                        void * t25 = gm_l0;
                        void * t26 = (void *)(((uint8_t *)t25) + 8);
                        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 16));
                        uint8_t t28 = gm_f_has_param_u9t0efs(t26, GM_CONV(gm_csguankko4lmhd, t27));
                        if (!t28) goto ie24;
                        {
                            uint8_t t30 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix24;
ie24:;
ix24:;
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 627, 5);
me20:;
                }
lc11:;
                {
                    uint64_t t32 = gm_l2;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l2 = t35;
                }
            }
lb11:;
        }
    }
    {
        uint8_t t36 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_ambient_m41pl1e(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ambient_m41pl1e F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* eff */
    void * gm_l2; /* info */
    GM_STACK_CHECK_SHALLOW(&F, 346u);
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
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1048) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srclv0dcbbfhves, 716, 14);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1048) + 0)) + t4 * 56);
        gm_l2 = t7;
    }
    {
        void * t9 = gm_l2;
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 16));
        uint64_t t11 = (uintptr_t)t10;
        void * t12 = gm_l0;
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 200) + 0) + 8);
        if (t11 >= (uintptr_t)(t14)) gm_trap_index(t11, t14, gm_srclv0dcbbfhves, 717, 12);
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 200) + 0)) + t11 * 184) + 112));
        uint8_t t16 = (uint8_t)!t15;
        if (!t16) goto ie8;
        {
            uint8_t t18 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        uint8_t t19 = 0;
        void * t20 = gm_l2;
        uint8_t t21 = (!((GM_LD(uintptr_t, (((uint8_t *)t20) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)t20) + 0)), gm_ss6g2koqn63bf, 4) == 0));
        if (t21) {
            void * t22 = gm_l2;
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 0));
            gm_t61mja4eihh2b t24 = GM_LD(gm_t61mja4eihh2b, gm_cp0okt01b9q);
            uint8_t t25 = 0;
            for (uintptr_t i = 0; i < (uintptr_t)(12); i++) if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + i * 16, ((uint8_t *)&t23))) { t25 = 1; break; }
            uint8_t t26 = (uint8_t)(t25);
            t19 = t26;
        } else t19 = 0;
        uint8_t t27 = 0;
        F.ret = t19;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_structural_alias_qpmv4mu(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_structural_alias_qpmv4mu F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    void * gm_l2; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 193u);
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
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srclv0dcbbfhves, 806, 11);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t3 * 216);
        gm_l2 = t7;
    }
    {
        void * t9 = gm_l2;
        uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 40) + 40) + 8);
        uint8_t t11 = (t10 > ((uint64_t)0ULL));
        if (!t11) goto ie8;
        {
            uint8_t t13 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t15 = gm_l2;
        uint8_t t16 = GM_LD(uint8_t, (((((uint8_t *)t15) + 40) + 64) + 0));
        if (t16 != ((uint8_t)0ULL)) goto ie14;
        gm_tv2uf4leil5hq t17 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t15) + 40) + 64) + 8));
        F.l3 = t17;
        {
            void * t20 = GM_LD(void *, (((uint8_t *)&F.l3) + 24));
            uint8_t t21;
            if ((GM_LD(void *, ((uint8_t *)&t20)) != 0)) goto mn23;
            t21 = ((uint8_t)0);
            goto me22;
mn23:;
            t21 = ((uint8_t)1);
            goto me22;
mn24:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 809, 8);
me22:;
            if (!t21) goto ie19;
            {
                uint8_t t26 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix19;
ie19:;
ix19:;
        }
        {
            void * t27 = GM_LD(void *, (((uint8_t *)&F.l3) + 40));
            uint8_t t28;
            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t27) + 8) + 0));
            if (t33 != ((uint8_t)24ULL)) goto pa32;
            goto po31;
pa32:;
            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t27) + 8) + 0));
            if (t35 != ((uint8_t)25ULL)) goto pa34;
            goto po31;
pa34:;
            uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t27) + 8) + 0));
            if (t37 != ((uint8_t)26ULL)) goto pa36;
            goto po31;
pa36:;
            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t27) + 8) + 0));
            if (t39 != ((uint8_t)27ULL)) goto pa38;
            goto po31;
pa38:;
            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t27) + 8) + 0));
            if (t41 != ((uint8_t)28ULL)) goto pa40;
            goto po31;
pa40:;
            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t27) + 8) + 0));
            if (t43 != ((uint8_t)29ULL)) goto pa42;
            goto po31;
pa42:;
            uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t27) + 8) + 0));
            if (t45 != ((uint8_t)30ULL)) goto pa44;
            goto po31;
pa44:;
            goto mn30;
po31:;
            t28 = ((uint8_t)1);
            goto me29;
mn30:;
            t28 = ((uint8_t)0);
            goto me29;
mn46:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 810, 12);
me29:;
            uint8_t t47 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        goto ix14;
ie14:;
        {
            uint8_t t49 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix14:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_non_constant_part_itk7b0v(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_prelude_effect_gilrmm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_prelude_effect_gilrmm5 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* prelude */
    uint64_t gm_l3; /* d */
    uint32_t gm_l4; /* eff */
    GM_STACK_CHECK(&F, 552u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_tf5srh4mp4b3i t4 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)t3) + 320));
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tqu3m2l3n8mnc t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        uint64_t t11 = gm_l2;
        gm_b16_8 t12 = gm_l1;
        gm_tf5srh4mp4b3i t13 = gm_f_lookup_59juqoo(t10, t11, t12);
        uint64_t t14;
        if (((((uint8_t *)&t13))[8] != 0)) {
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            gm_tqu3m2l3n8mnc t16;
            memset(&t16, 0, sizeof t16);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        gm_l3 = t14;
    }
    {
        void * t19 = gm_l0;
        uint64_t t20 = gm_l3;
        gm_t6qooom8qa2do t21;
        gm_f_resolve_decl_pus042v((void *)&t21, (void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
        if (t22 != ((uint8_t)2ULL)) goto ie18;
        uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&t21) + 8));
        gm_l4 = t23;
        {
            gm_tiilmfd16v3fc t25;
            memset((uint8_t *)&t25 + 4, 0, 4);
            uint32_t t26 = gm_l4;
            GM_ST(uint32_t, (uint8_t *)&t25 + 0, t26);
            gm_b16_8 t27 = gm_f_empty_slice_fh7svmj();
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 8, t28);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 28, 0);
            ((uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out))[24] = 1;
            GM_ST(gm_tiilmfd16v3fc, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 0, t25);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
        {
            gm_tqu3m2l3n8mnc t30;
            memset(&t30, 0, sizeof t30);
            uint8_t t31 = 0;
            F.ret = t30;
            goto gm_fret;
        }
ix18:;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_prelude_instance_arg_kh32dmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_prelude_instance_arg_kh32dmf F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* name */
    uint32_t gm_l3; /* id */
    gm_b16_8 gm_l4; /* args */
    uint32_t gm_l5; /* template */
    gm_t88rcucg1b7uk gm_l6; /* e */
    GM_STACK_CHECK(&F, 318u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
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
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 8));
        gm_l4 = t9;
        {
            void * t11 = gm_l0;
            gm_b16_8 t12 = gm_l2;
            gm_t9805qtonbsag t13 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t14;
            if (((((uint8_t *)&t13))[4] != 0)) {
                uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&t13) + 0));
                t14 = t15;
            } else {
                gm_tkn42u7na3t81 t16;
                memset(&t16, 0, sizeof t16);
                uint8_t t17 = 0;
                F.ret = t16;
                goto gm_fret;
            }
            gm_l5 = t14;
        }
        {
            uint8_t t19 = 0;
            void * t20 = gm_l0;
            void * t21 = (void *)(((uint8_t *)t20) + 8);
            uint32_t t22 = gm_l3;
            uint32_t t23 = gm_f_template_9o92cvv(t21, t22);
            uint32_t t24 = gm_l5;
            uint8_t t25 = (!(t23 == t24));
            if (!t25) {
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
                t19 = t27;
            } else t19 = 1;
            if (!t19) goto ie18;
            {
                gm_tkn42u7na3t81 t29;
                memset(&t29, 0, sizeof t29);
                uint8_t t30 = 0;
                F.ret = t29;
                goto gm_fret;
            }
            goto ix18;
ie18:;
ix18:;
        }
        {
            uint64_t t32 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srclv0dcbbfhves, 692, 17);
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t32 * 40) + 0));
            if (t34 != ((uint8_t)0ULL)) goto ie31;
            gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t32 * 40) + 8));
            gm_l6 = t35;
            {
                gm_t88rcucg1b7uk t37 = gm_l6;
                gm_tkn42u7na3t81 t38;
                GM_ST(uint8_t, (uint8_t *)&t38 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t38 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t38), t37);
                ((uint8_t *)&t38)[4] = 1;
                uint8_t t39 = 0;
                F.ret = t38;
                goto gm_fret;
            }
            goto ix31;
ie31:;
            {
                gm_tkn42u7na3t81 t41;
                memset(&t41, 0, sizeof t41);
                uint8_t t42 = 0;
                F.ret = t41;
                goto gm_fret;
            }
ix31:;
        }
        goto ix2;
ie2:;
        {
            gm_tkn42u7na3t81 t44;
            memset(&t44, 0, sizeof t44);
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
ix2:;
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_prelude_template_7s9nsp0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_prelude_template_7s9nsp0 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* prelude */
    uint64_t gm_l3; /* d */
    uint32_t gm_l4; /* template */
    GM_STACK_CHECK(&F, 449u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_tf5srh4mp4b3i t4 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)t3) + 320));
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_t9805qtonbsag t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        uint64_t t11 = gm_l2;
        gm_b16_8 t12 = gm_l1;
        gm_tf5srh4mp4b3i t13 = gm_f_lookup_59juqoo(t10, t11, t12);
        uint64_t t14;
        if (((((uint8_t *)&t13))[8] != 0)) {
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            gm_t9805qtonbsag t16;
            memset(&t16, 0, sizeof t16);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        gm_l3 = t14;
    }
    {
        void * t19 = gm_l0;
        uint64_t t20 = gm_l3;
        gm_t6qooom8qa2do t21;
        gm_f_resolve_decl_pus042v((void *)&t21, (void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
        if (t22 != ((uint8_t)1ULL)) goto ie18;
        uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&t21) + 8));
        gm_l4 = t23;
        {
            uint32_t t25 = gm_l4;
            gm_t9805qtonbsag t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t26), t25);
            ((uint8_t *)&t26)[4] = 1;
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix18;
ie18:;
        {
            gm_t9805qtonbsag t29;
            memset(&t29, 0, sizeof t29);
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
ix18:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_field_tys_v4ep4t3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2) {
    struct gm_fr_push_field_tys_v4ep4t3 F;
    gm_b16_8 gm_l0; /* fields */
    gm_t9qkh0rsooc8d gm_l1; /* vis */
    void * gm_l2; /* tys */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l6; /* need */
    GM_STACK_CHECK(&F, 197u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l3 = t3;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t0mglksrkfa5e t15 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 40));
                    F.l5 = t15;
                    {
                        uint8_t t18 = ((GM_LD(uint8_t, (((uint8_t *)&F.l5) + 20) + 0) == ((uint8_t)0ULL)));
                        if (!t18) goto ie17;
                        {
                            goto lc4;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    {
                        gm_t9qkh0rsooc8d t20;
                        gm_t9qkh0rsooc8d t22 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l5) + 20));
                        uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)&t22));
                        gm_t9qkh0rsooc8d t24 = gm_l1;
                        uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)&t24));
                        uint8_t t26 = (t23 < t25);
                        if (!t26) goto ie21;
                        gm_t9qkh0rsooc8d t28 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l5) + 20));
                        gm_t9qkh0rsooc8d t29 = t28;
                        t20 = t29;
                        goto ix21;
ie21:;
                        gm_t9qkh0rsooc8d t31 = gm_l1;
                        gm_t9qkh0rsooc8d t32 = t31;
                        t20 = t32;
ix21:;
                        gm_l6 = t20;
                    }
                    {
                        void * t33 = gm_l2;
                        gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 16));
                        gm_t9qkh0rsooc8d t35 = gm_l6;
                        gm_t3jsf43p1pths t36;
                        GM_ST(uint8_t, (uint8_t *)&t36 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t36 + 6, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t36 + 0), t34);
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 4), t35);
                        gm_tftb1odc739t5 t37 = gm_f_push_h37silv(t33, t36);
                        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_srclv0dcbbfhves, 1270, 9);
                        gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1267, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t40 = gm_l4;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l4 = t43;
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
inline __attribute__((aligned(4))) gm_thetuhtouve7f gm_f_range_of_dmmo768(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_range_of_dmmo768 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* e */
    GM_STACK_CHECK(&F, 187u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_seue7mq07v0oc));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(5));
        gm_tkn42u7na3t81 t6 = gm_f_prelude_instance_arg_kh32dmf((void *)NULL, cx, NULL, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t6))[4] != 0)) goto ie2;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t6));
        gm_l2 = t7;
        {
            gm_t88rcucg1b7uk t9 = gm_l2;
            gm_tbngattl1fo51 t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t10 + 4), t9);
            gm_thetuhtouve7f t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 10, 0);
            GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t11), t10);
            ((uint8_t *)&t11)[8] = 1;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t14 = gm_l0;
        gm_t88rcucg1b7uk t15 = gm_l1;
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s43cq4krigs2b));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(9));
        gm_tkn42u7na3t81 t17 = gm_f_prelude_instance_arg_kh32dmf((void *)NULL, cx, NULL, t14, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t17))[4] != 0)) goto ie13;
        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t17));
        gm_l3 = t18;
        {
            gm_t88rcucg1b7uk t20 = gm_l3;
            gm_tbngattl1fo51 t21;
            GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)1));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t21 + 4), t20);
            gm_thetuhtouve7f t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 10, 0);
            GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t22), t21);
            ((uint8_t *)&t22)[8] = 1;
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_thetuhtouve7f t24;
        memset(&t24, 0, sizeof t24);
        uint8_t t25 = 0;
        F.ret = t24;
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
extern __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_range_template_dbt7218(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_cycle_mauubd4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_report_cycle_mauubd4 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    uint64_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* cycle */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* d */
    gm_tl874t2atnlh3 gm_l8; /* span */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* c */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* c */
    GM_STACK_CHECK(&F, 1461u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 1648) + 0));
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
        uint64_t t5 = gm_l1;
        gm_tf5srh4mp4b3i t6 = gm_f_index_of_s9trr09(t4, t5);
        uint64_t t7;
        if (((((uint8_t *)&t6))[8] != 0)) {
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            t7 = ((uint64_t)0ULL);
        }
        gm_l2 = t7;
    }
    {
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 1648) + 0) + 8);
        void * t11 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t9) + 1648) + 0)));
        uint64_t t12 = gm_l2;
        uint64_t t13 = (uintptr_t)t12;
        if (t13 > (uintptr_t)t10 || (uintptr_t)t10 > (uintptr_t)t10) gm_trap_slice(t13, (uintptr_t)t10, t10, gm_srclv0dcbbfhves, 818, 14);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t11 + t13 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)((uintptr_t)t10 - t13));
        gm_l3 = t14;
    }
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t16 = gm_l3;
            gm_l5 = t16;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l6;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l6;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t24 * 8));
                    uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)&t26));
                    gm_l7 = t29;
                    {
                        void * t31 = (void *)((uint8_t *)&F.l4);
                        uint64_t t32 = gm_l7;
                        uint64_t t33 = (uintptr_t)t32;
                        void * t34 = gm_l0;
                        void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
                        uint64_t t36 = GM_LD(uintptr_t, ((((uint8_t *)t35) + 240) + 0) + 8);
                        if (t33 >= (uintptr_t)(t36)) gm_trap_index(t33, t36, gm_srclv0dcbbfhves, 820, 52);
                        gm_b16_8 t37 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t35) + 240) + 0)) + t33 * 216) + 0) + 0));
                        gm_tnpifv20irnph t38;
                        GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t37);
                        gm_tabb710d9mq2e t39;
                        gm_f_format_h4jcodi((void *)&t39, (void *)NULL, cx, NULL, t38);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_srclv0dcbbfhves, 820, 33);
                        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
                        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                        gm_tftb1odc739t5 t42 = gm_f_push_3ef746b(t31, t41);
                        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srclv0dcbbfhves, 820, 22);
                        gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 820, 5);
me27:;
                }
lc17:;
                {
                    uint64_t t45 = gm_l6;
                    uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                    uint64_t t48 = r46;
                    gm_l6 = t48;
                }
            }
gm_ll21:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
            }
            goto gm_lad;
lb17:;
        }
    }
    {
        uint64_t t49 = gm_l1;
        uint64_t t50 = (uintptr_t)t49;
        void * t51 = gm_l0;
        void * t52 = GM_LD(void *, (((uint8_t *)t51) + 0));
        uint64_t t53 = GM_LD(uintptr_t, ((((uint8_t *)t52) + 240) + 0) + 8);
        if (t50 >= (uintptr_t)(t53)) gm_trap_index(t50, t53, gm_srclv0dcbbfhves, 821, 13);
        gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t52) + 240) + 0)) + t50 * 216) + 0) + 16));
        gm_l8 = t54;
    }
    {
        gm_tl874t2atnlh3 t55 = gm_l8;
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
        gm_b16_8 t58;
        GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sdbvaa960ee0f));
        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(4));
        gm_tabb710d9mq2e t59;
        gm_f_join_sfknljb((void *)&t59, (void *)NULL, cx, NULL, t57, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 8, gm_srclv0dcbbfhves, 822, 83);
        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t59) + 8));
        gm_tnpifv20irnph t61;
        GM_ST(gm_b16_8, ((uint8_t *)&t61 + 0), t60);
        gm_tabb710d9mq2e t62;
        gm_f_format_slm5ovr((void *)&t62, (void *)NULL, cx, NULL, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 8, gm_srclv0dcbbfhves, 822, 27);
        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&t62) + 8));
        gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
        gm_f_error_s6hn871((void *)&F.l9, (void *)NULL, cx, NULL, t55, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            void * t67 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
            uint64_t t68 = (uintptr_t)((uint64_t)1ULL);
            if (t68 > (uintptr_t)t66 || (uintptr_t)t66 > (uintptr_t)t66) gm_trap_slice(t68, (uintptr_t)t66, t66, gm_srclv0dcbbfhves, 823, 14);
            gm_b16_8 t69;
            GM_ST(void *, (uint8_t *)&t69, (void *)(((uint8_t *)t67 + t68 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)((uintptr_t)t66 - t68));
            gm_l10 = t69;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t71 = gm_l11;
                uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t73 = (t71 < t72);
                if (!t73) goto lb70;
                {
                    uint64_t t76 = gm_l11;
                    uint64_t t77 = (uintptr_t)t76;
                    uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint64_t t79 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t77 * 8));
                    uint64_t t82 = GM_LD(uint64_t, ((uint8_t *)&t79));
                    gm_l12 = t82;
                    {
                        void * t84 = (void *)((uint8_t *)&F.l9);
                        uint64_t t85 = gm_l12;
                        uint64_t t86 = (uintptr_t)t85;
                        void * t87 = gm_l0;
                        void * t88 = GM_LD(void *, (((uint8_t *)t87) + 0));
                        uint64_t t89 = GM_LD(uintptr_t, ((((uint8_t *)t88) + 240) + 0) + 8);
                        if (t86 >= (uintptr_t)(t89)) gm_trap_index(t86, t89, gm_srclv0dcbbfhves, 823, 39);
                        gm_tl874t2atnlh3 t90 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t88) + 240) + 0)) + t86 * 216) + 0) + 16));
                        gm_b16_8 t91;
                        GM_ST(void *, (uint8_t *)&t91, (void *)(gm_s68uiiatavvqh));
                        GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(17));
                        gm_f_add_label_50fj7vv(t84, GM_CONV(gm_cseahvg8v7p0vs, t90), t91);
                    }
                    goto me80;
mn81:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 823, 5);
me80:;
                }
lc70:;
                {
                    uint64_t t93 = gm_l11;
                    uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                    uint64_t t96 = r94;
                    gm_l11 = t96;
                }
            }
lb70:;
        }
    }
    {
        void * t97 = gm_l0;
        gm_b0_1 t98 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t97, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l9)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t100 = gm_l3;
            gm_l13 = t100;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t102 = gm_l14;
                uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t104 = (t102 < t103);
                if (!t104) goto lb101;
                {
                    uint64_t t107 = gm_l14;
                    uint64_t t108 = (uintptr_t)t107;
                    uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    uint64_t t110 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t108 * 8));
                    uint64_t t113 = GM_LD(uint64_t, ((uint8_t *)&t110));
                    gm_l15 = t113;
                    {
                        void * t115 = gm_l0;
                        uint64_t t116 = gm_l15;
                        gm_t6qooom8qa2do t117;
                        memset(&t117, 0, sizeof t117);
                        GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)11ULL));
                        gm_tagrkbqh054h7 t118;
                        GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t118 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&t118 + 8), t117);
                        gm_b0_1 t119 = gm_f_set_state_3j300fi((void *)NULL, cx, NULL, t115, t116, &t118);
                        if (gm_unw.kind) {
                            goto gm_ll105;
                        }
                    }
                    goto me111;
mn112:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 825, 5);
me111:;
                }
lc101:;
                {
                    uint64_t t121 = gm_l14;
                    uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                    uint64_t t124 = r122;
                    gm_l14 = t124;
                }
            }
gm_ll105:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb101; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc101; }
            }
            goto gm_lad;
lb101:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_resolve_ambient_6orl0vo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_resolve_ambient_6orl0vo F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* prelude */
    uint64_t gm_l2; /* d */
    uint32_t gm_l3; /* e */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* name */
    uint64_t gm_l7; /* d */
    GM_STACK_CHECK(&F, 823u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_tf5srh4mp4b3i t4 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)t3) + 320));
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_b0_1 t7;
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        uint64_t t12 = gm_l1;
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_ses7tdrdt8a6u));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(5));
        gm_tf5srh4mp4b3i t14 = gm_f_lookup_59juqoo(t11, t12, t13);
        if (!((((uint8_t *)&t14))[8] != 0)) goto ie9;
        uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t14));
        gm_l2 = t15;
        {
            void * t18 = gm_l0;
            uint64_t t19 = gm_l2;
            gm_t6qooom8qa2do t20;
            gm_f_resolve_decl_pus042v((void *)&t20, (void *)NULL, cx, NULL, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
            if (t21 != ((uint8_t)2ULL)) goto ie17;
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&t20) + 8));
            gm_l3 = t22;
            {
                void * t24 = gm_l0;
                uint32_t t25 = gm_l3;
                gm_t9805qtonbsag t26;
                GM_ST(uint8_t, (uint8_t *)&t26 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t26 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t26), t25);
                ((uint8_t *)&t26)[4] = 1;
                GM_ST(gm_t9805qtonbsag, (((uint8_t *)t24) + 1956), t26);
            }
            goto ix17;
ie17:;
ix17:;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        {
            void * t28 = ((uint8_t *)gm_cp0okt01b9q);
            if ((uintptr_t)0 > (uintptr_t)12 || (uintptr_t)12 > (uintptr_t)12) gm_trap_slice((uintptr_t)0, (uintptr_t)12, 12, gm_srclv0dcbbfhves, 663, 5);
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t28 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)((uintptr_t)12 - (uintptr_t)0));
            gm_l4 = t29;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t31 = gm_l5;
                uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t33 = (t31 < t32);
                if (!t33) goto lb30;
                {
                    uint64_t t36 = gm_l5;
                    uint64_t t37 = (uintptr_t)t36;
                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t37 * 16));
                    gm_l6 = t41;
                    {
                        void * t44 = gm_l0;
                        void * t45 = GM_LD(void *, (((uint8_t *)t44) + 0));
                        uint64_t t46 = gm_l1;
                        gm_b16_8 t47 = gm_l6;
                        gm_tf5srh4mp4b3i t48 = gm_f_lookup_59juqoo(t45, t46, t47);
                        if (!((((uint8_t *)&t48))[8] != 0)) goto ie43;
                        uint64_t t49 = GM_LD(uint64_t, ((uint8_t *)&t48));
                        gm_l7 = t49;
                        {
                            {
                                void * t52 = gm_l0;
                                uint64_t t53 = gm_l7;
                                gm_t6qooom8qa2do t54;
                                gm_f_resolve_decl_pus042v((void *)&t54, (void *)NULL, cx, NULL, t52, t53);
                                if (gm_unw.kind) {
                                    goto gm_ll34;
                                }
                            }
                        }
                        goto ix43;
ie43:;
ix43:;
                    }
                    goto me39;
mn40:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 663, 5);
me39:;
                }
lc30:;
                {
                    uint64_t t56 = gm_l5;
                    uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                    uint64_t t59 = r57;
                    gm_l5 = t59;
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
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_resolve_decl_pus042v F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    uint64_t gm_l4; /* i */
    uint8_t gm_l5; /* quiet */
    GM_STACK_CHECK(&F, 1588u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_tagrkbqh054h7 t4;
        gm_f_state_a5bnsjb((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma6; default: goto ma9; }
ma6:;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t12 != ((uint8_t)2ULL)) goto mn11;
        F.l2 = (*(gm_t6qooom8qa2do *)(((uint8_t *)&t4) + 8));
        memmove(gm_out, ((uint8_t *)&F.l2), 208);
        goto gm_fdone;
        goto me5;
mn11:;
ma7:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t14 != ((uint8_t)1ULL)) goto mn13;
        {
            void * t17 = gm_l0;
            uint64_t t18 = gm_l1;
            uint8_t t19 = gm_f_is_structural_alias_qpmv4mu(t17, t18);
            if (!t19) goto ie16;
            uint64_t t20 = gm_l1;
            void * t21 = gm_l0;
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((((uint8_t *)t21) + 1688) + 0));
            uint8_t t23 = 0;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t22) + 8)); i++) if (gm_eq6groq37ram(((uint8_t *)GM_LD(void *, ((uint8_t *)&t22))) + i * 8, ((uint8_t *)&t20))) { t23 = 1; break; }
            uint8_t t24 = (uint8_t)(!t23);
            if (!t24) goto ie16;
            {
                void * t26 = gm_l0;
                void * t27 = (void *)(((uint8_t *)t26) + 1688);
                uint64_t t28 = gm_l1;
                gm_tftb1odc739t5 t29 = gm_f_push_ucgmlo2(t27, t28);
                if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srclv0dcbbfhves, 764, 17);
                gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
            }
            {
                void * t31 = gm_l0;
                uint64_t t32 = gm_l1;
                gm_f_compute_decl_gcf8mq9((void *)&F.l3, (void *)NULL, cx, NULL, t31, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t34 = gm_l0;
                gm_b16_8 t35 = GM_LD(gm_b16_8, ((((uint8_t *)t34) + 1688) + 0));
                gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
                uint64_t t37 = gm_l1;
                gm_tf5srh4mp4b3i t38 = gm_f_index_of_s9trr09(t36, t37);
                if (!((((uint8_t *)&t38))[8] != 0)) goto ie33;
                uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&t38));
                gm_l4 = t39;
                {
                    {
                        void * t42 = gm_l0;
                        void * t43 = (void *)(((uint8_t *)t42) + 1688);
                        uint64_t t44 = gm_l4;
                        uint64_t t45 = gm_f_remove_om8q0q2(t43, t44);
                    }
                }
                goto ix33;
ie33:;
ix33:;
            }
            {
                memmove(gm_out, ((uint8_t *)&F.l3), 208);
                goto gm_fdone;
            }
            goto ix16;
ie16:;
ix16:;
        }
        {
            void * t46 = gm_l0;
            uint64_t t47 = gm_l1;
            gm_b0_1 t48 = gm_f_report_cycle_mauubd4((void *)NULL, cx, NULL, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)11ULL));
            goto gm_fdone;
        }
        goto me5;
mn13:;
ma8:;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t50 != ((uint8_t)0ULL)) goto mn49;
        goto me5;
mn49:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 757, 5);
me5:;
    }
    {
        void * t52 = gm_l0;
        uint64_t t53 = gm_l1;
        gm_tagrkbqh054h7 t54;
        memset(&t54, 0, sizeof t54);
        GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)1ULL));
        gm_b0_1 t55 = gm_f_set_state_3j300fi((void *)NULL, cx, NULL, t52, t53, &t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t56 = gm_l0;
        void * t57 = (void *)(((uint8_t *)t56) + 1648);
        uint64_t t58 = gm_l1;
        gm_tftb1odc739t5 t59 = gm_f_push_ucgmlo2(t57, t58);
        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srclv0dcbbfhves, 775, 5);
        gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
    }
    {
        void * t61 = gm_l0;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)t61) + 2536));
        gm_l5 = t62;
    }
    {
        void * t63 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t63) + 2536), ((uint8_t)0));
    }
    {
    }
    {
        void * t67 = gm_l0;
        gm_trqfvmjtt4sij t68 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t67) + 1488));
        F.l6 = t68;
    }
    {
        void * t69 = gm_l0;
        gm_trqfvmjtt4sij t70 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t69) + 1528));
        F.l7 = t70;
    }
    {
        void * t71 = gm_l0;
        gm_trqfvmjtt4sij t72 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t71) + 1568));
        F.l8 = t72;
    }
    {
        void * t73 = gm_l0;
        gm_trqfvmjtt4sij t74 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t73) + 1608));
        F.l9 = t74;
    }
    {
        void * t75 = gm_l0;
        uint8_t *t76 = (((uint8_t *)t75) + 1488);
        gm_f_list_mvhhv5u((void *)&(*(gm_trqfvmjtt4sij *)t76), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t77 = gm_l0;
        uint8_t *t78 = (((uint8_t *)t77) + 1528);
        gm_f_list_ue2t2bm((void *)&(*(gm_trqfvmjtt4sij *)t78), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t79 = gm_l0;
        uint8_t *t80 = (((uint8_t *)t79) + 1568);
        gm_f_list_1jabh22((void *)&(*(gm_trqfvmjtt4sij *)t80), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t81 = gm_l0;
        uint8_t *t82 = (((uint8_t *)t81) + 1608);
        gm_f_list_0bj26n7((void *)&(*(gm_trqfvmjtt4sij *)t82), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t83 = gm_l0;
        uint64_t t84 = gm_l1;
        gm_f_compute_decl_gcf8mq9((void *)&F.l10, (void *)NULL, cx, NULL, t83, t84);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t85 = gm_l0;
        gm_trqfvmjtt4sij t86 = F.l6;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t85) + 1488), t86);
    }
    {
        void * t87 = gm_l0;
        gm_trqfvmjtt4sij t88 = F.l7;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t87) + 1528), t88);
    }
    {
        void * t89 = gm_l0;
        gm_trqfvmjtt4sij t90 = F.l8;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t89) + 1568), t90);
    }
    {
        void * t91 = gm_l0;
        gm_trqfvmjtt4sij t92 = F.l9;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t91) + 1608), t92);
    }
    {
        {
            void * t94 = gm_l0;
            void * t95 = (void *)(((uint8_t *)t94) + 1648);
            gm_tf5srh4mp4b3i t96 = gm_f_pop_j4rpvm5(t95);
        }
    }
    {
        uint64_t t98 = gm_l1;
        uint64_t t99 = (uintptr_t)t98;
        void * t100 = gm_l0;
        uint64_t t101 = GM_LD(uintptr_t, ((((uint8_t *)t100) + 304) + 0) + 8);
        if (t99 >= (uintptr_t)(t101)) gm_trap_index(t99, t101, gm_srclv0dcbbfhves, 797, 8);
        uint8_t t102 = ((GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t100) + 304) + 0)) + t99 * 216) + 0) == ((uint8_t)1ULL)));
        if (!t102) goto ie97;
        {
            uint64_t t104 = gm_l1;
            uint64_t t105 = (uintptr_t)t104;
            void * t106 = gm_l0;
            uint64_t t107 = GM_LD(uintptr_t, ((((uint8_t *)t106) + 304) + 0) + 8);
            if (t105 >= (uintptr_t)(t107)) gm_trap_index(t105, t107, gm_srclv0dcbbfhves, 797, 44);
            const uint8_t *t108 = ((uint8_t *)&F.l10);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tagrkbqh054h7 *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t106) + 304) + 0)) + t105 * 216)) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tagrkbqh054h7 *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t106) + 304) + 0)) + t105 * 216)) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tagrkbqh054h7 *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t106) + 304) + 0)) + t105 * 216)) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tagrkbqh054h7 *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t106) + 304) + 0)) + t105 * 216)) + 0, ((uint8_t)2ULL));
            memmove((uint8_t *)&(*(gm_tagrkbqh054h7 *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t106) + 304) + 0)) + t105 * 216)) + 8, t108, 208);
        }
        goto ix97;
ie97:;
ix97:;
    }
    {
        uint64_t t110 = gm_l1;
        uint64_t t111 = (uintptr_t)t110;
        void * t112 = gm_l0;
        uint64_t t113 = GM_LD(uintptr_t, ((((uint8_t *)t112) + 304) + 0) + 8);
        if (t111 >= (uintptr_t)(t113)) gm_trap_index(t111, t113, gm_srclv0dcbbfhves, 798, 20);
        uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t112) + 304) + 0)) + t111 * 216) + 0));
        if (t114 != ((uint8_t)2ULL)) goto ie109;
        gm_t6qooom8qa2do t115 = GM_LD(gm_t6qooom8qa2do, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t112) + 304) + 0)) + t111 * 216) + 8));
        F.l11 = t115;
        {
            uint8_t t117 = 0;
            F.ret = (*(gm_t6qooom8qa2do *)((uint8_t *)&F.l11));
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t117;
            goto gm_ld1_1;
        }
        goto ix109;
ie109:;
ix109:;
    }
    {
        memset(&F.ret, 0, sizeof F.ret);
        GM_ST(uint8_t, (uint8_t *)&F.ret + 0, ((uint8_t)11ULL));
        uint8_t t118 = 0;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t118;
        goto gm_ld1_1;
    }
    {
        {
            void * t120 = gm_l0;
            uint8_t t121 = gm_l5;
            GM_ST(uint8_t, (((uint8_t *)t120) + 2536), t121);
        }
    }
    goto gm_ls122;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t124 = gm_l0;
            uint8_t t125 = gm_l5;
            GM_ST(uint8_t, (((uint8_t *)t124) + 2536), t125);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls122:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_declarations_vrpg1fl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_resolve_declarations_vrpg1fl F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* done */
    uint64_t gm_l2; /* m */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 579u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l1;
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
            uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 200) + 0) + 8);
            uint8_t t7 = (t3 < t6);
            if (!t7) goto lb2;
            {
                uint64_t t10 = gm_l1;
                gm_l2 = t10;
            }
            {
                uint64_t t11 = gm_l1;
                uint64_t r12; bool o13 = __builtin_add_overflow(t11, ((uint64_t)1ULL), &r12);
                if (o13) gm_trap("integer overflow in `+`", gm_srclv0dcbbfhves, 728, 9);
                uint64_t t14 = r12;
                gm_l1 = t14;
            }
            {
                uint64_t t16 = gm_l2;
                uint64_t t17 = (uintptr_t)t16;
                void * t18 = gm_l0;
                void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
                uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 200) + 0) + 8);
                if (t17 >= (uintptr_t)(t20)) gm_trap_index(t17, t20, gm_srclv0dcbbfhves, 729, 12);
                uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 200) + 0)) + t17 * 184) + 112));
                if (!t21) goto ie15;
                {
                    goto lc2;
                }
                goto ix15;
ie15:;
ix15:;
            }
            {
                uint64_t t23 = gm_l2;
                uint64_t t24 = (uintptr_t)t23;
                void * t25 = gm_l0;
                void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
                uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 200) + 0) + 8);
                if (t24 >= (uintptr_t)(t27)) gm_trap_index(t24, t27, gm_srclv0dcbbfhves, 731, 14);
                uint64_t t28 = GM_LD(uintptr_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t26) + 200) + 0)) + t24 * 184) + 16) + 0) + 8);
                gm_l3 = t28;
            }
            {
                {
                    gm_l5 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t30 = gm_l3;
                    gm_l4 = t30;
                }
                {
                    for (;;) {
                        uint64_t t32 = gm_l5;
                        uint64_t t33 = gm_l4;
                        uint8_t t34 = (t32 < t33);
                        if (!t34) goto lb31;
                        {
                            uint64_t t37 = gm_l5;
                            uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&t37));
                            gm_l6 = t40;
                            {
                                {
                                    void * t43 = gm_l0;
                                    uint64_t t44 = gm_l6;
                                    uint64_t t45 = (uintptr_t)t44;
                                    uint64_t t46 = gm_l2;
                                    uint64_t t47 = (uintptr_t)t46;
                                    void * t48 = gm_l0;
                                    void * t49 = GM_LD(void *, (((uint8_t *)t48) + 0));
                                    uint64_t t50 = GM_LD(uintptr_t, ((((uint8_t *)t49) + 200) + 0) + 8);
                                    if (t47 >= (uintptr_t)(t50)) gm_trap_index(t47, t50, gm_srclv0dcbbfhves, 733, 32);
                                    uint64_t t51 = GM_LD(uintptr_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t49) + 200) + 0)) + t47 * 184) + 16) + 0) + 8);
                                    if (t45 >= (uintptr_t)(t51)) gm_trap_index(t45, t51, gm_srclv0dcbbfhves, 733, 32);
                                    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t49) + 200) + 0)) + t47 * 184) + 16) + 0)) + t45 * 24) + 16));
                                    gm_t6qooom8qa2do t53;
                                    gm_f_resolve_decl_pus042v((void *)&t53, (void *)NULL, cx, NULL, t43, t52);
                                    if (gm_unw.kind) {
                                        goto gm_ll35;
                                    }
                                }
                            }
                            goto me38;
mn39:;
                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 732, 9);
me38:;
                        }
lc31:;
                        {
                            uint64_t t55 = gm_l5;
                            uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                            uint64_t t58 = r56;
                            gm_l5 = t58;
                        }
                    }
gm_ll35:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb31; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc31; }
                    }
                    goto gm_ll8;
lb31:;
                }
            }
lc2:;
        }
gm_ll8:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
    }
    {
        void * t59 = gm_l0;
        gm_b0_1 t60 = gm_f_check_method_candidates_k08rvu6((void *)NULL, cx, NULL, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t61 = gm_l0;
        void * t62 = GM_LD(void *, (((uint8_t *)t61) + 0));
        gm_b0_1 t63 = gm_f_check_import_cycles_2u7oqo0((void *)NULL, cx, NULL, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t64 = gm_l0;
        gm_b0_1 t65 = gm_f_check_pub_signatures_plkds6r((void *)NULL, cx, NULL, t64);
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
inline __attribute__((aligned(4))) void gm_f_sema_r1nlnnm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_sema_r1nlnnm F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        memset((uint8_t *)&F.l1 + 585, 0, 7);
        memset((uint8_t *)&F.l1 + 673, 0, 7);
        memset((uint8_t *)&F.l1 + 1365, 0, 3);
        memset((uint8_t *)&F.l1 + 1772, 0, 4);
        memset((uint8_t *)&F.l1 + 1820, 0, 4);
        memset((uint8_t *)&F.l1 + 1865, 0, 7);
        memset((uint8_t *)&F.l1 + 1964, 0, 4);
        memset((uint8_t *)&F.l1 + 2164, 0, 4);
        memset((uint8_t *)&F.l1 + 2537, 0, 7);
        void * t2 = gm_l0;
        GM_ST(void *, (uint8_t *)&F.l1 + 0, t2);
        gm_f_types_8a61ga3((void *)&(*(gm_tpcj7kul6hlib *)((uint8_t *)&F.l1 + 8)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_eb5ua5r((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 304)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_dvp9pla((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 344)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_dvbol2p((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 384)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_0bg6tcp((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 424)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_atkrn3m((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 464)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_m7es0sh((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 504)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_qv7bq94((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 544)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 584, ((uint8_t)0));
        gm_f_list_vhqj1o4((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 592)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_sajcold((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 632)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 672, ((uint8_t)0));
        gm_f_list_4kh12ib((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 680)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_8r7pelg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 720)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_9q64p2g((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 760)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_tqcq99u((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 800)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_uun92du((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 840)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 880)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 920)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t3;
        memset(&t3, 0, sizeof t3);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&F.l1 + 960, t3);
        gm_f_list_c02589n((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 968)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_c02589n((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1008)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_2343nn9((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1048)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_h93u1dq((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1088)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_t29l5ca((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1128)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_au01ire((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1168)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_iug9p19((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 1208)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_iug9p19((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 1264)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_406coeg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1320)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint32_t, (uint8_t *)&F.l1 + 1360, ((uint32_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 1364, ((uint8_t)0));
        gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1368)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_dql1gah((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1408)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_5pborc0((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1448)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_mvhhv5u((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1488)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_ue2t2bm((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1528)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_1jabh22((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1568)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_0bj26n7((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1608)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_5dhgg4i((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1648)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_5dhgg4i((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1688)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1728)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint32_t, (uint8_t *)&F.l1 + 1768, ((uint32_t)0ULL));
        gm_f_list_ib5k4gb((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1776)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint32_t, (uint8_t *)&F.l1 + 1816, ((uint32_t)0ULL));
        gm_f_list_au01ire((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1824)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 1864, ((uint8_t)0));
        gm_f_list_v3cpbrb((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1872)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_je8tl46((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1912)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint32_t, (uint8_t *)&F.l1 + 1952, ((uint32_t)1ULL));
        gm_t9805qtonbsag t4;
        memset(&t4, 0, sizeof t4);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&F.l1 + 1956, t4);
        gm_f_list_l3jkoh2((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1968)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_84luf3l((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 2008)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_iibum07((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 2064)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2120)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint32_t, (uint8_t *)&F.l1 + 2160, ((uint32_t)0ULL));
        GM_ST(void *, (uint8_t *)&F.l1 + 2168, ((void *)0));
        GM_ST(void *, (uint8_t *)&F.l1 + 2176, ((void *)0));
        GM_ST(void *, (uint8_t *)&F.l1 + 2184, ((void *)0));
        gm_f_list_tn37ahu((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2192)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_i6iqfhd((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2232)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_0npefvp((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2272)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t5 = GM_LD(uint64_t, gm_cbbjgr0cpr9);
        GM_ST(uint64_t, (uint8_t *)&F.l1 + 2312, t5);
        gm_f_hash_map_0vd239u((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 2320)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_1teief7((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2376)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_kk91efj((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2416)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2456)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_7s4om10((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2496)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 2536, ((uint8_t)0));
        gm_f_list_s83jn2q((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2544)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_s83jn2q((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2584)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_1so3e0v((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 2624)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_ve01i1t((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 2680)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_8lre197((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 2736)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = (void *)(((uint8_t *)&F.l1) + 1048);
        gm_tammvh1iespi3 t7;
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_snenkhfev166s));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&t7 + 0, t8);
        GM_ST(uint64_t, (uint8_t *)&t7 + 16, ((uint64_t)0ULL));
        gm_b16_8 t9 = gm_f_empty_slice_d914i5u();
        GM_ST(gm_b16_8, (uint8_t *)&t7 + 24, t9);
        gm_b16_8 t10 = gm_f_empty_slice_p6mn4us();
        GM_ST(gm_b16_8, (uint8_t *)&t7 + 40, t10);
        gm_tftb1odc739t5 t11 = gm_f_push_puil3ql(t6, &t7);
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_srclv0dcbbfhves, 570, 5);
        gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 2792);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_state_a5bnsjb F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    uint64_t gm_l2; /* m */
    GM_STACK_CHECK(&F, 687u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        for (;;) {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 304) + 0) + 8);
            uint64_t t5 = gm_l1;
            uint8_t t6 = (t4 <= t5);
            if (!t6) goto lb2;
            {
                void * t9 = gm_l0;
                void * t10 = (void *)(((uint8_t *)t9) + 304);
                gm_tagrkbqh054h7 t11;
                memset(&t11, 0, sizeof t11);
                GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
                gm_tftb1odc739t5 t12 = gm_f_push_kpjuj6c(t10, &t11);
                if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srclv0dcbbfhves, 742, 38);
                gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
            }
lc2:;
        }
lb2:;
    }
    {
        uint64_t t15 = gm_l1;
        uint64_t t16 = (uintptr_t)t15;
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 304) + 0) + 8);
        if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srclv0dcbbfhves, 744, 8);
        uint8_t t19 = ((GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 304) + 0)) + t16 * 216) + 0) == ((uint8_t)0ULL)));
        if (!t19) goto ie14;
        uint64_t t20 = gm_l1;
        uint64_t t21 = (uintptr_t)t20;
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
        uint64_t t24 = GM_LD(uintptr_t, ((((uint8_t *)t23) + 240) + 0) + 8);
        if (t21 >= (uintptr_t)(t24)) gm_trap_index(t21, t24, gm_srclv0dcbbfhves, 744, 46);
        uint8_t t25;
        uint8_t t28 = GM_LD(uint8_t, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t23) + 240) + 0)) + t21 * 216) + 40) + 64) + 0));
        if (t28 != ((uint8_t)4ULL)) goto mn27;
        t25 = ((uint8_t)1);
        goto me26;
mn27:;
        t25 = ((uint8_t)0);
        goto me26;
mn29:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 744, 46);
me26:;
        if (!t25) goto ie14;
        {
            uint64_t t32 = gm_l1;
            uint64_t t33 = (uintptr_t)t32;
            void * t34 = gm_l0;
            void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
            uint64_t t36 = GM_LD(uintptr_t, ((((uint8_t *)t35) + 240) + 0) + 8);
            if (t33 >= (uintptr_t)(t36)) gm_trap_index(t33, t36, gm_srclv0dcbbfhves, 745, 26);
            uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t35) + 240) + 0)) + t33 * 216) + 200) + 0));
            if (t37 != ((uint8_t)1ULL)) goto ie31;
            uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t35) + 240) + 0)) + t33 * 216) + 200) + 8));
            gm_l2 = t38;
            {
                uint64_t t40 = gm_l1;
                uint64_t t41 = (uintptr_t)t40;
                void * t42 = gm_l0;
                uint64_t t43 = GM_LD(uintptr_t, ((((uint8_t *)t42) + 304) + 0) + 8);
                if (t41 >= (uintptr_t)(t43)) gm_trap_index(t41, t43, gm_srclv0dcbbfhves, 745, 54);
                uint64_t t44 = gm_l2;
                gm_t6qooom8qa2do t45;
                memset(&t45, 0, sizeof t45);
                GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)9ULL));
                GM_ST(uint64_t, ((uint8_t *)&t45 + 8), t44);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tagrkbqh054h7 *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t42) + 304) + 0)) + t41 * 216)) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tagrkbqh054h7 *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t42) + 304) + 0)) + t41 * 216)) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tagrkbqh054h7 *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t42) + 304) + 0)) + t41 * 216)) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tagrkbqh054h7 *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t42) + 304) + 0)) + t41 * 216)) + 0, ((uint8_t)2ULL));
                GM_ST(gm_t6qooom8qa2do, (uint8_t *)&(*(gm_tagrkbqh054h7 *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t42) + 304) + 0)) + t41 * 216)) + 8, t45);
            }
            goto ix31;
ie31:;
ix31:;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        uint64_t t46 = gm_l1;
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, ((((uint8_t *)t48) + 304) + 0) + 8);
        if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srclv0dcbbfhves, 747, 12);
        memmove(gm_out, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t48) + 304) + 0)) + t47 * 216), 216);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_walk_v858co7 F;
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    F.l2 = a2;
    {
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
    }
    {
        void * t8 = F.l0;
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        gm_t88rcucg1b7uk t10 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&F.l3, t9, GM_CONV(gm_csguankko4lmhd, t10));
    }
    {
        void * t11 = (void *)((uint8_t *)&F.l3);
        gm_closure t12 = ((gm_closure){(void *)gm_f_fn_bfunt71, (void *)&F.h, cx});
        gm_f_for_each_component_o5cuh7c(t11, t12);
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
