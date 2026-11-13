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
static uint64_t gm_mix(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the eight little-endian bytes of x, whatever the byte order of the machine. */
static uint64_t gm_mix64(uint64_t h, uint64_t x) {
    for (int i = 0; i < 8; i++) { h ^= (uint8_t)(x >> (8 * i)); h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the minimal two's-complement little-endian bytes of v (as BigInt::to_signed_bytes_le). */
static uint64_t gm_hash_int(uint64_t h, __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if ((v == 0 && !(byte & 0x80)) || (v == -1 && (byte & 0x80))) break;
    }
    return h;
}
static uint64_t gm_hash_uint(uint64_t h, unsigned __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if (v == 0 && !(byte & 0x80)) break;
        if (v == 0) { h *= 0x100000001b3ULL; break; }
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
typedef struct gm_csr3mh7spg80kh { uint32_t f0; uint32_t f1; _Bool f2; _Bool f3; _Bool f4; } gm_csr3mh7spg80kh;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; uint32_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t pe[1]; } gm_t5ogmjn0ml54c;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
_Static_assert(sizeof(gm_csr3mh7spg80kh) == 12 && _Alignof(gm_csr3mh7spg80kh) == 4 && offsetof(gm_csr3mh7spg80kh, f0) == 0 && offsetof(gm_csr3mh7spg80kh, f1) == 4 && offsetof(gm_csr3mh7spg80kh, f2) == 8 && offsetof(gm_csr3mh7spg80kh, f3) == 9 && offsetof(gm_csr3mh7spg80kh, f4) == 10, "layout of `StringMetrics`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t5ogmjn0ml54c) == 12 && _Alignof(gm_t5ogmjn0ml54c) == 4 && offsetof(gm_t5ogmjn0ml54c, f0) == 0 && offsetof(gm_t5ogmjn0ml54c, f1) == 4 && offsetof(gm_t5ogmjn0ml54c, f2) == 8 && offsetof(gm_t5ogmjn0ml54c, f3) == 9 && offsetof(gm_t5ogmjn0ml54c, f4) == 10, "layout of `StringMetrics`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; int64_t v1; uint8_t v2; gm_trqfvmjtt4sij v3; void * v4; gm_b16_8 v5; } f1; } gm_tktr4rmbindgv;
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; void * v1; } f1; } gm_trssmtuv8jr0v;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_b16_8 f4; gm_b16_8 f5; gm_th2lqbo77pvsc f6; gm_b16_8 f7; gm_b16_8 f8; } gm_ts1nru0t7g7aj;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tin97tt4ud0s1 v1; } f1; } gm_t62efos6ssciq;
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
typedef struct { gm_ts1nru0t7g7aj f0; uint8_t f1; uint8_t pe[7]; } gm_tie69pa8pplgc;
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tktr4rmbindgv) == 48 && _Alignof(gm_tktr4rmbindgv) == 8 && offsetof(gm_tktr4rmbindgv, f0) == 0 && offsetof(gm_tktr4rmbindgv, f1) == 8, "layout of `Value`");
_Static_assert(sizeof(gm_trssmtuv8jr0v) == 40 && _Alignof(gm_trssmtuv8jr0v) == 8 && offsetof(gm_trssmtuv8jr0v, f0) == 0 && offsetof(gm_trssmtuv8jr0v, f1) == 8, "layout of `TomlError!^Table`");
_Static_assert(sizeof(gm_ts1nru0t7g7aj) == 168 && _Alignof(gm_ts1nru0t7g7aj) == 8 && offsetof(gm_ts1nru0t7g7aj, f0) == 0 && offsetof(gm_ts1nru0t7g7aj, f1) == 16 && offsetof(gm_ts1nru0t7g7aj, f2) == 32 && offsetof(gm_ts1nru0t7g7aj, f3) == 56 && offsetof(gm_ts1nru0t7g7aj, f4) == 80 && offsetof(gm_ts1nru0t7g7aj, f5) == 96 && offsetof(gm_ts1nru0t7g7aj, f6) == 112 && offsetof(gm_ts1nru0t7g7aj, f7) == 136 && offsetof(gm_ts1nru0t7g7aj, f8) == 152, "layout of `LockedPackage`");
typedef struct { gm_tin97tt4ud0s1 f0; uint8_t f1; uint8_t pe[7]; } gm_tv37d3dpt2opu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tv37d3dpt2opu v1; } f1; } gm_t074vrl1hb4f6;
_Static_assert(sizeof(gm_t62efos6ssciq) == 48 && _Alignof(gm_t62efos6ssciq) == 8 && offsetof(gm_t62efos6ssciq, f0) == 0 && offsetof(gm_t62efos6ssciq, f1) == 8, "layout of `string!VersionReq`");
_Static_assert(sizeof(gm_tie69pa8pplgc) == 176 && _Alignof(gm_tie69pa8pplgc) == 8 && offsetof(gm_tie69pa8pplgc, f0) == 0 && offsetof(gm_tie69pa8pplgc, f1) == 168, "layout of `?LockedPackage`");
_Static_assert(sizeof(gm_tv37d3dpt2opu) == 48 && _Alignof(gm_tv37d3dpt2opu) == 8 && offsetof(gm_tv37d3dpt2opu, f0) == 0 && offsetof(gm_tv37d3dpt2opu, f1) == 40, "layout of `?Resolution`");
_Static_assert(sizeof(gm_t074vrl1hb4f6) == 56 && _Alignof(gm_t074vrl1hb4f6) == 8 && offsetof(gm_t074vrl1hb4f6, f0) == 0 && offsetof(gm_t074vrl1hb4f6, f1) == 8, "layout of `string!?Resolution`");
struct gm_fr_basic_string_3q58uai {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_basic_string_ml_n90kq9h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_check_package_hgpf1l9 {
    gm_frame h;
    gm_tl572d7579hns ret;
};
struct gm_fr_display_version_94faa6b {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_find_locked_83oh60j {
    gm_frame h;
    gm_ts1nru0t7g7aj l5; /* p */
    gm_tie69pa8pplgc ret;
};
struct gm_fr_is_archive_vibcvpj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_sha256_3g83n88 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_option_less_72r3i7m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_package_less_4m46djp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_package_string_rn5sliu {
    gm_frame h;
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_parse_error_ghm43oi {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* out */
    gm_b16_8 ret;
};
struct gm_fr_parse_lock_87bbbka {
    gm_frame h;
    gm_tin97tt4ud0s1 l3; /* lock */
    gm_trqfvmjtt4sij l7; /* items */
    gm_tktr4rmbindgv l10; /* it */
    gm_trqfvmjtt4sij l19; /* deps */
    gm_trqfvmjtt4sij l21; /* ds */
    gm_tktr4rmbindgv l24; /* x */
    gm_ts1nru0t7g7aj l32; /* package */
    gm_t62efos6ssciq ret;
};
struct gm_fr_read_lock_8pmv1su {
    gm_frame h;
    gm_tin97tt4ud0s1 l4; /* lock */
    gm_t074vrl1hb4f6 ret;
};
struct gm_fr_render_lock_bj7rst0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* pkgs */
    gm_ts1nru0t7g7aj l4; /* p */
    gm_trqfvmjtt4sij l5; /* out */
    gm_ts1nru0t7g7aj l8; /* p */
    gm_trqfvmjtt4sij l13; /* deps */
    gm_trqfvmjtt4sij l17; /* quoted */
    gm_b16_8 ret;
};
struct gm_fr_string_metrics_otdg4n0 {
    gm_frame h;
    gm_t5ogmjn0ml54c ret;
};
struct gm_fr_toml_string_cagbrmp {
    gm_frame h;
    gm_b16_8 ret;
};
bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b);
bool gm_eqkci3evs8m2(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_as_str_982o16v(void *gm_out, const void * a0);
__attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_basic_string_3q58uai(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_basic_string_ml_n90kq9h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_check_package_hgpf1l9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_display_version_94faa6b(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_find_locked_83oh60j(void *gm_out, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_tie69pa8pplgc gm_f_find_locked_83oh60j_c(void * a0, gm_b16_8 a1, gm_th2lqbo77pvsc a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_04pispc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_0t23jv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3aass3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_484p9e9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ane7f49(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_g5opmgl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_g9g5b0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hcca65h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hum25d9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_k399iof(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kvsrtt1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_lk5ln6l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0);
__attribute__((aligned(4))) void gm_f_format_m2o7vq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nlu0p4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qvc703s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r8c6rs0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_sa1940l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void * gm_f_get_9rme0gb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_is_archive_vibcvpj(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_sha256_3g83n88(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_subdir_sjdvkdc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_pfs7ocg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_name_problem_6r4mp11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_option_less_72r3i7m(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_option_less_72r3i7m_c(gm_th2lqbo77pvsc a0, gm_th2lqbo77pvsc a1);
__attribute__((aligned(4))) uint8_t gm_f_package_less_4m46djp(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_package_string_rn5sliu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_parse_error_ghm43oi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_lock_87bbbka(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_toml_gjtmf79(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_1htqqaf(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_read_lock_8pmv1su(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_read_message_6p6ba86(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_render_lock_bj7rst0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_sort_by_2q4cr01(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_csr3mh7spg80kh gm_f_string_metrics_otdg4n0(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_toml_string_cagbrmp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_url_problem_38kdh3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5bn7867(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5cc70a8(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6s6h7ts(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_894ocpq(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_90msg31(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dp2uuhb(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_g32vbf8(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_g6r6ugc(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_i42q2qm(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_m0brg2a(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rv9cdbb(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_tlurko4(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u8qima9(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vt977k2(void * a0, gm_tnpifv20irnph a1);
bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
extern uint8_t gm_cduv4b53gs5[16] __attribute__((aligned(8)));
extern const uint8_t gm_s0g8veu8021ch[3];
extern const uint8_t gm_s1b0nuksrh572[29];
extern const uint8_t gm_s22g482qk7a40[3];
extern const uint8_t gm_s2j8t1jqamft8[5];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s6gt8b7hsisgu[3];
extern const uint8_t gm_s8rm42n03sfss[4];
extern const uint8_t gm_s9cb6j5ok7bdk[3];
extern const uint8_t gm_sb54ui2amic32[2];
extern const uint8_t gm_sf9ilivr4c86m[4];
extern const uint8_t gm_sfvspsnv23gbt[7];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_shde4bsckk9mt[4];
extern const uint8_t gm_sius9pki5u4e3[5];
extern const uint8_t gm_sjiaitbr3bp01[10];
extern const uint8_t gm_skof48dte1s1o[13];
extern const uint8_t gm_skrj4540qohca[37];
extern const uint8_t gm_sla3alt5340mf[8];
extern const uint8_t gm_sllgtfi45c44a[8];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_smr3sbhcj1goc[7];
extern const uint8_t gm_snqvo5bgc2fch[8];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_son0j0k0t10ro[8];
extern const uint8_t gm_spchsf3taendr[3];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_srcu4ee17t4impf[];
extern const uint8_t gm_srehvgd1sod2m[18];
extern const uint8_t gm_sso1h04lhm0cu[32];
extern const uint8_t gm_ssou2pjir6lie[9];
extern const uint8_t gm_supi6mhbcvtna[5];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) gm_b16_8 gm_f_basic_string_3q58uai(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1) {
    struct gm_fr_basic_string_3q58uai F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* multiline */
    uint32_t gm_l3; /* max_dq */
    uint32_t gm_l4; /* doubles */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* b */
    GM_STACK_CHECK(&F, 677u);
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
        void * t2 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t3;
        uint8_t t5 = gm_l1;
        if (!t5) goto ie4;
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s8rm42n03sfss));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(3));
        gm_b16_8 t8 = t7;
        t3 = t8;
        goto ix4;
ie4:;
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sb54ui2amic32));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(1));
        gm_b16_8 t11 = t10;
        t3 = t11;
ix4:;
        gm_tftb1odc739t5 t12 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcu4ee17t4impf, 140, 5);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        uint32_t t15;
        uint8_t t17 = gm_l1;
        if (!t17) goto ie16;
        uint32_t t19 = ((uint32_t)2ULL);
        t15 = t19;
        goto ix16;
ie16:;
        uint32_t t21 = ((uint32_t)0ULL);
        t15 = t21;
ix16:;
        gm_l3 = t15;
    }
    {
        gm_l4 = ((uint32_t)0ULL);
    }
    {
        {
            gm_b16_8 t23 = gm_l0;
            gm_l5 = t23;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t25 = gm_l6;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    uint64_t t30 = gm_l6;
                    uint64_t t31 = (uintptr_t)t30;
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t33 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t31 * 1));
                    uint8_t t34 = t33;
                    uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)&t34));
                    gm_l7 = t37;
                    {
                        uint8_t t40 = gm_l7;
                        uint8_t t41 = ((t40 == ((uint8_t)34ULL)));
                        if (!t41) goto ie39;
                        {
                            uint32_t t43 = gm_l4;
                            uint32_t r44; bool o45 = __builtin_add_overflow(t43, ((uint32_t)1ULL), &r44);
                            if (o45) gm_trap("integer overflow in `+`", gm_srcu4ee17t4impf, 145, 13);
                            uint32_t t46 = r44;
                            gm_l4 = t46;
                        }
                        goto ix39;
ie39:;
                        {
                            gm_l4 = ((uint32_t)0ULL);
                        }
ix39:;
                    }
                    {
                        uint8_t t49 = gm_l7;
                        uint8_t t50 = ((t49 == ((uint8_t)34ULL)));
                        if (!t50) goto ie48;
                        uint32_t t51 = gm_l4;
                        uint32_t t52 = gm_l3;
                        uint8_t t53 = (t51 > t52);
                        if (!t53) goto ie48;
                        {
                            void * t55 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t56;
                            GM_ST(void *, (uint8_t *)&t56, (void *)(gm_smee1janbl2t9));
                            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t57 = gm_f_extend_0cs85gs(t55, t56);
                            gm_tftb1odc739t5 t58 = t57;
                            if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_srcu4ee17t4impf, 150, 13);
                            gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
                        }
                        {
                            gm_l4 = ((uint32_t)0ULL);
                        }
                        goto ix48;
ie48:;
                        {
                            uint8_t t61 = gm_l7;
                            uint8_t t62 = t61;
                            uint8_t t65 = GM_LD(uint8_t, ((uint8_t *)&t62));
                            if (!(t65 == ((uint8_t)8ULL))) goto mn64;
                            void * t66 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t67;
                            GM_ST(void *, (uint8_t *)&t67, (void *)(gm_s0g8veu8021ch));
                            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t68 = gm_f_extend_0cs85gs(t66, t67);
                            gm_tftb1odc739t5 t69 = t68;
                            if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 1, gm_srcu4ee17t4impf, 154, 22);
                            gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 1));
                            goto me63;
mn64:;
                            uint8_t t72 = GM_LD(uint8_t, ((uint8_t *)&t62));
                            if (!(t72 == ((uint8_t)9ULL))) goto mn71;
                            void * t73 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t74;
                            GM_ST(void *, (uint8_t *)&t74, (void *)(gm_s22g482qk7a40));
                            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t75 = gm_f_extend_0cs85gs(t73, t74);
                            gm_tftb1odc739t5 t76 = t75;
                            if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcu4ee17t4impf, 155, 25);
                            gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
                            goto me63;
mn71:;
                            uint8_t t79 = GM_LD(uint8_t, ((uint8_t *)&t62));
                            if (!(t79 == ((uint8_t)10ULL))) goto mn78;
                            uint8_t t81 = gm_l1;
                            if (!t81) goto ie80;
                            {
                                void * t83 = (void *)((uint8_t *)&F.l2);
                                uint8_t t84 = gm_l7;
                                gm_tftb1odc739t5 t85 = gm_f_push_t83rg2m(t83, t84);
                                gm_tftb1odc739t5 t86 = t85;
                                if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srcu4ee17t4impf, 156, 40);
                                gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
                            }
                            goto ix80;
ie80:;
                            {
                                void * t89 = (void *)((uint8_t *)&F.l2);
                                gm_b16_8 t90;
                                GM_ST(void *, (uint8_t *)&t90, (void *)(gm_s6gt8b7hsisgu));
                                GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t91 = gm_f_extend_0cs85gs(t89, t90);
                                gm_tftb1odc739t5 t92 = t91;
                                if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_srcu4ee17t4impf, 156, 62);
                                gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
                            }
ix80:;
                            goto me63;
mn78:;
                            uint8_t t95 = GM_LD(uint8_t, ((uint8_t *)&t62));
                            if (!(t95 == ((uint8_t)12ULL))) goto mn94;
                            void * t96 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t97;
                            GM_ST(void *, (uint8_t *)&t97, (void *)(gm_spchsf3taendr));
                            GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t98 = gm_f_extend_0cs85gs(t96, t97);
                            gm_tftb1odc739t5 t99 = t98;
                            if (((((uint8_t *)&t99))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t99) + 1, gm_srcu4ee17t4impf, 157, 23);
                            gm_b0_1 t100 = GM_LD(gm_b0_1, (((uint8_t *)&t99) + 1));
                            goto me63;
mn94:;
                            uint8_t t102 = GM_LD(uint8_t, ((uint8_t *)&t62));
                            if (!(t102 == ((uint8_t)13ULL))) goto mn101;
                            void * t103 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t104;
                            GM_ST(void *, (uint8_t *)&t104, (void *)(gm_s9cb6j5ok7bdk));
                            GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t105 = gm_f_extend_0cs85gs(t103, t104);
                            gm_tftb1odc739t5 t106 = t105;
                            if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 1, gm_srcu4ee17t4impf, 158, 25);
                            gm_b0_1 t107 = GM_LD(gm_b0_1, (((uint8_t *)&t106) + 1));
                            goto me63;
mn101:;
                            uint8_t t109 = GM_LD(uint8_t, ((uint8_t *)&t62));
                            if (!(t109 == ((uint8_t)92ULL))) goto mn108;
                            void * t110 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t111;
                            GM_ST(void *, (uint8_t *)&t111, (void *)(gm_s3p8gug7bvl1p));
                            GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t112 = gm_f_extend_0cs85gs(t110, t111);
                            gm_tftb1odc739t5 t113 = t112;
                            if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 1, gm_srcu4ee17t4impf, 159, 25);
                            gm_b0_1 t114 = GM_LD(gm_b0_1, (((uint8_t *)&t113) + 1));
                            goto me63;
mn108:;
                            uint8_t t117 = 0;
                            uint8_t t118 = gm_l7;
                            uint8_t t119 = (t118 <= ((uint8_t)31ULL));
                            if (!t119) {
                                uint8_t t120 = gm_l7;
                                uint8_t t121 = ((t120 == ((uint8_t)127ULL)));
                                t117 = t121;
                            } else t117 = 1;
                            if (!t117) goto ie116;
                            {
                                void * t123 = (void *)((uint8_t *)&F.l2);
                                uint8_t t124 = gm_l7;
                                gm_t9qkh0rsooc8d t125;
                                GM_ST(uint8_t, ((uint8_t *)&t125 + 0), t124);
                                gm_tftb1odc739t5 t126 = gm_f_write_5bn7867(t123, t125);
                                gm_tftb1odc739t5 t127 = t126;
                                if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 1, gm_srcu4ee17t4impf, 160, 50);
                                gm_b0_1 t128 = GM_LD(gm_b0_1, (((uint8_t *)&t127) + 1));
                            }
                            goto ix116;
ie116:;
                            {
                                void * t130 = (void *)((uint8_t *)&F.l2);
                                uint8_t t131 = gm_l7;
                                gm_tftb1odc739t5 t132 = gm_f_push_t83rg2m(t130, t131);
                                gm_tftb1odc739t5 t133 = t132;
                                if (((((uint8_t *)&t133))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t133) + 1, gm_srcu4ee17t4impf, 160, 86);
                                gm_b0_1 t134 = GM_LD(gm_b0_1, (((uint8_t *)&t133) + 1));
                            }
ix116:;
                            goto me63;
mn115:;
                            gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 153, 13);
me63:;
                        }
ix48:;
                    }
                    goto me35;
mn36:;
                    gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 143, 5);
me35:;
                }
lc24:;
                {
                    uint64_t t136 = gm_l6;
                    uint64_t r137; bool o138 = __builtin_add_overflow(t136, ((uint64_t)1ULL), &r137);
                    uint64_t t139 = r137;
                    gm_l6 = t139;
                }
            }
lb24:;
        }
    }
    {
        void * t140 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t141;
        uint8_t t143 = gm_l1;
        if (!t143) goto ie142;
        gm_b16_8 t145;
        GM_ST(void *, (uint8_t *)&t145, (void *)(gm_s8rm42n03sfss));
        GM_ST(uintptr_t, (uint8_t *)&t145 + 8, (uintptr_t)(3));
        gm_b16_8 t146 = t145;
        t141 = t146;
        goto ix142;
ie142:;
        gm_b16_8 t148;
        GM_ST(void *, (uint8_t *)&t148, (void *)(gm_sb54ui2amic32));
        GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(1));
        gm_b16_8 t149 = t148;
        t141 = t149;
ix142:;
        gm_tftb1odc739t5 t150 = gm_f_extend_0cs85gs(t140, t141);
        gm_tftb1odc739t5 t151 = t150;
        if (((((uint8_t *)&t151))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t151) + 1, gm_srcu4ee17t4impf, 164, 5);
        gm_b0_1 t152 = GM_LD(gm_b0_1, (((uint8_t *)&t151) + 1));
    }
    {
        gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t154 = t153;
        gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&t154));
        uint8_t t156 = 0;
        F.ret = t155;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_basic_string_ml_n90kq9h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_basic_string_ml_n90kq9h F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* body */
    GM_STACK_CHECK(&F, 307u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_basic_string_3q58uai((void *)NULL, cx, NULL, t2, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t3;
    }
    {
        gm_b16_8 t5 = gm_l0;
        gm_t5ogmjn0ml54c t6 = GM_CONV(gm_t5ogmjn0ml54c, gm_f_string_metrics_otdg4n0(t5));
        gm_t5ogmjn0ml54c t7 = t6;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 10));
        if (!t8) goto ie4;
        {
            uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
            uint64_t t12 = (uintptr_t)((uint64_t)3ULL);
            if (t12 > (uintptr_t)t10 || (uintptr_t)t10 > (uintptr_t)t10) gm_trap_slice(t12, (uintptr_t)t10, t10, gm_srcu4ee17t4impf, 185, 41);
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t11 + t12 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)((uintptr_t)t10 - t12));
            gm_tnpifv20irnph t14;
            GM_ST(gm_b16_8, ((uint8_t *)&t14 + 0), t13);
            gm_tabb710d9mq2e t15;
            gm_f_format_m2o7vq2((void *)&t15, (void *)NULL, cx, NULL, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t16 = t15;
            if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_srcu4ee17t4impf, 185, 16);
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
            gm_b16_8 t18 = t17;
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        gm_b16_8 t21 = gm_l1;
        uint8_t t22 = 0;
        F.ret = t21;
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
__attribute__((aligned(4))) void gm_f_check_package_hgpf1l9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_package_hgpf1l9 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* problem */
    gm_b16_8 gm_l2; /* url */
    gm_b16_8 gm_l3; /* problem */
    gm_b16_8 gm_l4; /* s */
    gm_b16_8 gm_l5; /* a */
    uint8_t gm_l6; /* hex */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint8_t gm_l9; /* c */
    uint8_t gm_l10; /* <tmp> */
    uint8_t gm_l11; /* <tmp> */
    GM_STACK_CHECK(&F, 1763u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_snqvo5bgc2fch));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(7));
        gm_th2lqbo77pvsc t6;
        gm_f_name_problem_6r4mp11((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t7 = t6;
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l1 = t8;
        {
            gm_b16_8 t10 = gm_l1;
            gm_tl572d7579hns t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t11 + 8), t10);
            gm_tl572d7579hns t12 = t11;
            uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t14 = gm_l0;
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t14) + 16));
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s2j8t1jqamft8));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(4));
        gm_th2lqbo77pvsc t17;
        gm_f_strip_prefix_b4949s8((void *)&t17, t15, t16);
        gm_th2lqbo77pvsc t18 = t17;
        gm_b16_8 t19;
        if (((((uint8_t *)&t18))[16] != 0)) {
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 0));
            t19 = t20;
        } else {
            void * t21 = gm_l0;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 16));
            t19 = t22;
        }
        gm_l2 = t19;
    }
    {
        gm_b16_8 t24 = gm_l2;
        gm_th2lqbo77pvsc t25;
        gm_f_url_problem_38kdh3a((void *)&t25, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t26 = t25;
        if (!((((uint8_t *)&t26))[16] != 0)) goto ie23;
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        gm_l3 = t27;
        {
            gm_t6j6smj7qoctu t29;
            void * t30 = gm_l0;
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t30) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t29 + 0, t31);
            gm_b16_8 t32 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t29 + 16, t32);
            gm_tabb710d9mq2e t33;
            gm_f_format_k399iof((void *)&t33, (void *)NULL, cx, NULL, &t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t34 = t33;
            if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srcu4ee17t4impf, 47, 51);
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
            gm_b16_8 t36 = t35;
            gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
            gm_tl572d7579hns t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
            ((uint8_t *)&t38)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t38 + 8), t37);
            gm_tl572d7579hns t39 = t38;
            uint8_t t40 = ((((uint8_t *)&t39))[0] != 0);
            F.ret = t38;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t42 = gm_l0;
        if (!(((((uint8_t *)t42) + 32))[16] != 0)) goto ie41;
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)t42) + 32));
        gm_l4 = t43;
        {
            gm_b16_8 t46 = gm_l4;
            uint8_t t47 = gm_f_is_subdir_sjdvkdc((void *)NULL, cx, NULL, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t48 = (uint8_t)!t47;
            if (!t48) goto ie45;
            {
                void * t50 = gm_l0;
                gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)t50) + 0));
                gm_tnpifv20irnph t52;
                GM_ST(gm_b16_8, ((uint8_t *)&t52 + 0), t51);
                gm_tabb710d9mq2e t53;
                gm_f_format_r8c6rs0((void *)&t53, (void *)NULL, cx, NULL, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t54 = t53;
                if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 8, gm_srcu4ee17t4impf, 50, 21);
                gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
                gm_b16_8 t56 = t55;
                gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                gm_tl572d7579hns t58;
                GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                ((uint8_t *)&t58)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t58 + 8), t57);
                gm_tl572d7579hns t59 = t58;
                uint8_t t60 = ((((uint8_t *)&t59))[0] != 0);
                F.ret = t58;
                goto gm_fret;
            }
            goto ix45;
