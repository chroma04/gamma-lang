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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { gm_tl874t2atnlh3 f0; gm_t88rcucg1b7uk f1; } gm_trlabqsjjp0ds;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint64_t v1; } f1; } gm_t833u93mr4iob;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t76123gfet73g v0; unsigned __int128 v1; } f1; } gm_tdlro53ljvkl0;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
_Static_assert(sizeof(gm_trlabqsjjp0ds) == 16 && _Alignof(gm_trlabqsjjp0ds) == 4 && offsetof(gm_trlabqsjjp0ds, f0) == 0 && offsetof(gm_trlabqsjjp0ds, f1) == 12, "layout of `(Span, Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t833u93mr4iob) == 32 && _Alignof(gm_t833u93mr4iob) == 8 && offsetof(gm_t833u93mr4iob, f0) == 0 && offsetof(gm_t833u93mr4iob, f1) == 8, "layout of `Flow!u64`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlro53ljvkl0) == 48 && _Alignof(gm_tdlro53ljvkl0) == 16 && offsetof(gm_tdlro53ljvkl0, f0) == 0 && offsetof(gm_tdlro53ljvkl0, f1) == 16, "layout of `Flow!u128`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; } f1; } gm_tgfdnufetdnfc;
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tgfdnufetdnfc) == 48 && _Alignof(gm_tgfdnufetdnfc) == 8 && offsetof(gm_tgfdnufetdnfc, f0) == 0 && offsetof(gm_tgfdnufetdnfc, f1) == 8, "layout of `MemError!()`");
typedef struct { uint64_t f0; uint8_t p1[8]; gm_t6evimt84lndq f1; } gm_thf8as07kj3uj;
_Static_assert(sizeof(gm_thf8as07kj3uj) == 64 && _Alignof(gm_thf8as07kj3uj) == 16 && offsetof(gm_thf8as07kj3uj, f0) == 0 && offsetof(gm_thf8as07kj3uj, f1) == 16, "layout of `(i: usize, x: Value)`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_bit_width_0fd5f52 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_cell_addr_bl2jmen {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_const_addr_heqn3dl {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_const_bits_1of8c6t {
    gm_frame h;
    gm_t6evimt84lndq l2; /* v */
    gm_t6evimt84lndq l14; /* x */
    gm_tdlro53ljvkl0 ret;
};
struct gm_fr_const_id_2mlh0og {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_fn_addr_odbbf6a {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_global_addr_9an4270 {
    gm_frame h;
    gm_tq7qjpkmqmc5s l4; /* gl */
    gm_t833u93mr4iob ret;
};
struct gm_fr_kind_htv6093 {
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
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_opt_inner_q4ga84u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_payload_offset_la0c8ft {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_place_addr_m751en7 {
    gm_frame h;
    gm_tpautr4br7h3l l17; /* sd */
    gm_t833u93mr4iob ret;
};
struct gm_fr_put_ptr_188p3mm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_u64_irp77r2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsupported_70vvk1e {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_value_bytes_l7s126n {
    gm_frame h;
    gm_t6evimt84lndq l2; /* v */
    gm_tckdobunaulee l18; /* vd */
    gm_t6evimt84lndq l28; /* x */
    gm_t6evimt84lndq l34; /* x */
    gm_tpautr4br7h3l l36; /* sd */
    gm_t6evimt84lndq l43; /* x */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_wr_word_npte9fs {
    gm_frame h;
    gm_b0_1 ret;
};
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_alloc_mem_0vu6kcs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_t9qkh0rsooc8d a4, uint8_t a5);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_0fd5f52(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) gm_tplpbqf3duvn3 gm_f_bits_field_vqua3kd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_cell_addr_bl2jmen(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_t88rcucg1b7uk a3, uint64_t a4, uint8_t a5);
__attribute__((aligned(4))) void gm_f_const_addr_heqn3dl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_const_bits_1of8c6t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_fn_addr_odbbf6a(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_3tqcmdm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_ftjmcbc(void * a0, gm_tn68el8d14vku a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pk8ncss(void * a0, gm_trlabqsjjp0ds a1);
__attribute__((aligned(4))) void gm_f_get_srnes22(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_global_addr_9an4270(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_global_index_nhjk80t(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_init_9fuf99v(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_make_fill_jpvm0kc(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint8_t a1, const void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_place_addr_m751en7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_f99phma(void * a0, gm_tn68el8d14vku a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_float_fiad72d(void * a0, gm_csguankko4lmhd a1, double a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_t4gfk5t(void * a0, uint64_t a1, gm_tlcm4lvu3g1a8 a2);
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) gm_tgglga8ro3isc gm_f_res_types_pg7rvck(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) void gm_f_value_bytes_l7s126n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, uint64_t a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2);
extern uint8_t gm_cmiriali67r[8] __attribute__((aligned(8)));
extern uint8_t gm_k5m9lemc4jn[40] __attribute__((aligned(8)));
extern uint8_t gm_kba69021uoh[40] __attribute__((aligned(8)));
extern uint8_t gm_kjqpf95jhcm[40] __attribute__((aligned(8)));
extern uint8_t gm_koedfqu36hd[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0ggo8ek7kfrt[34];
extern const uint8_t gm_s3js9g3l46oiv[40];
extern const uint8_t gm_s67fd8s6hf84e[24];
extern const uint8_t gm_sa6mes8ijqmmk[30];
extern const uint8_t gm_sbbavs945cjhp[32];
extern const uint8_t gm_sct0vr0h329vl[43];
extern const uint8_t gm_sfali8nl85hc9[25];
extern const uint8_t gm_sgiqpj781s1a9[36];
extern const uint8_t gm_sqrl3kgomorsv[39];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src6hq50r4rk7e6[];
extern const char gm_src8sgunk80orlb[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const uint8_t gm_svlcdvcv2bp94[24];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_15fjgq1 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 16);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_align_of_k6eqqrn((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_of_k6eqqrn F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_a5ovnrm((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 8));
    F.ret = t5;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_0fd5f52(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bit_width_0fd5f52 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 8));
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint32_t t4 = gm_f_field_bits_2ktfm1k((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_fn_addr_odbbf6a(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_addr_odbbf6a F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1 = GM_LD(uint64_t, gm_cmiriali67r);
    uint32_t t2 = gm_l1;
    uint64_t t3 = (uint64_t)t2;
    uint64_t r4; bool o5 = __builtin_mul_overflow(((uint64_t)16ULL), t3, &r4);
    if (o5) gm_trap("integer overflow in `*`", gm_src6hq50r4rk7e6, 5, 64);
    uint64_t t6 = r4;
    uint64_t r7; bool o8 = __builtin_add_overflow(t1, t6, &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 5, 52);
    uint64_t t9 = r7;
    F.ret = t9;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_htv6093 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 8));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&(*(gm_t53at80pfdus3 *)gm_out), t3, GM_CONV(gm_csguankko4lmhd, t4));
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
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nullable_pointer_7qfmn6o F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    uint8_t t3 = gm_f_is_nullable_pointer_nkie06s(t1, GM_CONV(gm_csguankko4lmhd, t2));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_qpt7c30 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 16);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_of_e6hfj1r((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 16));
    F.ret = t5;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_opt_inner_q4ga84u F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* inner */
    GM_STACK_CHECK_SHALLOW(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_htv6093((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)13ULL)) goto ie2;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 8));
        gm_l2 = t7;
        {
            gm_t88rcucg1b7uk t9 = gm_l2;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_t88rcucg1b7uk t12 = gm_l1;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_payload_offset_la0c8ft F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* offs */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t4;
    }
    {
        uint64_t t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t8 = (t7 > ((uint64_t)1ULL));
        if (!t8) goto ie6;
        uint64_t t10 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 231, 30);
        uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t10 * 8));
        uint64_t t13 = t12;
        t5 = t13;
        goto ix6;
ie6:;
        uint64_t t15 = ((uint64_t)0ULL);
        t5 = t15;
ix6:;
        uint8_t t16 = 0;
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
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_ptr_188p3mm F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* addr */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 176));
    uint64_t t8 = gm_f_us_cuv01fp(t7);
    uint64_t t9 = (uintptr_t)t8;
    if (t5 > t9 || t9 > (uintptr_t)t3) gm_trap_slice(t5, t9, t3, gm_srcnqtpfma6qe4a, 106, 67);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t4 + t5 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9 - t5));
    uint64_t t11 = gm_l2;
    gm_f_put_u64_irp77r2(t1, t10, t11);
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_u64_irp77r2 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = (t3 <= ((uint64_t)8ULL));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            gm_b16_8 t7 = gm_l1;
            uint64_t t8 = gm_l2;
            gm_f_wr_word_npte9fs(t6, t7, t8);
        }
        goto ix2;
ie2:;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_l1;
            uint64_t t12 = gm_l2;
            unsigned __int128 t13 = (unsigned __int128)t12;
            gm_f_put_uint_cddin99(t10, t11, t13);
        }
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_70vvk1e F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_l2;
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    gm_tabb710d9mq2e t5;
    gm_f_format_46el73v((void *)&t5, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcpit8mdn0781q, 437, 107);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t7);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wr_word_npte9fs F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 89));
        if (!t4) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)8ULL)));
            if (!t8) goto ie6;
            {
                uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 329, 13);
                uint64_t t12 = gm_l2;
                uint8_t t13 = (uint8_t)t12;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1), t13);
            }
            {
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 330, 13);
                uint64_t t16 = gm_l2;
                unsigned __int128 n17 = (unsigned __int128)((uint64_t)8ULL);
                if (n17 >= 64) gm_trap_shift((uint64_t)n17, "u64", gm_srcnqtpfma6qe4a, 330, 21);
                uint64_t t18 = (uint64_t)(t16 >> (int)n17);
                uint8_t t19 = (uint8_t)t18;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1), t19);
            }
            {
                uint64_t t20 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srcnqtpfma6qe4a, 331, 13);
                uint64_t t22 = gm_l2;
                unsigned __int128 n23 = (unsigned __int128)((uint64_t)16ULL);
                if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcnqtpfma6qe4a, 331, 21);
                uint64_t t24 = (uint64_t)(t22 >> (int)n23);
                uint8_t t25 = (uint8_t)t24;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t20 * 1), t25);
            }
            {
                uint64_t t26 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srcnqtpfma6qe4a, 332, 13);
                uint64_t t28 = gm_l2;
                unsigned __int128 n29 = (unsigned __int128)((uint64_t)24ULL);
                if (n29 >= 64) gm_trap_shift((uint64_t)n29, "u64", gm_srcnqtpfma6qe4a, 332, 21);
                uint64_t t30 = (uint64_t)(t28 >> (int)n29);
                uint8_t t31 = (uint8_t)t30;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t26 * 1), t31);
            }
            {
                uint64_t t32 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 333, 13);
                uint64_t t34 = gm_l2;
                unsigned __int128 n35 = (unsigned __int128)((uint64_t)32ULL);
                if (n35 >= 64) gm_trap_shift((uint64_t)n35, "u64", gm_srcnqtpfma6qe4a, 333, 21);
                uint64_t t36 = (uint64_t)(t34 >> (int)n35);
                uint8_t t37 = (uint8_t)t36;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1), t37);
            }
            {
                uint64_t t38 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srcnqtpfma6qe4a, 334, 13);
                uint64_t t40 = gm_l2;
                unsigned __int128 n41 = (unsigned __int128)((uint64_t)40ULL);
                if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcnqtpfma6qe4a, 334, 21);
                uint64_t t42 = (uint64_t)(t40 >> (int)n41);
                uint8_t t43 = (uint8_t)t42;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t38 * 1), t43);
            }
            {
                uint64_t t44 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srcnqtpfma6qe4a, 335, 13);
                uint64_t t46 = gm_l2;
                unsigned __int128 n47 = (unsigned __int128)((uint64_t)48ULL);
                if (n47 >= 64) gm_trap_shift((uint64_t)n47, "u64", gm_srcnqtpfma6qe4a, 335, 21);
                uint64_t t48 = (uint64_t)(t46 >> (int)n47);
                uint8_t t49 = (uint8_t)t48;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t44 * 1), t49);
            }
            {
                uint64_t t50 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 336, 13);
                uint64_t t52 = gm_l2;
                unsigned __int128 n53 = (unsigned __int128)((uint64_t)56ULL);
                if (n53 >= 64) gm_trap_shift((uint64_t)n53, "u64", gm_srcnqtpfma6qe4a, 336, 21);
                uint64_t t54 = (uint64_t)(t52 >> (int)n53);
                uint8_t t55 = (uint8_t)t54;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1), t55);
            }
            {
                gm_b0_1 t56;
                uint8_t t57 = 0;
                F.ret = t56;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t60 = ((t59 == ((uint64_t)1ULL)));
            if (!t60) goto ie58;
            {
                uint64_t t62 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t62 >= (uintptr_t)(t63)) gm_trap_index(t62, t63, gm_srcnqtpfma6qe4a, 340, 13);
                uint64_t t64 = gm_l2;
                uint8_t t65 = (uint8_t)t64;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t62 * 1), t65);
            }
            {
                gm_b0_1 t66;
                uint8_t t67 = 0;
                F.ret = t66;
                goto gm_fret;
            }
            goto ix58;
ie58:;
ix58:;
        }
        {
            uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t70 = ((t69 == ((uint64_t)4ULL)));
            if (!t70) goto ie68;
            {
                uint64_t t72 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t72 >= (uintptr_t)(t73)) gm_trap_index(t72, t73, gm_srcnqtpfma6qe4a, 344, 13);
                uint64_t t74 = gm_l2;
                uint8_t t75 = (uint8_t)t74;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t72 * 1), t75);
            }
            {
                uint64_t t76 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_srcnqtpfma6qe4a, 345, 13);
                uint64_t t78 = gm_l2;
                unsigned __int128 n79 = (unsigned __int128)((uint64_t)8ULL);
                if (n79 >= 64) gm_trap_shift((uint64_t)n79, "u64", gm_srcnqtpfma6qe4a, 345, 21);
                uint64_t t80 = (uint64_t)(t78 >> (int)n79);
                uint8_t t81 = (uint8_t)t80;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t76 * 1), t81);
            }
            {
                uint64_t t82 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcnqtpfma6qe4a, 346, 13);
                uint64_t t84 = gm_l2;
                unsigned __int128 n85 = (unsigned __int128)((uint64_t)16ULL);
                if (n85 >= 64) gm_trap_shift((uint64_t)n85, "u64", gm_srcnqtpfma6qe4a, 346, 21);
                uint64_t t86 = (uint64_t)(t84 >> (int)n85);
                uint8_t t87 = (uint8_t)t86;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1), t87);
            }
            {
                uint64_t t88 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t88 >= (uintptr_t)(t89)) gm_trap_index(t88, t89, gm_srcnqtpfma6qe4a, 347, 13);
                uint64_t t90 = gm_l2;
                unsigned __int128 n91 = (unsigned __int128)((uint64_t)24ULL);
                if (n91 >= 64) gm_trap_shift((uint64_t)n91, "u64", gm_srcnqtpfma6qe4a, 347, 21);
                uint64_t t92 = (uint64_t)(t90 >> (int)n91);
                uint8_t t93 = (uint8_t)t92;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t88 * 1), t93);
            }
            {
                gm_b0_1 t94;
                uint8_t t95 = 0;
                F.ret = t94;
                goto gm_fret;
            }
            goto ix68;
ie68:;
ix68:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t96 = gm_l0;
        gm_b16_8 t97 = gm_l1;
        uint64_t t98 = gm_l2;
        unsigned __int128 t99 = (unsigned __int128)t98;
        gm_f_put_uint_cddin99(t96, t97, t99);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_cell_addr_bl2jmen(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_t88rcucg1b7uk a3, uint64_t a4, uint8_t a5) {
    struct gm_fr_cell_addr_bl2jmen F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* cell */
    gm_t88rcucg1b7uk gm_l3; /* t */
    uint64_t gm_l4; /* align */
    uint8_t gm_l5; /* mutable */
    uint64_t gm_l6; /* key */
    gm_tlcm4lvu3g1a8 gm_l7; /* a */
    uint64_t gm_l8; /* n */
    uint64_t gm_l9; /* addr */
    gm_b16_8 gm_l10; /* b */
    GM_STACK_CHECK(&F, 751u);
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
        void * t2 = gm_l2;
        uint64_t t3 = (uintptr_t)t2;
        gm_l6 = t3;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 896);
        uint64_t t7 = gm_l6;
        gm_tb7u19loo3j87 t8;
        gm_f_get_srnes22((void *)&t8, t6, t7);
        if (!((((uint8_t *)&t8))[16] != 0)) goto ie4;
        gm_tlcm4lvu3g1a8 t9 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t8));
        gm_l7 = t9;
        {
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 0));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t11);
            goto gm_fdone;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t12 = gm_l0;
        gm_t88rcucg1b7uk t13 = gm_l3;
        uint64_t t14 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t14;
    }
    {
        void * t15 = gm_l0;
        gm_tl874t2atnlh3 t16 = gm_l1;
        uint64_t t17 = gm_l8;
        uint64_t t18 = gm_l4;
        gm_t9qkh0rsooc8d t19;
        uint8_t t21 = gm_l5;
        if (!t21) goto ie20;
        gm_t9qkh0rsooc8d t23;
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)5ULL));
        gm_t9qkh0rsooc8d t24 = t23;
        t19 = t24;
        goto ix20;
ie20:;
        gm_t9qkh0rsooc8d t26;
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)4ULL));
        gm_t9qkh0rsooc8d t27 = t26;
        t19 = t27;
ix20:;
        uint8_t t28 = gm_l5;
        gm_t833u93mr4iob t29;
        gm_f_alloc_mem_0vu6kcs((void *)&t29, (void *)NULL, cx, NULL, t15, t16, t17, t18, t19, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t29))[0] != 0)) {
            gm_t833u93mr4iob t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
            ((uint8_t *)&t30)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t30 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t29) + 8)));
            F.ret = t30;
            goto gm_fret;
        }
        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&t29) + 8));
        gm_l9 = t31;
    }
    {
        void * t32 = gm_l0;
        void * t33 = (void *)(((uint8_t *)t32) + 896);
        uint64_t t34 = gm_l6;
        uint64_t t35 = gm_l9;
        gm_t88rcucg1b7uk t36 = gm_l3;
        gm_tlcm4lvu3g1a8 t37;
        GM_ST(uint32_t, (uint8_t *)&t37 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t37 + 0), t35);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t37 + 8), t36);
        gm_tftb1odc739t5 t38 = gm_f_put_t4gfk5t(t33, t34, t37);
        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_src8sgunk80orlb, 20, 5);
        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
    }
    {
        uint64_t t40 = gm_l8;
        uint64_t t41 = gm_f_us_cuv01fp(t40);
        gm_tabb710d9mq2e t42;
        gm_f_make_fill_jpvm0kc((void *)&t42, (void *)NULL, cx, NULL, t41, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k5m9lemc4jn)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_src8sgunk80orlb, 21, 10);
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
        gm_l10 = t43;
    }
    {
    }
    {
        void * t47 = gm_l0;
        gm_tl874t2atnlh3 t48 = gm_l1;
        void * t49 = gm_l2;
        gm_t88rcucg1b7uk t50 = gm_l3;
        gm_b16_8 t51 = gm_l10;
        gm_tcfa6l7reoosn t52;
        gm_f_value_bytes_l7s126n((void *)&t52, (void *)NULL, cx, NULL, t47, t48, &(*(gm_t6evimt84lndq *)(((uint8_t *)t49) + 0)), t50, t51, ((uint64_t)0ULL));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        if (((((uint8_t *)&t52))[0] != 0)) {
            gm_t833u93mr4iob t53;
            GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
            ((uint8_t *)&t53)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t53 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t52) + 8)));
            F.ret = t53;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_1;
        }
        gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 8));
    }
    {
        {
            void * t56 = gm_l0;
            void * t57 = (void *)(((uint8_t *)t56) + 184);
            uint64_t t58 = gm_l9;
            gm_b16_8 t59 = gm_l10;
            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
            gm_tgfdnufetdnfc t61;
            gm_f_init_9fuf99v((void *)&t61, t57, t58, t60);
        }
    }
    {
        uint64_t t62 = gm_l9;
        GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&F.ret + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&F.ret + 24, 0);
        ((uint8_t *)&F.ret)[0] = 0;
        GM_ST(uint64_t, (uint8_t *)&F.ret + 8, t62);
        uint8_t t63 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t63;
        goto gm_ld1_1;
    }
    {
        gm_b16_8 t64 = gm_l10;
        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
        gm_b0_1 t66 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t65, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kba69021uoh)));
    }
    goto gm_ls67;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_b16_8 t68 = gm_l10;
        gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
        gm_b0_1 t70 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t69, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kba69021uoh)));
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls67:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_const_addr_heqn3dl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_const_addr_heqn3dl F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tl874t2atnlh3 gm_l3; /* at */
    uint32_t gm_l4; /* id */
    void * gm_l5; /* cell */
    GM_STACK_CHECK(&F, 360u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
        gm_tl874t2atnlh3 t4 = gm_l1;
        gm_t88rcucg1b7uk t5 = gm_l2;
        gm_t9805qtonbsag t6 = gm_f_const_id_2mlh0og(t3, GM_CONV(gm_cseahvg8v7p0vs, t4), GM_CONV(gm_csguankko4lmhd, t5));
        uint32_t t7;
        if (((((uint8_t *)&t6))[4] != 0)) {
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            void * t9 = gm_l0;
            gm_tl874t2atnlh3 t10 = gm_l3;
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s0ggo8ek7kfrt));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(33));
            gm_t76123gfet73g t12;
            gm_f_unsupported_70vvk1e((void *)&t12, (void *)NULL, cx, NULL, t9, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t12);
            goto gm_fdone;
        }
        gm_l4 = t7;
    }
    {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 8));
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((((uint8_t *)t14) + 1968) + 0));
        gm_b16_8 t16 = t15;
        uint32_t t17 = gm_l4;
        uint64_t t18 = (uint64_t)t17;
        uint64_t t19 = gm_f_us_cuv01fp(t18);
        uint64_t t20 = (uintptr_t)t19;
        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&t16) + 8);
        if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_src8sgunk80orlb, 8, 13);
        void * t22 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t16)) + t20 * 88) + 80));
        void * t23;
        if ((GM_LD(void *, ((uint8_t *)&t22)) != 0)) {
            void * t24 = GM_LD(void *, (((uint8_t *)&t22) + 0));
            t23 = t24;
        } else {
            void * t25 = gm_l0;
            gm_tl874t2atnlh3 t26 = gm_l3;
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_s3js9g3l46oiv));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(39));
            gm_t76123gfet73g t28;
            gm_f_unsupported_70vvk1e((void *)&t28, (void *)NULL, cx, NULL, t25, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t28);
            goto gm_fdone;
        }
        gm_l5 = t23;
    }
    {
        void * t29 = gm_l0;
        gm_tl874t2atnlh3 t30 = gm_l3;
        void * t31 = gm_l5;
        gm_t88rcucg1b7uk t32 = gm_l2;
        void * t33 = gm_l0;
        gm_t88rcucg1b7uk t34 = gm_l2;
        uint64_t t35 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_cell_addr_bl2jmen((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t29, t30, t31, t32, t35, ((uint8_t)0));
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
__attribute__((aligned(4))) void gm_f_const_bits_1of8c6t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_const_bits_1of8c6t F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l3; /* t */
    uint8_t gm_l4; /* b */
    unsigned __int128 gm_l5; /* bits */
    uint32_t gm_l6; /* w */
    uint32_t gm_l7; /* index */
    gm_tcmkompbl09il gm_l8; /* u */
    void * gm_l9; /* a */
    unsigned __int128 gm_l10; /* raw */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* k */
    gm_tplpbqf3duvn3 gm_l15; /* bf */
    GM_STACK_CHECK(&F, 1452u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    {
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; default: goto ma7; }
ma3:;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t11 != ((uint8_t)2ULL)) goto mn10;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 16));
        gm_l4 = t12;
        gm_tdlro53ljvkl0 t13;
        uint8_t t15 = gm_l4;
        if (!t15) goto ie14;
        unsigned __int128 t17 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
        gm_tdlro53ljvkl0 t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 40, 0);
        ((uint8_t *)&t18)[0] = 0;
        GM_ST(unsigned __int128, ((uint8_t *)&t18 + 16), t17);
        t13 = t18;
        goto ix14;
ie14:;
        unsigned __int128 t20 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
        gm_tdlro53ljvkl0 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t21 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t21 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t21 + 40, 0);
        ((uint8_t *)&t21)[0] = 0;
        GM_ST(unsigned __int128, ((uint8_t *)&t21 + 16), t20);
        t13 = t21;
ix14:;
        uint8_t t22 = ((((uint8_t *)&t13))[0] != 0);
        F.ret = t13;
        goto gm_fret;
        goto me2;
mn10:;
ma4:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t24 != ((uint8_t)0ULL)) goto mn23;
        unsigned __int128 t25 = GM_LD(unsigned __int128, (((uint8_t *)&F.l2) + 16));
        gm_l5 = t25;
        {
            void * t27 = gm_l0;
            gm_t88rcucg1b7uk t28 = gm_l3;
            uint32_t t29 = gm_f_bit_width_0fd5f52((void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t29;
        }
        {
            gm_tdlro53ljvkl0 t30;
            uint32_t t32 = gm_l6;
            uint8_t t33 = (t32 >= ((uint32_t)128ULL));
            if (!t33) goto ie31;
            unsigned __int128 t35 = gm_l5;
            unsigned __int128 t36 = t35;
            gm_tdlro53ljvkl0 t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t37 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t37 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t37 + 40, 0);
            ((uint8_t *)&t37)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t37 + 16), t36);
            t30 = t37;
            goto ix31;
ie31:;
            unsigned __int128 t39 = gm_l5;
            uint32_t t40 = gm_l6;
            unsigned __int128 t41 = (unsigned __int128)t40;
            unsigned __int128 n42 = (unsigned __int128)t41;
            if (n42 >= 128) gm_trap_shift((uint64_t)n42, "u128", gm_src8sgunk80orlb, 175, 57);
            unsigned __int128 r43 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n42);
            bool o44 = (unsigned __int128)(r43 >> (int)n42) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
            if (o44) gm_trap("integer overflow in `<<`", gm_src8sgunk80orlb, 175, 57);
            unsigned __int128 t45 = r43;
            unsigned __int128 r46; bool o47 = __builtin_sub_overflow(t45, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r46);
            if (o47) gm_trap("integer overflow in `-`", gm_src8sgunk80orlb, 175, 56);
            unsigned __int128 t48 = r46;
            unsigned __int128 t49 = (unsigned __int128)(t39 & t48);
            unsigned __int128 t50 = t49;
            gm_tdlro53ljvkl0 t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t51 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t51 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t51 + 40, 0);
            ((uint8_t *)&t51)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t51 + 16), t50);
            t30 = t51;
ix31:;
            uint8_t t52 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t30;
            goto gm_fret;
        }
        goto me2;