ie45:;
ix45:;
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        void * t62 = gm_l0;
        if (!(((((uint8_t *)t62) + 112))[16] != 0)) goto ie61;
        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)t62) + 112));
        gm_l5 = t63;
        {
            gm_b16_8 t66 = gm_l5;
            uint8_t t67 = gm_f_is_sha256_3g83n88(t66);
            uint8_t t68 = (uint8_t)!t67;
            if (!t68) goto ie65;
            {
                void * t70 = gm_l0;
                gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)t70) + 0));
                gm_tnpifv20irnph t72;
                GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t71);
                gm_tabb710d9mq2e t73;
                gm_f_format_g5opmgl((void *)&t73, (void *)NULL, cx, NULL, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t74 = t73;
                if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 8, gm_srcu4ee17t4impf, 55, 21);
                gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t74) + 8));
                gm_b16_8 t76 = t75;
                gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                gm_tl572d7579hns t78;
                GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
                ((uint8_t *)&t78)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t78 + 8), t77);
                gm_tl572d7579hns t79 = t78;
                uint8_t t80 = ((((uint8_t *)&t79))[0] != 0);
                F.ret = t78;
                goto gm_fret;
            }
            goto ix65;
ie65:;
ix65:;
        }
        goto ix61;
ie61:;
        {
            gm_l6 = ((uint8_t)1);
        }
        {
            {
                void * t83 = gm_l0;
                gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)t83) + 96));
                gm_l7 = t84;
            }
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t86 = gm_l8;
                    uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint8_t t88 = (t86 < t87);
                    if (!t88) goto lb85;
                    {
                        uint64_t t91 = gm_l8;
                        uint64_t t92 = (uintptr_t)t91;
                        uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t94 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t92 * 1));
                        uint8_t t95 = t94;
                        uint8_t t98 = GM_LD(uint8_t, ((uint8_t *)&t95));
                        gm_l9 = t98;
                        {
                            uint8_t t101 = 0;
                            {
                                uint8_t t103 = gm_l9;
                                gm_l10 = t103;
                            }
                            uint8_t t104 = 0;
                            uint8_t t105 = gm_l10;
                            uint8_t t106 = (((uint8_t)48ULL) <= t105);
                            if (t106) {
                                uint8_t t107 = gm_l10;
                                uint8_t t108 = (t107 <= ((uint8_t)57ULL));
                                t104 = t108;
                            } else t104 = 0;
                            uint8_t t109 = t104;
                            if (!t109) {
                                {
                                    uint8_t t111 = gm_l9;
                                    gm_l11 = t111;
                                }
                                uint8_t t112 = 0;
                                uint8_t t113 = gm_l11;
                                uint8_t t114 = (((uint8_t)97ULL) <= t113);
                                if (t114) {
                                    uint8_t t115 = gm_l11;
                                    uint8_t t116 = (t115 <= ((uint8_t)102ULL));
                                    t112 = t116;
                                } else t112 = 0;
                                uint8_t t117 = t112;
                                t101 = t117;
                            } else t101 = 1;
                            uint8_t t118 = (uint8_t)!t101;
                            if (!t118) goto ie100;
                            {
                                gm_l6 = ((uint8_t)0);
                            }
                            goto ix100;
ie100:;
ix100:;
                        }
                        goto me96;