mn23:;
ma5:;
        uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t54 != ((uint8_t)10ULL)) goto mn53;
        uint32_t t55 = GM_LD(uint32_t, ((((uint8_t *)&F.l2) + 16) + 0));
        gm_l7 = t55;
        {
            void * t57 = gm_l0;
            gm_t88rcucg1b7uk t58 = gm_l3;
            gm_t57euh612ligr t59;
            gm_f_union_def_h8mvf4t((void *)&t59, t57, GM_CONV(gm_csguankko4lmhd, t58));
            gm_tcmkompbl09il t60;
            if (((((uint8_t *)&t59))[24] != 0)) {
                t60 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t59) + 0));
            } else {
                void * t61 = gm_l0;
                gm_tl874t2atnlh3 t62 = gm_l1;
                gm_b16_8 t63;
                GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sfali8nl85hc9));
                GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(24));
                gm_t76123gfet73g t64;
                gm_f_unsupported_70vvk1e((void *)&t64, (void *)NULL, cx, NULL, t61, t62, t63);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 16, t64);
                goto gm_fdone;
            }
            gm_l8 = t60;
        }
        {
            void * t65 = gm_l0;
            gm_tl874t2atnlh3 t66 = gm_l1;
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_b16_8 t68 = t67;
            uint32_t t69 = gm_l7;
            uint64_t t70 = (uint64_t)t69;
            uint64_t t71 = gm_f_us_cuv01fp(t70);
            uint64_t t72 = (uintptr_t)t71;
            uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&t68) + 8);
            if (t72 >= (uintptr_t)(t73)) gm_trap_index(t72, t73, gm_src8sgunk80orlb, 179, 59);
            gm_tbci5ospk5nnn t74 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t68)) + t72 * 72) + 24));
            unsigned __int128 t75 = gm_f_bits128_jigkui4(&t74);
            gm_t6evimt84lndq t76;
            GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t76 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t76 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t76 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t76 + 16), t75);
            gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l8) + 0));
            gm_f_const_bits_1of8c6t((void *)&(*(gm_tdlro53ljvkl0 *)gm_out), (void *)NULL, cx, NULL, t65, t66, &t76, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me2;
mn53:;
ma6:;
        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t79 != ((uint8_t)3ULL)) goto mn78;
        void * t80 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
        gm_l9 = t80;
        {
            gm_l10 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
        }
        {
            {
                void * t83 = gm_l9;
                gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)t83) + 0));
                gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                gm_l11 = t85;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t87 = gm_l12;
                    uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t89 = (t87 < t88);
                    if (!t89) goto lb86;
                    {
                        gm_thf8as07kj3uj t92;
                        memset((uint8_t *)&t92 + 8, 0, 8);
                        uint64_t t93 = gm_l12;
                        GM_ST(uint64_t, (uint8_t *)&t92 + 0, t93);
                        uint64_t t94 = gm_l12;
                        uint64_t t95 = (uintptr_t)t94;
                        uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        memcpy((uint8_t *)&t92 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t95 * 48), 48);
                        uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&t92) + 0));
                        gm_l13 = t99;
                        F.l14 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t92) + 16));
                        {
                            void * t101 = gm_l0;
                            gm_t88rcucg1b7uk t102 = gm_l3;
                            uint64_t t103 = gm_l13;
                            uint32_t t104 = (uint32_t)t103;
                            gm_tplpbqf3duvn3 t105 = gm_f_bits_field_vqua3kd((void *)NULL, cx, NULL, t101, t102, t104);
                            if (gm_unw.kind) {
                                goto gm_ll90;
                            }
                            gm_l15 = t105;
                        }
                        {
                            unsigned __int128 t106 = gm_l10;
                            void * t107 = gm_l0;
                            gm_tl874t2atnlh3 t108 = gm_l1;
                            gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l15) + 4));
                            gm_tdlro53ljvkl0 t110;
                            gm_f_const_bits_1of8c6t((void *)&t110, (void *)NULL, cx, NULL, t107, t108, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l14)), t109);
                            if (gm_unw.kind) {
                                goto gm_ll90;
                            }
                            if (((((uint8_t *)&t110))[0] != 0)) {
                                gm_tdlro53ljvkl0 t111;
                                GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t111 + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&t111 + 40, 0);
                                ((uint8_t *)&t111)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t111 + 16), (*(gm_t76123gfet73g *)(((uint8_t *)&t110) + 16)));
                                F.ret = t111;
                                goto gm_fret;
                            }
                            unsigned __int128 t112 = GM_LD(unsigned __int128, (((uint8_t *)&t110) + 16));
                            uint32_t t113 = GM_LD(uint32_t, (((uint8_t *)&gm_l15) + 0));
                            unsigned __int128 t114 = (unsigned __int128)t113;
                            unsigned __int128 n115 = (unsigned __int128)t114;
                            if (n115 >= 128) gm_trap_shift((uint64_t)n115, "u128", gm_src8sgunk80orlb, 185, 24);
                            unsigned __int128 r116 = (unsigned __int128)((unsigned __int128)t112 << (int)n115);
                            bool o117 = (unsigned __int128)(r116 >> (int)n115) != t112;
                            if (o117) gm_trap("integer overflow in `<<`", gm_src8sgunk80orlb, 185, 24);
                            unsigned __int128 t118 = r116;
                            unsigned __int128 t119 = (unsigned __int128)(t106 | t118);
                            gm_l10 = t119;
                        }
                        goto me97;
mn98:;
                        gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 183, 13);
me97:;
                    }
lc86:;
                    {
                        uint64_t t121 = gm_l12;
                        uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                        uint64_t t124 = r122;
                        gm_l12 = t124;
                    }
                }
gm_ll90:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb86; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc86; }
                }
                goto gm_lad;
lb86:;
            }
        }
        {
            unsigned __int128 t125 = gm_l10;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out))[0] = 0;
            GM_ST(unsigned __int128, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 16, t125);
            goto gm_fdone;
        }
        goto me2;
mn78:;
ma7:;
        void * t127 = gm_l0;
        gm_tl874t2atnlh3 t128 = gm_l1;
        gm_b16_8 t129;
        GM_ST(void *, (uint8_t *)&t129, (void *)(gm_sfali8nl85hc9));
        GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(24));
        gm_t76123gfet73g t130;
        gm_f_unsupported_70vvk1e((void *)&t130, (void *)NULL, cx, NULL, t127, t128, t129);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 40, 0);
        ((uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 16, t130);
        goto gm_fdone;
        goto me2;
mn126:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 171, 5);
me2:;
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
__attribute__((aligned(4))) void gm_f_global_addr_9an4270(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2) {
    struct gm_fr_global_addr_9an4270 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint64_t gm_l2; /* decl */
    uint64_t gm_l3; /* i */
    gm_tn68el8d14vku gm_l5; /* key */
    uint64_t gm_l6; /* a */
    uint64_t gm_l7; /* al */
    uint64_t gm_l8; /* addr */
    void * gm_l9; /* cell */
    void * gm_l10; /* cell */
    gm_b16_8 gm_l11; /* b */
    GM_STACK_CHECK(&F, 1615u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint64_t t4 = gm_l2;
        gm_tf5srh4mp4b3i t5 = gm_f_global_index_nhjk80t(t3, t4);
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            void * t8 = gm_l0;
            gm_tl874t2atnlh3 t9 = gm_l1;
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sct0vr0h329vl));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(42));
            gm_t76123gfet73g t11;
            gm_f_trap_qh84l9d((void *)&t11, (void *)NULL, cx, NULL, t8, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t11);
            goto gm_fdone;
        }
        gm_l3 = t6;
    }
    {
        uint64_t t12 = gm_l3;
        uint64_t t13 = (uintptr_t)t12;
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 8));
        uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 1448) + 0) + 8);
        if (t13 >= (uintptr_t)(t16)) gm_trap_index(t13, t16, gm_src8sgunk80orlb, 31, 11);
        gm_tq7qjpkmqmc5s t17 = GM_LD(gm_tq7qjpkmqmc5s, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 1448) + 0)) + t13 * 104));
        F.l4 = t17;
    }
    {
        uint64_t t18 = gm_l3;
        uint32_t t19;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 48));
        if (!t21) goto ie20;
        void * t23 = gm_l0;
        uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)t23) + 1656) + 40));
        uint32_t t25 = t24;
        t19 = t25;
        goto ix20;
ie20:;
        uint32_t t27 = ((uint32_t)0ULL);
        t19 = t27;
ix20:;
        gm_tn68el8d14vku t28;
        GM_ST(uint32_t, (uint8_t *)&t28 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t28 + 0), t18);
        GM_ST(uint32_t, ((uint8_t *)&t28 + 8), t19);
        gm_l5 = t28;
    }
    {
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 48));
        if (!t30) goto ie29;
        {
            void * t33 = gm_l0;
            void * t34 = (void *)(((uint8_t *)t33) + 992);
            gm_tn68el8d14vku t35 = gm_l5;
            gm_tf5srh4mp4b3i t36 = gm_f_get_ftjmcbc(t34, t35);
            if (!((((uint8_t *)&t36))[8] != 0)) goto ie32;
            uint64_t t37 = GM_LD(uint64_t, ((uint8_t *)&t36));
            gm_l6 = t37;
            {
                uint64_t t39 = gm_l6;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t39);
                goto gm_fdone;
            }
            goto ix32;
ie32:;
ix32:;
        }
        goto ix29;
ie29:;
        uint64_t t41 = gm_l3;
        uint64_t t42 = (uintptr_t)t41;
        void * t43 = gm_l0;
        uint64_t t44 = GM_LD(uintptr_t, ((((uint8_t *)t43) + 952) + 0) + 8);
        if (t42 >= (uintptr_t)(t44)) gm_trap_index(t42, t44, gm_src8sgunk80orlb, 36, 15);
        uint64_t t45 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t43) + 952) + 0)) + t42 * 8));
        uint8_t t46 = (!(t45 == ((uint64_t)0ULL)));
        if (!t46) goto ie40;
        {
            uint64_t t48 = gm_l3;
            uint64_t t49 = (uintptr_t)t48;
            void * t50 = gm_l0;
            uint64_t t51 = GM_LD(uintptr_t, ((((uint8_t *)t50) + 952) + 0) + 8);
            if (t49 >= (uintptr_t)(t51)) gm_trap_index(t49, t51, gm_src8sgunk80orlb, 37, 16);
            uint64_t t52 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t50) + 952) + 0)) + t49 * 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t52);
            goto gm_fdone;
        }
        goto ix40;
ie40:;
ix40:;
ix29:;
    }
    {
        uint8_t t54;
        if ((((((uint8_t *)&F.l4) + 56))[16] != 0)) goto mn56;
        t54 = ((uint8_t)0);
        goto me55;
mn56:;
        t54 = ((uint8_t)1);
        goto me55;
mn57:;
        gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 39, 8);
me55:;
        if (!t54) goto ie53;
        {
            void * t59 = gm_l0;
            gm_tl874t2atnlh3 t60 = gm_l1;
            gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
            gm_tnpifv20irnph t62;
            GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t61);
            gm_tabb710d9mq2e t63;
            gm_f_format_3tqcmdm((void *)&t63, (void *)NULL, cx, NULL, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_src8sgunk80orlb, 39, 63);
            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
            gm_t76123gfet73g t66;
            gm_f_unsupported_70vvk1e((void *)&t66, (void *)NULL, cx, NULL, t59, t60, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t66);
            goto gm_fdone;
        }
        goto ix53;
ie53:;
ix53:;
    }
    {
        void * t67 = gm_l0;
        gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
        uint64_t t69 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t67, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tf5srh4mp4b3i t70 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l4) + 80));
        uint64_t t71;
        if (((((uint8_t *)&t70))[8] != 0)) {
            uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&t70) + 0));
            t71 = t72;
        } else {
            t71 = ((uint64_t)1ULL);
        }
        uint64_t t73 = (t69 > t71 ? t69 : t71);
        gm_l7 = t73;
    }
    {
        gm_l8 = ((uint64_t)0ULL);
    }
    {
        void * t75 = GM_LD(void *, (((uint8_t *)&F.l4) + 96));
        if (!(GM_LD(void *, ((uint8_t *)&t75)) != 0)) goto ie74;
        void * t76 = GM_LD(void *, ((uint8_t *)&t75));
        gm_l9 = t76;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 48));
        uint8_t t78 = (uint8_t)!t77;
        if (!t78) goto ie74;
        {
            void * t80 = gm_l0;
            gm_tl874t2atnlh3 t81 = gm_l1;
            void * t82 = gm_l9;
            gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
            uint64_t t84 = gm_l7;
            gm_t833u93mr4iob t85;
            gm_f_cell_addr_bl2jmen((void *)&t85, (void *)NULL, cx, NULL, t80, t81, t82, t83, t84, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t85))[0] != 0)) {
                gm_t833u93mr4iob t86;
                GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
                ((uint8_t *)&t86)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t86 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t85) + 8)));
                F.ret = t86;
                goto gm_fret;
            }
            uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&t85) + 8));
            gm_l8 = t87;
        }
        goto ix74;
ie74:;
        void * t89 = GM_LD(void *, (((uint8_t *)&F.l4) + 96));
        if (!(GM_LD(void *, ((uint8_t *)&t89)) != 0)) goto ie88;
        void * t90 = GM_LD(void *, ((uint8_t *)&t89));
        gm_l10 = t90;
        {
            void * t92 = gm_l0;
            gm_tl874t2atnlh3 t93 = gm_l1;
            void * t94 = gm_l0;
            gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
            uint64_t t96 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t94, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t97 = gm_l7;
            gm_t9qkh0rsooc8d t98;
            GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)5ULL));
            gm_t833u93mr4iob t99;
            gm_f_alloc_mem_0vu6kcs((void *)&t99, (void *)NULL, cx, NULL, t92, t93, t96, t97, t98, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t99))[0] != 0)) {
                gm_t833u93mr4iob t100;
                GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
                ((uint8_t *)&t100)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t100 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t99) + 8)));
                F.ret = t100;
                goto gm_fret;
            }
            uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&t99) + 8));
            gm_l8 = t101;
        }
        {
            void * t102 = gm_l0;
            gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
            uint64_t t104 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t102, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t105 = gm_f_us_cuv01fp(t104);
            gm_tabb710d9mq2e t106;
            gm_f_make_fill_jpvm0kc((void *)&t106, (void *)NULL, cx, NULL, t105, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_koedfqu36hd)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 8, gm_src8sgunk80orlb, 47, 14);
            gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&t106) + 8));
            gm_l11 = t107;
        }
        {
        }
        {
            void * t111 = gm_l0;
            gm_tl874t2atnlh3 t112 = gm_l1;
            void * t113 = gm_l10;
            gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
            gm_b16_8 t115 = gm_l11;
            gm_tcfa6l7reoosn t116;
            gm_f_value_bytes_l7s126n((void *)&t116, (void *)NULL, cx, NULL, t111, t112, &(*(gm_t6evimt84lndq *)(((uint8_t *)t113) + 0)), t114, t115, ((uint64_t)0ULL));
            if (gm_unw.kind) {
                goto gm_ld91_1;
            }
            if (((((uint8_t *)&t116))[0] != 0)) {
                gm_t833u93mr4iob t117;
                GM_ST(uint8_t, (uint8_t *)&t117 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t117 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t117 + 4, 0);
                ((uint8_t *)&t117)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t117 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t116) + 8)));
                F.ret = t117;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld91_1;
            }
            gm_b0_1 t118 = GM_LD(gm_b0_1, (((uint8_t *)&t116) + 8));
        }
        {
            {
                void * t120 = gm_l0;
                void * t121 = (void *)(((uint8_t *)t120) + 184);
                uint64_t t122 = gm_l8;
                gm_b16_8 t123 = gm_l11;
                gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
                gm_tgfdnufetdnfc t125;
                gm_f_init_9fuf99v((void *)&t125, t121, t122, t124);
            }
        }
        {
            gm_b16_8 t126 = gm_l11;
            gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
            gm_b0_1 t128 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t127, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjqpf95jhcm)));
        }
        goto gm_ls129;
gm_ld91_1:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            gm_b16_8 t130 = gm_l11;
            gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&t130));
            gm_b0_1 t132 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t131, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjqpf95jhcm)));
            gm_unw = gm_saved;
        }
        goto gm_lad;
gm_ls129:;
        goto ix88;
ie88:;
        {
            void * t134 = gm_l0;
            gm_tl874t2atnlh3 t135 = gm_l1;
            void * t136 = gm_l0;
            gm_t88rcucg1b7uk t137 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
            uint64_t t138 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t136, t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t139 = gm_l7;
            gm_t9qkh0rsooc8d t140;
            GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)5ULL));
            gm_t833u93mr4iob t141;
            gm_f_alloc_mem_0vu6kcs((void *)&t141, (void *)NULL, cx, NULL, t134, t135, t138, t139, t140, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t141))[0] != 0)) {
                gm_t833u93mr4iob t142;
                GM_ST(uint8_t, (uint8_t *)&t142 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t142 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t142 + 4, 0);
                ((uint8_t *)&t142)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t142 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t141) + 8)));
                F.ret = t142;
                goto gm_fret;
            }
            uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&t141) + 8));
            gm_l8 = t143;
        }
ix88:;
ix74:;
    }
    {
        uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 48));
        if (!t145) goto ie144;
        {
            void * t147 = gm_l0;
            void * t148 = (void *)(((uint8_t *)t147) + 992);
            gm_tn68el8d14vku t149 = gm_l5;
            uint64_t t150 = gm_l8;
            gm_tftb1odc739t5 t151 = gm_f_put_f99phma(t148, t149, t150);
            if (((((uint8_t *)&t151))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t151) + 1, gm_src8sgunk80orlb, 54, 26);
            gm_b0_1 t152 = GM_LD(gm_b0_1, (((uint8_t *)&t151) + 1));
        }
        goto ix144;
ie144:;
        {
            uint64_t t154 = gm_l3;
            uint64_t t155 = (uintptr_t)t154;
            void * t156 = gm_l0;
            uint64_t t157 = GM_LD(uintptr_t, ((((uint8_t *)t156) + 952) + 0) + 8);
            if (t155 >= (uintptr_t)(t157)) gm_trap_index(t155, t157, gm_src8sgunk80orlb, 54, 68);
            uint64_t t158 = gm_l8;
            GM_ST(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t156) + 952) + 0)) + t155 * 8), t158);
        }
ix144:;
    }
    {
        uint64_t t159 = gm_l8;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t159);
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
__attribute__((aligned(4))) void gm_f_place_addr_m751en7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_place_addr_m751en7 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tkaapcii86udk gm_l2; /* pl */
    gm_t88rcucg1b7uk gm_l3; /* pointee */
    gm_t88rcucg1b7uk gm_l4; /* root */
    gm_tlcm4lvu3g1a8 gm_l5; /* known */
    void * gm_l6; /* a */
    uint64_t gm_l7; /* base */
    uint64_t gm_l8; /* addend */
    gm_t88rcucg1b7uk gm_l9; /* cur */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    uint64_t gm_l13; /* len */
    gm_t88rcucg1b7uk gm_l14; /* elem */
    gm_b16_8 gm_l15; /* elems */
    uint32_t gm_l16; /* id */
    GM_STACK_CHECK(&F, 1328u);
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
        void * t5 = (void *)(((uint8_t *)t4) + 896);
        void * t6 = GM_LD(void *, (((uint8_t *)a2) + 0));
        uint64_t t7 = (uintptr_t)t6;
        gm_tb7u19loo3j87 t8;
        gm_f_get_srnes22((void *)&t8, t5, t7);
        if (!((((uint8_t *)&t8))[16] != 0)) goto ie3;
        gm_tlcm4lvu3g1a8 t9 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t8));
        gm_l5 = t9;
        {
            gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
            gm_l4 = t11;
        }
        goto ix3;
ie3:;
        uint64_t t13 = GM_LD(uintptr_t, (((uint8_t *)a2) + 8) + 8);
        uint8_t t14 = ((t13 == ((uint64_t)1ULL)));
        if (!t14) goto ie12;
        void * t15 = GM_LD(void *, (((uint8_t *)a2) + 0));
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t15) + 0) + 0));
        if (t16 != ((uint8_t)3ULL)) goto ie12;
        void * t17 = GM_LD(void *, ((((uint8_t *)t15) + 0) + 16));
        gm_l6 = t17;
        {
            void * t19 = gm_l0;
            void * t20 = GM_LD(void *, (((uint8_t *)t19) + 8));
            void * t21 = (void *)(((uint8_t *)t20) + 8);
            void * t22 = gm_l6;
            uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)t22) + 0) + 8);
            uint64_t t24 = (uint64_t)t23;
            gm_t88rcucg1b7uk t25 = gm_l3;
            gm_t88rcucg1b7uk t26 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t21, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t26;
        }
        goto ix12;
ie12:;
        uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)a2) + 8) + 8);
        uint8_t t29 = (t28 > ((uint64_t)0ULL));
        if (!t29) goto ie27;
        {
            void * t31 = gm_l0;
            gm_tl874t2atnlh3 t32 = gm_l1;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sqrl3kgomorsv));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(38));
            gm_t76123gfet73g t34;
            gm_f_unsupported_70vvk1e((void *)&t34, (void *)NULL, cx, NULL, t31, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t34);
            goto gm_fdone;
        }
        goto ix27;
ie27:;
ix27:;
ix12:;
ix3:;
    }
    {
        void * t35 = gm_l0;
        gm_tl874t2atnlh3 t36 = gm_l1;
        void * t37 = GM_LD(void *, (((uint8_t *)a2) + 0));
        gm_t88rcucg1b7uk t38 = gm_l4;
        void * t39 = gm_l0;
        gm_t88rcucg1b7uk t40 = gm_l4;
        uint64_t t41 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t39, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t42 = GM_LD(void *, (((uint8_t *)a2) + 0));
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)t42) + 48));
        uint8_t t44 = (uint8_t)!t43;
        gm_t833u93mr4iob t45;
        gm_f_cell_addr_bl2jmen((void *)&t45, (void *)NULL, cx, NULL, t35, t36, t37, t38, t41, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t45))[0] != 0)) {
            gm_t833u93mr4iob t46;
            GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
            ((uint8_t *)&t46)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t46 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t45) + 8)));
            F.ret = t46;
            goto gm_fret;
        }
        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&t45) + 8));
        gm_l7 = t47;
    }
    {
        gm_l8 = ((uint64_t)0ULL);
    }
    {
        gm_t88rcucg1b7uk t48 = gm_l4;
        gm_l9 = t48;
    }
    {
        {
            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
            gm_l10 = t50;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t52 = gm_l11;
                uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t54 = (t52 < t53);
                if (!t54) goto lb51;
                {
                    uint64_t t57 = gm_l11;
                    uint64_t t58 = (uintptr_t)t57;
                    uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint64_t t60 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t58 * 8));
                    uint64_t t63 = GM_LD(uint64_t, ((uint8_t *)&t60));
                    gm_l12 = t63;
                    {
                        void * t65 = gm_l0;
                        gm_t88rcucg1b7uk t66 = gm_l9;
                        gm_t53at80pfdus3 t67;
                        gm_f_kind_htv6093((void *)&t67, t65, GM_CONV(gm_csguankko4lmhd, t66));
                        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t67) + 0));
                        switch (t74) { case ((uint8_t)0ULL): goto ma72; case ((uint8_t)1ULL): goto ma72; case ((uint8_t)2ULL): goto ma72; case ((uint8_t)3ULL): goto ma72; case ((uint8_t)4ULL): goto ma72; case ((uint8_t)5ULL): goto ma72; case ((uint8_t)6ULL): goto ma72; case ((uint8_t)7ULL): goto ma72; case ((uint8_t)8ULL): goto ma72; case ((uint8_t)9ULL): goto ma72; case ((uint8_t)10ULL): goto ma72; case ((uint8_t)11ULL): goto ma69; case ((uint8_t)12ULL): goto ma70; case ((uint8_t)13ULL): goto ma72; case ((uint8_t)14ULL): goto ma72; case ((uint8_t)15ULL): goto ma72; case ((uint8_t)16ULL): goto ma72; case ((uint8_t)17ULL): goto ma71; case ((uint8_t)18ULL): goto ma72; case ((uint8_t)19ULL): goto ma72; case ((uint8_t)20ULL): goto ma72; case ((uint8_t)21ULL): goto ma72; case ((uint8_t)22ULL): goto ma72; case ((uint8_t)23ULL): goto ma72; case ((uint8_t)24ULL): goto ma72; case ((uint8_t)25ULL): goto ma72; case ((uint8_t)26ULL): goto ma72; default: goto ma72; }
ma69:;
                        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&t67) + 0));
                        if (t76 != ((uint8_t)11ULL)) goto mn75;
                        uint64_t t77 = GM_LD(uint64_t, ((((uint8_t *)&t67) + 8) + 0));
                        gm_l13 = t77;
                        gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t67) + 8) + 8));
                        gm_l14 = t78;
                        {
                            uint64_t t80 = gm_l8;
                            uint64_t t81 = gm_l12;
                            void * t82 = gm_l0;
                            gm_t88rcucg1b7uk t83 = gm_l14;
                            uint64_t t84 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t82, t83);
                            if (gm_unw.kind) {
                                goto gm_ll55;
                            }
                            uint64_t r85; bool o86 = __builtin_mul_overflow(t81, t84, &r85);
                            if (o86) gm_trap("integer overflow in `*`", gm_src8sgunk80orlb, 151, 27);
                            uint64_t t87 = r85;
                            uint64_t r88; bool o89 = __builtin_add_overflow(t80, t87, &r88);
                            if (o89) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 151, 17);
                            uint64_t t90 = r88;
                            gm_l8 = t90;
                        }
                        {
                            uint64_t t92 = gm_l12;
                            uint64_t t93 = gm_l13;
                            uint8_t t94 = (t92 < t93);
                            if (!t94) goto ie91;
                            {
                                gm_t88rcucg1b7uk t96 = gm_l14;
                                gm_l9 = t96;
                            }
                            goto ix91;
ie91:;
ix91:;
                        }
                        goto me68;
mn75:;
ma70:;
                        uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&t67) + 0));
                        if (t98 != ((uint8_t)12ULL)) goto mn97;
                        gm_b16_8 t99 = GM_LD(gm_b16_8, ((((uint8_t *)&t67) + 8) + 0));
                        gm_l15 = t99;
                        {
                            uint64_t t101 = gm_l8;
                            void * t102 = gm_l0;
                            gm_t88rcucg1b7uk t103 = gm_l9;
                            gm_b16_8 t104 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t102, t103);
                            if (gm_unw.kind) {
                                goto gm_ll55;
                            }
                            gm_b16_8 t105 = t104;
                            uint64_t t106 = gm_l12;
                            uint64_t t107 = gm_f_us_cuv01fp(t106);
                            uint64_t t108 = (uintptr_t)t107;
                            uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&t105) + 8);
                            if (t108 >= (uintptr_t)(t109)) gm_trap_index(t108, t109, gm_src8sgunk80orlb, 155, 27);
                            uint64_t t110 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t105)) + t108 * 8));
                            uint64_t r111; bool o112 = __builtin_add_overflow(t101, t110, &r111);
                            if (o112) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 155, 17);
                            uint64_t t113 = r111;
                            gm_l8 = t113;
                        }
                        {
                            gm_b16_8 t114 = gm_l15;
                            gm_b16_8 t115 = t114;
                            uint64_t t116 = gm_l12;
                            uint64_t t117 = gm_f_us_cuv01fp(t116);
                            uint64_t t118 = (uintptr_t)t117;
                            uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&t115) + 8);
                            if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_src8sgunk80orlb, 156, 23);
                            gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t115)) + t118 * 4));
                            gm_l9 = t120;
                        }
                        goto me68;