mn97:;
                        gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 59, 9);
me96:;
                    }
lc85:;
                    {
                        uint64_t t121 = gm_l8;
                        uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                        uint64_t t124 = r122;
                        gm_l8 = t124;
                    }
                }
lb85:;
            }
        }
        {
            uint8_t t126 = 0;
            uint8_t t127 = gm_l6;
            if (t127) {
                uint8_t t128 = 0;
                void * t129 = gm_l0;
                uint64_t t130 = GM_LD(uintptr_t, (((uint8_t *)t129) + 96) + 8);
                uint8_t t131 = ((t130 == ((uint64_t)40ULL)));
                if (!t131) {
                    void * t132 = gm_l0;
                    uint64_t t133 = GM_LD(uintptr_t, (((uint8_t *)t132) + 96) + 8);
                    uint8_t t134 = ((t133 == ((uint64_t)64ULL)));
                    t128 = t134;
                } else t128 = 1;
                t126 = t128;
            } else t126 = 0;
            uint8_t t135 = (uint8_t)!t126;
            if (!t135) goto ie125;
            {
                void * t137 = gm_l0;
                gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)t137) + 0));
                gm_tnpifv20irnph t139;
                GM_ST(gm_b16_8, ((uint8_t *)&t139 + 0), t138);
                gm_tabb710d9mq2e t140;
                gm_f_format_0t23jv4((void *)&t140, (void *)NULL, cx, NULL, t139);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t141 = t140;
                if (((((uint8_t *)&t141))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t141) + 8, gm_srcu4ee17t4impf, 63, 21);
                gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&t141) + 8));
                gm_b16_8 t143 = t142;
                gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                gm_tl572d7579hns t145;
                GM_ST(uint8_t, (uint8_t *)&t145 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t145 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t145 + 4, 0);
                ((uint8_t *)&t145)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t145 + 8), t144);
                gm_tl572d7579hns t146 = t145;
                uint8_t t147 = ((((uint8_t *)&t146))[0] != 0);
                F.ret = t145;
                goto gm_fret;
            }
            goto ix125;
ie125:;
ix125:;
        }
ix61:;
    }
    {
        void * t149 = gm_l0;
        gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)t149) + 136));
        uint8_t t151 = gm_f_is_sha256_3g83n88(t150);
        uint8_t t152 = (uint8_t)!t151;
        if (!t152) goto ie148;
        {
            void * t154 = gm_l0;
            gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)t154) + 0));
            gm_tnpifv20irnph t156;
            GM_ST(gm_b16_8, ((uint8_t *)&t156 + 0), t155);
            gm_tabb710d9mq2e t157;
            gm_f_format_qvc703s((void *)&t157, (void *)NULL, cx, NULL, t156);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t158 = t157;
            if (((((uint8_t *)&t158))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t158) + 8, gm_srcu4ee17t4impf, 67, 17);
            gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&t158) + 8));
            gm_b16_8 t160 = t159;
            gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
            gm_tl572d7579hns t162;
            GM_ST(uint8_t, (uint8_t *)&t162 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t162 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
            ((uint8_t *)&t162)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t162 + 8), t161);
            gm_tl572d7579hns t163 = t162;
            uint8_t t164 = ((((uint8_t *)&t163))[0] != 0);
            F.ret = t162;
            goto gm_fret;
        }
        goto ix148;
ie148:;
ix148:;
    }
    {
        gm_b0_1 t165;
        gm_tl572d7579hns t166;
        GM_ST(uint8_t, (uint8_t *)&t166 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t166 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t166 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t166 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t166 + 16, 0);
        ((uint8_t *)&t166)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t166 + 8), t165);
        gm_tl572d7579hns t167 = t166;
        uint8_t t168 = ((((uint8_t *)&t167))[0] != 0);
        F.ret = t166;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_display_version_94faa6b(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_display_version_94faa6b F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* a */
    gm_b16_8 gm_l2; /* hash */
    gm_b16_8 gm_l3; /* v */
    GM_STACK_CHECK(&F, 925u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        if (!(((((uint8_t *)t3) + 112))[16] != 0)) goto ie2;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 112));
        gm_l1 = t4;
        {
            gm_b16_8 t6 = gm_l1;
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_son0j0k0t10ro));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(7));
            gm_th2lqbo77pvsc t8;
            gm_f_strip_prefix_b4949s8((void *)&t8, t6, t7);
            gm_th2lqbo77pvsc t9 = t8;
            gm_b16_8 t10;
            if (((((uint8_t *)&t9))[16] != 0)) {
                gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 0));
                t10 = t11;
            } else {
                gm_b16_8 t12 = gm_l1;
                t10 = t12;
            }
            gm_l2 = t10;
        }
        {
            gm_b16_8 t13 = gm_l2;
            gm_b16_8 t14 = t13;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&t14) + 8);
            void * t16 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t14)));
            uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t18;
            uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t21 = (t20 < ((uint64_t)12ULL));
            if (!t21) goto ie19;
            uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t t24 = t23;
            t18 = t24;
            goto ix19;
ie19:;
            uint64_t t26 = ((uint64_t)12ULL);
            t18 = t26;
ix19:;
            uint64_t t27 = (uintptr_t)t18;
            if (t17 > t27 || t27 > (uintptr_t)t15) gm_trap_slice(t17, t27, t15, gm_srcu4ee17t4impf, 79, 41);
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t16 + t17 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t17));
            gm_tnpifv20irnph t29;
            GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t28);
            gm_tabb710d9mq2e t30;
            gm_f_format_ane7f49((void *)&t30, (void *)NULL, cx, NULL, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t31 = t30;
            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_srcu4ee17t4impf, 79, 16);
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
            gm_b16_8 t33 = t32;
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
            uint8_t t35 = 0;
            F.ret = t34;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t37 = gm_l0;
        if (!(((((uint8_t *)t37) + 56))[16] != 0)) goto ie36;
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)t37) + 56));
        gm_l3 = t38;
        {
            gm_b16_8 t40 = gm_l3;
            gm_tnpifv20irnph t41;
            GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t40);
            gm_tabb710d9mq2e t42;
            gm_f_format_hcca65h((void *)&t42, (void *)NULL, cx, NULL, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t43 = t42;
            if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcu4ee17t4impf, 81, 32);
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
            gm_b16_8 t45 = t44;
            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
            uint8_t t47 = 0;
            F.ret = t46;
            goto gm_fret;
        }
        goto ix36;
ie36:;
ix36:;
    }
    {
        gm_t6j6smj7qoctu t48;
        void * t49 = gm_l0;
        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)t49) + 80));
        GM_ST(gm_b16_8, (uint8_t *)&t48 + 0, t50);
        void * t51 = gm_l0;
        gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)t51) + 96));
        gm_b16_8 t53 = t52;
        uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&t53) + 8);
        void * t55 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t53)));
        uint64_t t56 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t57;
        void * t59 = gm_l0;
        uint64_t t60 = GM_LD(uintptr_t, (((uint8_t *)t59) + 96) + 8);
        uint8_t t61 = (t60 < ((uint64_t)12ULL));
        if (!t61) goto ie58;
        void * t63 = gm_l0;
        uint64_t t64 = GM_LD(uintptr_t, (((uint8_t *)t63) + 96) + 8);
        uint64_t t65 = t64;
        t57 = t65;
        goto ix58;
ie58:;
        uint64_t t67 = ((uint64_t)12ULL);
        t57 = t67;
ix58:;
        uint64_t t68 = (uintptr_t)t57;
        if (t56 > t68 || t68 > (uintptr_t)t54) gm_trap_slice(t56, t68, t54, gm_srcu4ee17t4impf, 82, 47);
        gm_b16_8 t69;
        GM_ST(void *, (uint8_t *)&t69, (void *)(((uint8_t *)t55 + t56 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(t68 - t56));
        GM_ST(gm_b16_8, (uint8_t *)&t48 + 16, t69);
        gm_tabb710d9mq2e t70;
        gm_f_format_kvsrtt1((void *)&t70, (void *)NULL, cx, NULL, &t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t71 = t70;
        if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 8, gm_srcu4ee17t4impf, 82, 12);
        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t71) + 8));
        gm_b16_8 t73 = t72;
        gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
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
inline __attribute__((aligned(4))) void gm_f_find_locked_83oh60j(void *gm_out, void * a0, gm_b16_8 a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_locked_83oh60j F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* url */
    gm_th2lqbo77pvsc gm_l2; /* subdir */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 1005u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 0));
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
                    gm_ts1nru0t7g7aj t16 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 168));
                    F.l5 = t16;
                    {
                        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
                        gm_b16_8 t20 = gm_l1;
                        gm_b16_8 t21 = t19;
                        gm_b16_8 t22 = t20;
                        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (!t23) goto ie18;
                        gm_th2lqbo77pvsc t24 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l5) + 32));
                        gm_th2lqbo77pvsc t25 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)a2));
                        gm_th2lqbo77pvsc t26 = t24;
                        gm_th2lqbo77pvsc t27 = t25;
                        uint8_t t28 = (gm_eq0ogca82g1f(((uint8_t *)&t26), ((uint8_t *)&t27)));
                        if (!t28) goto ie18;
                        {
                            gm_ts1nru0t7g7aj t30 = F.l5;
                            gm_tie69pa8pplgc t31;
                            GM_ST(uint8_t, (uint8_t *)&t31 + 169, 0);
                            GM_ST(uint16_t, (uint8_t *)&t31 + 170, 0);
                            GM_ST(uint32_t, (uint8_t *)&t31 + 172, 0);
                            GM_ST(gm_ts1nru0t7g7aj, ((uint8_t *)&t31), t30);
                            ((uint8_t *)&t31)[168] = 1;
                            uint8_t t32 = 0;
                            F.ret = t31;
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 91, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t34 = gm_l4;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l4 = t37;
                }
            }
lb5:;
        }
    }
    {
        gm_tie69pa8pplgc t38;
        memset(&t38, 0, sizeof t38);
        uint8_t t39 = 0;
        F.ret = t38;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tie69pa8pplgc gm_f_find_locked_83oh60j_c(void * a0, gm_b16_8 a1, gm_th2lqbo77pvsc a2) { gm_tie69pa8pplgc gm_r; gm_f_find_locked_83oh60j(&gm_r, a0, a1, &a2); return gm_r; }
inline __attribute__((aligned(4))) uint8_t gm_f_is_archive_vibcvpj(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_archive_vibcvpj F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    if ((((((uint8_t *)t1) + 112))[16] != 0)) goto mn4;
    t2 = ((uint8_t)0);
    goto me3;
mn4:;
    t2 = ((uint8_t)1);
    goto me3;
mn5:;
    gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 73, 54);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_sha256_3g83n88(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_sha256_3g83n88 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* h */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    uint8_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 278u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_son0j0k0t10ro));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_th2lqbo77pvsc t4;
        gm_f_strip_prefix_b4949s8((void *)&t4, t2, t3);
        gm_th2lqbo77pvsc t5 = t4;
        gm_b16_8 t6;
        if (((((uint8_t *)&t5))[16] != 0)) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t11 = (!(t10 == ((uint64_t)64ULL)));
        if (!t11) goto ie9;
        {
            uint8_t t13 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        {
            gm_b16_8 t15 = gm_l1;
            gm_l2 = t15;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t17 = gm_l3;
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t19 = (t17 < t18);
                if (!t19) goto lb16;
                {
                    uint64_t t22 = gm_l3;
                    uint64_t t23 = (uintptr_t)t22;
                    uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t23 * 1));
                    uint8_t t26 = t25;
                    uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)&t26));
                    gm_l4 = t29;
                    {
                        uint8_t t32 = 0;
                        {
                            uint8_t t34 = gm_l4;
                            gm_l5 = t34;
                        }
                        uint8_t t35 = 0;
                        uint8_t t36 = gm_l5;
                        uint8_t t37 = (((uint8_t)48ULL) <= t36);
                        if (t37) {
                            uint8_t t38 = gm_l5;
                            uint8_t t39 = (t38 <= ((uint8_t)57ULL));
                            t35 = t39;
                        } else t35 = 0;
                        uint8_t t40 = t35;
                        if (!t40) {
                            {
                                uint8_t t42 = gm_l4;
                                gm_l6 = t42;
                            }
                            uint8_t t43 = 0;
                            uint8_t t44 = gm_l6;
                            uint8_t t45 = (((uint8_t)97ULL) <= t44);
                            if (t45) {
                                uint8_t t46 = gm_l6;
                                uint8_t t47 = (t46 <= ((uint8_t)102ULL));
                                t43 = t47;
                            } else t43 = 0;
                            uint8_t t48 = t43;
                            t32 = t48;
                        } else t32 = 1;
                        uint8_t t49 = (uint8_t)!t32;
                        if (!t49) goto ie31;
                        {
                            uint8_t t51 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix31;
ie31:;
ix31:;
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 36, 5);
me27:;
                }
lc16:;
                {
                    uint64_t t53 = gm_l3;
                    uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                    uint64_t t56 = r54;
                    gm_l3 = t56;
                }
            }
lb16:;
        }
    }
    {
        uint8_t t57 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_option_less_72r3i7m(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_option_less_72r3i7m F;
    gm_th2lqbo77pvsc gm_l0; /* a */
    gm_th2lqbo77pvsc gm_l1; /* b */
    gm_b16_8 gm_l2; /* x */
    gm_b16_8 gm_l3; /* y */
    GM_STACK_CHECK(&F, 311u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_th2lqbo77pvsc t2 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)a0));
        gm_th2lqbo77pvsc t3 = t2;
        gm_b16_8 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            uint8_t t6;
            if (((((uint8_t *)a1))[16] != 0)) goto mn8;
            t6 = ((uint8_t)0);
            goto me7;
mn8:;
            t6 = ((uint8_t)1);
            goto me7;
mn9:;
            gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 202, 24);
me7:;
            uint8_t t10 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l2 = t4;
    }
    {
        gm_th2lqbo77pvsc t11 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)a1));
        gm_th2lqbo77pvsc t12 = t11;
        gm_b16_8 t13;
        if (((((uint8_t *)&t12))[16] != 0)) {
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 0));
            t13 = t14;
        } else {
            uint8_t t15 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l3 = t13;
    }
    {
        gm_b16_8 t16 = gm_l2;
        gm_b16_8 t17 = gm_l3;
        gm_b16_8 t18 = t16;
        gm_b16_8 t19 = t17;
        uint8_t t20 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t18), ((uint8_t *)&t19));
        uint8_t t21 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_option_less_72r3i7m_c(gm_th2lqbo77pvsc a0, gm_th2lqbo77pvsc a1) { return gm_f_option_less_72r3i7m(&a0, &a1); }
inline __attribute__((aligned(4))) uint8_t gm_f_package_less_4m46djp(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_package_less_4m46djp F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK(&F, 362u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
        void * t5 = gm_l1;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t5) + 0));
        gm_b16_8 t7 = t4;
        gm_b16_8 t8 = t6;
        uint8_t t9 = (!gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
        if (!t9) goto ie2;
        {
            void * t11 = gm_l0;
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)t11) + 0));
            void * t13 = gm_l1;
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 0));
            gm_b16_8 t15 = t12;
            gm_b16_8 t16 = t14;
            uint8_t t17 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16));
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t20 = gm_l0;
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)t20) + 16));
        void * t22 = gm_l1;
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 16));
        gm_b16_8 t24 = t21;
        gm_b16_8 t25 = t23;
        uint8_t t26 = (!gm_eq0iqiubfm7e(((uint8_t *)&t24), ((uint8_t *)&t25)));
        if (!t26) goto ie19;
        {
            void * t28 = gm_l0;
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 16));
            void * t30 = gm_l1;
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t30) + 16));
            gm_b16_8 t32 = t29;
            gm_b16_8 t33 = t31;
            uint8_t t34 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t32), ((uint8_t *)&t33));
            uint8_t t35 = 0;
            F.ret = t34;
            goto gm_fret;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t36 = gm_l0;
        void * t37 = gm_l1;
        uint8_t t38 = gm_f_option_less_72r3i7m(&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t36) + 32)), &(*(gm_th2lqbo77pvsc *)(((uint8_t *)t37) + 32)));
        uint8_t t39 = 0;
        F.ret = t38;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_package_string_rn5sliu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_package_string_rn5sliu F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* key */
    void * gm_l2; /* e */
    GM_STACK_CHECK(&F, 562u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        void * t4 = gm_f_get_9rme0gb(t2, t3);
        void * t5 = t4;
        void * t6;
        if ((GM_LD(void *, ((uint8_t *)&t5)) != 0)) {
            void * t7 = GM_LD(void *, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_b16_8 t8 = gm_l1;
            gm_tnpifv20irnph t9;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t8);
            gm_tabb710d9mq2e t10;
            gm_f_format_nlu0p4d((void *)&t10, (void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t11 = t10;
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_srcu4ee17t4impf, 242, 34);
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
            gm_b16_8 t13 = t12;
            gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
            gm_t3ujhu6qph7de t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t15 + 8), t14);
            gm_t3ujhu6qph7de t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        void * t18 = gm_l2;
        gm_th2lqbo77pvsc t19;
        gm_f_as_str_982o16v((void *)&t19, &(*(gm_tktr4rmbindgv *)(((uint8_t *)t18) + 16)));
        gm_th2lqbo77pvsc t20 = t19;
        gm_t3ujhu6qph7de t21;
        if (((((uint8_t *)&t20))[16] != 0)) {
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 0));
            gm_t3ujhu6qph7de t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
            ((uint8_t *)&t23)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t23 + 8), t22);
            t21 = t23;
        } else {
            gm_b16_8 t24 = gm_l1;
            gm_tnpifv20irnph t25;
            GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
            gm_tabb710d9mq2e t26;
            gm_f_format_nlu0p4d((void *)&t26, (void *)NULL, cx, NULL, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t27 = t26;
            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_srcu4ee17t4impf, 243, 39);
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
            gm_b16_8 t29 = t28;
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
            gm_t3ujhu6qph7de t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t31 + 8), t30);
            t21 = t31;
        }
        gm_t3ujhu6qph7de t32 = t21;
        uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
        F.ret = t21;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_parse_error_ghm43oi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_parse_error_ghm43oi F;
    gm_b16_8 gm_l0; /* text */
    gm_tob1snuii5bme gm_l1; /* e */
    uint64_t gm_l2; /* start */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* nl */
    uint64_t gm_l5; /* end */
    gm_b16_8 gm_l6; /* shown */
    gm_b16_8 gm_l7; /* num */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    GM_STACK_CHECK(&F, 1028u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        gm_l3 = ((uint64_t)1ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l3;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)a1) + 0));
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t10 = gm_l2;
                uint64_t t11 = (uintptr_t)t10;
                if (t11 > (uintptr_t)t8 || (uintptr_t)t8 > (uintptr_t)t8) gm_trap_slice(t11, (uintptr_t)t8, t8, gm_srcu4ee17t4impf, 252, 24);
                gm_b16_8 t12;
                GM_ST(void *, (uint8_t *)&t12, (void *)(((uint8_t *)t9 + t11 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)((uintptr_t)t8 - t11));
                gm_b16_8 t13;
                GM_ST(void *, (uint8_t *)&t13, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(1));
                gm_tf5srh4mp4b3i t14 = gm_f_find_nab29co(t12, t13);
                gm_tf5srh4mp4b3i t15 = t14;
                uint64_t t16;
                if (((((uint8_t *)&t15))[8] != 0)) {
                    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
                    t16 = t17;
                } else {
                    goto lb2;
                }
                gm_l4 = t16;
            }
            {
                uint64_t t18 = gm_l2;
                uint64_t t19 = gm_l4;
                uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
                if (o21) gm_trap("integer overflow in `+`", gm_srcu4ee17t4impf, 253, 18);
                uint64_t t22 = r20;
                uint64_t r23; bool o24 = __builtin_add_overflow(t18, t22, &r23);
                if (o24) gm_trap("integer overflow in `+`", gm_srcu4ee17t4impf, 253, 9);
                uint64_t t25 = r23;
                gm_l2 = t25;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                if (o28) gm_trap("integer overflow in `+`", gm_srcu4ee17t4impf, 254, 9);
                uint64_t t29 = r27;
                gm_l3 = t29;
            }
lc2:;
        }
lb2:;
    }
    {
        uint64_t t30 = gm_l2;
        gm_l5 = t30;
    }
    {
        for (;;) {
            uint64_t t32 = gm_l5;
            uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t34 = (t32 < t33);
            if (!t34) goto lb31;
            uint64_t t35 = gm_l5;
            uint64_t t36 = (uintptr_t)t35;
            uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t36 >= (uintptr_t)(t37)) gm_trap_index(t36, t37, gm_srcu4ee17t4impf, 257, 30);
            uint8_t t38 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t36 * 1));
            uint8_t t39 = (!(t38 == ((uint8_t)10ULL)));
            if (!t39) goto lb31;
            {
                uint64_t t42 = gm_l5;
                uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                if (o44) gm_trap("integer overflow in `+`", gm_srcu4ee17t4impf, 257, 54);
                uint64_t t45 = r43;
                gm_l5 = t45;
            }
lc31:;
        }
lb31:;
    }
    {
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t47 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t48 = gm_l2;
        uint64_t t49 = (uintptr_t)t48;
        uint64_t t50 = gm_l5;
        uint64_t t51 = (uintptr_t)t50;
        if (t49 > t51 || t51 > (uintptr_t)t46) gm_trap_slice(t49, t51, t46, gm_srcu4ee17t4impf, 258, 14);
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t47 + t49 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(t51 - t49));
        gm_l6 = t52;
    }
    {
        uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)a1) + 0));
        gm_t4ce0jd3tdro4 t54;
        GM_ST(uint64_t, ((uint8_t *)&t54 + 0), t53);
        gm_tabb710d9mq2e t55;
        gm_f_format_sa1940l((void *)&t55, (void *)NULL, cx, NULL, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t56 = t55;
        if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_srcu4ee17t4impf, 259, 12);
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
        gm_l7 = t57;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t58 = (void *)((uint8_t *)&F.l8);
        uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)a1) + 0));
        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
        gm_t2lqu8jsj511d t61;
        GM_ST(uint64_t, ((uint8_t *)&t61 + 0), t59);
        GM_ST(uint64_t, ((uint8_t *)&t61 + 8), t60);
        gm_tftb1odc739t5 t62 = gm_f_write_i42q2qm(t58, t61);
        gm_tftb1odc739t5 t63 = t62;
        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srcu4ee17t4impf, 261, 5);
        gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
    }
    {
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            gm_l9 = t66;
        }
        {
            for (;;) {
                uint64_t t68 = gm_l10;
                uint64_t t69 = gm_l9;
                uint8_t t70 = (t68 < t69);
                if (!t70) goto lb67;
                {
                    uint64_t t73 = gm_l10;
                    uint64_t t74 = t73;
                    {
                        void * t78 = (void *)((uint8_t *)&F.l8);
                        gm_tftb1odc739t5 t79 = gm_f_push_t83rg2m(t78, ((uint8_t)32ULL));
                        gm_tftb1odc739t5 t80 = t79;
                        if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_srcu4ee17t4impf, 262, 27);
                        gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
                    }
                    goto me75;
mn76:;
                    gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 262, 5);
me75:;
                }
lc67:;
                {
                    uint64_t t83 = gm_l10;
                    uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                    uint64_t t86 = r84;
                    gm_l10 = t86;
                }
            }
lb67:;
        }
    }
    {
        void * t87 = (void *)((uint8_t *)&F.l8);
        gm_t6j6smj7qoctu t88;
        gm_b16_8 t89 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t88 + 0, t89);
        gm_b16_8 t90 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t88 + 16, t90);
        gm_tftb1odc739t5 t91 = gm_f_write_g6r6ugc(t87, &t88);
        gm_tftb1odc739t5 t92 = t91;
        if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_srcu4ee17t4impf, 263, 5);
        gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
    }
    {
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            uint64_t t95 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            gm_l11 = t95;
        }
        {
            for (;;) {
                uint64_t t97 = gm_l12;
                uint64_t t98 = gm_l11;
                uint8_t t99 = (t97 < t98);
                if (!t99) goto lb96;
                {
                    uint64_t t102 = gm_l12;
                    uint64_t t103 = t102;
                    {
                        void * t107 = (void *)((uint8_t *)&F.l8);
                        gm_tftb1odc739t5 t108 = gm_f_push_t83rg2m(t107, ((uint8_t)32ULL));
                        gm_tftb1odc739t5 t109 = t108;
                        if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 1, gm_srcu4ee17t4impf, 264, 27);
                        gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 1));
                    }
                    goto me104;
mn105:;
                    gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 264, 5);
me104:;
                }
lc96:;
                {
                    uint64_t t112 = gm_l12;
                    uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                    uint64_t t115 = r113;
                    gm_l12 = t115;
                }
            }