mn97:;
ma71:;
                        uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&t67) + 0));
                        if (t122 != ((uint8_t)17ULL)) goto mn121;
                        uint32_t t123 = GM_LD(uint32_t, ((((uint8_t *)&t67) + 8) + 0));
                        gm_l16 = t123;
                        {
                            void * t126 = gm_l0;
                            void * t127 = GM_LD(void *, (((uint8_t *)t126) + 8));
                            gm_b16_8 t128 = GM_LD(gm_b16_8, ((((uint8_t *)t127) + 1088) + 0));
                            gm_b16_8 t129 = t128;
                            uint32_t t130 = gm_l16;
                            uint64_t t131 = (uint64_t)t130;
                            uint64_t t132 = gm_f_us_cuv01fp(t131);
                            uint64_t t133 = (uintptr_t)t132;
                            uint64_t t134 = GM_LD(uintptr_t, ((uint8_t *)&t129) + 8);
                            if (t133 >= (uintptr_t)(t134)) gm_trap_index(t133, t134, gm_src8sgunk80orlb, 159, 32);
                            uint8_t t135 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t129)) + t133 * 184) + 24) + 0));
                            if (t135 != ((uint8_t)0ULL)) goto ie125;
                            gm_tpautr4br7h3l t136 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t129)) + t133 * 184) + 24) + 8));
                            F.l17 = t136;
                            {
                                uint64_t t138 = gm_l8;
                                void * t139 = gm_l0;
                                gm_t88rcucg1b7uk t140 = gm_l9;
                                gm_b16_8 t141 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t139, t140);
                                if (gm_unw.kind) {
                                    goto gm_ll55;
                                }
                                gm_b16_8 t142 = t141;
                                uint64_t t143 = gm_l12;
                                uint64_t t144 = gm_f_us_cuv01fp(t143);
                                uint64_t t145 = (uintptr_t)t144;
                                uint64_t t146 = GM_LD(uintptr_t, ((uint8_t *)&t142) + 8);
                                if (t145 >= (uintptr_t)(t146)) gm_trap_index(t145, t146, gm_src8sgunk80orlb, 160, 27);
                                uint64_t t147 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t142)) + t145 * 8));
                                uint64_t r148; bool o149 = __builtin_add_overflow(t138, t147, &r148);
                                if (o149) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 160, 17);
                                uint64_t t150 = r148;
                                gm_l8 = t150;
                            }
                            {
                                gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                                gm_b16_8 t152 = t151;
                                uint64_t t153 = gm_l12;
                                uint64_t t154 = gm_f_us_cuv01fp(t153);
                                uint64_t t155 = (uintptr_t)t154;
                                uint64_t t156 = GM_LD(uintptr_t, ((uint8_t *)&t152) + 8);
                                if (t155 >= (uintptr_t)(t156)) gm_trap_index(t155, t156, gm_src8sgunk80orlb, 161, 23);
                                gm_t88rcucg1b7uk t157 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t152)) + t155 * 40) + 16));
                                gm_l9 = t157;
                            }
                            goto ix125;
ie125:;
                            {
                                void * t159 = gm_l0;
                                gm_tl874t2atnlh3 t160 = gm_l1;
                                gm_b16_8 t161;
                                GM_ST(void *, (uint8_t *)&t161, (void *)(gm_sqrl3kgomorsv));
                                GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(38));
                                gm_t76123gfet73g t162;
                                gm_f_unsupported_70vvk1e((void *)&t162, (void *)NULL, cx, NULL, t159, t160, t161);
                                if (gm_unw.kind) {
                                    goto gm_ll55;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t162);
                                goto gm_fdone;
                            }
ix125:;
                        }
                        goto me68;
mn121:;
ma72:;
                        void * t164 = gm_l0;
                        gm_tl874t2atnlh3 t165 = gm_l1;
                        gm_b16_8 t166;
                        GM_ST(void *, (uint8_t *)&t166, (void *)(gm_sqrl3kgomorsv));
                        GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(38));
                        gm_t76123gfet73g t167;
                        gm_f_unsupported_70vvk1e((void *)&t167, (void *)NULL, cx, NULL, t164, t165, t166);
                        if (gm_unw.kind) {
                            goto gm_ll55;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t167);
                        goto gm_fdone;
                        goto me68;
mn163:;
ma73:;
                        gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 149, 9);
me68:;
                    }
                    goto me61;
mn62:;
                    gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 148, 5);
me61:;
                }
lc51:;
                {
                    uint64_t t169 = gm_l11;
                    uint64_t r170; bool o171 = __builtin_add_overflow(t169, ((uint64_t)1ULL), &r170);
                    uint64_t t172 = r170;
                    gm_l11 = t172;
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
        uint64_t t173 = gm_l7;
        uint64_t t174 = gm_l8;
        uint64_t r175; bool o176 = __builtin_add_overflow(t173, t174, &r175);
        if (o176) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 166, 12);
        uint64_t t177 = r175;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t177);
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
__attribute__((aligned(4))) void gm_f_value_bytes_l7s126n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, uint64_t a5) {
    struct gm_fr_value_bytes_l7s126n F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* out */
    uint64_t gm_l5; /* off */
    uint64_t gm_l6; /* o */
    unsigned __int128 gm_l7; /* bits */
    double gm_l8; /* x */
    uint8_t gm_l9; /* b */
    void * gm_l10; /* x */
    gm_t88rcucg1b7uk gm_l11; /* inner */
    void * gm_l12; /* x */
    void * gm_l13; /* x */
    gm_b16_8 gm_l14; /* offs */
    uint32_t gm_l15; /* index */
    void * gm_l16; /* payload */
    gm_tcmkompbl09il gm_l17; /* u */
    uint64_t gm_l19; /* n */
    void * gm_l20; /* x */
    gm_t88rcucg1b7uk gm_l21; /* pt */
    void * gm_l22; /* a */
    gm_t88rcucg1b7uk gm_l23; /* elem */
    uint64_t gm_l24; /* esz */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* k */
    gm_b16_8 gm_l29; /* elems */
    gm_b16_8 gm_l30; /* offs */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* k */
    uint32_t gm_l35; /* id */
    gm_t88rcucg1b7uk gm_l37; /* u */
    unsigned __int128 gm_l38; /* raw */
    gm_b16_8 gm_l39; /* offs */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* k */
    gm_tkaapcii86udk gm_l44; /* pl */
    gm_t88rcucg1b7uk gm_l45; /* p */
    gm_t88rcucg1b7uk gm_l46; /* p */
    gm_t88rcucg1b7uk gm_l47; /* inner */
    gm_t88rcucg1b7uk gm_l48; /* p */
    gm_t88rcucg1b7uk gm_l49; /* p */
    gm_t88rcucg1b7uk gm_l50; /* pointee */
    gm_tkaapcii86udk gm_l51; /* start */
    uint64_t gm_l52; /* len */
    gm_t88rcucg1b7uk gm_l53; /* elem */
    uint64_t gm_l54; /* p */
    uint64_t gm_l55; /* addr */
    uint32_t gm_l56; /* f */
    GM_STACK_CHECK(&F, 5125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        {
            uint64_t t3 = gm_l5;
            uint64_t t4 = gm_f_us_cuv01fp(t3);
            gm_l6 = t4;
        }
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            switch (t20) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma15; case ((uint8_t)5ULL): goto ma16; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma17; case ((uint8_t)12ULL): goto ma18; case ((uint8_t)13ULL): goto ma18; default: goto ma19; }
ma6:;
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t22 != ((uint8_t)0ULL)) goto mn21;
            unsigned __int128 t23 = GM_LD(unsigned __int128, (((uint8_t *)&F.l2) + 16));
            gm_l7 = t23;
            void * t24 = gm_l0;
            gm_b16_8 t25 = gm_l4;
            uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&t25) + 8);
            void * t27 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t25)));
            uint64_t t28 = gm_l6;
            uint64_t t29 = (uintptr_t)t28;
            uint64_t t30 = gm_l6;
            void * t31 = gm_l0;
            gm_t88rcucg1b7uk t32 = gm_l3;
            uint64_t t33 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t34 = gm_f_us_cuv01fp(t33);
            uint64_t r35; bool o36 = __builtin_add_overflow(t30, t34, &r35);
            if (o36) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 62, 41);
            uint64_t t37 = r35;
            uint64_t t38 = (uintptr_t)t37;
            if (t29 > t38 || t38 > (uintptr_t)t26) gm_trap_slice(t29, t38, t26, gm_src8sgunk80orlb, 62, 34);
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t27 + t29 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(t38 - t29));
            unsigned __int128 t40 = gm_l7;
            gm_f_put_uint_cddin99(t24, t39, t40);
            goto me5;
mn21:;
ma7:;
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t42 != ((uint8_t)1ULL)) goto mn41;
            double t43 = GM_LD(double, (((uint8_t *)&F.l2) + 16));
            gm_l8 = t43;
            void * t44 = gm_l0;
            gm_t88rcucg1b7uk t45 = gm_l3;
            double t46 = gm_l8;
            gm_b16_8 t47 = gm_l4;
            uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&t47) + 8);
            void * t49 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t47)));
            uint64_t t50 = gm_l6;
            uint64_t t51 = (uintptr_t)t50;
            uint64_t t52 = gm_l6;
            void * t53 = gm_l0;
            gm_t88rcucg1b7uk t54 = gm_l3;
            uint64_t t55 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t56 = gm_f_us_cuv01fp(t55);
            uint64_t r57; bool o58 = __builtin_add_overflow(t52, t56, &r57);
            if (o58) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 63, 47);
            uint64_t t59 = r57;
            uint64_t t60 = (uintptr_t)t59;
            if (t51 > t60 || t60 > (uintptr_t)t48) gm_trap_slice(t51, t60, t48, gm_src8sgunk80orlb, 63, 40);
            gm_b16_8 t61;
            GM_ST(void *, (uint8_t *)&t61, (void *)(((uint8_t *)t49 + t51 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(t60 - t51));
            gm_f_put_float_fiad72d(t44, GM_CONV(gm_csguankko4lmhd, t45), t46, t61);
            goto me5;
mn41:;
ma8:;
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t63 != ((uint8_t)2ULL)) goto mn62;
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 16));
            gm_l9 = t64;
            {
                uint64_t t66 = gm_l6;
                uint64_t t67 = (uintptr_t)t66;
                uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_src8sgunk80orlb, 64, 23);
                uint8_t *t69 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t67 * 1);
                uint8_t t70;
                uint8_t t72 = gm_l9;
                if (!t72) goto ie71;
                uint8_t t74 = ((uint8_t)1ULL);
                t70 = t74;
                goto ix71;
ie71:;
                uint8_t t76 = ((uint8_t)0ULL);
                t70 = t76;
ix71:;
                GM_ST(uint8_t, t69, t70);
            }
            goto me5;
mn62:;
ma9:;
            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t78 != ((uint8_t)6ULL)) goto mn77;
            goto me5;
mn77:;
ma10:;
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t81 != ((uint8_t)7ULL)) goto mn80;
            void * t82 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l10 = t82;
            {
                void * t84 = gm_l0;
                gm_t88rcucg1b7uk t85 = gm_l3;
                gm_t88rcucg1b7uk t86 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t84, GM_CONV(gm_csguankko4lmhd, t85)));
                gm_l11 = t86;
            }
            {
                void * t87 = gm_l0;
                gm_tl874t2atnlh3 t88 = gm_l1;
                void * t89 = gm_l10;
                gm_t88rcucg1b7uk t90 = gm_l11;
                gm_b16_8 t91 = gm_l4;
                uint64_t t92 = gm_l5;
                gm_tcfa6l7reoosn t93;
                gm_f_value_bytes_l7s126n((void *)&t93, (void *)NULL, cx, NULL, t87, t88, &(*(gm_t6evimt84lndq *)((uint8_t *)t89)), t90, t91, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t93))[0] != 0)) {
                    gm_tcfa6l7reoosn t94;
                    GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
                    ((uint8_t *)&t94)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t94 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t93) + 8)));
                    F.ret = t94;
                    goto gm_fret;
                }
                gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 8));
            }
            {
                void * t97 = gm_l0;
                void * t98 = GM_LD(void *, (((uint8_t *)t97) + 8));
                gm_t88rcucg1b7uk t99 = gm_l11;
                uint8_t t100 = gm_f_nullable_pointer_7qfmn6o(t98, GM_CONV(gm_csguankko4lmhd, t99));
                uint8_t t101 = (uint8_t)!t100;
                if (!t101) goto ie96;
                {
                    gm_b16_8 t103 = gm_l4;
                    gm_b16_8 t104 = t103;
                    uint64_t t105 = gm_l5;
                    uint64_t t106 = (uintptr_t)((uint64_t)1ULL);
                    void * t107 = gm_l0;
                    gm_t88rcucg1b7uk t108 = gm_l3;
                    gm_b16_8 t109 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t107, t108);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&t109) + 8);
                    if (t106 >= (uintptr_t)(t110)) gm_trap_index(t106, t110, gm_src8sgunk80orlb, 69, 69);
                    uint64_t t111 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t109)) + t106 * 8));
                    uint64_t r112; bool o113 = __builtin_add_overflow(t105, t111, &r112);
                    if (o113) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 69, 63);
                    uint64_t t114 = r112;
                    uint64_t t115 = gm_f_us_cuv01fp(t114);
                    uint64_t t116 = (uintptr_t)t115;
                    uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&t104) + 8);
                    if (t116 >= (uintptr_t)(t117)) gm_trap_index(t116, t117, gm_src8sgunk80orlb, 69, 56);
                    GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t104)) + t116 * 1), ((uint8_t)1ULL));
                }
                goto ix96;
ie96:;
ix96:;
            }
            goto me5;