lb96:;
        }
    }
    {
        void * t116 = (void *)((uint8_t *)&F.l8);
        gm_b16_8 t117;
        GM_ST(void *, (uint8_t *)&t117, (void *)(gm_shde4bsckk9mt));
        GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(3));
        gm_tftb1odc739t5 t118 = gm_f_extend_0cs85gs(t116, t117);
        gm_tftb1odc739t5 t119 = t118;
        if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 1, gm_srcu4ee17t4impf, 265, 5);
        gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 1));
    }
    {
        {
            gm_l14 = ((uint64_t)1ULL);
        }
        {
            uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
            gm_l13 = t122;
        }
        {
            for (;;) {
                uint64_t t124 = gm_l14;
                uint64_t t125 = gm_l13;
                uint8_t t126 = (t124 < t125);
                if (!t126) goto lb123;
                {
                    uint64_t t129 = gm_l14;
                    uint64_t t130 = t129;
                    {
                        void * t134 = (void *)((uint8_t *)&F.l8);
                        gm_tftb1odc739t5 t135 = gm_f_push_t83rg2m(t134, ((uint8_t)32ULL));
                        gm_tftb1odc739t5 t136 = t135;
                        if (((((uint8_t *)&t136))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t136) + 1, gm_srcu4ee17t4impf, 266, 25);
                        gm_b0_1 t137 = GM_LD(gm_b0_1, (((uint8_t *)&t136) + 1));
                    }
                    goto me131;
mn132:;
                    gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 266, 5);
me131:;
                }
lc123:;
                {
                    uint64_t t139 = gm_l14;
                    uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                    uint64_t t142 = r140;
                    gm_l14 = t142;
                }
            }
lb123:;
        }
    }
    {
        void * t143 = (void *)((uint8_t *)&F.l8);
        gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
        gm_tnpifv20irnph t145;
        GM_ST(gm_b16_8, ((uint8_t *)&t145 + 0), t144);
        gm_tftb1odc739t5 t146 = gm_f_write_6s6h7ts(t143, t145);
        gm_tftb1odc739t5 t147 = t146;
        if (((((uint8_t *)&t147))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t147) + 1, gm_srcu4ee17t4impf, 267, 5);
        gm_b0_1 t148 = GM_LD(gm_b0_1, (((uint8_t *)&t147) + 1));
    }
    {
        gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t150 = t149;
        gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
        uint8_t t152 = 0;
        F.ret = t151;
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
__attribute__((aligned(4))) void gm_f_parse_lock_87bbbka(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_lock_87bbbka F;
    gm_b16_8 gm_l0; /* text */
    gm_tob1snuii5bme gm_l1; /* e */
    void * gm_l2; /* doc */
    void * gm_l4; /* ver */
    int64_t gm_l5; /* v */
    void * gm_l6; /* pkgs */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    void * gm_l11; /* t */
    gm_b16_8 gm_l12; /* source */
    gm_th2lqbo77pvsc gm_l13; /* archive */
    void * gm_l14; /* a */
    gm_b16_8 gm_l15; /* url */
    gm_b16_8 gm_l16; /* u */
    gm_b16_8 gm_l17; /* reference */
    gm_b16_8 gm_l18; /* rev */
    void * gm_l20; /* d */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_b16_8 gm_l25; /* s */
    gm_th2lqbo77pvsc gm_l26; /* subdir */
    void * gm_l27; /* s */
    gm_th2lqbo77pvsc gm_l28; /* version */
    void * gm_l29; /* s */
    gm_b16_8 gm_l30; /* name */
    gm_b16_8 gm_l31; /* checksum */
    GM_STACK_CHECK(&F, 4173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_trssmtuv8jr0v t3;
        gm_f_parse_toml_gjtmf79((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trssmtuv8jr0v t4 = t3;
        void * t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            void * t6 = GM_LD(void *, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_tob1snuii5bme t7 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            gm_b16_8 t9 = gm_l0;
            gm_b16_8 t10 = gm_f_parse_error_ghm43oi((void *)NULL, cx, NULL, t9, &(*(gm_tob1snuii5bme *)((uint8_t *)&gm_l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t62efos6ssciq t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 40, 0);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t11 + 8), t10);
            gm_t62efos6ssciq t12 = t11;
            uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
            F.ret = t11;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        gm_f_list_pfs7ocg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 0)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t14 = gm_l2;
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sla3alt5340mf));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(7));
        void * t16 = gm_f_get_9rme0gb(t14, t15);
        gm_l4 = t16;
    }
    {
        void * t18 = gm_l4;
        void * t19 = t18;
        uint8_t t20;
        if ((GM_LD(void *, ((uint8_t *)&t19)) != 0)) goto mn22;
        t20 = ((uint8_t)1);
        goto me21;
mn22:;
        t20 = ((uint8_t)0);
        goto me21;
mn23:;
        gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 279, 8);
me21:;
        if (!t20) goto ie17;
        {
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_srehvgd1sod2m));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(17));
            gm_t62efos6ssciq t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t26 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t26 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t26 + 40, 0);
            ((uint8_t *)&t26)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t26 + 8), t25);
            gm_t62efos6ssciq t27 = t26;
            uint8_t t28 = ((((uint8_t *)&t27))[0] != 0);
            F.ret = t26;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        void * t30 = gm_l4;
        void * t31 = t30;
        if (!(GM_LD(void *, ((uint8_t *)&t31)) != 0)) gm_trap("unwrap of null", gm_srcu4ee17t4impf, 280, 16);
        void * t32 = GM_LD(void *, ((uint8_t *)&t31));
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t32) + 16) + 0));
        if (t33 != ((uint8_t)1ULL)) goto ie29;
        int64_t t34 = GM_LD(int64_t, ((((uint8_t *)t32) + 16) + 8));
        gm_l5 = t34;
        {
            int64_t t37 = gm_l5;
            uint8_t t38 = (!(t37 == ((int64_t)1LL)));
            if (!t38) goto ie36;
            {
                int64_t t40 = gm_l5;
                gm_tm98ap4112hee t41;
                GM_ST(int64_t, ((uint8_t *)&t41 + 0), t40);
                gm_tabb710d9mq2e t42;
                gm_f_format_lk5ln6l((void *)&t42, (void *)NULL, cx, NULL, t41);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t43 = t42;
                if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcu4ee17t4impf, 281, 29);
                gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
                gm_b16_8 t45 = t44;
                gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                gm_t62efos6ssciq t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t47 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t47 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t47 + 40, 0);
                ((uint8_t *)&t47)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t47 + 8), t46);
                gm_t62efos6ssciq t48 = t47;
                uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
                F.ret = t47;
                goto gm_fret;
            }
            goto ix36;
ie36:;
ix36:;
        }
        {
            void * t50 = gm_l2;
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(gm_snqvo5bgc2fch));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(7));
            void * t52 = gm_f_get_9rme0gb(t50, t51);
            void * t53 = t52;
            void * t54;
            if ((GM_LD(void *, ((uint8_t *)&t53)) != 0)) {
                void * t55 = GM_LD(void *, (((uint8_t *)&t53) + 0));
                t54 = t55;
            } else {
                gm_tin97tt4ud0s1 t56 = F.l3;
                gm_t62efos6ssciq t57;
                GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                ((uint8_t *)&t57)[0] = 0;
                GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&t57 + 8), t56);
                gm_t62efos6ssciq t58 = t57;
                uint8_t t59 = ((((uint8_t *)&t58))[0] != 0);
                F.ret = t57;
                goto gm_fret;
            }
            gm_l6 = t54;
        }
        {
            void * t61 = gm_l6;
            uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t61) + 16) + 0));
            if (t62 != ((uint8_t)3ULL)) goto ie60;
            gm_trqfvmjtt4sij t63 = GM_LD(gm_trqfvmjtt4sij, ((((uint8_t *)t61) + 16) + 8));
            F.l7 = t63;
            {
                {
                    gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                    gm_l8 = t66;
                }
                {
                    gm_l9 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t68 = gm_l9;
                        uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        uint8_t t70 = (t68 < t69);
                        if (!t70) goto lb67;
                        {
                            uint64_t t73 = gm_l9;
                            uint64_t t74 = (uintptr_t)t73;
                            uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                            gm_tktr4rmbindgv t78 = GM_LD(gm_tktr4rmbindgv, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t74 * 48));
                            F.l10 = t78;
                            {
                                void * t80 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)((uint8_t *)&F.l10)));
                                void * t81 = t80;
                                void * t82;
                                if ((GM_LD(void *, ((uint8_t *)&t81)) != 0)) {
                                    void * t83 = GM_LD(void *, (((uint8_t *)&t81) + 0));
                                    t82 = t83;
                                } else {
                                    gm_b16_8 t84;
                                    GM_ST(void *, (uint8_t *)&t84, (void *)(gm_skrj4540qohca));
                                    GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(36));
                                    gm_t62efos6ssciq t85;
                                    GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t85 + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t85 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t85 + 40, 0);
                                    ((uint8_t *)&t85)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t85 + 8), t84);
                                    gm_t62efos6ssciq t86 = t85;
                                    uint8_t t87 = ((((uint8_t *)&t86))[0] != 0);
                                    F.ret = t85;
                                    goto gm_fret;
                                }
                                gm_l11 = t82;
                            }
                            {
                                void * t88 = gm_l11;
                                gm_b16_8 t89;
                                GM_ST(void *, (uint8_t *)&t89, (void *)(gm_smr3sbhcj1goc));
                                GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(6));
                                gm_t3ujhu6qph7de t90;
                                gm_f_package_string_rn5sliu((void *)&t90, (void *)NULL, cx, NULL, t88, t89);
                                if (gm_unw.kind) {
                                    goto gm_ll71;
                                }
                                gm_t3ujhu6qph7de t91 = t90;
                                if (((((uint8_t *)&t91))[0] != 0)) {
                                    gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 8));
                                    gm_t62efos6ssciq t93;
                                    GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t93 + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t93 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t93 + 40, 0);
                                    ((uint8_t *)&t93)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t93 + 8), t92);
                                    F.ret = t93;
                                    goto gm_fret;
                                }
                                gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 8));
                                gm_l12 = t94;
                            }
                            {
                                gm_th2lqbo77pvsc t95;
                                memset(&t95, 0, sizeof t95);
                                gm_l13 = t95;
                            }
                            {
                                void * t97 = gm_l11;
                                gm_b16_8 t98;
                                GM_ST(void *, (uint8_t *)&t98, (void *)(gm_sllgtfi45c44a));
                                GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(7));
                                void * t99 = gm_f_get_9rme0gb(t97, t98);
                                void * t100 = t99;
                                if (!(GM_LD(void *, ((uint8_t *)&t100)) != 0)) goto ie96;
                                void * t101 = GM_LD(void *, ((uint8_t *)&t100));
                                gm_l14 = t101;
                                {
                                    void * t103 = gm_l14;
                                    gm_f_as_str_982o16v((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l13)), &(*(gm_tktr4rmbindgv *)(((uint8_t *)t103) + 16)));
                                }
                                goto ix96;
ie96:;
ix96:;
                            }
                            {
                                gm_b16_8 t104;
                                GM_ST(void *, (uint8_t *)&t104, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(0));
                                gm_l15 = t104;
                            }
                            {
                                gm_b16_8 t106 = gm_l12;
                                gm_b16_8 t107;
                                GM_ST(void *, (uint8_t *)&t107, (void *)(gm_supi6mhbcvtna));
                                GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(4));
                                gm_th2lqbo77pvsc t108;
                                gm_f_strip_prefix_b4949s8((void *)&t108, t106, t107);
                                gm_th2lqbo77pvsc t109 = t108;
                                if (!((((uint8_t *)&t109))[16] != 0)) goto ie105;
                                gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
                                gm_l16 = t110;
                                {
                                    gm_b16_8 t112 = gm_l16;
                                    gm_l15 = t112;
                                }
                                goto ix105;
ie105:;
                                gm_b16_8 t114 = gm_l12;
                                gm_b16_8 t115;
                                GM_ST(void *, (uint8_t *)&t115, (void *)(gm_s2j8t1jqamft8));
                                GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(4));
                                uint8_t t116 = gm_f_starts_with_6vvt01e(t114, t115);
                                if (!t116) goto ie113;
                                uint8_t t117;
                                if (((((uint8_t *)&gm_l13))[16] != 0)) goto mn119;
                                t117 = ((uint8_t)0);
                                goto me118;
mn119:;
                                t117 = ((uint8_t)1);
                                goto me118;
mn120:;
                                gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 292, 50);
me118:;
                                if (!t117) goto ie113;
                                {
                                    gm_b16_8 t122 = gm_l12;
                                    gm_l15 = t122;
                                }
                                goto ix113;
ie113:;
                                {
                                    gm_b16_8 t124 = gm_l12;
                                    gm_tnpifv20irnph t125;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t125 + 0), t124);
                                    gm_tabb710d9mq2e t126;
                                    gm_f_format_hum25d9((void *)&t126, (void *)NULL, cx, NULL, t125);
                                    if (gm_unw.kind) {
                                        goto gm_ll71;
                                    }
                                    gm_tabb710d9mq2e t127 = t126;
                                    if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 8, gm_srcu4ee17t4impf, 295, 21);
                                    gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&t127) + 8));
                                    gm_b16_8 t129 = t128;
                                    gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
                                    gm_t62efos6ssciq t131;
                                    GM_ST(uint8_t, (uint8_t *)&t131 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t131 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t131 + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t131 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t131 + 40, 0);
                                    ((uint8_t *)&t131)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t131 + 8), t130);
                                    gm_t62efos6ssciq t132 = t131;
                                    uint8_t t133 = ((((uint8_t *)&t132))[0] != 0);
                                    F.ret = t131;
                                    goto gm_fret;
                                }
ix113:;
ix105:;
                            }
                            {
                                gm_b16_8 t134;
                                GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(0));
                                gm_l17 = t134;
                            }
                            {
                                gm_b16_8 t135;
                                GM_ST(void *, (uint8_t *)&t135, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(0));
                                gm_l18 = t135;
                            }
                            {
                                uint8_t t137;
                                if (((((uint8_t *)&gm_l13))[16] != 0)) goto mn139;
                                t137 = ((uint8_t)1);
                                goto me138;
mn139:;
                                t137 = ((uint8_t)0);
                                goto me138;
mn140:;
                                gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 299, 12);
me138:;
                                if (!t137) goto ie136;
                                {
                                    void * t142 = gm_l11;
                                    gm_b16_8 t143;
                                    GM_ST(void *, (uint8_t *)&t143, (void *)(gm_sjiaitbr3bp01));
                                    GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(9));
                                    gm_t3ujhu6qph7de t144;
                                    gm_f_package_string_rn5sliu((void *)&t144, (void *)NULL, cx, NULL, t142, t143);
                                    if (gm_unw.kind) {
                                        goto gm_ll71;
                                    }
                                    gm_t3ujhu6qph7de t145 = t144;
                                    if (((((uint8_t *)&t145))[0] != 0)) {
                                        gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&t145) + 8));
                                        gm_t62efos6ssciq t147;
                                        GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t147 + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t147 + 32, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t147 + 40, 0);
                                        ((uint8_t *)&t147)[0] = 1;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t147 + 8), t146);
                                        F.ret = t147;
                                        goto gm_fret;
                                    }
                                    gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)&t145) + 8));
                                    gm_l17 = t148;
                                }
                                {
                                    void * t149 = gm_l11;
                                    gm_b16_8 t150;
                                    GM_ST(void *, (uint8_t *)&t150, (void *)(gm_sf9ilivr4c86m));
                                    GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(3));
                                    gm_t3ujhu6qph7de t151;
                                    gm_f_package_string_rn5sliu((void *)&t151, (void *)NULL, cx, NULL, t149, t150);
                                    if (gm_unw.kind) {
                                        goto gm_ll71;
                                    }
                                    gm_t3ujhu6qph7de t152 = t151;
                                    if (((((uint8_t *)&t152))[0] != 0)) {
                                        gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&t152) + 8));
                                        gm_t62efos6ssciq t154;
                                        GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t154 + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t154 + 32, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t154 + 40, 0);
                                        ((uint8_t *)&t154)[0] = 1;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t154 + 8), t153);
                                        F.ret = t154;
                                        goto gm_fret;
                                    }
                                    gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&t152) + 8));
                                    gm_l18 = t155;
                                }
                                goto ix136;