mn80:;
ma11:;
            uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t119 != ((uint8_t)8ULL)) goto mn118;
            void * t120 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l12 = t120;
            void * t121 = gm_l0;
            gm_tl874t2atnlh3 t122 = gm_l1;
            void * t123 = gm_l12;
            gm_t6evimt84lndq t124 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t123));
            void * t125 = gm_l0;
            gm_t88rcucg1b7uk t126 = gm_l3;
            gm_tgglga8ro3isc t127 = gm_f_res_types_pg7rvck(t125, GM_CONV(gm_csguankko4lmhd, t126));
            gm_t88rcucg1b7uk t128 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t127) + 4));
            gm_b16_8 t129 = gm_l4;
            uint64_t t130 = gm_l5;
            uint64_t t131 = (uintptr_t)((uint64_t)1ULL);
            void * t132 = gm_l0;
            gm_t88rcucg1b7uk t133 = gm_l3;
            gm_b16_8 t134 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t132, t133);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t135 = GM_LD(uintptr_t, ((uint8_t *)&t134) + 8);
            if (t131 >= (uintptr_t)(t135)) gm_trap_index(t131, t135, gm_src8sgunk80orlb, 71, 77);
            uint64_t t136 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t134)) + t131 * 8));
            uint64_t r137; bool o138 = __builtin_add_overflow(t130, t136, &r137);
            if (o138) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 71, 71);
            uint64_t t139 = r137;
            gm_tcfa6l7reoosn t140;
            gm_f_value_bytes_l7s126n((void *)&t140, (void *)NULL, cx, NULL, t121, t122, &t124, t128, t129, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t140))[0] != 0)) {
                gm_tcfa6l7reoosn t141;
                GM_ST(uint8_t, (uint8_t *)&t141 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t141 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t141 + 4, 0);
                ((uint8_t *)&t141)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t141 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t140) + 8)));
                F.ret = t141;
                goto gm_fret;
            }
            gm_b0_1 t142 = GM_LD(gm_b0_1, (((uint8_t *)&t140) + 8));
            goto me5;
mn118:;
ma12:;
            uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t144 != ((uint8_t)9ULL)) goto mn143;
            void * t145 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l13 = t145;
            {
                void * t147 = gm_l0;
                gm_t88rcucg1b7uk t148 = gm_l3;
                gm_b16_8 t149 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t147, t148);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t149;
            }
            {
                gm_b16_8 t150 = gm_l4;
                gm_b16_8 t151 = t150;
                uint64_t t152 = gm_l5;
                uint64_t t153 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t154 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                if (t153 >= (uintptr_t)(t154)) gm_trap_index(t153, t154, gm_src8sgunk80orlb, 74, 26);
                uint64_t t155 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t153 * 8));
                uint64_t r156; bool o157 = __builtin_add_overflow(t152, t155, &r156);
                if (o157) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 74, 20);
                uint64_t t158 = r156;
                uint64_t t159 = gm_f_us_cuv01fp(t158);
                uint64_t t160 = (uintptr_t)t159;
                uint64_t t161 = GM_LD(uintptr_t, ((uint8_t *)&t151) + 8);
                if (t160 >= (uintptr_t)(t161)) gm_trap_index(t160, t161, gm_src8sgunk80orlb, 74, 13);
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t151)) + t160 * 1), ((uint8_t)1ULL));
            }
            {
                void * t162 = gm_l0;
                gm_tl874t2atnlh3 t163 = gm_l1;
                void * t164 = gm_l13;
                gm_t6evimt84lndq t165 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t164));
                void * t166 = gm_l0;
                gm_t88rcucg1b7uk t167 = gm_l3;
                gm_tgglga8ro3isc t168 = gm_f_res_types_pg7rvck(t166, GM_CONV(gm_csguankko4lmhd, t167));
                gm_t88rcucg1b7uk t169 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t168) + 0));
                gm_b16_8 t170 = gm_l4;
                uint64_t t171 = gm_l5;
                uint64_t t172 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t173 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                if (t172 >= (uintptr_t)(t173)) gm_trap_index(t172, t173, gm_src8sgunk80orlb, 75, 72);
                uint64_t t174 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t172 * 8));
                uint64_t r175; bool o176 = __builtin_add_overflow(t171, t174, &r175);
                if (o176) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 75, 66);
                uint64_t t177 = r175;
                gm_tcfa6l7reoosn t178;
                gm_f_value_bytes_l7s126n((void *)&t178, (void *)NULL, cx, NULL, t162, t163, &t165, t169, t170, t177);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t178))[0] != 0)) {
                    gm_tcfa6l7reoosn t179;
                    GM_ST(uint8_t, (uint8_t *)&t179 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t179 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t179 + 4, 0);
                    ((uint8_t *)&t179)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t179 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t178) + 8)));
                    F.ret = t179;
                    goto gm_fret;
                }
                gm_b0_1 t180 = GM_LD(gm_b0_1, (((uint8_t *)&t178) + 8));
            }
            goto me5;
mn143:;
ma13:;
            uint8_t t182 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t182 != ((uint8_t)10ULL)) goto mn181;
            uint32_t t183 = GM_LD(uint32_t, ((((uint8_t *)&F.l2) + 16) + 0));
            gm_l15 = t183;
            void * t184 = GM_LD(void *, ((((uint8_t *)&F.l2) + 16) + 8));
            gm_l16 = t184;
            {
                void * t186 = gm_l0;
                gm_t88rcucg1b7uk t187 = gm_l3;
                gm_t57euh612ligr t188;
                gm_f_union_def_h8mvf4t((void *)&t188, t186, GM_CONV(gm_csguankko4lmhd, t187));
                gm_tcmkompbl09il t189;
                if (((((uint8_t *)&t188))[24] != 0)) {
                    t189 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t188) + 0));
                } else {
                    void * t190 = gm_l0;
                    gm_tl874t2atnlh3 t191 = gm_l1;
                    gm_b16_8 t192;
                    GM_ST(void *, (uint8_t *)&t192, (void *)(gm_sa6mes8ijqmmk));
                    GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(29));
                    gm_t76123gfet73g t193;
                    gm_f_unsupported_70vvk1e((void *)&t193, (void *)NULL, cx, NULL, t190, t191, t192);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t193);
                    goto gm_fdone;
                }
                gm_l17 = t189;
            }
            {
                gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                gm_b16_8 t195 = t194;
                uint32_t t196 = gm_l15;
                uint64_t t197 = (uint64_t)t196;
                uint64_t t198 = gm_f_us_cuv01fp(t197);
                uint64_t t199 = (uintptr_t)t198;
                uint64_t t200 = GM_LD(uintptr_t, ((uint8_t *)&t195) + 8);
                if (t199 >= (uintptr_t)(t200)) gm_trap_index(t199, t200, gm_src8sgunk80orlb, 79, 19);
                gm_tckdobunaulee t201 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t195)) + t199 * 72));
                F.l18 = t201;
            }
            {
                void * t202 = gm_l0;
                gm_t88rcucg1b7uk t203 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 0));
                uint64_t t204 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t202, t203);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t205 = gm_f_us_cuv01fp(t204);
                gm_l19 = t205;
            }
            {
                void * t206 = gm_l0;
                uint64_t t207 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                void * t208 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                uint64_t t209 = gm_l6;
                uint64_t t210 = (uintptr_t)t209;
                uint64_t t211 = gm_l6;
                uint64_t t212 = gm_l19;
                uint64_t r213; bool o214 = __builtin_add_overflow(t211, t212, &r213);
                if (o214) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 81, 31);
                uint64_t t215 = r213;
                uint64_t t216 = (uintptr_t)t215;
                if (t210 > t216 || t216 > (uintptr_t)t207) gm_trap_slice(t210, t216, t207, gm_src8sgunk80orlb, 81, 24);
                gm_b16_8 t217;
                GM_ST(void *, (uint8_t *)&t217, (void *)(((uint8_t *)t208 + t210 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(t216 - t210));
                unsigned __int128 t218 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l18) + 24)));
                gm_f_put_uint_cddin99(t206, t217, t218);
            }
            {
                void * t220 = gm_l16;
                if (!(GM_LD(void *, ((uint8_t *)&t220)) != 0)) goto ie219;
                void * t221 = GM_LD(void *, ((uint8_t *)&t220));
                gm_l20 = t221;
                {
                    gm_tkn42u7na3t81 t223 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l18) + 16));
                    gm_t88rcucg1b7uk t224;
                    if (((((uint8_t *)&t223))[4] != 0)) {
                        gm_t88rcucg1b7uk t225 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t223) + 0));
                        t224 = t225;
                    } else {
                        gm_b0_1 t226;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t226);
                        goto gm_fdone;
                    }
                    gm_l21 = t224;
                }
                {
                    void * t227 = gm_l0;
                    gm_tl874t2atnlh3 t228 = gm_l1;
                    void * t229 = gm_l20;
                    gm_t6evimt84lndq t230 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t229));
                    gm_t88rcucg1b7uk t231 = gm_l21;
                    gm_b16_8 t232 = gm_l4;
                    uint64_t t233 = gm_l5;
                    void * t234 = gm_l0;
                    gm_t88rcucg1b7uk t235 = gm_l3;
                    uint64_t t236 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t234, t235);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t r237; bool o238 = __builtin_add_overflow(t233, t236, &r237);
                    if (o238) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 84, 54);
                    uint64_t t239 = r237;
                    gm_tcfa6l7reoosn t240;
                    gm_f_value_bytes_l7s126n((void *)&t240, (void *)NULL, cx, NULL, t227, t228, &t230, t231, t232, t239);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t240))[0] != 0)) {
                        gm_tcfa6l7reoosn t241;
                        GM_ST(uint8_t, (uint8_t *)&t241 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t241 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t241 + 4, 0);
                        ((uint8_t *)&t241)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t241 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t240) + 8)));
                        F.ret = t241;
                        goto gm_fret;
                    }
                    gm_b0_1 t242 = GM_LD(gm_b0_1, (((uint8_t *)&t240) + 8));
                }
                goto ix219;
ie219:;
ix219:;
            }
            goto me5;
mn181:;
ma14:;
            uint8_t t244 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t244 != ((uint8_t)3ULL)) goto mn243;
            void * t245 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l22 = t245;
            void * t246 = gm_l0;
            gm_t88rcucg1b7uk t247 = gm_l3;
            gm_t53at80pfdus3 t248;
            gm_f_kind_htv6093((void *)&t248, t246, GM_CONV(gm_csguankko4lmhd, t247));
            uint8_t t255 = GM_LD(uint8_t, (((uint8_t *)&t248) + 0));
            switch (t255) { case ((uint8_t)0ULL): goto ma253; case ((uint8_t)1ULL): goto ma253; case ((uint8_t)2ULL): goto ma253; case ((uint8_t)3ULL): goto ma253; case ((uint8_t)4ULL): goto ma253; case ((uint8_t)5ULL): goto ma253; case ((uint8_t)6ULL): goto ma253; case ((uint8_t)7ULL): goto ma253; case ((uint8_t)8ULL): goto ma253; case ((uint8_t)9ULL): goto ma253; case ((uint8_t)10ULL): goto ma253; case ((uint8_t)11ULL): goto ma250; case ((uint8_t)12ULL): goto ma251; case ((uint8_t)13ULL): goto ma253; case ((uint8_t)14ULL): goto ma253; case ((uint8_t)15ULL): goto ma253; case ((uint8_t)16ULL): goto ma253; case ((uint8_t)17ULL): goto ma252; case ((uint8_t)18ULL): goto ma253; case ((uint8_t)19ULL): goto ma253; case ((uint8_t)20ULL): goto ma253; case ((uint8_t)21ULL): goto ma253; case ((uint8_t)22ULL): goto ma253; case ((uint8_t)23ULL): goto ma253; case ((uint8_t)24ULL): goto ma253; case ((uint8_t)25ULL): goto ma253; case ((uint8_t)26ULL): goto ma253; default: goto ma253; }
ma250:;
            uint8_t t257 = GM_LD(uint8_t, (((uint8_t *)&t248) + 0));
            if (t257 != ((uint8_t)11ULL)) goto mn256;
            gm_t88rcucg1b7uk t258 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t248) + 8) + 8));
            gm_l23 = t258;
            {
                void * t260 = gm_l0;
                gm_t88rcucg1b7uk t261 = gm_l23;
                uint64_t t262 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t260, t261);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t262;
            }
            {
                {
                    void * t264 = gm_l22;
                    gm_b16_8 t265 = GM_LD(gm_b16_8, (((uint8_t *)t264) + 0));
                    gm_b16_8 t266 = GM_LD(gm_b16_8, ((uint8_t *)&t265));
                    gm_l25 = t266;
                }
                {
                    gm_l26 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t268 = gm_l26;
                        uint64_t t269 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                        uint8_t t270 = (t268 < t269);
                        if (!t270) goto lb267;
                        {
                            gm_thf8as07kj3uj t273;
                            memset((uint8_t *)&t273 + 8, 0, 8);
                            uint64_t t274 = gm_l26;
                            GM_ST(uint64_t, (uint8_t *)&t273 + 0, t274);
                            uint64_t t275 = gm_l26;
                            uint64_t t276 = (uintptr_t)t275;
                            uint64_t t277 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                            memcpy((uint8_t *)&t273 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t276 * 48), 48);
                            uint64_t t280 = GM_LD(uint64_t, (((uint8_t *)&t273) + 0));
                            gm_l27 = t280;
                            F.l28 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t273) + 16));
                            {
                                void * t282 = gm_l0;
                                gm_tl874t2atnlh3 t283 = gm_l1;
                                gm_t6evimt84lndq t284 = F.l28;
                                gm_t88rcucg1b7uk t285 = gm_l23;
                                gm_b16_8 t286 = gm_l4;
                                uint64_t t287 = gm_l5;
                                uint64_t t288 = gm_l27;
                                uint64_t t289 = (uint64_t)t288;
                                uint64_t t290 = gm_l24;
                                uint64_t r291; bool o292 = __builtin_mul_overflow(t289, t290, &r291);
                                if (o292) gm_trap("integer overflow in `*`", gm_src8sgunk80orlb, 90, 94);
                                uint64_t t293 = r291;
                                uint64_t r294; bool o295 = __builtin_add_overflow(t287, t293, &r294);
                                if (o295) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 90, 88);
                                uint64_t t296 = r294;
                                gm_tcfa6l7reoosn t297;
                                gm_f_value_bytes_l7s126n((void *)&t297, (void *)NULL, cx, NULL, t282, t283, &t284, t285, t286, t296);
                                if (gm_unw.kind) {
                                    goto gm_ll271;
                                }
                                if (((((uint8_t *)&t297))[0] != 0)) {
                                    gm_tcfa6l7reoosn t298;
                                    GM_ST(uint8_t, (uint8_t *)&t298 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t298 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t298 + 4, 0);
                                    ((uint8_t *)&t298)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t298 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t297) + 8)));
                                    F.ret = t298;
                                    goto gm_fret;
                                }
                                gm_b0_1 t299 = GM_LD(gm_b0_1, (((uint8_t *)&t297) + 8));
                            }
                            goto me278;