ie136:;
ix136:;
                            }
                            {
                                gm_f_list_epvducg((void *)&F.l19, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll71;
                                }
                            }
                            {
                                void * t157 = gm_l11;
                                gm_b16_8 t158;
                                GM_ST(void *, (uint8_t *)&t158, (void *)(gm_skof48dte1s1o));
                                GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(12));
                                void * t159 = gm_f_get_9rme0gb(t157, t158);
                                void * t160 = t159;
                                if (!(GM_LD(void *, ((uint8_t *)&t160)) != 0)) goto ie156;
                                void * t161 = GM_LD(void *, ((uint8_t *)&t160));
                                gm_l20 = t161;
                                {
                                    void * t164 = gm_l20;
                                    uint8_t t165 = GM_LD(uint8_t, ((((uint8_t *)t164) + 16) + 0));
                                    if (t165 != ((uint8_t)3ULL)) goto ie163;
                                    gm_trqfvmjtt4sij t166 = GM_LD(gm_trqfvmjtt4sij, ((((uint8_t *)t164) + 16) + 8));
                                    F.l21 = t166;
                                    {
                                        {
                                            gm_b16_8 t169 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
                                            gm_l22 = t169;
                                        }
                                        {
                                            gm_l23 = ((uint64_t)0ULL);
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t171 = gm_l23;
                                                uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                                uint8_t t173 = (t171 < t172);
                                                if (!t173) goto lb170;
                                                {
                                                    uint64_t t176 = gm_l23;
                                                    uint64_t t177 = (uintptr_t)t176;
                                                    uint64_t t178 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                                    gm_tktr4rmbindgv t181 = GM_LD(gm_tktr4rmbindgv, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t177 * 48));
                                                    F.l24 = t181;
                                                    {
                                                        gm_th2lqbo77pvsc t183;
                                                        gm_f_as_str_982o16v((void *)&t183, &(*(gm_tktr4rmbindgv *)((uint8_t *)&F.l24)));
                                                        gm_th2lqbo77pvsc t184 = t183;
                                                        gm_b16_8 t185;
                                                        if (((((uint8_t *)&t184))[16] != 0)) {
                                                            gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&t184) + 0));
                                                            t185 = t186;
                                                        } else {
                                                            gm_b16_8 t187;
                                                            GM_ST(void *, (uint8_t *)&t187, (void *)(gm_s1b0nuksrh572));
                                                            GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(28));
                                                            gm_t62efos6ssciq t188;
                                                            GM_ST(uint8_t, (uint8_t *)&t188 + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t188 + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t188 + 4, 0);
                                                            GM_ST(uint64_t, (uint8_t *)&t188 + 24, 0);
                                                            GM_ST(uint64_t, (uint8_t *)&t188 + 32, 0);
                                                            GM_ST(uint64_t, (uint8_t *)&t188 + 40, 0);
                                                            ((uint8_t *)&t188)[0] = 1;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t188 + 8), t187);
                                                            gm_t62efos6ssciq t189 = t188;
                                                            uint8_t t190 = ((((uint8_t *)&t189))[0] != 0);
                                                            F.ret = t188;
                                                            goto gm_fret;
                                                        }
                                                        gm_l25 = t185;
                                                    }
                                                    {
                                                        void * t191 = (void *)((uint8_t *)&F.l19);
                                                        gm_b16_8 t192 = gm_l25;
                                                        gm_tftb1odc739t5 t193 = gm_f_push_3ef746b(t191, t192);
                                                        gm_tftb1odc739t5 t194 = t193;
                                                        if (((((uint8_t *)&t194))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t194) + 1, gm_srcu4ee17t4impf, 308, 17);
                                                        gm_b0_1 t195 = GM_LD(gm_b0_1, (((uint8_t *)&t194) + 1));
                                                    }
                                                    goto me179;
mn180:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 306, 13);
me179:;
                                                }
lc170:;
                                                {
                                                    uint64_t t197 = gm_l23;
                                                    uint64_t r198; bool o199 = __builtin_add_overflow(t197, ((uint64_t)1ULL), &r198);
                                                    uint64_t t200 = r198;
                                                    gm_l23 = t200;
                                                }
                                            }
lb170:;
                                        }
                                    }
                                    goto ix163;
ie163:;
                                    {
                                        gm_b16_8 t202;
                                        GM_ST(void *, (uint8_t *)&t202, (void *)(gm_sso1h04lhm0cu));
                                        GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(31));
                                        gm_t62efos6ssciq t203;
                                        GM_ST(uint8_t, (uint8_t *)&t203 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t203 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t203 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t203 + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t203 + 32, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t203 + 40, 0);
                                        ((uint8_t *)&t203)[0] = 1;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t203 + 8), t202);
                                        gm_t62efos6ssciq t204 = t203;
                                        uint8_t t205 = ((((uint8_t *)&t204))[0] != 0);
                                        F.ret = t203;
                                        goto gm_fret;
                                    }
ix163:;
                                }
                                goto ix156;
ie156:;
ix156:;
                            }
                            {
                                gm_th2lqbo77pvsc t206;
                                memset(&t206, 0, sizeof t206);
                                gm_l26 = t206;
                            }
                            {
                                void * t208 = gm_l11;
                                gm_b16_8 t209;
                                GM_ST(void *, (uint8_t *)&t209, (void *)(gm_sfvspsnv23gbt));
                                GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(6));
                                void * t210 = gm_f_get_9rme0gb(t208, t209);
                                void * t211 = t210;
                                if (!(GM_LD(void *, ((uint8_t *)&t211)) != 0)) goto ie207;
                                void * t212 = GM_LD(void *, ((uint8_t *)&t211));
                                gm_l27 = t212;
                                {
                                    void * t214 = gm_l27;
                                    gm_f_as_str_982o16v((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l26)), &(*(gm_tktr4rmbindgv *)(((uint8_t *)t214) + 16)));
                                }
                                goto ix207;
ie207:;
ix207:;
                            }
                            {
                                gm_th2lqbo77pvsc t215;
                                memset(&t215, 0, sizeof t215);
                                gm_l28 = t215;
                            }
                            {
                                void * t217 = gm_l11;
                                gm_b16_8 t218;
                                GM_ST(void *, (uint8_t *)&t218, (void *)(gm_sla3alt5340mf));
                                GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(7));
                                void * t219 = gm_f_get_9rme0gb(t217, t218);
                                void * t220 = t219;
                                if (!(GM_LD(void *, ((uint8_t *)&t220)) != 0)) goto ie216;
                                void * t221 = GM_LD(void *, ((uint8_t *)&t220));
                                gm_l29 = t221;
                                {
                                    void * t223 = gm_l29;
                                    gm_f_as_str_982o16v((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l28)), &(*(gm_tktr4rmbindgv *)(((uint8_t *)t223) + 16)));
                                }
                                goto ix216;
ie216:;
ix216:;
                            }
                            {
                                void * t224 = gm_l11;
                                gm_b16_8 t225;
                                GM_ST(void *, (uint8_t *)&t225, (void *)(gm_sius9pki5u4e3));
                                GM_ST(uintptr_t, (uint8_t *)&t225 + 8, (uintptr_t)(4));
                                gm_t3ujhu6qph7de t226;
                                gm_f_package_string_rn5sliu((void *)&t226, (void *)NULL, cx, NULL, t224, t225);
                                if (gm_unw.kind) {
                                    goto gm_ll71;
                                }
                                gm_t3ujhu6qph7de t227 = t226;
                                if (((((uint8_t *)&t227))[0] != 0)) {
                                    gm_b16_8 t228 = GM_LD(gm_b16_8, (((uint8_t *)&t227) + 8));
                                    gm_t62efos6ssciq t229;
                                    GM_ST(uint8_t, (uint8_t *)&t229 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t229 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t229 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t229 + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t229 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t229 + 40, 0);
                                    ((uint8_t *)&t229)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t229 + 8), t228);
                                    F.ret = t229;
                                    goto gm_fret;
                                }
                                gm_b16_8 t230 = GM_LD(gm_b16_8, (((uint8_t *)&t227) + 8));
                                gm_l30 = t230;
                            }
                            {
                                void * t231 = gm_l11;
                                gm_b16_8 t232;
                                GM_ST(void *, (uint8_t *)&t232, (void *)(gm_ssou2pjir6lie));
                                GM_ST(uintptr_t, (uint8_t *)&t232 + 8, (uintptr_t)(8));
                                gm_t3ujhu6qph7de t233;
                                gm_f_package_string_rn5sliu((void *)&t233, (void *)NULL, cx, NULL, t231, t232);
                                if (gm_unw.kind) {
                                    goto gm_ll71;
                                }
                                gm_t3ujhu6qph7de t234 = t233;
                                if (((((uint8_t *)&t234))[0] != 0)) {
                                    gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&t234) + 8));
                                    gm_t62efos6ssciq t236;
                                    GM_ST(uint8_t, (uint8_t *)&t236 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t236 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t236 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t236 + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t236 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t236 + 40, 0);
                                    ((uint8_t *)&t236)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t236 + 8), t235);
                                    F.ret = t236;
                                    goto gm_fret;
                                }
                                gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)&t234) + 8));
                                gm_l31 = t237;
                            }
                            {
                                gm_b16_8 t238 = gm_l30;
                                GM_ST(gm_b16_8, (uint8_t *)&F.l32 + 0, t238);
                                gm_b16_8 t239 = gm_l15;
                                GM_ST(gm_b16_8, (uint8_t *)&F.l32 + 16, t239);
                                memcpy((uint8_t *)&F.l32 + 32, ((uint8_t *)&gm_l26), 24);
                                memcpy((uint8_t *)&F.l32 + 56, ((uint8_t *)&gm_l28), 24);
                                gm_b16_8 t240 = gm_l17;
                                GM_ST(gm_b16_8, (uint8_t *)&F.l32 + 80, t240);
                                gm_b16_8 t241 = gm_l18;
                                GM_ST(gm_b16_8, (uint8_t *)&F.l32 + 96, t241);
                                memcpy((uint8_t *)&F.l32 + 112, ((uint8_t *)&gm_l13), 24);
                                gm_b16_8 t242 = gm_l31;
                                GM_ST(gm_b16_8, (uint8_t *)&F.l32 + 136, t242);
                                gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                                gm_b16_8 t244 = t243;
                                gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)&t244));
                                GM_ST(gm_b16_8, (uint8_t *)&F.l32 + 152, t245);
                            }
                            {
                                void * t246 = (void *)((uint8_t *)&F.l32);
                                gm_tl572d7579hns t247;
                                gm_f_check_package_hgpf1l9((void *)&t247, (void *)NULL, cx, NULL, t246);
                                if (gm_unw.kind) {
                                    goto gm_ll71;
                                }
                                gm_tl572d7579hns t248 = t247;
                                if (((((uint8_t *)&t248))[0] != 0)) {
                                    gm_b16_8 t249 = GM_LD(gm_b16_8, (((uint8_t *)&t248) + 8));
                                    gm_t62efos6ssciq t250;
                                    GM_ST(uint8_t, (uint8_t *)&t250 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t250 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t250 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t250 + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t250 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t250 + 40, 0);
                                    ((uint8_t *)&t250)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t250 + 8), t249);
                                    F.ret = t250;
                                    goto gm_fret;
                                }
                                gm_b0_1 t251 = GM_LD(gm_b0_1, (((uint8_t *)&t248) + 8));
                            }
                            {
                                void * t252 = (void *)(((uint8_t *)&F.l3) + 0);
                                gm_tftb1odc739t5 t253 = gm_f_push_1htqqaf(t252, &(*(gm_ts1nru0t7g7aj *)((uint8_t *)&F.l32)));
                                gm_tftb1odc739t5 t254 = t253;
                                if (((((uint8_t *)&t254))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t254) + 1, gm_srcu4ee17t4impf, 329, 9);
                                gm_b0_1 t255 = GM_LD(gm_b0_1, (((uint8_t *)&t254) + 1));
                            }
                            goto me76;
mn77:;
                            gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 284, 5);
me76:;
                        }
lc67:;
                        {
                            uint64_t t257 = gm_l9;
                            uint64_t r258; bool o259 = __builtin_add_overflow(t257, ((uint64_t)1ULL), &r258);
                            uint64_t t260 = r258;
                            gm_l9 = t260;
                        }
                    }
gm_ll71:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb67; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc67; }
                    }
                    goto gm_lad;
lb67:;
                }
            }
            {
                gm_tin97tt4ud0s1 t261 = F.l3;
                gm_t62efos6ssciq t262;
                GM_ST(uint8_t, (uint8_t *)&t262 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t262 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t262 + 4, 0);
                ((uint8_t *)&t262)[0] = 0;
                GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&t262 + 8), t261);
                gm_t62efos6ssciq t263 = t262;
                uint8_t t264 = ((((uint8_t *)&t263))[0] != 0);
                F.ret = t262;
                goto gm_fret;
            }
            goto ix60;
ie60:;
            {
                gm_b16_8 t266;
                GM_ST(void *, (uint8_t *)&t266, (void *)(gm_skrj4540qohca));
                GM_ST(uintptr_t, (uint8_t *)&t266 + 8, (uintptr_t)(36));
                gm_t62efos6ssciq t267;
                GM_ST(uint8_t, (uint8_t *)&t267 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t267 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t267 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t267 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t267 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t267 + 40, 0);
                ((uint8_t *)&t267)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t267 + 8), t266);
                gm_t62efos6ssciq t268 = t267;
                uint8_t t269 = ((((uint8_t *)&t268))[0] != 0);
                F.ret = t267;
                goto gm_fret;
            }
ix60:;
        }
        goto ix29;
ie29:;
        {
            gm_b16_8 t271;
            GM_ST(void *, (uint8_t *)&t271, (void *)(gm_srehvgd1sod2m));
            GM_ST(uintptr_t, (uint8_t *)&t271 + 8, (uintptr_t)(17));
            gm_t62efos6ssciq t272;
            GM_ST(uint8_t, (uint8_t *)&t272 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t272 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t272 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t272 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t272 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t272 + 40, 0);
            ((uint8_t *)&t272)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t272 + 8), t271);
            gm_t62efos6ssciq t273 = t272;
            uint8_t t274 = ((((uint8_t *)&t273))[0] != 0);
            F.ret = t272;
            goto gm_fret;
        }
ix29:;
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
__attribute__((aligned(4))) void gm_f_read_lock_8pmv1su(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_read_lock_8pmv1su F;
    gm_b16_8 gm_l0; /* path */
    gm_tj5jn79kq9pnm gm_l1; /* e */
    gm_b16_8 gm_l2; /* data */
    gm_b16_8 gm_l3; /* e */
    GM_STACK_CHECK(&F, 1619u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_tgvh15qshf643 t3;
        gm_f_read_all_r9rh9qd((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgvh15qshf643 t4 = t3;
        gm_b16_8 t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_tj5jn79kq9pnm t7 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            {
                gm_tj5jn79kq9pnm t10 = gm_l1;
                gm_tj5jn79kq9pnm t11 = t10;
                gm_toe5fmr3vvf4d t12;
                GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
                gm_toe5fmr3vvf4d t13 = t12;
                uint8_t t14 = (((GM_LD(uint8_t, ((uint8_t *)&t11) + 0) == ((uint8_t)0ULL)) && gm_eqkci3evs8m2((((uint8_t *)&t11) + 4), ((uint8_t *)&t13))));
                if (!t14) goto ie9;
                {
                    gm_tv37d3dpt2opu t16;
                    memset(&t16, 0, sizeof t16);
                    gm_t074vrl1hb4f6 t17;
                    GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
                    ((uint8_t *)&t17)[0] = 0;
                    GM_ST(gm_tv37d3dpt2opu, ((uint8_t *)&t17 + 8), t16);
                    gm_t074vrl1hb4f6 t18 = t17;
                    uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
                    F.ret = t17;
                    goto gm_fret;
                }
                goto ix9;
ie9:;
ix9:;
            }
            gm_t6j6smj7qoctu t20;
            gm_b16_8 t21 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t20 + 0, t21);
            gm_tj5jn79kq9pnm t22 = gm_l1;
            gm_b16_8 t23 = gm_f_read_message_6p6ba86((void *)NULL, cx, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t20 + 16, t23);
            gm_tabb710d9mq2e t24;
            gm_f_format_04pispc((void *)&t24, (void *)NULL, cx, NULL, &t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t25 = t24;
            if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_srcu4ee17t4impf, 338, 17);
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
            gm_b16_8 t27 = t26;
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            gm_t074vrl1hb4f6 t29;
            memset(&t29, 0, sizeof t29);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t29 + 8), t28);
            gm_t074vrl1hb4f6 t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        gm_b16_8 t33 = gm_l2;
        gm_b16_8 t34 = t33;
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
        gm_b16_8 t36 = gm_f_trim_fdrfe6i(t35);
        gm_b16_8 t37 = t36;
        uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&t37) + 8);
        uint8_t t39 = ((t38 == ((uint64_t)0ULL)));
        if (!t39) goto ie32;
        {
            gm_tv37d3dpt2opu t41;
            memset(&t41, 0, sizeof t41);
            gm_t074vrl1hb4f6 t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
            ((uint8_t *)&t42)[0] = 0;
            GM_ST(gm_tv37d3dpt2opu, ((uint8_t *)&t42 + 8), t41);
            gm_t074vrl1hb4f6 t43 = t42;
            uint8_t t44 = ((((uint8_t *)&t43))[0] != 0);
            F.ret = t42;
            goto gm_fret;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        gm_b16_8 t45 = gm_l2;
        gm_b16_8 t46 = t45;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
        gm_t62efos6ssciq t48;
        gm_f_parse_lock_87bbbka((void *)&t48, (void *)NULL, cx, NULL, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t62efos6ssciq t49 = t48;
        gm_tin97tt4ud0s1 t50;
        if (!((((uint8_t *)&t49))[0] != 0)) {
            gm_tin97tt4ud0s1 t51 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)&t49) + 8));
            t50 = t51;
        } else {
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 8));
            gm_l3 = t52;
            gm_t6j6smj7qoctu t54;
            gm_b16_8 t55 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t54 + 0, t55);
            gm_b16_8 t56 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t54 + 16, t56);
            gm_tabb710d9mq2e t57;
            gm_f_format_f6rro2t((void *)&t57, (void *)NULL, cx, NULL, &t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t58 = t57;
            if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 8, gm_srcu4ee17t4impf, 342, 50);
            gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 8));
            gm_b16_8 t60 = t59;
            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
            gm_t074vrl1hb4f6 t62;
            memset(&t62, 0, sizeof t62);
            ((uint8_t *)&t62)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t62 + 8), t61);
            gm_t074vrl1hb4f6 t63 = t62;
            uint8_t t64 = ((((uint8_t *)&t63))[0] != 0);
            F.ret = t62;
            goto gm_fret;
        }
        F.l4 = t50;
    }
    {
        gm_tin97tt4ud0s1 t65 = F.l4;
        gm_tv37d3dpt2opu t66;
        GM_ST(uint8_t, (uint8_t *)&t66 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t66 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t66 + 44, 0);
        GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&t66), t65);
        ((uint8_t *)&t66)[40] = 1;
        gm_t074vrl1hb4f6 t67;
        GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
        ((uint8_t *)&t67)[0] = 0;
        GM_ST(gm_tv37d3dpt2opu, ((uint8_t *)&t67 + 8), t66);
        gm_t074vrl1hb4f6 t68 = t67;
        uint8_t t69 = ((((uint8_t *)&t68))[0] != 0);
        F.ret = t67;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_render_lock_bj7rst0(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_render_lock_bj7rst0 F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l9; /* source */
    gm_b16_8 gm_l10; /* s */
    gm_b16_8 gm_l11; /* a */
    gm_b16_8 gm_l12; /* v */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l16; /* d */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_b16_8 gm_l21; /* d */
    GM_STACK_CHECK(&F, 2244u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_pfs7ocg((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 0));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_ts1nru0t7g7aj t16 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 168));
                    F.l4 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l1);
                        gm_tftb1odc739t5 t19 = gm_f_push_1htqqaf(t18, &(*(gm_ts1nru0t7g7aj *)((uint8_t *)&F.l4)));
                        gm_tftb1odc739t5 t20 = t19;
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcu4ee17t4impf, 211, 33);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 211, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
                }
            }
lb5:;
        }
    }
    {
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_closure t28 = ((gm_closure){GM_PTR_TAG(((void *)gm_f_package_less_4m46djp)), NULL, cx});
        gm_f_sort_by_2q4cr01(t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t30 = GM_LD(gm_b16_8, gm_cduv4b53gs5);
        gm_tnpifv20irnph t31;
        GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
        gm_tftb1odc739t5 t32 = gm_f_write_5cc70a8(t29, t31);
        gm_tftb1odc739t5 t33 = t32;
        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcu4ee17t4impf, 214, 5);
        gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
    }
    {
        {
            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
            gm_l6 = t36;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t38 = gm_l7;
                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t40 = (t38 < t39);
                if (!t40) goto lb37;
                {
                    uint64_t t43 = gm_l7;
                    uint64_t t44 = (uintptr_t)t43;
                    uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_ts1nru0t7g7aj t48 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t44 * 168));
                    F.l8 = t48;
                    {
                        void * t50 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                        gm_b16_8 t52 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t51);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        gm_tnpifv20irnph t53;
                        GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
                        gm_tftb1odc739t5 t54 = gm_f_write_g32vbf8(t50, t53);
                        gm_tftb1odc739t5 t55 = t54;
                        if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_srcu4ee17t4impf, 216, 9);
                        gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
                    }
                    {
                        gm_b16_8 t57;
                        void * t59 = (void *)((uint8_t *)&F.l8);
                        uint8_t t60 = gm_f_is_archive_vibcvpj(t59);
                        if (!t60) goto ie58;
                        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 16));
                        gm_b16_8 t63 = t62;
                        t57 = t63;
                        goto ix58;
ie58:;
                        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 16));
                        gm_tnpifv20irnph t66;
                        GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
                        gm_tabb710d9mq2e t67;
                        gm_f_format_484p9e9((void *)&t67, (void *)NULL, cx, NULL, t66);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        gm_tabb710d9mq2e t68 = t67;
                        if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_srcu4ee17t4impf, 217, 54);
                        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
                        gm_b16_8 t70 = t69;
                        gm_b16_8 t71 = t70;
                        gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                        t57 = t72;
ix58:;
                        gm_l9 = t57;
                    }
                    {
                        void * t73 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t74 = gm_l9;
                        gm_b16_8 t75 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t74);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        gm_tnpifv20irnph t76;
                        GM_ST(gm_b16_8, ((uint8_t *)&t76 + 0), t75);
                        gm_tftb1odc739t5 t77 = gm_f_write_vt977k2(t73, t76);
                        gm_tftb1odc739t5 t78 = t77;
                        if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 1, gm_srcu4ee17t4impf, 218, 9);
                        gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 1));
                    }
                    {
                        if (!(((((uint8_t *)&F.l8) + 32))[16] != 0)) goto ie80;
                        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 32));
                        gm_l10 = t81;
                        {
                            void * t83 = (void *)((uint8_t *)&F.l5);
                            gm_b16_8 t84 = gm_l10;
                            gm_b16_8 t85 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t84);
                            if (gm_unw.kind) {
                                goto gm_ll41;
                            }
                            gm_tnpifv20irnph t86;
                            GM_ST(gm_b16_8, ((uint8_t *)&t86 + 0), t85);
                            gm_tftb1odc739t5 t87 = gm_f_write_90msg31(t83, t86);
                            gm_tftb1odc739t5 t88 = t87;
                            if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 1, gm_srcu4ee17t4impf, 219, 28);
                            gm_b0_1 t89 = GM_LD(gm_b0_1, (((uint8_t *)&t88) + 1));
                        }
                        goto ix80;
ie80:;
ix80:;
                    }
                    {
                        if (!(((((uint8_t *)&F.l8) + 112))[16] != 0)) goto ie90;
                        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 112));
                        gm_l11 = t91;
                        {
                            void * t93 = (void *)((uint8_t *)&F.l5);
                            gm_b16_8 t94 = gm_l11;
                            gm_b16_8 t95 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t94);
                            if (gm_unw.kind) {
                                goto gm_ll41;
                            }
                            gm_tnpifv20irnph t96;
                            GM_ST(gm_b16_8, ((uint8_t *)&t96 + 0), t95);
                            gm_tftb1odc739t5 t97 = gm_f_write_dp2uuhb(t93, t96);
                            gm_tftb1odc739t5 t98 = t97;
                            if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 1, gm_srcu4ee17t4impf, 221, 13);
                            gm_b0_1 t99 = GM_LD(gm_b0_1, (((uint8_t *)&t98) + 1));
                        }
                        goto ix90;
ie90:;
                        {
                            if (!(((((uint8_t *)&F.l8) + 56))[16] != 0)) goto ie101;
                            gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 56));
                            gm_l12 = t102;
                            {
                                void * t104 = (void *)((uint8_t *)&F.l5);
                                gm_b16_8 t105 = gm_l12;
                                gm_b16_8 t106 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t105);
                                if (gm_unw.kind) {
                                    goto gm_ll41;
                                }
                                gm_tnpifv20irnph t107;
                                GM_ST(gm_b16_8, ((uint8_t *)&t107 + 0), t106);
                                gm_tftb1odc739t5 t108 = gm_f_write_u8qima9(t104, t107);
                                gm_tftb1odc739t5 t109 = t108;
                                if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 1, gm_srcu4ee17t4impf, 223, 33);
                                gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 1));
                            }
                            goto ix101;
ie101:;
ix101:;
                        }
                        {
                            void * t111 = (void *)((uint8_t *)&F.l5);
                            gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 80));
                            gm_b16_8 t113 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t112);
                            if (gm_unw.kind) {
                                goto gm_ll41;
                            }
                            gm_tnpifv20irnph t114;
                            GM_ST(gm_b16_8, ((uint8_t *)&t114 + 0), t113);
                            gm_tftb1odc739t5 t115 = gm_f_write_m0brg2a(t111, t114);
                            gm_tftb1odc739t5 t116 = t115;
                            if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 1, gm_srcu4ee17t4impf, 224, 13);
                            gm_b0_1 t117 = GM_LD(gm_b0_1, (((uint8_t *)&t116) + 1));
                        }
                        {
                            void * t118 = (void *)((uint8_t *)&F.l5);
                            gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 96));
                            gm_b16_8 t120 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t119);
                            if (gm_unw.kind) {
                                goto gm_ll41;
                            }
                            gm_tnpifv20irnph t121;
                            GM_ST(gm_b16_8, ((uint8_t *)&t121 + 0), t120);
                            gm_tftb1odc739t5 t122 = gm_f_write_894ocpq(t118, t121);
                            gm_tftb1odc739t5 t123 = t122;
                            if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 1, gm_srcu4ee17t4impf, 225, 13);
                            gm_b0_1 t124 = GM_LD(gm_b0_1, (((uint8_t *)&t123) + 1));
                        }
ix90:;
                    }
                    {
                        void * t125 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 136));
                        gm_b16_8 t127 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t126);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        gm_tnpifv20irnph t128;
                        GM_ST(gm_b16_8, ((uint8_t *)&t128 + 0), t127);
                        gm_tftb1odc739t5 t129 = gm_f_write_rv9cdbb(t125, t128);
                        gm_tftb1odc739t5 t130 = t129;
                        if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 1, gm_srcu4ee17t4impf, 227, 9);
                        gm_b0_1 t131 = GM_LD(gm_b0_1, (((uint8_t *)&t130) + 1));
                    }
                    {
                        gm_f_list_epvducg((void *)&F.l13, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                    }
                    {
                        {
                            gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 152));
                            gm_l14 = t133;
                        }
                        {
                            gm_l15 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t135 = gm_l15;
                                uint64_t t136 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                uint8_t t137 = (t135 < t136);
                                if (!t137) goto lb134;
                                {
                                    uint64_t t140 = gm_l15;
                                    uint64_t t141 = (uintptr_t)t140;
                                    uint64_t t142 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                    gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t141 * 16));
                                    gm_l16 = t145;
                                    {
                                        void * t147 = (void *)((uint8_t *)&F.l13);
                                        gm_b16_8 t148 = gm_l16;
                                        gm_tftb1odc739t5 t149 = gm_f_push_3ef746b(t147, t148);
                                        gm_tftb1odc739t5 t150 = t149;
                                        if (((((uint8_t *)&t150))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t150) + 1, gm_srcu4ee17t4impf, 229, 35);
                                        gm_b0_1 t151 = GM_LD(gm_b0_1, (((uint8_t *)&t150) + 1));
                                    }
                                    goto me143;
mn144:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 229, 9);
me143:;
                                }
lc134:;
                                {
                                    uint64_t t153 = gm_l15;
                                    uint64_t r154; bool o155 = __builtin_add_overflow(t153, ((uint64_t)1ULL), &r154);
                                    uint64_t t156 = r154;
                                    gm_l15 = t156;
                                }
                            }