mn279:;
                            gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 90, 17);
me278:;
                        }
lc267:;
                        {
                            uint64_t t301 = gm_l26;
                            uint64_t r302; bool o303 = __builtin_add_overflow(t301, ((uint64_t)1ULL), &r302);
                            uint64_t t304 = r302;
                            gm_l26 = t304;
                        }
                    }
gm_ll271:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb267; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc267; }
                    }
                    goto gm_lad;
lb267:;
                }
            }
            goto me249;
mn256:;
ma251:;
            uint8_t t306 = GM_LD(uint8_t, (((uint8_t *)&t248) + 0));
            if (t306 != ((uint8_t)12ULL)) goto mn305;
            gm_b16_8 t307 = GM_LD(gm_b16_8, ((((uint8_t *)&t248) + 8) + 0));
            gm_l29 = t307;
            {
                void * t309 = gm_l0;
                gm_t88rcucg1b7uk t310 = gm_l3;
                gm_b16_8 t311 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t309, t310);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l30 = t311;
            }
            {
                {
                    void * t313 = gm_l22;
                    gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)t313) + 0));
                    gm_b16_8 t315 = GM_LD(gm_b16_8, ((uint8_t *)&t314));
                    gm_l31 = t315;
                }
                {
                    gm_l32 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t317 = gm_l32;
                        uint64_t t318 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                        uint8_t t319 = (t317 < t318);
                        if (!t319) goto lb316;
                        {
                            gm_thf8as07kj3uj t322;
                            memset((uint8_t *)&t322 + 8, 0, 8);
                            uint64_t t323 = gm_l32;
                            GM_ST(uint64_t, (uint8_t *)&t322 + 0, t323);
                            uint64_t t324 = gm_l32;
                            uint64_t t325 = (uintptr_t)t324;
                            uint64_t t326 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                            memcpy((uint8_t *)&t322 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l31)) + t325 * 48), 48);
                            uint64_t t329 = GM_LD(uint64_t, (((uint8_t *)&t322) + 0));
                            gm_l33 = t329;
                            F.l34 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t322) + 16));
                            {
                                void * t331 = gm_l0;
                                gm_tl874t2atnlh3 t332 = gm_l1;
                                uint64_t t333 = gm_l33;
                                uint64_t t334 = (uintptr_t)t333;
                                uint64_t t335 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                                if (t334 >= (uintptr_t)(t335)) gm_trap_index(t334, t335, gm_src8sgunk80orlb, 94, 77);
                                gm_t88rcucg1b7uk t336 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t334 * 4));
                                gm_b16_8 t337 = gm_l4;
                                uint64_t t338 = gm_l5;
                                uint64_t t339 = gm_l33;
                                uint64_t t340 = (uintptr_t)t339;
                                uint64_t t341 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                                if (t340 >= (uintptr_t)(t341)) gm_trap_index(t340, t341, gm_src8sgunk80orlb, 94, 98);
                                uint64_t t342 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t340 * 8));
                                uint64_t r343; bool o344 = __builtin_add_overflow(t338, t342, &r343);
                                if (o344) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 94, 92);
                                uint64_t t345 = r343;
                                gm_tcfa6l7reoosn t346;
                                gm_f_value_bytes_l7s126n((void *)&t346, (void *)NULL, cx, NULL, t331, t332, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l34)), t336, t337, t345);
                                if (gm_unw.kind) {
                                    goto gm_ll320;
                                }
                                if (((((uint8_t *)&t346))[0] != 0)) {
                                    gm_tcfa6l7reoosn t347;
                                    GM_ST(uint8_t, (uint8_t *)&t347 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t347 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t347 + 4, 0);
                                    ((uint8_t *)&t347)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t347 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t346) + 8)));
                                    F.ret = t347;
                                    goto gm_fret;
                                }
                                gm_b0_1 t348 = GM_LD(gm_b0_1, (((uint8_t *)&t346) + 8));
                            }
                            goto me327;
mn328:;
                            gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 94, 17);
me327:;
                        }
lc316:;
                        {
                            uint64_t t350 = gm_l32;
                            uint64_t r351; bool o352 = __builtin_add_overflow(t350, ((uint64_t)1ULL), &r351);
                            uint64_t t353 = r351;
                            gm_l32 = t353;
                        }
                    }
gm_ll320:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb316; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc316; }
                    }
                    goto gm_lad;
lb316:;
                }
            }
            goto me249;
mn305:;
ma252:;
            uint8_t t355 = GM_LD(uint8_t, (((uint8_t *)&t248) + 0));
            if (t355 != ((uint8_t)17ULL)) goto mn354;
            uint32_t t356 = GM_LD(uint32_t, ((((uint8_t *)&t248) + 8) + 0));
            gm_l35 = t356;
            void * t357 = gm_l0;
            void * t358 = GM_LD(void *, (((uint8_t *)t357) + 8));
            gm_b16_8 t359 = GM_LD(gm_b16_8, ((((uint8_t *)t358) + 1088) + 0));
            gm_b16_8 t360 = t359;
            uint32_t t361 = gm_l35;
            uint64_t t362 = (uint64_t)t361;
            uint64_t t363 = gm_f_us_cuv01fp(t362);
            uint64_t t364 = (uintptr_t)t363;
            uint64_t t365 = GM_LD(uintptr_t, ((uint8_t *)&t360) + 8);
            if (t364 >= (uintptr_t)(t365)) gm_trap_index(t364, t365, gm_src8sgunk80orlb, 96, 34);
            uint8_t t368 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t360)) + t364 * 184) + 24) + 0));
            if (t368 != ((uint8_t)0ULL)) goto mn367;
            gm_tpautr4br7h3l t369 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t360)) + t364 * 184) + 24) + 8));
            F.l36 = t369;
            {
                if (!(((((uint8_t *)&F.l36) + 40))[4] != 0)) goto ie371;
                gm_t88rcucg1b7uk t372 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l36) + 40));
                gm_l37 = t372;
                {
                    void * t374 = gm_l0;
                    gm_tl874t2atnlh3 t375 = gm_l1;
                    gm_t88rcucg1b7uk t376 = gm_l3;
                    gm_tdlro53ljvkl0 t377;
                    gm_f_const_bits_1of8c6t((void *)&t377, (void *)NULL, cx, NULL, t374, t375, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l2)), t376);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t377))[0] != 0)) {
                        gm_tcfa6l7reoosn t378;
                        GM_ST(uint8_t, (uint8_t *)&t378 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t378 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t378 + 4, 0);
                        ((uint8_t *)&t378)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t378 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t377) + 16)));
                        F.ret = t378;
                        goto gm_fret;
                    }
                    unsigned __int128 t379 = GM_LD(unsigned __int128, (((uint8_t *)&t377) + 16));
                    gm_l38 = t379;
                }
                {
                    void * t380 = gm_l0;
                    gm_b16_8 t381 = gm_l4;
                    uint64_t t382 = GM_LD(uintptr_t, ((uint8_t *)&t381) + 8);
                    void * t383 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t381)));
                    uint64_t t384 = gm_l6;
                    uint64_t t385 = (uintptr_t)t384;
                    uint64_t t386 = gm_l6;
                    void * t387 = gm_l0;
                    gm_t88rcucg1b7uk t388 = gm_l37;
                    uint64_t t389 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t387, t388);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t390 = gm_f_us_cuv01fp(t389);
                    uint64_t r391; bool o392 = __builtin_add_overflow(t386, t390, &r391);
                    if (o392) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 100, 43);
                    uint64_t t393 = r391;
                    uint64_t t394 = (uintptr_t)t393;
                    if (t385 > t394 || t394 > (uintptr_t)t382) gm_trap_slice(t385, t394, t382, gm_src8sgunk80orlb, 100, 36);
                    gm_b16_8 t395;
                    GM_ST(void *, (uint8_t *)&t395, (void *)(((uint8_t *)t383 + t385 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t395 + 8, (uintptr_t)(t394 - t385));
                    unsigned __int128 t396 = gm_l38;
                    gm_f_put_uint_cddin99(t380, t395, t396);
                }
                {
                    gm_b0_1 t397;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t397);
                    goto gm_fdone;
                }
                goto ix371;
ie371:;
ix371:;
            }
            {
                void * t398 = gm_l0;
                gm_t88rcucg1b7uk t399 = gm_l3;
                gm_b16_8 t400 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t398, t399);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l39 = t400;
            }
            {
                {
                    void * t402 = gm_l22;
                    gm_b16_8 t403 = GM_LD(gm_b16_8, (((uint8_t *)t402) + 0));
                    gm_b16_8 t404 = GM_LD(gm_b16_8, ((uint8_t *)&t403));
                    gm_l40 = t404;
                }
                {
                    gm_l41 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t406 = gm_l41;
                        uint64_t t407 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                        uint8_t t408 = (t406 < t407);
                        if (!t408) goto lb405;
                        {
                            gm_thf8as07kj3uj t411;
                            memset((uint8_t *)&t411 + 8, 0, 8);
                            uint64_t t412 = gm_l41;
                            GM_ST(uint64_t, (uint8_t *)&t411 + 0, t412);
                            uint64_t t413 = gm_l41;
                            uint64_t t414 = (uintptr_t)t413;
                            uint64_t t415 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                            memcpy((uint8_t *)&t411 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t414 * 48), 48);
                            uint64_t t418 = GM_LD(uint64_t, (((uint8_t *)&t411) + 0));
                            gm_l42 = t418;
                            F.l43 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t411) + 16));
                            {
                                void * t420 = gm_l0;
                                gm_tl874t2atnlh3 t421 = gm_l1;
                                uint64_t t422 = gm_l42;
                                uint64_t t423 = (uintptr_t)t422;
                                uint64_t t424 = GM_LD(uintptr_t, (((uint8_t *)&F.l36) + 0) + 8);
                                if (t423 >= (uintptr_t)(t424)) gm_trap_index(t423, t424, gm_src8sgunk80orlb, 104, 81);
                                gm_t88rcucg1b7uk t425 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l36) + 0)) + t423 * 40) + 16));
                                gm_b16_8 t426 = gm_l4;
                                uint64_t t427 = gm_l5;
                                uint64_t t428 = gm_l42;
                                uint64_t t429 = (uintptr_t)t428;
                                uint64_t t430 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                                if (t429 >= (uintptr_t)(t430)) gm_trap_index(t429, t430, gm_src8sgunk80orlb, 104, 109);
                                uint64_t t431 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l39)) + t429 * 8));
                                uint64_t r432; bool o433 = __builtin_add_overflow(t427, t431, &r432);
                                if (o433) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 104, 103);
                                uint64_t t434 = r432;
                                gm_tcfa6l7reoosn t435;
                                gm_f_value_bytes_l7s126n((void *)&t435, (void *)NULL, cx, NULL, t420, t421, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l43)), t425, t426, t434);
                                if (gm_unw.kind) {
                                    goto gm_ll409;
                                }
                                if (((((uint8_t *)&t435))[0] != 0)) {
                                    gm_tcfa6l7reoosn t436;
                                    GM_ST(uint8_t, (uint8_t *)&t436 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t436 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t436 + 4, 0);
                                    ((uint8_t *)&t436)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t436 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t435) + 8)));
                                    F.ret = t436;
                                    goto gm_fret;
                                }
                                gm_b0_1 t437 = GM_LD(gm_b0_1, (((uint8_t *)&t435) + 8));
                            }
                            goto me416;
mn417:;
                            gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 104, 21);
me416:;
                        }
lc405:;
                        {
                            uint64_t t439 = gm_l41;
                            uint64_t r440; bool o441 = __builtin_add_overflow(t439, ((uint64_t)1ULL), &r440);
                            uint64_t t442 = r440;
                            gm_l41 = t442;
                        }
                    }
gm_ll409:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb405; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc405; }
                    }
                    goto gm_lad;
lb405:;
                }
            }
            goto me366;
mn367:;
            void * t444 = gm_l0;
            gm_tl874t2atnlh3 t445 = gm_l1;
            gm_b16_8 t446;
            GM_ST(void *, (uint8_t *)&t446, (void *)(gm_s67fd8s6hf84e));
            GM_ST(uintptr_t, (uint8_t *)&t446 + 8, (uintptr_t)(23));
            gm_t76123gfet73g t447;
            gm_f_unsupported_70vvk1e((void *)&t447, (void *)NULL, cx, NULL, t444, t445, t446);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t447);
            goto gm_fdone;
            goto me366;
mn443:;
            gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 96, 28);
me366:;
            goto me249;
mn354:;
ma253:;
            void * t449 = gm_l0;
            gm_tl874t2atnlh3 t450 = gm_l1;
            gm_b16_8 t451;
            GM_ST(void *, (uint8_t *)&t451, (void *)(gm_s67fd8s6hf84e));
            GM_ST(uintptr_t, (uint8_t *)&t451 + 8, (uintptr_t)(23));
            gm_t76123gfet73g t452;
            gm_f_unsupported_70vvk1e((void *)&t452, (void *)NULL, cx, NULL, t449, t450, t451);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t452);
            goto gm_fdone;
            goto me249;