lb134:;
                        }
                    }
                    {
                        gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                        gm_f_sort_skeree7(t157);
                    }
                    {
                        gm_f_list_epvducg((void *)&F.l17, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                    }
                    {
                        {
                            gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                            gm_b16_8 t160 = t159;
                            gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
                            gm_l18 = t161;
                        }
                        {
                            gm_l19 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t163 = gm_l19;
                                uint64_t t164 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                uint8_t t165 = (t163 < t164);
                                if (!t165) goto lb162;
                                {
                                    gm_tf1bjg81u0glv t168;
                                    uint64_t t169 = gm_l19;
                                    GM_ST(uint64_t, (uint8_t *)&t168 + 0, t169);
                                    uint64_t t170 = gm_l19;
                                    uint64_t t171 = (uintptr_t)t170;
                                    uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                    gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t171 * 16));
                                    GM_ST(gm_b16_8, (uint8_t *)&t168 + 8, t173);
                                    gm_tf1bjg81u0glv t174 = t168;
                                    uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&t174) + 0));
                                    gm_l20 = t177;
                                    gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&t174) + 8));
                                    gm_l21 = t178;
                                    {
                                        uint64_t t181 = gm_l20;
                                        uint8_t t182 = (t181 > ((uint64_t)0ULL));
                                        if (!t182) goto ie180;
                                        uint64_t t183 = gm_l20;
                                        uint64_t r184; bool o185 = __builtin_sub_overflow(t183, ((uint64_t)1ULL), &r184);
                                        if (o185) gm_trap("integer overflow in `-`", gm_srcu4ee17t4impf, 233, 37);
                                        uint64_t t186 = r184;
                                        uint64_t t187 = (uintptr_t)t186;
                                        uint64_t t188 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                                        if (t187 >= (uintptr_t)(t188)) gm_trap_index(t187, t188, gm_srcu4ee17t4impf, 233, 26);
                                        gm_b16_8 t189 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t187 * 16));
                                        gm_b16_8 t190 = gm_l21;
                                        gm_b16_8 t191 = t189;
                                        gm_b16_8 t192 = t190;
                                        uint8_t t193 = (gm_eq0iqiubfm7e(((uint8_t *)&t191), ((uint8_t *)&t192)));
                                        if (!t193) goto ie180;
                                        {
                                            goto lc162;
                                        }
                                        goto ix180;
ie180:;
ix180:;
                                    }
                                    {
                                        void * t195 = (void *)((uint8_t *)&F.l17);
                                        gm_b16_8 t196 = gm_l21;
                                        gm_b16_8 t197 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t196);
                                        if (gm_unw.kind) {
                                            goto gm_ll166;
                                        }
                                        gm_tftb1odc739t5 t198 = gm_f_push_3ef746b(t195, t197);
                                        gm_tftb1odc739t5 t199 = t198;
                                        if (((((uint8_t *)&t199))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t199) + 1, gm_srcu4ee17t4impf, 234, 13);
                                        gm_b0_1 t200 = GM_LD(gm_b0_1, (((uint8_t *)&t199) + 1));
                                    }
                                    goto me175;
mn176:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 232, 9);
me175:;
                                }
lc162:;
                                {
                                    uint64_t t202 = gm_l19;
                                    uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)1ULL), &r203);
                                    uint64_t t205 = r203;
                                    gm_l19 = t205;
                                }
                            }
gm_ll166:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb162; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc162; }
                            }
                            goto gm_ll41;
lb162:;
                        }
                    }
                    {
                        void * t206 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                        gm_b16_8 t208 = t207;
                        gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                        gm_b16_8 t210;
                        GM_ST(void *, (uint8_t *)&t210, (void *)(gm_sg5gdnrith2i6));
                        GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(2));
                        gm_tabb710d9mq2e t211;
                        gm_f_join_sfknljb((void *)&t211, (void *)NULL, cx, NULL, t209, t210);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        gm_tabb710d9mq2e t212 = t211;
                        if (((((uint8_t *)&t212))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t212) + 8, gm_srcu4ee17t4impf, 236, 44);
                        gm_b16_8 t213 = GM_LD(gm_b16_8, (((uint8_t *)&t212) + 8));
                        gm_tnpifv20irnph t214;
                        GM_ST(gm_b16_8, ((uint8_t *)&t214 + 0), t213);
                        gm_tftb1odc739t5 t215 = gm_f_write_tlurko4(t206, t214);
                        gm_tftb1odc739t5 t216 = t215;
                        if (((((uint8_t *)&t216))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t216) + 1, gm_srcu4ee17t4impf, 236, 9);
                        gm_b0_1 t217 = GM_LD(gm_b0_1, (((uint8_t *)&t216) + 1));
                    }
                    goto me46;
mn47:;
                    gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 215, 5);
me46:;
                }
lc37:;
                {
                    uint64_t t219 = gm_l7;
                    uint64_t r220; bool o221 = __builtin_add_overflow(t219, ((uint64_t)1ULL), &r220);
                    uint64_t t222 = r220;
                    gm_l7 = t222;
                }
            }
gm_ll41:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb37; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc37; }
            }
            goto gm_lad;
lb37:;
        }
    }
    {
        gm_b16_8 t223 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t224 = t223;
        gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&t224));
        uint8_t t226 = 0;
        F.ret = t225;
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
__attribute__((aligned(4))) gm_csr3mh7spg80kh gm_f_string_metrics_otdg4n0(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_string_metrics_otdg4n0 F;
    gm_b16_8 gm_l0; /* s */
    gm_t5ogmjn0ml54c gm_l1; /* m */
    uint32_t gm_l2; /* singles */
    uint32_t gm_l3; /* doubles */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* b */
    GM_STACK_CHECK(&F, 243u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_t5ogmjn0ml54c t2;
        GM_ST(uint8_t, (uint8_t *)&t2 + 11, 0);
        GM_ST(uint32_t, ((uint8_t *)&t2 + 0), ((uint32_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t2 + 4), ((uint32_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 8), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 9), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 10), ((uint8_t)0));
        gm_l1 = t2;
    }
    {
        gm_l2 = ((uint32_t)0ULL);
    }
    {
        gm_l3 = ((uint32_t)0ULL);
    }
    {
        {
            gm_b16_8 t4 = gm_l0;
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
                    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t12 * 1));
                    uint8_t t15 = t14;
                    uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)&t15));
                    gm_l6 = t18;
                    {
                        uint8_t t21 = gm_l6;
                        uint8_t t22 = ((t21 == ((uint8_t)39ULL)));
                        if (!t22) goto ie20;
                        {
                            uint32_t t24 = gm_l2;
                            uint32_t r25; bool o26 = __builtin_add_overflow(t24, ((uint32_t)1ULL), &r25);
                            if (o26) gm_trap("integer overflow in `+`", gm_srcu4ee17t4impf, 115, 13);
                            uint32_t t27 = r25;
                            gm_l2 = t27;
                        }
                        {
                            uint32_t t29 = gm_l2;
                            uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t31 = (t29 > t30);
                            if (!t31) goto ie28;
                            {
                                uint32_t t33 = gm_l2;
                                GM_ST(uint32_t, (((uint8_t *)&gm_l1) + 0), t33);
                            }
                            goto ix28;
ie28:;
ix28:;
                        }
                        goto ix20;
ie20:;
                        {
                            gm_l2 = ((uint32_t)0ULL);
                        }
ix20:;
                    }
                    {
                        uint8_t t36 = gm_l6;
                        uint8_t t37 = ((t36 == ((uint8_t)34ULL)));
                        if (!t37) goto ie35;
                        {
                            uint32_t t39 = gm_l3;
                            uint32_t r40; bool o41 = __builtin_add_overflow(t39, ((uint32_t)1ULL), &r40);
                            if (o41) gm_trap("integer overflow in `+`", gm_srcu4ee17t4impf, 121, 13);
                            uint32_t t42 = r40;
                            gm_l3 = t42;
                        }
                        {
                            uint32_t t44 = gm_l3;
                            uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
                            uint8_t t46 = (t44 > t45);
                            if (!t46) goto ie43;
                            {
                                uint32_t t48 = gm_l3;
                                GM_ST(uint32_t, (((uint8_t *)&gm_l1) + 4), t48);
                            }
                            goto ix43;
ie43:;
ix43:;
                        }
                        goto ix35;
ie35:;
                        {
                            gm_l3 = ((uint32_t)0ULL);
                        }
ix35:;
                    }
                    {
                        uint8_t t51 = gm_l6;
                        uint8_t t52 = ((t51 == ((uint8_t)92ULL)));
                        if (!t52) goto ie50;
                        {
                            GM_ST(uint8_t, (((uint8_t *)&gm_l1) + 9), ((uint8_t)1));
                        }
                        goto ix50;
ie50:;
                        uint8_t t55 = gm_l6;
                        uint8_t t56 = ((t55 == ((uint8_t)10ULL)));
                        if (!t56) goto ie54;
                        {
                            GM_ST(uint8_t, (((uint8_t *)&gm_l1) + 10), ((uint8_t)1));
                        }
                        goto ix54;
ie54:;
                        uint8_t t59 = gm_l6;
                        uint8_t t60 = (!(t59 == ((uint8_t)9ULL)));
                        if (!t60) goto ie58;
                        uint8_t t61 = 0;
                        uint8_t t62 = gm_l6;
                        uint8_t t63 = (t62 <= ((uint8_t)31ULL));
                        if (!t63) {
                            uint8_t t64 = gm_l6;
                            uint8_t t65 = ((t64 == ((uint8_t)127ULL)));
                            t61 = t65;
                        } else t61 = 1;
                        if (!t61) goto ie58;
                        {
                            GM_ST(uint8_t, (((uint8_t *)&gm_l1) + 8), ((uint8_t)1));
                        }
                        goto ix58;
ie58:;
ix58:;
ix54:;
ix50:;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcu4ee17t4impf, 113, 5);
me16:;
                }
lc5:;
                {
                    uint64_t t68 = gm_l5;
                    uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                    uint64_t t71 = r69;
                    gm_l5 = t71;
                }
            }
lb5:;
        }
    }
    {
        gm_t5ogmjn0ml54c t72 = gm_l1;
        uint8_t t73 = 0;
        F.ret = t72;
        goto gm_fret;
    }
gm_fret:
    return GM_CONV(gm_csr3mh7spg80kh, F.ret);
gm_fprop:
    return GM_CONV(gm_csr3mh7spg80kh, F.ret);
}
__attribute__((aligned(4))) gm_b16_8 gm_f_toml_string_cagbrmp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_toml_string_cagbrmp F;
    gm_b16_8 gm_l0; /* s */
    gm_t5ogmjn0ml54c gm_l1; /* m */
    GM_STACK_CHECK(&F, 663u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_t5ogmjn0ml54c t3 = GM_CONV(gm_t5ogmjn0ml54c, gm_f_string_metrics_otdg4n0(t2));
        gm_l1 = t3;
    }
    {
        uint8_t t5 = 0;
        uint8_t t6 = 0;
        uint8_t t7 = 0;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8));
        if (!t8) {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 9));
            t7 = t9;
        } else t7 = 1;
        if (!t7) {
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            uint8_t t11 = (t10 > ((uint32_t)0ULL));
            t6 = t11;
        } else t6 = 1;
        if (!t6) {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 10));
            t5 = t12;
        } else t5 = 1;
        uint8_t t13 = (uint8_t)!t5;
        if (!t13) goto ie4;
        {
            gm_b16_8 t15 = gm_l0;
            gm_b16_8 t16 = gm_f_basic_string_3q58uai((void *)NULL, cx, NULL, t15, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint8_t t19 = 0;
        uint8_t t20 = 0;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8));
        if (!t21) {
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t23 = (t22 > ((uint32_t)0ULL));
            t20 = t23;
        } else t20 = 1;
        if (!t20) {
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 10));
            t19 = t24;
        } else t19 = 1;
        uint8_t t25 = (uint8_t)!t19;
        if (!t25) goto ie18;
        {
            gm_b16_8 t27 = gm_l0;
            gm_tnpifv20irnph t28;
            GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t27);
            gm_tabb710d9mq2e t29;
            gm_f_format_3aass3a((void *)&t29, (void *)NULL, cx, NULL, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t30 = t29;
            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_srcu4ee17t4impf, 173, 71);
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
            gm_b16_8 t32 = t31;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
            uint8_t t34 = 0;
            F.ret = t33;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        uint8_t t36 = 0;
        uint8_t t37 = 0;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8));
        if (!t38) {
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 9));
            t37 = t39;
        } else t37 = 1;
        if (!t37) {
            uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            uint8_t t41 = (t40 > ((uint32_t)2ULL));
            t36 = t41;
        } else t36 = 1;
        uint8_t t42 = (uint8_t)!t36;
        if (!t42) goto ie35;
        {
            gm_b16_8 t44 = gm_l0;
            gm_b16_8 t45 = gm_f_basic_string_ml_n90kq9h((void *)NULL, cx, NULL, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t46 = 0;
            F.ret = t45;
            goto gm_fret;
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        uint8_t t48 = 0;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8));
        if (!t49) {
            uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t51 = (t50 > ((uint32_t)2ULL));
            t48 = t51;
        } else t48 = 1;
        uint8_t t52 = (uint8_t)!t48;
        if (!t52) goto ie47;
        {
            gm_t6j6smj7qoctu t54;
            gm_b16_8 t55;
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 10));
            if (!t57) goto ie56;
            gm_b16_8 t59;
            GM_ST(void *, (uint8_t *)&t59, (void *)(gm_so514oj5nbhcu));
            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(1));
            gm_b16_8 t60 = t59;
            t55 = t60;
            goto ix56;
ie56:;
            gm_b16_8 t62;
            GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(0));
            gm_b16_8 t63 = t62;
            t55 = t63;
ix56:;
            GM_ST(gm_b16_8, (uint8_t *)&t54 + 0, t55);
            gm_b16_8 t64 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t54 + 16, t64);
            gm_tabb710d9mq2e t65;
            gm_f_format_g9g5b0g((void *)&t65, (void *)NULL, cx, NULL, &t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t66 = t65;
            if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 8, gm_srcu4ee17t4impf, 176, 16);
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
            gm_b16_8 t68 = t67;
            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
            uint8_t t70 = 0;
            F.ret = t69;
            goto gm_fret;
        }
        goto ix47;
ie47:;
ix47:;
    }
    {
        gm_b16_8 t71;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 10));
        if (!t73) goto ie72;
        gm_b16_8 t75 = gm_l0;
        gm_b16_8 t76 = gm_f_basic_string_ml_n90kq9h((void *)NULL, cx, NULL, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t77 = t76;
        t71 = t77;
        goto ix72;
ie72:;
        gm_b16_8 t79 = gm_l0;
        gm_b16_8 t80 = gm_f_basic_string_3q58uai((void *)NULL, cx, NULL, t79, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t81 = t80;
        t71 = t81;
ix72:;
        uint8_t t82 = 0;
        F.ret = t71;
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