mn448:;
ma254:;
            gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 87, 20);
me249:;
            goto me5;
mn243:;
ma15:;
            uint8_t t454 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t454 != ((uint8_t)4ULL)) goto mn453;
            gm_tkaapcii86udk t455 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l2) + 16));
            gm_l44 = t455;
            {
                void * t457 = gm_l0;
                gm_t88rcucg1b7uk t458 = gm_l3;
                gm_t53at80pfdus3 t459;
                gm_f_kind_htv6093((void *)&t459, t457, GM_CONV(gm_csguankko4lmhd, t458));
                gm_t88rcucg1b7uk t460;
                uint8_t t466 = GM_LD(uint8_t, (((uint8_t *)&t459) + 0));
                switch (t466) { case ((uint8_t)0ULL): goto ma464; case ((uint8_t)1ULL): goto ma464; case ((uint8_t)2ULL): goto ma464; case ((uint8_t)3ULL): goto ma464; case ((uint8_t)4ULL): goto ma464; case ((uint8_t)5ULL): goto ma464; case ((uint8_t)6ULL): goto ma464; case ((uint8_t)7ULL): goto ma464; case ((uint8_t)8ULL): goto ma462; case ((uint8_t)9ULL): goto ma462; case ((uint8_t)10ULL): goto ma464; case ((uint8_t)11ULL): goto ma464; case ((uint8_t)12ULL): goto ma464; case ((uint8_t)13ULL): goto ma463; case ((uint8_t)14ULL): goto ma464; case ((uint8_t)15ULL): goto ma464; case ((uint8_t)16ULL): goto ma464; case ((uint8_t)17ULL): goto ma464; case ((uint8_t)18ULL): goto ma464; case ((uint8_t)19ULL): goto ma464; case ((uint8_t)20ULL): goto ma464; case ((uint8_t)21ULL): goto ma464; case ((uint8_t)22ULL): goto ma464; case ((uint8_t)23ULL): goto ma464; case ((uint8_t)24ULL): goto ma464; case ((uint8_t)25ULL): goto ma464; case ((uint8_t)26ULL): goto ma464; default: goto ma464; }
ma462:;
                uint8_t t470 = GM_LD(uint8_t, (((uint8_t *)&t459) + 0));
                if (t470 != ((uint8_t)8ULL)) goto pa469;
                gm_t88rcucg1b7uk t471 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t459) + 8) + 4));
                gm_l45 = t471;
                goto po468;
pa469:;
                uint8_t t473 = GM_LD(uint8_t, (((uint8_t *)&t459) + 0));
                if (t473 != ((uint8_t)9ULL)) goto pa472;
                gm_t88rcucg1b7uk t474 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t459) + 8) + 4));
                gm_l45 = t474;
                goto po468;
pa472:;
                goto mn467;
po468:;
                gm_t88rcucg1b7uk t475 = gm_l45;
                t460 = t475;
                goto me461;
mn467:;
ma463:;
                uint8_t t477 = GM_LD(uint8_t, (((uint8_t *)&t459) + 0));
                if (t477 != ((uint8_t)13ULL)) goto mn476;
                gm_t88rcucg1b7uk t478 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t459) + 8));
                gm_l47 = t478;
                void * t479 = gm_l0;
                gm_t88rcucg1b7uk t480 = gm_l47;
                gm_t53at80pfdus3 t481;
                gm_f_kind_htv6093((void *)&t481, t479, GM_CONV(gm_csguankko4lmhd, t480));
                gm_t88rcucg1b7uk t482;
                uint8_t t487 = GM_LD(uint8_t, (((uint8_t *)&t481) + 0));
                if (t487 != ((uint8_t)8ULL)) goto pa486;
                gm_t88rcucg1b7uk t488 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t481) + 8) + 4));
                gm_l48 = t488;
                goto po485;
pa486:;
                uint8_t t490 = GM_LD(uint8_t, (((uint8_t *)&t481) + 0));
                if (t490 != ((uint8_t)9ULL)) goto pa489;
                gm_t88rcucg1b7uk t491 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t481) + 8) + 4));
                gm_l48 = t491;
                goto po485;
pa489:;
                goto mn484;
po485:;
                gm_t88rcucg1b7uk t492 = gm_l48;
                t482 = t492;
                goto me483;
mn484:;
                void * t494 = gm_l0;
                gm_tl874t2atnlh3 t495 = gm_l1;
                gm_b16_8 t496;
                GM_ST(void *, (uint8_t *)&t496, (void *)(gm_sgiqpj781s1a9));
                GM_ST(uintptr_t, (uint8_t *)&t496 + 8, (uintptr_t)(35));
                gm_t76123gfet73g t497;
                gm_f_unsupported_70vvk1e((void *)&t497, (void *)NULL, cx, NULL, t494, t495, t496);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t497);
                goto gm_fdone;
                goto me483;
mn493:;
                gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 113, 37);
me483:;
                t460 = t482;
                goto me461;
mn476:;
ma464:;
                void * t499 = gm_l0;
                gm_tl874t2atnlh3 t500 = gm_l1;
                gm_b16_8 t501;
                GM_ST(void *, (uint8_t *)&t501, (void *)(gm_sgiqpj781s1a9));
                GM_ST(uintptr_t, (uint8_t *)&t501 + 8, (uintptr_t)(35));
                gm_t76123gfet73g t502;
                gm_f_unsupported_70vvk1e((void *)&t502, (void *)NULL, cx, NULL, t499, t500, t501);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t502);
                goto gm_fdone;
                goto me461;
mn498:;
ma465:;
                gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 111, 27);
me461:;
                gm_l50 = t460;
            }
            {
                void * t503 = gm_l0;
                uint64_t t504 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                void * t505 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                uint64_t t506 = gm_l6;
                uint64_t t507 = (uintptr_t)t506;
                if (t507 > (uintptr_t)t504 || (uintptr_t)t504 > (uintptr_t)t504) gm_trap_slice(t507, (uintptr_t)t504, t504, gm_src8sgunk80orlb, 119, 23);
                gm_b16_8 t508;
                GM_ST(void *, (uint8_t *)&t508, (void *)(((uint8_t *)t505 + t507 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t508 + 8, (uintptr_t)((uintptr_t)t504 - t507));
                void * t509 = gm_l0;
                gm_tl874t2atnlh3 t510 = gm_l1;
                gm_t88rcucg1b7uk t511 = gm_l50;
                gm_t833u93mr4iob t512;
                gm_f_place_addr_m751en7((void *)&t512, (void *)NULL, cx, NULL, t509, t510, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l44)), t511);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t512))[0] != 0)) {
                    gm_tcfa6l7reoosn t513;
                    GM_ST(uint8_t, (uint8_t *)&t513 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t513 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t513 + 4, 0);
                    ((uint8_t *)&t513)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t513 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t512) + 8)));
                    F.ret = t513;
                    goto gm_fret;
                }
                uint64_t t514 = GM_LD(uint64_t, (((uint8_t *)&t512) + 8));
                gm_f_put_ptr_188p3mm(t503, t508, t514);
            }
            goto me5;
mn453:;
ma16:;
            uint8_t t516 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t516 != ((uint8_t)5ULL)) goto mn515;
            gm_tkaapcii86udk t517 = GM_LD(gm_tkaapcii86udk, ((((uint8_t *)&F.l2) + 16) + 0));
            gm_l51 = t517;
            uint64_t t518 = GM_LD(uint64_t, ((((uint8_t *)&F.l2) + 16) + 24));
            gm_l52 = t518;
            {
                void * t521 = gm_l0;
                gm_t88rcucg1b7uk t522 = gm_l3;
                gm_t53at80pfdus3 t523;
                gm_f_kind_htv6093((void *)&t523, t521, GM_CONV(gm_csguankko4lmhd, t522));
                uint8_t t524 = GM_LD(uint8_t, (((uint8_t *)&t523) + 0));
                if (t524 != ((uint8_t)10ULL)) goto ie520;
                gm_t88rcucg1b7uk t525 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t523) + 8) + 4));
                gm_l53 = t525;
                {
                    void * t527 = gm_l0;
                    uint64_t t528 = GM_LD(uint64_t, (((uint8_t *)t527) + 176));
                    uint64_t t529 = gm_f_us_cuv01fp(t528);
                    gm_l54 = t529;
                }
                {
                    gm_l55 = ((uint64_t)0ULL);
                }
                {
                    uint8_t t531 = 0;
                    uint64_t t532 = gm_l52;
                    uint8_t t533 = (t532 > ((uint64_t)0ULL));
                    if (!t533) {
                        uint64_t t534 = GM_LD(uintptr_t, (((uint8_t *)&gm_l51) + 8) + 8);
                        uint8_t t535 = (t534 > ((uint64_t)0ULL));
                        t531 = t535;
                    } else t531 = 1;
                    if (!t531) goto ie530;
                    {
                        void * t537 = gm_l0;
                        gm_tl874t2atnlh3 t538 = gm_l1;
                        gm_t88rcucg1b7uk t539 = gm_l53;
                        gm_t833u93mr4iob t540;
                        gm_f_place_addr_m751en7((void *)&t540, (void *)NULL, cx, NULL, t537, t538, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l51)), t539);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t540))[0] != 0)) {
                            gm_tcfa6l7reoosn t541;
                            GM_ST(uint8_t, (uint8_t *)&t541 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t541 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t541 + 4, 0);
                            ((uint8_t *)&t541)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t541 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t540) + 8)));
                            F.ret = t541;
                            goto gm_fret;
                        }
                        uint64_t t542 = GM_LD(uint64_t, (((uint8_t *)&t540) + 8));
                        gm_l55 = t542;
                    }
                    goto ix530;
ie530:;
ix530:;
                }
                {
                    void * t543 = gm_l0;
                    uint64_t t544 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    void * t545 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                    uint64_t t546 = gm_l6;
                    uint64_t t547 = (uintptr_t)t546;
                    uint64_t t548 = gm_l6;
                    uint64_t t549 = gm_l54;
                    uint64_t r550; bool o551 = __builtin_mul_overflow(((uint64_t)2ULL), t549, &r550);
                    if (o551) gm_trap("integer overflow in `*`", gm_src8sgunk80orlb, 126, 36);
                    uint64_t t552 = r550;
                    uint64_t r553; bool o554 = __builtin_add_overflow(t548, t552, &r553);
                    if (o554) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 126, 32);
                    uint64_t t555 = r553;
                    uint64_t t556 = (uintptr_t)t555;
                    if (t547 > t556 || t556 > (uintptr_t)t544) gm_trap_slice(t547, t556, t544, gm_src8sgunk80orlb, 126, 25);
                    gm_b16_8 t557;
                    GM_ST(void *, (uint8_t *)&t557, (void *)(((uint8_t *)t545 + t547 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t557 + 8, (uintptr_t)(t556 - t547));
                    uint64_t t558 = gm_l55;
                    uint64_t t559 = gm_l52;
                    gm_f_put_slice_ko14loi(t543, t557, t558, t559);
                }
                goto ix520;
ie520:;
                {
                    void * t561 = gm_l0;
                    gm_tl874t2atnlh3 t562 = gm_l1;
                    gm_b16_8 t563;
                    GM_ST(void *, (uint8_t *)&t563, (void *)(gm_svlcdvcv2bp94));
                    GM_ST(uintptr_t, (uint8_t *)&t563 + 8, (uintptr_t)(23));
                    gm_t76123gfet73g t564;
                    gm_f_unsupported_70vvk1e((void *)&t564, (void *)NULL, cx, NULL, t561, t562, t563);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t564);
                    goto gm_fdone;
                }
ix520:;
            }
            goto me5;
mn515:;
ma17:;
            uint8_t t566 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t566 != ((uint8_t)11ULL)) goto mn565;
            uint32_t t567 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 16));
            gm_l56 = t567;
            void * t568 = gm_l0;
            uint64_t t569 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            void * t570 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
            uint64_t t571 = gm_l6;
            uint64_t t572 = (uintptr_t)t571;
            if (t572 > (uintptr_t)t569 || (uintptr_t)t569 > (uintptr_t)t569) gm_trap_slice(t572, (uintptr_t)t569, t569, gm_src8sgunk80orlb, 128, 29);
            gm_b16_8 t573;
            GM_ST(void *, (uint8_t *)&t573, (void *)(((uint8_t *)t570 + t572 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t573 + 8, (uintptr_t)((uintptr_t)t569 - t572));
            void * t574 = gm_l0;
            uint32_t t575 = gm_l56;
            uint64_t t576 = gm_f_fn_addr_odbbf6a(t574, t575);
            gm_f_put_ptr_188p3mm(t568, t573, t576);
            goto me5;
mn565:;
ma18:;
            uint8_t t580 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t580 != ((uint8_t)12ULL)) goto pa579;
            goto po578;
pa579:;
            uint8_t t582 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t582 != ((uint8_t)13ULL)) goto pa581;
            goto po578;
pa581:;
            goto mn577;
po578:;
            void * t583 = gm_l0;
            gm_tl874t2atnlh3 t584 = gm_l1;
            gm_b16_8 t585;
            GM_ST(void *, (uint8_t *)&t585, (void *)(gm_sbbavs945cjhp));
            GM_ST(uintptr_t, (uint8_t *)&t585 + 8, (uintptr_t)(31));
            gm_t76123gfet73g t586;
            gm_f_unsupported_70vvk1e((void *)&t586, (void *)NULL, cx, NULL, t583, t584, t585);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t586);
            goto gm_fdone;
            goto me5;
mn577:;
ma19:;
            gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 61, 5);
me5:;
        }
    }
    gm_b0_1 t587;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t587);
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
