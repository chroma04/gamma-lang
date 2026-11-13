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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f1; } gm_ttsm01t0d8srg;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint8_t f3; uint8_t p4[3]; uint32_t f4; } gm_t84tkl8vf34ko;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttsm01t0d8srg) == 1 && _Alignof(gm_ttsm01t0d8srg) == 1 && offsetof(gm_ttsm01t0d8srg, f1) == 0, "layout of `?()`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t84tkl8vf34ko) == 56 && _Alignof(gm_t84tkl8vf34ko) == 8 && offsetof(gm_t84tkl8vf34ko, f0) == 0 && offsetof(gm_t84tkl8vf34ko, f1) == 16 && offsetof(gm_t84tkl8vf34ko, f2) == 32 && offsetof(gm_t84tkl8vf34ko, f3) == 48 && offsetof(gm_t84tkl8vf34ko, f4) == 52, "layout of `Decl`");
typedef struct { uint64_t f0; gm_t84tkl8vf34ko f1; } gm_t9re6l0g6jicn;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint64_t f0; gm_trqfvmjtt4sij f1; } gm_tmupb6k0asuti;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_t9re6l0g6jicn) == 64 && _Alignof(gm_t9re6l0g6jicn) == 8 && offsetof(gm_t9re6l0g6jicn, f0) == 0 && offsetof(gm_t9re6l0g6jicn, f1) == 8, "layout of `(i: usize, x: Decl)`");
_Static_assert(sizeof(gm_tmupb6k0asuti) == 48 && _Alignof(gm_tmupb6k0asuti) == 8 && offsetof(gm_tmupb6k0asuti, f0) == 0 && offsetof(gm_tmupb6k0asuti, f1) == 8, "layout of `(i: usize, x: List((name: string, def: string)))`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
struct gm_fr_brace_balance_aofv41t {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_c_fn_name_77uspqp {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_decl_before_fp800ft {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_decl_items_l330ksi {
    gm_frame h;
    gm_b16_8 l0; /* text */
    gm_trqfvmjtt4sij l1; /* items */
    uint64_t l2; /* start */
    int32_t l3; /* depth */
    uint64_t l4; /* i */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_declared_name_hovvm2b {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_decls_of_srep9du {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_external_f65nm8q {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_fn_575gb0c {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_if8ammt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_efhhmqv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_fe8qj55 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_hfbghc3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_jojopru {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_uo0k55e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_vn00ddp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_function_o2lv8g6 {
    gm_frame h;
    gm_ttu09lij36f8c ret;
};
struct gm_fr_gm_names_6ner0lu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_t4c7ipjj9s3fp l2; /* seen */
    gm_b16_8 ret;
};
struct gm_fr_has_word_l11ltcj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_ident_char_pmpasvq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_ident_set_9ajalpn {
    gm_frame h;
    gm_t4c7ipjj9s3fp l1; /* set */
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_inline_definition_6aokb9d {
    gm_frame h;
    gm_ttu09lij36f8c ret;
};
struct gm_fr_is_inline_aqdknmj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_items_of_q103oiv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* items */
    gm_th2lqbo77pvsc ret;
    gm_b0_1 w5;
};
struct gm_fr_line_reset_73tgnor {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_optimized_unit_count_5mur81l {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_pop_vr08sii {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_set_depths_1781b1p {
    gm_frame h;
    gm_t84tkl8vf34ko l5; /* d */
    gm_trqfvmjtt4sij l10; /* l */
    gm_b0_1 ret;
};
struct gm_fr_skip_literal_17lq2p8 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_sort_name_ro82p6b {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_split_by_file_hgjqgsp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* decls */
    gm_t4c7ipjj9s3fp l2; /* inline_names */
    gm_trqfvmjtt4sij l3; /* bodies */
    gm_trqfvmjtt4sij l7; /* fns */
    gm_trqfvmjtt4sij l15; /* externs */
    gm_trqfvmjtt4sij l19; /* groups */
    gm_t4c7ipjj9s3fp l23; /* by_name */
    gm_trqfvmjtt4sij l24; /* nameless */
    gm_t84tkl8vf34ko l28; /* d */
    gm_trqfvmjtt4sij l30; /* l */
    gm_trqfvmjtt4sij l31; /* units */
    gm_trqfvmjtt4sij l35; /* g */
    gm_trqfvmjtt4sij l36; /* text */
    gm_trqfvmjtt4sij l43; /* fns */
    gm_trqfvmjtt4sij l44; /* ext */
    gm_trqfvmjtt4sij l45; /* all */
    gm_trqfvmjtt4sij l46; /* body */
    gm_ttu09lij36f8c ret;
    gm_b0_1 w3;
    gm_b0_1 w5;
};
struct gm_fr_split_jendjdm {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_split_shared_a91530r {
    gm_frame h;
    gm_t4c7ipjj9s3fp l3; /* inline_names */
    gm_trqfvmjtt4sij l4; /* inline_defs */
    gm_trqfvmjtt4sij l9; /* decls */
    gm_trqfvmjtt4sij l10; /* defs */
    gm_trqfvmjtt4sij l11; /* externs */
    gm_trqfvmjtt4sij l14; /* items */
    gm_trqfvmjtt4sij l25; /* units */
    gm_trqfvmjtt4sij l26; /* first */
    gm_trqfvmjtt4sij l31; /* out */
    gm_trqfvmjtt4sij l34; /* u */
    gm_ttu09lij36f8c ret;
    gm_b0_1 w1;
    gm_b0_1 w2;
};
struct gm_fr_unit_count_dk0d2e4 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_unit_text_mmejmp8 {
    gm_frame h;
    gm_b16_8 l0; /* decls */
    gm_trqfvmjtt4sij l5; /* taken */
    gm_t4c7ipjj9s3fp l8; /* defined */
    gm_trqfvmjtt4sij l9; /* work */
    gm_trqfvmjtt4sij l11; /* l */
    gm_trqfvmjtt4sij l26; /* chosen */
    gm_trqfvmjtt4sij l31; /* text */
    gm_t4c7ipjj9s3fp l32; /* seen */
    gm_trqfvmjtt4sij l36; /* used */
    gm_t4c7ipjj9s3fp l37; /* idents */
    gm_b16_8 ret;
};
bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) int32_t gm_f_brace_balance_aofv41t(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_c_fn_name_77uspqp(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_c_fn_name_77uspqp_c(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_decl_before_fp800ft(void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_decl_items_l330ksi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_declared_name_hovvm2b(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_decls_of_srep9du(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_deinit_lehld1n(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_nvuvq05(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_vtu6170(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_k51ik07(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_external_f65nm8q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_fn_575gb0c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_fn_if8ammt(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_efhhmqv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_fe8qj55(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hfbghc3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_jojopru(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_uo0k55e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vn00ddp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_1u4jnnu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_69nn7l3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8l8mb6d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c2lphrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_deiomvg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dg0m8l8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_q8i7gsb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rse2efa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_stmnn5h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_u8fgvjp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_uaq30dv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_function_o2lv8g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_get_ece4opn(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_ohh8mo3(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_get_ptr_53uc4fm(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_gm_names_6ner0lu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_guard_o7k9a0e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_has_word_l11ltcj(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_hash64_apkb7bo(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_hash_map_dklib0j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hash_map_lljdp3h(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_ident_char_pmpasvq(uint8_t a0);
__attribute__((aligned(4))) void gm_f_ident_set_9ajalpn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_inline_definition_6aokb9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_inline_aqdknmj(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_items_of_q103oiv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_line_reset_73tgnor(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s6rkd4t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sa157kc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_scbceav(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_snvhnj5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_surb2to(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_svrdqg8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_list_016fo30(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_1g734tk(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_b7l80f8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_c8kt75n(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_lj5vg1g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint64_t gm_f_optimized_unit_count_5mur81l(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0ek3ea4(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_97b8r3k(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rda4o9n(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rlpbnuq(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vnejmrp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7o5vpta(void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_drsjd1t(void * a0, gm_b16_8 a1, gm_b0_1 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qoojoqv(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_replace_alloc_t7hndlq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_set_depths_1781b1p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) uint64_t gm_f_skip_literal_17lq2p8(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_sort_by_08k4kf2(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_sort_by_4j6ktaf(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sort_name_ro82p6b(void * a0);
__attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_split_by_file_hgjqgsp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_split_jendjdm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_split_shared_a91530r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_suffix_8155gbi(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_unit_count_dk0d2e4(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_unit_text_mmejmp8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_815rgbg(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cdouvhd(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_d8c7tpm(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qm3m7a0(void * a0, const void * a1);
bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
extern uint8_t gm_c30tdq2p48i[1] __attribute__((aligned(1)));
extern uint8_t gm_c4crumb5mc9[1] __attribute__((aligned(1)));
extern uint8_t gm_c72j1g8d455[1] __attribute__((aligned(1)));
extern uint8_t gm_ccmgjp6th6r[16] __attribute__((aligned(8)));
extern uint8_t gm_cos4c858gsq[1] __attribute__((aligned(1)));
extern uint8_t gm_crpb2rekieb[1] __attribute__((aligned(1)));
extern void *const gm_hl8sdnavrdjn[];
extern void *const gm_hlbopa954cfe[];
extern void *const gm_hln0l2hrppmf[];
extern void *const gm_hls2l2mrnqun[];
extern void *const gm_hlt54s6krmq6[];
extern void *const gm_hlu84m640fm9[];
extern const uint8_t gm_s1oiqt105ulvb[20];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s7ipvt5onv6ht[4];
extern const uint8_t gm_s8sgcefhqa4i0[43];
extern const uint8_t gm_saalafjdd3tnp[9];
extern const uint8_t gm_sao4mumvadoct[4];
extern const uint8_t gm_sasea0sobfkrs[8];
extern const uint8_t gm_sd1uj6gf5v8qh[8];
extern const uint8_t gm_seccpktgmrevt[2];
extern const uint8_t gm_shv3ttlq9l359[15];
extern const uint8_t gm_so0h0lkl6bp4l[39];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so7qmna0fo4so[6];
extern const uint8_t gm_soa9mnl4ahjud[8];
extern const uint8_t gm_spkrf9e7cr6h4[7];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_srccfhjr4r9mdef[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ssi4rrma2q00g[7];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_vr08sii F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* x */
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
            gm_th2lqbo77pvsc t8;
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
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 16));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_b16_8 t29 = gm_l2;
        gm_th2lqbo77pvsc t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[16] = 1;
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
inline __attribute__((aligned(4))) int32_t gm_f_brace_balance_aofv41t(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_brace_balance_aofv41t F;
    gm_b16_8 gm_l0; /* line */
    int32_t gm_l1; /* d */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((int32_t)0LL);
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l2;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                uint64_t t8 = gm_l2;
                uint64_t t9 = (uintptr_t)t8;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srccfhjr4r9mdef, 392, 14);
                uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 1));
                gm_l3 = t11;
            }
            {
                uint8_t t13 = 0;
                uint8_t t14 = gm_l3;
                uint8_t t15 = ((t14 == ((uint8_t)34ULL)));
                if (!t15) {
                    uint8_t t16 = gm_l3;
                    uint8_t t17 = ((t16 == ((uint8_t)39ULL)));
                    t13 = t17;
                } else t13 = 1;
                if (!t13) goto ie12;
                {
                    gm_b16_8 t19 = gm_l0;
                    uint64_t t20 = gm_l2;
                    uint64_t t21 = gm_f_skip_literal_17lq2p8(t19, t20);
                    gm_l2 = t21;
                }
                {
                    goto lc2;
                }
                goto ix12;
ie12:;
ix12:;
            }
            {
                uint8_t t23 = gm_l3;
                uint8_t t24 = ((t23 == ((uint8_t)123ULL)));
                if (!t24) goto ie22;
                {
                    int32_t t26 = gm_l1;
                    int32_t r27; bool o28 = __builtin_add_overflow(t26, ((int32_t)1LL), &r27);
                    if (o28) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 397, 23);
                    int32_t t29 = r27;
                    gm_l1 = t29;
                }
                goto ix22;
ie22:;
                uint8_t t31 = gm_l3;
                uint8_t t32 = ((t31 == ((uint8_t)125ULL)));
                if (!t32) goto ie30;
                {
                    int32_t t34 = gm_l1;
                    int32_t r35; bool o36 = __builtin_sub_overflow(t34, ((int32_t)1LL), &r35);
                    if (o36) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 397, 51);
                    int32_t t37 = r35;
                    gm_l1 = t37;
                }
                goto ix30;
ie30:;
ix30:;
ix22:;
            }
            {
                uint64_t t38 = gm_l2;
                uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                if (o40) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 398, 9);
                uint64_t t41 = r39;
                gm_l2 = t41;
            }
lc2:;
        }
lb2:;
    }
    {
        int32_t t42 = gm_l1;
        uint8_t t43 = 0;
        F.ret = t42;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_c_fn_name_77uspqp(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_c_fn_name_77uspqp F;
    gm_b16_8 gm_l0; /* line */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* j */
    GM_STACK_CHECK(&F, 439u);
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
            uint64_t r4; bool o5 = __builtin_add_overflow(t3, ((uint64_t)3ULL), &r4);
            if (o5) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 174, 11);
            uint64_t t6 = r4;
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t8 = (t6 <= t7);
            if (!t8) goto lb2;
            {
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t13 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t14 = gm_l1;
                uint64_t t15 = (uintptr_t)t14;
                uint64_t t16 = gm_l1;
                uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)3ULL), &r17);
                if (o18) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 175, 20);
                uint64_t t19 = r17;
                uint64_t t20 = (uintptr_t)t19;
                if (t15 > t20 || t20 > (uintptr_t)t12) gm_trap_slice(t15, t20, t12, gm_srccfhjr4r9mdef, 175, 12);
                gm_b16_8 t21;
                GM_ST(void *, (uint8_t *)&t21, (void *)(((uint8_t *)t13 + t15 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(t20 - t15));
                gm_b16_8 t22 = t21;
                uint8_t t23 = (((GM_LD(uintptr_t, ((uint8_t *)&t22) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&t22)), gm_s7ipvt5onv6ht, 3) == 0));
                if (!t23) goto ie11;
                uint8_t t24 = 0;
                uint64_t t25 = gm_l1;
                uint8_t t26 = ((t25 == ((uint64_t)0ULL)));
                if (!t26) {
                    uint64_t t27 = gm_l1;
                    uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                    if (o29) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 175, 81);
                    uint64_t t30 = r28;
                    uint64_t t31 = (uintptr_t)t30;
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t31 >= (uintptr_t)(t32)) gm_trap_index(t31, t32, gm_srccfhjr4r9mdef, 175, 76);
                    uint8_t t33 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t31 * 1));
                    uint8_t t34 = gm_f_is_ident_cont_317fpdb(t33);
                    uint8_t t35 = (uint8_t)!t34;
                    t24 = t35;
                } else t24 = 1;
                if (!t24) goto ie11;
                {
                    uint64_t t37 = gm_l1;
                    gm_l2 = t37;
                }
                {
                    for (;;) {
                        uint64_t t39 = gm_l2;
                        uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t41 = (t39 < t40);
                        if (!t41) goto lb38;
                        uint64_t t42 = gm_l2;
                        uint64_t t43 = (uintptr_t)t42;
                        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t43 >= (uintptr_t)(t44)) gm_trap_index(t43, t44, gm_srccfhjr4r9mdef, 177, 57);
                        uint8_t t45 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t43 * 1));
                        uint8_t t46 = gm_f_is_ident_cont_317fpdb(t45);
                        if (!t46) goto lb38;
                        {
                            uint64_t t49 = gm_l2;
                            uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                            if (o51) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 177, 68);
                            uint64_t t52 = r50;
                            gm_l2 = t52;
                        }
lc38:;
                    }
lb38:;
                }
                {
                    uint64_t t54 = gm_l2;
                    uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t56 = (t54 < t55);
                    if (!t56) goto ie53;
                    uint64_t t57 = gm_l2;
                    uint64_t t58 = (uintptr_t)t57;
                    uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t58 >= (uintptr_t)(t59)) gm_trap_index(t58, t59, gm_srccfhjr4r9mdef, 178, 33);
                    uint8_t t60 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t58 * 1));
                    uint8_t t61 = ((t60 == ((uint8_t)40ULL)));
                    if (!t61) goto ie53;
                    {
                        uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        void * t64 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                        uint64_t t65 = gm_l1;
                        uint64_t t66 = (uintptr_t)t65;
                        uint64_t t67 = gm_l2;
                        uint64_t t68 = (uintptr_t)t67;
                        if (t66 > t68 || t68 > (uintptr_t)t63) gm_trap_slice(t66, t68, t63, gm_srccfhjr4r9mdef, 178, 57);
                        gm_b16_8 t69;
                        GM_ST(void *, (uint8_t *)&t69, (void *)(((uint8_t *)t64 + t66 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(t68 - t66));
                        gm_th2lqbo77pvsc t70;
                        GM_ST(uint8_t, (uint8_t *)&t70 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t70 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t70 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t70), t69);
                        ((uint8_t *)&t70)[16] = 1;
                        uint8_t t71 = 0;
                        F.ret = t70;
                        goto gm_fret;
                    }
                    goto ix53;
ie53:;
ix53:;
                }
                {
                    uint64_t t72 = gm_l2;
                    gm_l1 = t72;
                }
                goto ix11;
ie11:;
                {
                    uint64_t t74 = gm_l1;
                    uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                    if (o76) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 181, 13);
                    uint64_t t77 = r75;
                    gm_l1 = t77;
                }
ix11:;
            }
lc2:;
        }
lb2:;
    }
    {
        gm_th2lqbo77pvsc t78;
        memset(&t78, 0, sizeof t78);
        uint8_t t79 = 0;
        F.ret = t78;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_c_fn_name_77uspqp_c(gm_b16_8 a0) { gm_th2lqbo77pvsc gm_r; gm_f_c_fn_name_77uspqp(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) uint8_t gm_f_decl_before_fp800ft(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_before_fp800ft F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    gm_b16_8 gm_l2; /* an */
    gm_b16_8 gm_l3; /* bn */
    GM_STACK_CHECK(&F, 454u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 48));
        void * t5 = gm_l1;
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t5) + 48));
        uint8_t t7 = (!(t4 == t6));
        if (!t7) goto ie2;
        {
            void * t9 = gm_l0;
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t9) + 48));
            void * t11 = gm_l1;
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t11) + 48));
            uint8_t t13 = (t10 < t12);
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t16 = gm_l0;
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)t16) + 52));
        void * t18 = gm_l1;
        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)t18) + 52));
        uint8_t t20 = (!(t17 == t19));
        if (!t20) goto ie15;
        {
            void * t22 = gm_l0;
            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)t22) + 52));
            void * t24 = gm_l1;
            uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)t24) + 52));
            uint8_t t26 = (t23 < t25);
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        void * t28 = gm_l0;
        gm_b16_8 t29 = gm_f_sort_name_ro82p6b(t28);
        gm_l2 = t29;
    }
    {
        void * t30 = gm_l1;
        gm_b16_8 t31 = gm_f_sort_name_ro82p6b(t30);
        gm_l3 = t31;
    }
    {
        gm_b16_8 t33 = gm_l2;
        gm_b16_8 t34 = gm_l3;
        gm_b16_8 t35 = t33;
        gm_b16_8 t36 = t34;
        uint8_t t37 = (!gm_eq0iqiubfm7e(((uint8_t *)&t35), ((uint8_t *)&t36)));
        if (!t37) goto ie32;
        {
            gm_b16_8 t39 = gm_l2;
            gm_b16_8 t40 = gm_l3;
            gm_b16_8 t41 = t39;
            gm_b16_8 t42 = t40;
            uint8_t t43 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t41), ((uint8_t *)&t42));
            uint8_t t44 = 0;
            F.ret = t43;
            goto gm_fret;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        void * t46 = gm_l0;
        uint64_t t47 = GM_LD(uintptr_t, (((uint8_t *)t46) + 16) + 8);
        uint8_t t48 = ((t47 == ((uint64_t)0ULL)));
        void * t49 = gm_l1;
        uint64_t t50 = GM_LD(uintptr_t, (((uint8_t *)t49) + 16) + 8);
        uint8_t t51 = ((t50 == ((uint64_t)0ULL)));
        uint8_t t52 = (!(t48 == t51));
        if (!t52) goto ie45;
        {
            void * t54 = gm_l1;
            uint64_t t55 = GM_LD(uintptr_t, (((uint8_t *)t54) + 16) + 8);
            uint8_t t56 = ((t55 == ((uint64_t)0ULL)));
            uint8_t t57 = 0;
            F.ret = t56;
            goto gm_fret;
        }
        goto ix45;
ie45:;
ix45:;
    }
    {
        void * t58 = gm_l0;
        gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)t58) + 0));
        void * t60 = gm_l1;
        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)t60) + 0));
        gm_b16_8 t62 = t59;
        gm_b16_8 t63 = t61;
        uint8_t t64 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t62), ((uint8_t *)&t63));
        uint8_t t65 = 0;
        F.ret = t64;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_decl_items_l330ksi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_decl_items_l330ksi F;
    gm_b16_8 gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 311u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_f_list_epvducg((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        F.l2 = ((uint64_t)0ULL);
    }
    {
        F.l3 = ((int32_t)0LL);
    }
    {
        F.l4 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t3 = F.l0;
            gm_l5 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlu84m640fm9, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l5;
        gm_b0_1 t9 = gm_f_lines_9uns0k2_sa157kc((void *)NULL, (&wn5), NULL, t8);
        if (gm_unw.kind) {
            goto gm_ll7;
        }
        F.w0 = t9;
        goto gm_ls10;
gm_ll7:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we6; }
        goto gm_lad;
gm_ls10:;
we6:;
        gm_b0_1 t11 = F.w0;
        gm_b0_1 t12 = t11;
    }
    {
        uint64_t t14 = F.l2;
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
        uint8_t t16 = (t14 < t15);
        if (!t16) goto ie13;
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
        void * t18 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l0)));
        uint64_t t19 = F.l2;
        uint64_t t20 = (uintptr_t)t19;
        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
        uint64_t t22 = (uintptr_t)t21;
        if (t20 > t22 || t22 > (uintptr_t)t17) gm_trap_slice(t20, t22, t17, gm_srccfhjr4r9mdef, 383, 38);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t18 + t20 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22 - t20));
        gm_b16_8 t24 = gm_f_trim_fdrfe6i(t23);
        gm_b16_8 t25 = t24;
        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&t25) + 8);
        uint8_t t27 = (t26 > ((uint64_t)0ULL));
        if (!t27) goto ie13;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
            void * t31 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l0)));
            uint64_t t32 = F.l2;
            uint64_t t33 = (uintptr_t)t32;
            uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
            uint64_t t35 = (uintptr_t)t34;
            if (t33 > t35 || t35 > (uintptr_t)t30) gm_trap_slice(t33, t35, t30, gm_srccfhjr4r9mdef, 383, 82);
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t31 + t33 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(t35 - t33));
            gm_tftb1odc739t5 t37 = gm_f_push_3ef746b(t29, t36);
            gm_tftb1odc739t5 t38 = t37;
            if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srccfhjr4r9mdef, 383, 71);
            gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t41 = 0;
        F.ret = t40;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_declared_name_hovvm2b(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_declared_name_hovvm2b F;
    gm_b16_8 gm_l0; /* item */
    gm_b16_8 gm_l1; /* text */
    uint64_t gm_l2; /* nl */
    gm_b16_8 gm_l3; /* rest */
    uint64_t gm_l4; /* j */
    gm_b16_8 gm_l5; /* name */
    int32_t gm_l6; /* depth */
    uint64_t gm_l7; /* i */
    uint8_t gm_l8; /* c */
    uint64_t gm_l9; /* j */
    GM_STACK_CHECK(&F, 1044u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_l1 = t2;
    }
    {
        for (;;) {
            gm_b16_8 t4 = gm_l1;
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_so7qmna0fo4so));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(5));
            uint8_t t6 = gm_f_starts_with_6vvt01e(t4, t5);
            if (!t6) goto lb3;
            {
                gm_b16_8 t9 = gm_l1;
                gm_b16_8 t10;
                GM_ST(void *, (uint8_t *)&t10, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(1));
                gm_tf5srh4mp4b3i t11 = gm_f_find_nab29co(t9, t10);
                gm_tf5srh4mp4b3i t12 = t11;
                uint64_t t13;
                if (((((uint8_t *)&t12))[8] != 0)) {
                    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&t12) + 0));
                    t13 = t14;
                } else {
                    gm_b16_8 t15;
                    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(0));
                    uint8_t t16 = 0;
                    F.ret = t15;
                    goto gm_fret;
                }
                gm_l2 = t13;
            }
            {
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                void * t18 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                uint64_t t19 = gm_l2;
                uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
                if (o21) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 451, 21);
                uint64_t t22 = r20;
                uint64_t t23 = (uintptr_t)t22;
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint64_t t25 = (uintptr_t)t24;
                if (t23 > t25 || t25 > (uintptr_t)t17) gm_trap_slice(t23, t25, t17, gm_srccfhjr4r9mdef, 451, 16);
                gm_b16_8 t26;
                GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t18 + t23 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                gm_l1 = t26;
            }
lc3:;
        }
lb3:;
    }
    {
        gm_b16_8 t28 = gm_l1;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_saalafjdd3tnp));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(8));
        uint8_t t30 = gm_f_starts_with_6vvt01e(t28, t29);
        if (!t30) goto ie27;
        {
            uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            void * t33 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_saalafjdd3tnp));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(8));
            gm_b16_8 t35 = t34;
            uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&t35) + 8);
            uint64_t t37 = (uintptr_t)t36;
            uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t t39 = (uintptr_t)t38;
            if (t37 > t39 || t39 > (uintptr_t)t32) gm_trap_slice(t37, t39, t32, gm_srccfhjr4r9mdef, 454, 17);
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(((uint8_t *)t33 + t37 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(t39 - t37));
            gm_l3 = t40;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t42 = gm_l4;
                uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t44 = (t42 < t43);
                if (!t44) goto lb41;
                uint64_t t45 = gm_l4;
                uint64_t t46 = (uintptr_t)t45;
                uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t46 >= (uintptr_t)(t47)) gm_trap_index(t46, t47, gm_srccfhjr4r9mdef, 456, 43);
                uint8_t t48 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t46 * 1));
                uint8_t t49 = gm_f_ident_char_pmpasvq(t48);
                if (!t49) goto lb41;
                {
                    uint64_t t52 = gm_l4;
                    uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                    if (o54) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 456, 54);
                    uint64_t t55 = r53;
                    gm_l4 = t55;
                }
lc41:;
            }
lb41:;
        }
        {
            uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            void * t57 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
            uint64_t t58 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t59 = gm_l4;
            uint64_t t60 = (uintptr_t)t59;
            if (t58 > t60 || t60 > (uintptr_t)t56) gm_trap_slice(t58, t60, t56, gm_srccfhjr4r9mdef, 457, 16);
            gm_b16_8 t61;
            GM_ST(void *, (uint8_t *)&t61, (void *)(((uint8_t *)t57 + t58 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(t60 - t58));
            uint8_t t62 = 0;
            F.ret = t61;
            goto gm_fret;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        gm_b16_8 t64 = gm_l1;
        gm_b16_8 t65;
        GM_ST(void *, (uint8_t *)&t65, (void *)(gm_seccpktgmrevt));
        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(1));
        uint8_t t66 = gm_f_starts_with_6vvt01e(t64, t65);
        if (!t66) goto ie63;
        {
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(0));
            uint8_t t69 = 0;
            F.ret = t68;
            goto gm_fret;
        }
        goto ix63;
ie63:;
ix63:;
    }
    {
        gm_b16_8 t70;
        GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(0));
        gm_l5 = t70;
    }
    {
        gm_l6 = ((int32_t)0LL);
    }
    {
        gm_l7 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t72 = gm_l7;
            uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t74 = (t72 < t73);
            if (!t74) goto lb71;
            {
                uint64_t t77 = gm_l7;
                uint64_t t78 = (uintptr_t)t77;
                uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t78 >= (uintptr_t)(t79)) gm_trap_index(t78, t79, gm_srccfhjr4r9mdef, 464, 14);
                uint8_t t80 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t78 * 1));
                gm_l8 = t80;
            }
            {
                uint8_t t82 = 0;
                uint8_t t83 = gm_l8;
                uint8_t t84 = ((t83 == ((uint8_t)34ULL)));
                if (!t84) {
                    uint8_t t85 = gm_l8;
                    uint8_t t86 = ((t85 == ((uint8_t)39ULL)));
                    t82 = t86;
                } else t82 = 1;
                if (!t82) goto ie81;
                {
                    gm_b16_8 t88 = gm_l1;
                    uint64_t t89 = gm_l7;
                    uint64_t t90 = gm_f_skip_literal_17lq2p8(t88, t89);
                    gm_l7 = t90;
                }
                {
                    goto lc71;
                }
                goto ix81;
ie81:;
ix81:;
            }
            {
                uint8_t t92 = gm_l8;
                gm_toh0ootodhjue t93;
                GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)40ULL));
                GM_ST(uint8_t, ((uint8_t *)&t93 + 1), ((uint8_t)91ULL));
                GM_ST(uint8_t, ((uint8_t *)&t93 + 2), ((uint8_t)123ULL));
                gm_toh0ootodhjue t94 = t93;
                uint8_t t95 = 0;
                uint8_t t96 = t92;
                if (gm_eqjmccqdb90f(((uint8_t *)&t94) + 0, ((uint8_t *)&t96))) t95 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t94) + 1, ((uint8_t *)&t96))) t95 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t94) + 2, ((uint8_t *)&t96))) t95 = 1;
                uint8_t t97 = (uint8_t)(t95);
                if (!t97) goto ie91;
                {
                    int32_t t99 = gm_l6;
                    int32_t r100; bool o101 = __builtin_add_overflow(t99, ((int32_t)1LL), &r100);
                    if (o101) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 470, 13);
                    int32_t t102 = r100;
                    gm_l6 = t102;
                }
                goto ix91;
ie91:;
                uint8_t t104 = gm_l8;
                gm_toh0ootodhjue t105;
                GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)41ULL));
                GM_ST(uint8_t, ((uint8_t *)&t105 + 1), ((uint8_t)93ULL));
                GM_ST(uint8_t, ((uint8_t *)&t105 + 2), ((uint8_t)125ULL));
                gm_toh0ootodhjue t106 = t105;
                uint8_t t107 = 0;
                uint8_t t108 = t104;
                if (gm_eqjmccqdb90f(((uint8_t *)&t106) + 0, ((uint8_t *)&t108))) t107 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t106) + 1, ((uint8_t *)&t108))) t107 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t106) + 2, ((uint8_t *)&t108))) t107 = 1;
                uint8_t t109 = (uint8_t)(t107);
                if (!t109) goto ie103;
                {
                    int32_t t111 = gm_l6;
                    int32_t r112; bool o113 = __builtin_sub_overflow(t111, ((int32_t)1LL), &r112);
                    if (o113) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 472, 13);
                    int32_t t114 = r112;
                    gm_l6 = t114;
                }
                goto ix103;
ie103:;
                uint8_t t116 = gm_l8;
                uint8_t t117 = ((t116 == ((uint8_t)61ULL)));
                if (!t117) goto ie115;
                int32_t t118 = gm_l6;
                uint8_t t119 = ((t118 == ((int32_t)0LL)));
                if (!t119) goto ie115;
                {
                    goto lb71;
                }
                goto ix115;
ie115:;
                uint8_t t122 = gm_l8;
                uint8_t t123 = gm_f_ident_char_pmpasvq(t122);
                if (!t123) goto ie121;
                {
                    uint64_t t125 = gm_l7;
                    gm_l9 = t125;
                }
                {
                    for (;;) {
                        uint64_t t127 = gm_l9;
                        uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        uint8_t t129 = (t127 < t128);
                        if (!t129) goto lb126;
                        uint64_t t130 = gm_l9;
                        uint64_t t131 = (uintptr_t)t130;
                        uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t131 >= (uintptr_t)(t132)) gm_trap_index(t131, t132, gm_srccfhjr4r9mdef, 477, 47);
                        uint8_t t133 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t131 * 1));
                        uint8_t t134 = gm_f_ident_char_pmpasvq(t133);
                        if (!t134) goto lb126;
                        {
                            uint64_t t137 = gm_l9;
                            uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                            if (o139) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 477, 58);
                            uint64_t t140 = r138;
                            gm_l9 = t140;
                        }
lc126:;
                    }
lb126:;
                }
                {
                    int32_t t142 = gm_l6;
                    uint8_t t143 = ((t142 == ((int32_t)0LL)));
                    if (!t143) goto ie141;
                    uint64_t t144 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    void * t145 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                    uint64_t t146 = gm_l7;
                    uint64_t t147 = (uintptr_t)t146;
                    uint64_t t148 = gm_l9;
                    uint64_t t149 = (uintptr_t)t148;
                    if (t147 > t149 || t149 > (uintptr_t)t144) gm_trap_slice(t147, t149, t144, gm_srccfhjr4r9mdef, 478, 31);
                    gm_b16_8 t150;
                    GM_ST(void *, (uint8_t *)&t150, (void *)(((uint8_t *)t145 + t147 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(t149 - t147));
                    gm_b16_8 t151;
                    GM_ST(void *, (uint8_t *)&t151, (void *)(gm_s7ipvt5onv6ht));
                    GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(3));
                    uint8_t t152 = gm_f_starts_with_6vvt01e(t150, t151);
                    if (!t152) goto ie141;
                    {
                        uint64_t t154 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        void * t155 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                        uint64_t t156 = gm_l7;
                        uint64_t t157 = (uintptr_t)t156;
                        uint64_t t158 = gm_l9;
                        uint64_t t159 = (uintptr_t)t158;
                        if (t157 > t159 || t159 > (uintptr_t)t154) gm_trap_slice(t157, t159, t154, gm_srccfhjr4r9mdef, 478, 70);
                        gm_b16_8 t160;
                        GM_ST(void *, (uint8_t *)&t160, (void *)(((uint8_t *)t155 + t157 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(t159 - t157));
                        gm_l5 = t160;
                    }
                    goto ix141;
ie141:;
ix141:;
                }
                {
                    uint64_t t161 = gm_l9;
                    gm_l7 = t161;
                }
                {
                    goto lc71;
                }
                goto ix121;
ie121:;
ix121:;
ix115:;
ix103:;
ix91:;
            }
            {
                uint64_t t162 = gm_l7;
                uint64_t r163; bool o164 = __builtin_add_overflow(t162, ((uint64_t)1ULL), &r163);
                if (o164) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 482, 9);
                uint64_t t165 = r163;
                gm_l7 = t165;
            }
lc71:;
        }
lb71:;
    }
    {
        gm_b16_8 t166 = gm_l5;
        uint8_t t167 = 0;
        F.ret = t166;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_decls_of_srep9du(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_decls_of_srep9du F;
    void * gm_l0; /* out */
    gm_b16_8 gm_l1; /* text */
    uint8_t gm_l2; /* rank */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* item */
    GM_STACK_CHECK(&F, 319u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_b16_8 t4 = gm_f_decl_items_l330ksi((void *)NULL, cx, NULL, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
                    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 16));
                    gm_l5 = t16;
                    {
                        void * t18 = gm_l0;
                        gm_t84tkl8vf34ko t19;
                        memset((uint8_t *)&t19 + 49, 0, 3);
                        gm_b16_8 t20 = gm_l5;
                        GM_ST(gm_b16_8, (uint8_t *)&t19 + 0, t20);
                        gm_b16_8 t21 = gm_l5;
                        gm_b16_8 t22 = gm_f_declared_name_hovvm2b(t21);
                        GM_ST(gm_b16_8, (uint8_t *)&t19 + 16, t22);
                        gm_b16_8 t23 = gm_l5;
                        gm_b16_8 t24 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t23);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t19 + 32, t24);
                        uint8_t t25 = gm_l2;
                        GM_ST(uint8_t, (uint8_t *)&t19 + 48, t25);
                        GM_ST(uint32_t, (uint8_t *)&t19 + 52, ((uint32_t)0ULL));
                        gm_tftb1odc739t5 t26 = gm_f_push_rda4o9n(t18, &t19);
                        gm_tftb1odc739t5 t27 = t26;
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srccfhjr4r9mdef, 354, 9);
                        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 353, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t30 = gm_l4;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l4 = t33;
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
inline __attribute__((aligned(4))) void gm_f_external_f65nm8q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_external_f65nm8q F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* r */
    gm_b16_8 gm_l2; /* rest */
    gm_b16_8 gm_l3; /* r */
    gm_b16_8 gm_l4; /* r */
    GM_STACK_CHECK(&F, 949u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s8sgcefhqa4i0));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(42));
        gm_th2lqbo77pvsc t5;
        gm_f_strip_prefix_b4949s8((void *)&t5, t3, t4);
        gm_th2lqbo77pvsc t6 = t5;
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l1 = t7;
        {
            gm_b16_8 t9 = gm_l1;
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_tabb710d9mq2e t11;
            gm_f_format_deiomvg((void *)&t11, (void *)NULL, cx, NULL, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t12 = t11;
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 8, gm_srccfhjr4r9mdef, 213, 91);
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
            gm_b16_8 t14 = t13;
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            gm_th2lqbo77pvsc t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t16 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[16] = 1;
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t19 = gm_l0;
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_shv3ttlq9l359));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(14));
        uint8_t t21 = gm_f_starts_with_6vvt01e(t19, t20);
        if (!t21) goto ie18;
        {
            gm_th2lqbo77pvsc t23;
            memset(&t23, 0, sizeof t23);
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(0));
        gm_l2 = t25;
    }
    {
        gm_b16_8 t27 = gm_l0;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_soa9mnl4ahjud));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(7));
        gm_th2lqbo77pvsc t29;
        gm_f_strip_prefix_b4949s8((void *)&t29, t27, t28);
        gm_th2lqbo77pvsc t30 = t29;
        if (!((((uint8_t *)&t30))[16] != 0)) goto ie26;
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
        gm_l3 = t31;
        {
            gm_b16_8 t33 = gm_l3;
            gm_l2 = t33;
        }
        goto ix26;
ie26:;
        {
            gm_b16_8 t35 = gm_l0;
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s1oiqt105ulvb));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(19));
            gm_th2lqbo77pvsc t37;
            gm_f_strip_prefix_b4949s8((void *)&t37, t35, t36);
            gm_th2lqbo77pvsc t38 = t37;
            gm_b16_8 t39;
            if (((((uint8_t *)&t38))[16] != 0)) {
                gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 0));
                t39 = t40;
            } else {
                gm_th2lqbo77pvsc t41;
                memset(&t41, 0, sizeof t41);
                uint8_t t42 = 0;
                F.ret = t41;
                goto gm_fret;
            }
            gm_l4 = t39;
        }
        {
            gm_b16_8 t43 = gm_l4;
            gm_tnpifv20irnph t44;
            GM_ST(gm_b16_8, ((uint8_t *)&t44 + 0), t43);
            gm_tabb710d9mq2e t45;
            gm_f_format_69nn7l3((void *)&t45, (void *)NULL, cx, NULL, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t46 = t45;
            if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_srccfhjr4r9mdef, 220, 16);
            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
            gm_b16_8 t48 = t47;
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            gm_l2 = t49;
        }
ix26:;
    }
    {
        gm_b16_8 t50 = gm_l2;
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_so0h0lkl6bp4l));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(38));
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(0));
        gm_tabb710d9mq2e t53;
        gm_f_replace_alloc_t7hndlq((void *)&t53, (void *)NULL, cx, NULL, t50, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t54 = t53;
        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 8, gm_srccfhjr4r9mdef, 222, 12);
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
        gm_b16_8 t56 = t55;
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
        gm_th2lqbo77pvsc t58;
        GM_ST(uint8_t, (uint8_t *)&t58 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t58 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t58 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t58), t57);
        ((uint8_t *)&t58)[16] = 1;
        uint8_t t59 = 0;
        F.ret = t58;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_575gb0c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_575gb0c F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, ((uint8_t *)t1));
    uint64_t t3 = (uintptr_t)t2;
    uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_unit_text_mmejmp8 *)((&F.h)->up))->l0) + 8);
    if (t3 >= (uintptr_t)(t4)) gm_trap_index(t3, t4, gm_srccfhjr4r9mdef, 659, 73);
    void * t5 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&((struct gm_fr_unit_text_mmejmp8 *)((&F.h)->up))->l0)) + t3 * 56);
    void * t6 = gm_l1;
    uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)t6));
    uint64_t t8 = (uintptr_t)t7;
    uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_unit_text_mmejmp8 *)((&F.h)->up))->l0) + 8);
    if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srccfhjr4r9mdef, 659, 85);
    void * t10 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&((struct gm_fr_unit_text_mmejmp8 *)((&F.h)->up))->l0)) + t8 * 56);
    uint8_t t11 = gm_f_decl_before_fp800ft(t5, t10);
    F.ret = t11;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_if8ammt(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_if8ammt F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK(&F, 263u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
    void * t4 = gm_l1;
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 0));
    gm_b16_8 t6 = t3;
    gm_b16_8 t7 = t5;
    uint8_t t8 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t6), ((uint8_t *)&t7));
    if (!t8) {
        uint8_t t9 = 0;
        void * t10 = gm_l0;
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 0));
        void * t12 = gm_l1;
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 0));
        gm_b16_8 t14 = t11;
        gm_b16_8 t15 = t13;
        uint8_t t16 = (gm_eq0iqiubfm7e(((uint8_t *)&t14), ((uint8_t *)&t15)));
        if (t16) {
            void * t17 = gm_l0;
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t17) + 16));
            void * t19 = gm_l1;
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t19) + 16));
            gm_b16_8 t21 = t18;
            gm_b16_8 t22 = t20;
            uint8_t t23 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22));
            t9 = t23;
        } else t9 = 0;
        t1 = t9;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_efhhmqv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_efhhmqv F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* d */
    GM_STACK_CHECK(&F, 794u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t6 = gm_l1;
        gm_th2lqbo77pvsc t7;
        gm_f_c_fn_name_77uspqp((void *)&t7, t6);
        gm_th2lqbo77pvsc t8 = t7;
        if (!((((uint8_t *)&t8))[16] != 0)) goto ie5;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_l2 = t9;
        void * t10 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l3);
        gm_b16_8 t11 = gm_l2;
        gm_ttsm01t0d8srg t12 = gm_f_get_ece4opn(t10, t11);
        gm_ttsm01t0d8srg t13 = t12;
        uint8_t t14;
        if (((((uint8_t *)&t13))[0] != 0)) goto mn16;
        t14 = ((uint8_t)0);
        goto me15;
mn16:;
        t14 = ((uint8_t)1);
        goto me15;
mn17:;
        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 116, 40);
me15:;
        if (!t14) goto ie5;
        {
            gm_b16_8 t19 = gm_l1;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_soa9mnl4ahjud));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(7));
            gm_th2lqbo77pvsc t21;
            gm_f_strip_prefix_b4949s8((void *)&t21, t19, t20);
            gm_th2lqbo77pvsc t22 = t21;
            gm_b16_8 t23;
            if (((((uint8_t *)&t22))[16] != 0)) {
                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 0));
                t23 = t24;
            } else {
                gm_b16_8 t25 = gm_l1;
                t23 = t25;
            }
            gm_l3 = t23;
        }
        {
            gm_b16_8 t27 = gm_l3;
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_sd1uj6gf5v8qh));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(7));
            uint8_t t29 = gm_f_contains_gkki4kq(t27, t28);
            uint8_t t30 = (uint8_t)!t29;
            if (!t30) goto ie26;
            {
                gm_b16_8 t32 = gm_l3;
                gm_tnpifv20irnph t33;
                GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
                gm_tabb710d9mq2e t34;
                gm_f_format_rse2efa((void *)&t34, (void *)NULL, cx, NULL, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t35 = t34;
                if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_srccfhjr4r9mdef, 118, 53);
                gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
                gm_b16_8 t37 = t36;
                gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
                gm_l3 = t38;
            }
            goto ix26;
ie26:;
ix26:;
        }
        {
            void * t39 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
            gm_b16_8 t40 = gm_l3;
            gm_tftb1odc739t5 t41 = gm_f_extend_0cs85gs(t39, t40);
            gm_tftb1odc739t5 t42 = t41;
            if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srccfhjr4r9mdef, 119, 13);
            gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
        }
        {
            void * t44 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
            gm_tftb1odc739t5 t45 = gm_f_push_t83rg2m(t44, ((uint8_t)10ULL));
            gm_tftb1odc739t5 t46 = t45;
            if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_srccfhjr4r9mdef, 120, 13);
            gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
        }
        {
            void * t48 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l11);
            gm_b16_8 t49 = gm_l1;
            gm_th2lqbo77pvsc t50;
            gm_f_external_f65nm8q((void *)&t50, (void *)NULL, cx, NULL, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t51 = t50;
            gm_b16_8 t52;
            if (((((uint8_t *)&t51))[16] != 0)) {
                gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 0));
                t52 = t53;
            } else {
                gm_b16_8 t54 = gm_l1;
                t52 = t54;
            }
            gm_tnpifv20irnph t55;
            GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t52);
            gm_tftb1odc739t5 t56 = gm_f_write_d8c7tpm(t48, t55);
            gm_tftb1odc739t5 t57 = t56;
            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srccfhjr4r9mdef, 121, 13);
            gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
        }
        {
            gm_b0_1 t59;
            uint8_t t60 = 0;
            F.ret = t59;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t61 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
        gm_b16_8 t62 = gm_l1;
        gm_th2lqbo77pvsc t63;
        gm_f_external_f65nm8q((void *)&t63, (void *)NULL, cx, NULL, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t64 = t63;
        gm_b16_8 t65;
        if (((((uint8_t *)&t64))[16] != 0)) {
            gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 0));
            t65 = t66;
        } else {
            gm_b16_8 t67 = gm_l1;
            t65 = t67;
        }
        gm_tftb1odc739t5 t68 = gm_f_extend_0cs85gs(t61, t65);
        gm_tftb1odc739t5 t69 = t68;
        if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 1, gm_srccfhjr4r9mdef, 124, 9);
        gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 1));
    }
    {
        void * t71 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
        gm_tftb1odc739t5 t72 = gm_f_push_t83rg2m(t71, ((uint8_t)10ULL));
        gm_tftb1odc739t5 t73 = t72;
        if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 1, gm_srccfhjr4r9mdef, 125, 9);
        gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 115, 5);
me1:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_fe8qj55(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_fe8qj55 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    uint64_t gm_l2; /* end */
    gm_b16_8 gm_l3; /* t */
    GM_STACK_CHECK(&F, 484u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l4));
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r7; bool o8 = __builtin_add_overflow(t5, t6, &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 366, 16);
        uint64_t t9 = r7;
        gm_l2 = t9;
    }
    {
        uint64_t t11 = gm_l2;
        uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l0) + 8);
        uint8_t t13 = (t11 < t12);
        if (!t13) goto ie10;
        {
            uint64_t t15 = gm_l2;
            uint64_t r16; bool o17 = __builtin_add_overflow(t15, ((uint64_t)1ULL), &r16);
            if (o17) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 367, 29);
            uint64_t t18 = r16;
            gm_l2 = t18;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        int32_t t20 = GM_LD(int32_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l3));
        uint8_t t21 = ((t20 == ((int32_t)0LL)));
        if (!t21) goto ie19;
        gm_b16_8 t22 = gm_l1;
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_seccpktgmrevt));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
        uint8_t t24 = gm_f_starts_with_6vvt01e(t22, t23);
        if (!t24) goto ie19;
        {
            void * t26 = (void *)((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l1);
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l0) + 8);
            void * t28 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l0)));
            uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l2));
            uint64_t t30 = (uintptr_t)t29;
            uint64_t t31 = gm_l2;
            uint64_t t32 = (uintptr_t)t31;
            if (t30 > t32 || t32 > (uintptr_t)t27) gm_trap_slice(t30, t32, t27, gm_srccfhjr4r9mdef, 369, 24);
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t28 + t30 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t30));
            gm_tftb1odc739t5 t34 = gm_f_push_3ef746b(t26, t33);
            gm_tftb1odc739t5 t35 = t34;
            if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_srccfhjr4r9mdef, 369, 13);
            gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
        }
        {
            uint64_t t37 = gm_l2;
            GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l2), t37);
        }
        {
            uint64_t t38 = gm_l2;
            GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l4), t38);
        }
        {
            gm_b0_1 t39;
            uint8_t t40 = 0;
            F.ret = t39;
            goto gm_fret;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        int32_t t41 = GM_LD(int32_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l3));
        gm_b16_8 t42 = gm_l1;
        int32_t t43 = gm_f_brace_balance_aofv41t(t42);
        int32_t r44; bool o45 = __builtin_add_overflow(t41, t43, &r44);
        if (o45) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 374, 9);
        int32_t t46 = r44;
        GM_ST(int32_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l3), t46);
    }
    {
        gm_b16_8 t47 = gm_l1;
        gm_b16_8 t48 = gm_f_trim_end_6sjv6rg(t47);
        gm_l3 = t48;
    }
    {
        int32_t t50 = GM_LD(int32_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l3));
        uint8_t t51 = (t50 <= ((int32_t)0LL));
        if (!t51) goto ie49;
        uint8_t t52 = 0;
        gm_b16_8 t53 = gm_l3;
        gm_b16_8 t54;
        GM_ST(void *, (uint8_t *)&t54, (void *)(gm_spod3tmkh25up));
        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(1));
        uint8_t t55 = gm_f_ends_with_biriu5l(t53, t54);
        if (!t55) {
            gm_b16_8 t56 = gm_l3;
            gm_b16_8 t57;
            GM_ST(void *, (uint8_t *)&t57, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(1));
            uint8_t t58 = gm_f_ends_with_biriu5l(t56, t57);
            t52 = t58;
        } else t52 = 1;
        if (!t52) goto ie49;
        {
            void * t60 = (void *)((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l1);
            uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l0) + 8);
            void * t62 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l0)));
            uint64_t t63 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l2));
            uint64_t t64 = (uintptr_t)t63;
            uint64_t t65 = gm_l2;
            uint64_t t66 = (uintptr_t)t65;
            if (t64 > t66 || t66 > (uintptr_t)t61) gm_trap_slice(t64, t66, t61, gm_srccfhjr4r9mdef, 377, 24);
            gm_b16_8 t67;
            GM_ST(void *, (uint8_t *)&t67, (void *)(((uint8_t *)t62 + t64 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(t66 - t64));
            gm_tftb1odc739t5 t68 = gm_f_push_3ef746b(t60, t67);
            gm_tftb1odc739t5 t69 = t68;
            if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 1, gm_srccfhjr4r9mdef, 377, 13);
            gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 1));
        }
        {
            uint64_t t71 = gm_l2;
            GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l2), t71);
        }
        {
            GM_ST(int32_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l3), ((int32_t)0LL));
        }
        goto ix49;
ie49:;
ix49:;
    }
    {
        uint64_t t72 = gm_l2;
        GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l4), t72);
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 365, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hfbghc3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_hfbghc3 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* def */
    gm_t6j6smj7qoctu gm_l3; /* eq */
    gm_b16_8 gm_l4; /* head */
    gm_b16_8 gm_l5; /* d */
    gm_b16_8 gm_l6; /* name */
    GM_STACK_CHECK(&F, 1584u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie5;
        {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        uint8_t t12 = 0;
        uint8_t t13 = 0;
        gm_b16_8 t14 = gm_l1;
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_seccpktgmrevt));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(1));
        uint8_t t16 = gm_f_starts_with_6vvt01e(t14, t15);
        if (!t16) {
            gm_b16_8 t17 = gm_l1;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sasea0sobfkrs));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(7));
            uint8_t t19 = gm_f_starts_with_6vvt01e(t17, t18);
            t13 = t19;
        } else t13 = 1;
        if (!t13) {
            gm_b16_8 t20 = gm_l1;
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_shv3ttlq9l359));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(14));
            uint8_t t22 = gm_f_starts_with_6vvt01e(t20, t21);
            t12 = t22;
        } else t12 = 1;
        if (!t12) goto ie11;
        {
            void * t24 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l1);
            gm_t84tkl8vf34ko t25;
            memset((uint8_t *)&t25 + 49, 0, 3);
            gm_b16_8 t26 = gm_l1;
            gm_tnpifv20irnph t27;
            GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t26);
            gm_tabb710d9mq2e t28;
            gm_f_format_uaq30dv((void *)&t28, (void *)NULL, cx, NULL, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t29 = t28;
            if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_srccfhjr4r9mdef, 551, 35);
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
            gm_b16_8 t31 = t30;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t32);
            gm_b16_8 t33 = gm_l1;
            gm_b16_8 t34 = gm_f_declared_name_hovvm2b(t33);
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 16, t34);
            gm_b16_8 t35 = gm_l1;
            gm_b16_8 t36 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 32, t36);
            uint8_t t37 = GM_LD(uint8_t, gm_c4crumb5mc9);
            GM_ST(uint8_t, (uint8_t *)&t25 + 48, t37);
            GM_ST(uint32_t, (uint8_t *)&t25 + 52, ((uint32_t)0ULL));
            gm_tftb1odc739t5 t38 = gm_f_push_rda4o9n(t24, &t25);
            gm_tftb1odc739t5 t39 = t38;
            if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srccfhjr4r9mdef, 551, 13);
            gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
        }
        {
            gm_b0_1 t41;
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b16_8 t43 = gm_l1;
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_soa9mnl4ahjud));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(7));
        gm_th2lqbo77pvsc t45;
        gm_f_strip_prefix_b4949s8((void *)&t45, t43, t44);
        gm_th2lqbo77pvsc t46 = t45;
        gm_b16_8 t47;
        if (((((uint8_t *)&t46))[16] != 0)) {
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 0));
            t47 = t48;
        } else {
            gm_ttu09lij36f8c t49;
            memset(&t49, 0, sizeof t49);
            uint8_t t50 = 0;
            ((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->ret = t49;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t50;
            goto gm_lad;
        }
        gm_l2 = t47;
    }
    {
        gm_b16_8 t51;
        gm_b16_8 t53 = gm_l2;
        gm_b16_8 t54;
        GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sao4mumvadoct));
        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(3));
        gm_ttu09lij36f8c t55;
        gm_f_split_once_mqcu1ce((void *)&t55, t53, t54);
        gm_ttu09lij36f8c t56 = t55;
        if (!((((uint8_t *)&t56))[32] != 0)) goto ie52;
        gm_t6j6smj7qoctu t57 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t56));
        gm_l3 = t57;
        gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
        gm_b16_8 t60 = t59;
        t51 = t60;
        goto ix52;
ie52:;
        gm_b16_8 t62 = gm_l2;
        gm_b16_8 t63;
        GM_ST(void *, (uint8_t *)&t63, (void *)(gm_spod3tmkh25up));
        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(1));
        gm_th2lqbo77pvsc t64;
        gm_f_strip_suffix_8155gbi((void *)&t64, t62, t63);
        gm_th2lqbo77pvsc t65 = t64;
        gm_b16_8 t66;
        if (((((uint8_t *)&t65))[16] != 0)) {
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 0));
            t66 = t67;
        } else {
            gm_ttu09lij36f8c t68;
            memset(&t68, 0, sizeof t68);
            uint8_t t69 = 0;
            ((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->ret = t68;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t69;
            goto gm_lad;
        }
        gm_b16_8 t70 = t66;
        t51 = t70;
ix52:;
        gm_l4 = t51;
    }
    {
        gm_b16_8 t71 = gm_l4;
        gm_tnpifv20irnph t72;
        GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t71);
        gm_tabb710d9mq2e t73;
        gm_f_format_8l8mb6d((void *)&t73, (void *)NULL, cx, NULL, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t74 = t73;
        if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 8, gm_srccfhjr4r9mdef, 556, 14);
        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t74) + 8));
        gm_l5 = t75;
    }
    {
        gm_b16_8 t76 = gm_l5;
        gm_b16_8 t77 = t76;
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
        gm_b16_8 t79 = gm_f_declared_name_hovvm2b(t78);
        gm_l6 = t79;
    }
    {
        void * t80 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l1);
        gm_t84tkl8vf34ko t81;
        memset((uint8_t *)&t81 + 49, 0, 3);
        gm_b16_8 t82 = gm_l5;
        gm_tnpifv20irnph t83;
        GM_ST(gm_b16_8, ((uint8_t *)&t83 + 0), t82);
        gm_tabb710d9mq2e t84;
        gm_f_format_1u4jnnu((void *)&t84, (void *)NULL, cx, NULL, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t85 = t84;
        if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 8, gm_srccfhjr4r9mdef, 558, 31);
        gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&t85) + 8));
        gm_b16_8 t87 = t86;
        gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
        GM_ST(gm_b16_8, (uint8_t *)&t81 + 0, t88);
        gm_b16_8 t89 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t81 + 16, t89);
        gm_b16_8 t90 = gm_l5;
        gm_b16_8 t91 = t90;
        gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
        gm_b16_8 t93 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t92);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t81 + 32, t93);
        uint8_t t94 = GM_LD(uint8_t, gm_c4crumb5mc9);
        GM_ST(uint8_t, (uint8_t *)&t81 + 48, t94);
        GM_ST(uint32_t, (uint8_t *)&t81 + 52, ((uint32_t)0ULL));
        gm_tftb1odc739t5 t95 = gm_f_push_rda4o9n(t80, &t81);
        gm_tftb1odc739t5 t96 = t95;
        if (((((uint8_t *)&t96))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t96) + 1, gm_srccfhjr4r9mdef, 558, 9);
        gm_b0_1 t97 = GM_LD(gm_b0_1, (((uint8_t *)&t96) + 1));
    }
    {
        gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l19) + 0));
        gm_b16_8 t99 = t98;
        gm_b16_8 t100 = gm_l6;
        uint64_t t101 = gm_f_hash64_apkb7bo(t100);
        if (((uint64_t)16ULL) == 0) gm_trap("division by zero", gm_srccfhjr4r9mdef, 559, 23);
        uint64_t t102 = (uint64_t)(t101 % ((uint64_t)16ULL));
        uint64_t t103 = (uint64_t)t102;
        uint64_t t104 = (uintptr_t)t103;
        gm_b16_8 t105 = t99;
        uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&t105) + 8);
        if (t104 >= (uintptr_t)(t106)) gm_trap_index(t104, t106, gm_srccfhjr4r9mdef, 559, 9);
        void * t107 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t105)) + t104 * 40);
        gm_b16_8 t108 = gm_l2;
        gm_tftb1odc739t5 t109 = gm_f_push_3ef746b(t107, t108);
        gm_tftb1odc739t5 t110 = t109;
        if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 1, gm_srccfhjr4r9mdef, 559, 9);
        gm_b0_1 t111 = GM_LD(gm_b0_1, (((uint8_t *)&t110) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 548, 5);
me1:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_jojopru(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_jojopru F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* def */
    gm_t6j6smj7qoctu gm_l3; /* eq */
    gm_b16_8 gm_l4; /* head */
    GM_STACK_CHECK(&F, 862u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint8_t t6 = 0;
        uint8_t t7 = 0;
        uint8_t t8 = 0;
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
        if (!t10) {
            gm_b16_8 t11 = gm_l1;
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_seccpktgmrevt));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(1));
            uint8_t t13 = gm_f_starts_with_6vvt01e(t11, t12);
            t8 = t13;
        } else t8 = 1;
        if (!t8) {
            gm_b16_8 t14 = gm_l1;
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sasea0sobfkrs));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(7));
            uint8_t t16 = gm_f_starts_with_6vvt01e(t14, t15);
            t7 = t16;
        } else t7 = 1;
        if (!t7) {
            gm_b16_8 t17 = gm_l1;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_shv3ttlq9l359));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(14));
            uint8_t t19 = gm_f_starts_with_6vvt01e(t17, t18);
            t6 = t19;
        } else t6 = 1;
        if (!t6) goto ie5;
        {
            void * t21 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
            gm_b16_8 t22 = gm_l1;
            gm_tftb1odc739t5 t23 = gm_f_extend_0cs85gs(t21, t22);
            gm_tftb1odc739t5 t24 = t23;
            if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srccfhjr4r9mdef, 129, 13);
            gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
        }
        {
            void * t26 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
            gm_tftb1odc739t5 t27 = gm_f_push_t83rg2m(t26, ((uint8_t)10ULL));
            gm_tftb1odc739t5 t28 = t27;
            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srccfhjr4r9mdef, 130, 13);
            gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
        }
        {
            gm_b0_1 t30;
            uint8_t t31 = 0;
            F.ret = t30;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_b16_8 t32 = gm_l1;
        gm_b16_8 t33;
        GM_ST(void *, (uint8_t *)&t33, (void *)(gm_soa9mnl4ahjud));
        GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(7));
        gm_th2lqbo77pvsc t34;
        gm_f_strip_prefix_b4949s8((void *)&t34, t32, t33);
        gm_th2lqbo77pvsc t35 = t34;
        gm_b16_8 t36;
        if (((((uint8_t *)&t35))[16] != 0)) {
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 0));
            t36 = t37;
        } else {
            gm_ttu09lij36f8c t38;
            memset(&t38, 0, sizeof t38);
            uint8_t t39 = 0;
            ((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->ret = t38;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t39;
            goto gm_lad;
        }
        gm_l2 = t36;
    }
    {
        gm_b16_8 t40;
        gm_b16_8 t42 = gm_l2;
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sao4mumvadoct));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(3));
        gm_ttu09lij36f8c t44;
        gm_f_split_once_mqcu1ce((void *)&t44, t42, t43);
        gm_ttu09lij36f8c t45 = t44;
        if (!((((uint8_t *)&t45))[32] != 0)) goto ie41;
        gm_t6j6smj7qoctu t46 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t45));
        gm_l3 = t46;
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
        gm_b16_8 t49 = t48;
        t40 = t49;
        goto ix41;
ie41:;
        gm_b16_8 t51 = gm_l2;
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_spod3tmkh25up));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(1));
        gm_th2lqbo77pvsc t53;
        gm_f_strip_suffix_8155gbi((void *)&t53, t51, t52);
        gm_th2lqbo77pvsc t54 = t53;
        gm_b16_8 t55;
        if (((((uint8_t *)&t54))[16] != 0)) {
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 0));
            t55 = t56;
        } else {
            gm_ttu09lij36f8c t57;
            memset(&t57, 0, sizeof t57);
            uint8_t t58 = 0;
            ((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->ret = t57;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t58;
            goto gm_lad;
        }
        gm_b16_8 t59 = t55;
        t40 = t59;
ix41:;
        gm_l4 = t40;
    }
    {
        void * t60 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
        gm_b16_8 t61 = gm_l4;
        gm_tnpifv20irnph t62;
        GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t61);
        gm_tftb1odc739t5 t63 = gm_f_write_815rgbg(t60, t62);
        gm_tftb1odc739t5 t64 = t63;
        if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_srccfhjr4r9mdef, 135, 9);
        gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
    }
    {
        void * t66 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l10);
        gm_b16_8 t67 = gm_l2;
        gm_tftb1odc739t5 t68 = gm_f_extend_0cs85gs(t66, t67);
        gm_tftb1odc739t5 t69 = t68;
        if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 1, gm_srccfhjr4r9mdef, 136, 9);
        gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 1));
    }
    {
        void * t71 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l10);
        gm_tftb1odc739t5 t72 = gm_f_push_t83rg2m(t71, ((uint8_t)10ULL));
        gm_tftb1odc739t5 t73 = t72;
        if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 1, gm_srccfhjr4r9mdef, 137, 9);
        gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 127, 5);
me1:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_uo0k55e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_uo0k55e F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* d */
    uint64_t gm_l4; /* k */
    gm_b16_8 gm_l5; /* d */
    GM_STACK_CHECK(&F, 1540u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie5;
        {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_b16_8 t12 = gm_l1;
        gm_th2lqbo77pvsc t13;
        gm_f_c_fn_name_77uspqp((void *)&t13, t12);
        gm_th2lqbo77pvsc t14 = t13;
        if (!((((uint8_t *)&t14))[16] != 0)) goto ie11;
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
        gm_l2 = t15;
        void * t16 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l2);
        gm_b16_8 t17 = gm_l2;
        gm_tf5srh4mp4b3i t18 = gm_f_get_espvf96(t16, t17);
        gm_tf5srh4mp4b3i t19 = t18;
        uint8_t t20;
        if (((((uint8_t *)&t19))[8] != 0)) goto mn22;
        t20 = ((uint8_t)0);
        goto me21;
mn22:;
        t20 = ((uint8_t)1);
        goto me21;
mn23:;
        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 533, 40);
me21:;
        if (!t20) goto ie11;
        {
            gm_b16_8 t25 = gm_l1;
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_soa9mnl4ahjud));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(7));
            gm_th2lqbo77pvsc t27;
            gm_f_strip_prefix_b4949s8((void *)&t27, t25, t26);
            gm_th2lqbo77pvsc t28 = t27;
            gm_b16_8 t29;
            if (((((uint8_t *)&t28))[16] != 0)) {
                gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 0));
                t29 = t30;
            } else {
                gm_b16_8 t31 = gm_l1;
                t29 = t31;
            }
            gm_l3 = t29;
        }
        {
            gm_b16_8 t33 = gm_l3;
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sd1uj6gf5v8qh));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(7));
            uint8_t t35 = gm_f_contains_gkki4kq(t33, t34);
            uint8_t t36 = (uint8_t)!t35;
            if (!t36) goto ie32;
            {
                gm_b16_8 t38 = gm_l3;
                gm_tnpifv20irnph t39;
                GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t38);
                gm_tabb710d9mq2e t40;
                gm_f_format_rse2efa((void *)&t40, (void *)NULL, cx, NULL, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t41 = t40;
                if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 8, gm_srccfhjr4r9mdef, 535, 53);
                gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&t41) + 8));
                gm_b16_8 t43 = t42;
                gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
                gm_l3 = t44;
            }
            goto ix32;
ie32:;
ix32:;
        }
        {
            void * t45 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l1);
            gm_t84tkl8vf34ko t46;
            memset((uint8_t *)&t46 + 49, 0, 3);
            gm_b16_8 t47 = gm_l3;
            gm_tnpifv20irnph t48;
            GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
            gm_tabb710d9mq2e t49;
            gm_f_format_uaq30dv((void *)&t49, (void *)NULL, cx, NULL, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t50 = t49;
            if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_srccfhjr4r9mdef, 536, 35);
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
            gm_b16_8 t52 = t51;
            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
            GM_ST(gm_b16_8, (uint8_t *)&t46 + 0, t53);
            gm_b16_8 t54 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t46 + 16, t54);
            gm_b16_8 t55 = gm_l3;
            gm_b16_8 t56 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t46 + 32, t56);
            uint8_t t57 = GM_LD(uint8_t, gm_c72j1g8d455);
            GM_ST(uint8_t, (uint8_t *)&t46 + 48, t57);
            GM_ST(uint32_t, (uint8_t *)&t46 + 52, ((uint32_t)0ULL));
            gm_tftb1odc739t5 t58 = gm_f_push_rda4o9n(t45, &t46);
            gm_tftb1odc739t5 t59 = t58;
            if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srccfhjr4r9mdef, 536, 13);
            gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
        }
        {
            void * t61 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l2);
            gm_b16_8 t62 = gm_l2;
            gm_tf5srh4mp4b3i t63 = gm_f_get_espvf96(t61, t62);
            gm_tf5srh4mp4b3i t64 = t63;
            uint64_t t65;
            if (((((uint8_t *)&t64))[8] != 0)) {
                uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&t64) + 0));
                t65 = t66;
            } else {
                gm_trap("reached `@unreachable()`", gm_srccfhjr4r9mdef, 537, 46);
            }
            gm_l4 = t65;
        }
        {
            uint64_t t67 = gm_l4;
            uint64_t t68 = (uintptr_t)t67;
            uint64_t t69 = GM_LD(uintptr_t, (((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l15) + 0) + 8);
            if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_srccfhjr4r9mdef, 538, 13);
            void * t70 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l15) + 0)) + t68 * 40);
            gm_t6j6smj7qoctu t71;
            gm_b16_8 t72 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t71 + 0, t72);
            gm_b16_8 t73 = gm_l1;
            gm_th2lqbo77pvsc t74;
            gm_f_external_f65nm8q((void *)&t74, (void *)NULL, cx, NULL, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t75 = t74;
            gm_b16_8 t76;
            if (((((uint8_t *)&t75))[16] != 0)) {
                gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 0));
                t76 = t77;
            } else {
                gm_b16_8 t78 = gm_l1;
                t76 = t78;
            }
            gm_tnpifv20irnph t79;
            GM_ST(gm_b16_8, ((uint8_t *)&t79 + 0), t76);
            gm_tabb710d9mq2e t80;
            gm_f_format_u8fgvjp((void *)&t80, (void *)NULL, cx, NULL, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t81 = t80;
            if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 8, gm_srccfhjr4r9mdef, 538, 53);
            gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&t81) + 8));
            gm_b16_8 t83 = t82;
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
            GM_ST(gm_b16_8, (uint8_t *)&t71 + 16, t84);
            gm_tftb1odc739t5 t85 = gm_f_push_0ek3ea4(t70, &t71);
            gm_tftb1odc739t5 t86 = t85;
            if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srccfhjr4r9mdef, 538, 13);
            gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
        }
        {
            gm_b0_1 t88;
            uint8_t t89 = 0;
            F.ret = t88;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b16_8 t90 = gm_l1;
        gm_th2lqbo77pvsc t91;
        gm_f_external_f65nm8q((void *)&t91, (void *)NULL, cx, NULL, t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t92 = t91;
        gm_b16_8 t93;
        if (((((uint8_t *)&t92))[16] != 0)) {
            gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 0));
            t93 = t94;
        } else {
            gm_b16_8 t95 = gm_l1;
            t93 = t95;
        }
        gm_l5 = t93;
    }
    {
        void * t96 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l1);
        gm_t84tkl8vf34ko t97;
        memset((uint8_t *)&t97 + 49, 0, 3);
        gm_b16_8 t98 = gm_l5;
        gm_tnpifv20irnph t99;
        GM_ST(gm_b16_8, ((uint8_t *)&t99 + 0), t98);
        gm_tabb710d9mq2e t100;
        gm_f_format_uaq30dv((void *)&t100, (void *)NULL, cx, NULL, t99);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t101 = t100;
        if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 8, gm_srccfhjr4r9mdef, 542, 31);
        gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&t101) + 8));
        gm_b16_8 t103 = t102;
        gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
        GM_ST(gm_b16_8, (uint8_t *)&t97 + 0, t104);
        gm_b16_8 t105 = gm_l5;
        gm_b16_8 t106 = gm_f_declared_name_hovvm2b(t105);
        GM_ST(gm_b16_8, (uint8_t *)&t97 + 16, t106);
        gm_b16_8 t107 = gm_l5;
        gm_b16_8 t108 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t107);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t97 + 32, t108);
        uint8_t t109 = GM_LD(uint8_t, gm_c72j1g8d455);
        GM_ST(uint8_t, (uint8_t *)&t97 + 48, t109);
        GM_ST(uint32_t, (uint8_t *)&t97 + 52, ((uint32_t)0ULL));
        gm_tftb1odc739t5 t110 = gm_f_push_rda4o9n(t96, &t97);
        gm_tftb1odc739t5 t111 = t110;
        if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 1, gm_srccfhjr4r9mdef, 542, 9);
        gm_b0_1 t112 = GM_LD(gm_b0_1, (((uint8_t *)&t111) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 531, 5);
me1:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vn00ddp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_vn00ddp F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* l */
    GM_STACK_CHECK(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t6 = gm_l1;
        gm_b16_8 t7 = gm_f_trim_fdrfe6i(t6);
        gm_b16_8 t8 = t7;
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&t8) + 8);
        uint8_t t10 = (t9 > ((uint64_t)0ULL));
        if (!t10) goto ie5;
        gm_b16_8 t11 = gm_l1;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_seccpktgmrevt));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(1));
        uint8_t t13 = gm_f_starts_with_6vvt01e(t11, t12);
        uint8_t t14 = (uint8_t)!t13;
        if (!t14) goto ie5;
        {
            gm_th2lqbo77pvsc t16;
            memset(&t16, 0, sizeof t16);
            uint8_t t17 = 0;
            ((struct gm_fr_items_of_q103oiv *)((&F.h)->up))->ret = t16;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t17;
            goto gm_lad;
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 329, 5);
me1:;
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
__attribute__((aligned(4))) void gm_f_function_o2lv8g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_function_o2lv8g6 F;
    gm_b16_8 gm_l0; /* item */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    gm_b16_8 gm_l3; /* pre */
    gm_b16_8 gm_l4; /* text */
    uint64_t gm_l5; /* first_end */
    gm_b16_8 gm_l6; /* first */
    gm_b16_8 gm_l7; /* def */
    gm_b16_8 gm_l8; /* ext */
    gm_b16_8 gm_l9; /* body */
    int32_t gm_l10; /* depth */
    gm_tf5srh4mp4b3i gm_l11; /* open */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    uint8_t gm_l15; /* c */
    uint64_t gm_l16; /* at */
    gm_b16_8 gm_l17; /* header */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint8_t gm_l20; /* c */
    GM_STACK_CHECK(&F, 2223u);
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
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            uint64_t t6 = gm_l1;
            uint64_t t7 = (uintptr_t)t6;
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srccfhjr4r9mdef, 229, 32);
            uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t7 * 1));
            uint8_t t10 = ((t9 == ((uint8_t)35ULL)));
            if (!t10) goto lb2;
            {
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t14 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t15 = gm_l1;
                uint64_t t16 = (uintptr_t)t15;
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint64_t t18 = (uintptr_t)t17;
                if (t16 > t18 || t18 > (uintptr_t)t13) gm_trap_slice(t16, t18, t13, gm_srccfhjr4r9mdef, 230, 25);
                gm_b16_8 t19;
                GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t14 + t16 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(t18 - t16));
                gm_b16_8 t20;
                GM_ST(void *, (uint8_t *)&t20, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
                gm_tf5srh4mp4b3i t21 = gm_f_find_nab29co(t19, t20);
                gm_tf5srh4mp4b3i t22 = t21;
                uint64_t t23;
                if (((((uint8_t *)&t22))[8] != 0)) {
                    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&t22) + 0));
                    t23 = t24;
                } else {
                    goto lb2;
                }
                gm_l2 = t23;
            }
            {
                uint64_t t25 = gm_l1;
                uint64_t t26 = gm_l2;
                uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                if (o28) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 231, 18);
                uint64_t t29 = r27;
                uint64_t r30; bool o31 = __builtin_add_overflow(t25, t29, &r30);
                if (o31) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 231, 9);
                uint64_t t32 = r30;
                gm_l1 = t32;
            }
lc2:;
        }
lb2:;
    }
    {
        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t34 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t35 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t36 = gm_l1;
        uint64_t t37 = (uintptr_t)t36;
        if (t35 > t37 || t37 > (uintptr_t)t33) gm_trap_slice(t35, t37, t33, gm_srccfhjr4r9mdef, 233, 12);
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t34 + t35 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
        gm_l3 = t38;
    }
    {
        uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t40 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t41 = gm_l1;
        uint64_t t42 = (uintptr_t)t41;
        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t44 = (uintptr_t)t43;
        if (t42 > t44 || t44 > (uintptr_t)t39) gm_trap_slice(t42, t44, t39, gm_srccfhjr4r9mdef, 234, 13);
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(((uint8_t *)t40 + t42 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(t44 - t42));
        gm_l4 = t45;
    }
    {
        gm_b16_8 t46 = gm_l4;
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t48 = gm_f_find_nab29co(t46, t47);
        gm_tf5srh4mp4b3i t49 = t48;
        uint64_t t50;
        if (((((uint8_t *)&t49))[8] != 0)) {
            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&t49) + 0));
            t50 = t51;
        } else {
            uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            t50 = t52;
        }
        gm_l5 = t50;
    }
    {
        uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        void * t54 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
        uint64_t t55 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t56 = gm_l5;
        uint64_t t57 = (uintptr_t)t56;
        if (t55 > t57 || t57 > (uintptr_t)t53) gm_trap_slice(t55, t57, t53, gm_srccfhjr4r9mdef, 236, 14);
        gm_b16_8 t58;
        GM_ST(void *, (uint8_t *)&t58, (void *)(((uint8_t *)t54 + t55 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(t57 - t55));
        gm_l6 = t58;
    }
    {
        gm_b16_8 t59 = gm_l0;
        gm_l7 = t59;
    }
    {
        gm_b16_8 t61 = gm_l6;
        gm_th2lqbo77pvsc t62;
        gm_f_external_f65nm8q((void *)&t62, (void *)NULL, cx, NULL, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t63 = t62;
        if (!((((uint8_t *)&t63))[16] != 0)) goto ie60;
        gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
        gm_l8 = t64;
        {
            gm_tfeab279c16rr t66;
            gm_b16_8 t67 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t66 + 0, t67);
            gm_b16_8 t68 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t66 + 16, t68);
            uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            void * t70 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
            uint64_t t71 = gm_l5;
            uint64_t t72 = (uintptr_t)t71;
            uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint64_t t74 = (uintptr_t)t73;
            if (t72 > t74 || t74 > (uintptr_t)t69) gm_trap_slice(t72, t74, t69, gm_srccfhjr4r9mdef, 239, 46);
            gm_b16_8 t75;
            GM_ST(void *, (uint8_t *)&t75, (void *)(((uint8_t *)t70 + t72 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(t74 - t72));
            GM_ST(gm_b16_8, (uint8_t *)&t66 + 32, t75);
            gm_tabb710d9mq2e t76;
            gm_f_format_dsta2ql((void *)&t76, (void *)NULL, cx, NULL, &t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t77 = t76;
            if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_srccfhjr4r9mdef, 239, 15);
            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
            gm_b16_8 t79 = t78;
            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
            gm_l7 = t80;
        }
        goto ix60;
ie60:;
        uint8_t t82 = 0;
        gm_b16_8 t83 = gm_l6;
        gm_b16_8 t84;
        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_soa9mnl4ahjud));
        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(7));
        uint8_t t85 = gm_f_starts_with_6vvt01e(t83, t84);
        if (!t85) {
            gm_b16_8 t86 = gm_l6;
            gm_b16_8 t87;
            GM_ST(void *, (uint8_t *)&t87, (void *)(gm_s1oiqt105ulvb));
            GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(19));
            uint8_t t88 = gm_f_starts_with_6vvt01e(t86, t87);
            t82 = t88;
        } else t82 = 1;
        if (!t82) goto ie81;
        {
            gm_ttu09lij36f8c t90;
            memset(&t90, 0, sizeof t90);
            uint8_t t91 = 0;
            F.ret = t90;
            goto gm_fret;
        }
        goto ix81;
ie81:;
ix81:;
ix60:;
    }
    {
        uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
        void * t93 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
        uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t t95 = (uintptr_t)t94;
        uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
        uint64_t t97 = (uintptr_t)t96;
        if (t95 > t97 || t97 > (uintptr_t)t92) gm_trap_slice(t95, t97, t92, gm_srccfhjr4r9mdef, 244, 13);
        gm_b16_8 t98;
        GM_ST(void *, (uint8_t *)&t98, (void *)(((uint8_t *)t93 + t95 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(t97 - t95));
        gm_l9 = t98;
    }
    {
        gm_l10 = ((int32_t)0LL);
    }
    {
        gm_tf5srh4mp4b3i t99;
        memset(&t99, 0, sizeof t99);
        gm_l11 = t99;
    }
    {
        {
            gm_b16_8 t101 = gm_l9;
            gm_l12 = t101;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t103 = gm_l13;
                uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t105 = (t103 < t104);
                if (!t105) goto lb102;
                {
                    uint64_t t108 = gm_l13;
                    uint64_t t109 = gm_l13;
                    uint64_t t110 = (uintptr_t)t109;
                    uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t112 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t110 * 1));
                    gm_tf5srh4mp4b3i t113;
                    GM_ST(uint8_t, (uint8_t *)&t113 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t113 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t113 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t113 + 0), t108);
                    GM_ST(uint8_t, ((uint8_t *)&t113 + 8), t112);
                    gm_tf5srh4mp4b3i t114 = t113;
                    uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)&t114) + 0));
                    gm_l14 = t117;
                    uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&t114) + 8));
                    gm_l15 = t118;
                    {
                        uint8_t t121 = gm_l15;
                        uint8_t t122 = ((t121 == ((uint8_t)40ULL)));
                        if (!t122) goto ie120;
                        {
                            int32_t t124 = gm_l10;
                            int32_t r125; bool o126 = __builtin_add_overflow(t124, ((int32_t)1LL), &r125);
                            if (o126) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 249, 13);
                            int32_t t127 = r125;
                            gm_l10 = t127;
                        }
                        goto ix120;
ie120:;
                        uint8_t t129 = gm_l15;
                        uint8_t t130 = ((t129 == ((uint8_t)41ULL)));
                        if (!t130) goto ie128;
                        {
                            int32_t t132 = gm_l10;
                            int32_t r133; bool o134 = __builtin_sub_overflow(t132, ((int32_t)1LL), &r133);
                            if (o134) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 251, 13);
                            int32_t t135 = r133;
                            gm_l10 = t135;
                        }
                        goto ix128;
ie128:;
                        uint8_t t137 = gm_l15;
                        uint8_t t138 = ((t137 == ((uint8_t)123ULL)));
                        if (!t138) goto ie136;
                        int32_t t139 = gm_l10;
                        uint8_t t140 = ((t139 == ((int32_t)0LL)));
                        if (!t140) goto ie136;
                        {
                            uint64_t t142 = gm_l14;
                            gm_tf5srh4mp4b3i t143;
                            GM_ST(uint8_t, (uint8_t *)&t143 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t143 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t143 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t143), t142);
                            ((uint8_t *)&t143)[8] = 1;
                            gm_l11 = t143;
                        }
                        {
                            goto lb102;
                        }
                        goto ix136;
ie136:;
ix136:;
ix128:;
ix120:;
                    }
                    goto me115;
mn116:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 247, 5);
me115:;
                }
lc102:;
                {
                    uint64_t t145 = gm_l13;
                    uint64_t r146; bool o147 = __builtin_add_overflow(t145, ((uint64_t)1ULL), &r146);
                    uint64_t t148 = r146;
                    gm_l13 = t148;
                }
            }
lb102:;
        }
    }
    {
        gm_tf5srh4mp4b3i t149 = gm_l11;
        gm_tf5srh4mp4b3i t150 = t149;
        uint64_t t151;
        if (((((uint8_t *)&t150))[8] != 0)) {
            uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&t150) + 0));
            t151 = t152;
        } else {
            gm_ttu09lij36f8c t153;
            memset(&t153, 0, sizeof t153);
            uint8_t t154 = 0;
            F.ret = t153;
            goto gm_fret;
        }
        gm_l16 = t151;
    }
    {
        uint64_t t155 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
        void * t156 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)));
        uint64_t t157 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t158 = gm_l16;
        uint64_t t159 = (uintptr_t)t158;
        if (t157 > t159 || t159 > (uintptr_t)t155) gm_trap_slice(t157, t159, t155, gm_srccfhjr4r9mdef, 258, 28);
        gm_b16_8 t160;
        GM_ST(void *, (uint8_t *)&t160, (void *)(((uint8_t *)t156 + t157 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(t159 - t157));
        gm_b16_8 t161 = gm_f_trim_end_6sjv6rg(t160);
        gm_l17 = t161;
    }
    {
        {
            gm_b16_8 t163 = gm_l17;
            gm_l18 = t163;
        }
        {
            gm_l19 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t165 = gm_l19;
                uint64_t t166 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t167 = (t165 < t166);
                if (!t167) goto lb164;
                {
                    uint64_t t170 = gm_l19;
                    uint64_t t171 = (uintptr_t)t170;
                    uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    uint8_t t173 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t171 * 1));
                    uint8_t t174 = t173;
                    uint8_t t177 = GM_LD(uint8_t, ((uint8_t *)&t174));
                    gm_l20 = t177;
                    {
                        uint8_t t180 = gm_l20;
                        gm_toh0ootodhjue t181;
                        GM_ST(uint8_t, ((uint8_t *)&t181 + 0), ((uint8_t)10ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t181 + 1), ((uint8_t)34ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t181 + 2), ((uint8_t)39ULL));
                        gm_toh0ootodhjue t182 = t181;
                        uint8_t t183 = 0;
                        uint8_t t184 = t180;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t182) + 0, ((uint8_t *)&t184))) t183 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t182) + 1, ((uint8_t *)&t184))) t183 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t182) + 2, ((uint8_t *)&t184))) t183 = 1;
                        uint8_t t185 = (uint8_t)(t183);
                        if (!t185) goto ie179;
                        {
                            gm_ttu09lij36f8c t187;
                            memset(&t187, 0, sizeof t187);
                            uint8_t t188 = 0;
                            F.ret = t187;
                            goto gm_fret;
                        }
                        goto ix179;
ie179:;
ix179:;
                    }
                    goto me175;
mn176:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 259, 5);
me175:;
                }
lc164:;
                {
                    uint64_t t190 = gm_l19;
                    uint64_t r191; bool o192 = __builtin_add_overflow(t190, ((uint64_t)1ULL), &r191);
                    uint64_t t193 = r191;
                    gm_l19 = t193;
                }
            }
lb164:;
        }
    }
    {
        gm_b16_8 t195 = gm_l17;
        gm_b16_8 t196;
        GM_ST(void *, (uint8_t *)&t196, (void *)(gm_ssi4rrma2q00g));
        GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(6));
        uint8_t t197 = gm_f_has_word_l11ltcj(t195, t196);
        if (!t197) goto ie194;
        {
            gm_ttu09lij36f8c t199;
            memset(&t199, 0, sizeof t199);
            uint8_t t200 = 0;
            F.ret = t199;
            goto gm_fret;
        }
        goto ix194;
ie194:;
ix194:;
    }
    {
        gm_b16_8 t202 = gm_l6;
        gm_b16_8 t203;
        GM_ST(void *, (uint8_t *)&t203, (void *)(gm_s8sgcefhqa4i0));
        GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(42));
        uint8_t t204 = gm_f_starts_with_6vvt01e(t202, t203);
        if (!t204) goto ie201;
        {
            gm_t6j6smj7qoctu t206;
            gm_b16_8 t207 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t206 + 0, t207);
            uint64_t t208 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            void * t209 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
            uint64_t t210 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t t211 = (uintptr_t)t210;
            uint64_t t212 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            uint64_t t213 = (uintptr_t)t212;
            if (t211 > t213 || t213 > (uintptr_t)t208) gm_trap_slice(t211, t213, t208, gm_srccfhjr4r9mdef, 267, 46);
            gm_b16_8 t214;
            GM_ST(void *, (uint8_t *)&t214, (void *)(((uint8_t *)t209 + t211 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t214 + 8, (uintptr_t)(t213 - t211));
            GM_ST(gm_b16_8, (uint8_t *)&t206 + 16, t214);
            gm_tabb710d9mq2e t215;
            gm_f_format_stmnn5h((void *)&t215, (void *)NULL, cx, NULL, &t206);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t216 = t215;
            if (((((uint8_t *)&t216))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t216) + 8, gm_srccfhjr4r9mdef, 267, 15);
            gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&t216) + 8));
            gm_b16_8 t218 = t217;
            gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&t218));
            gm_l7 = t219;
        }
        goto ix201;
ie201:;
ix201:;
    }
    {
        gm_t6j6smj7qoctu t220;
        gm_b16_8 t221 = gm_l17;
        gm_tnpifv20irnph t222;
        GM_ST(gm_b16_8, ((uint8_t *)&t222 + 0), t221);
        gm_tabb710d9mq2e t223;
        gm_f_format_dg0m8l8((void *)&t223, (void *)NULL, cx, NULL, t222);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t224 = t223;
        if (((((uint8_t *)&t224))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t224) + 8, gm_srccfhjr4r9mdef, 269, 19);
        gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&t224) + 8));
        gm_b16_8 t226 = t225;
        gm_b16_8 t227 = GM_LD(gm_b16_8, ((uint8_t *)&t226));
        GM_ST(gm_b16_8, (uint8_t *)&t220 + 0, t227);
        gm_b16_8 t228 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t220 + 16, t228);
        gm_ttu09lij36f8c t229;
        GM_ST(uint8_t, (uint8_t *)&t229 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t229 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t229 + 36, 0);
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t229), t220);
        ((uint8_t *)&t229)[32] = 1;
        uint8_t t230 = 0;
        F.ret = t229;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_gm_names_6ner0lu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_gm_names_6ner0lu F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* c */
    uint64_t gm_l5; /* j */
    gm_b16_8 gm_l6; /* id */
    GM_STACK_CHECK(&F, 512u);
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
        gm_f_hash_map_m04clhi((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t4 = gm_l3;
            uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t6 = (t4 < t5);
            if (!t6) goto lb3;
            {
                uint64_t t9 = gm_l3;
                uint64_t t10 = (uintptr_t)t9;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srccfhjr4r9mdef, 423, 14);
                uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t10 * 1));
                gm_l4 = t12;
            }
            {
                uint8_t t14 = 0;
                uint8_t t15 = gm_l4;
                uint8_t t16 = ((t15 == ((uint8_t)34ULL)));
                if (!t16) {
                    uint8_t t17 = gm_l4;
                    uint8_t t18 = ((t17 == ((uint8_t)39ULL)));
                    t14 = t18;
                } else t14 = 1;
                if (!t14) goto ie13;
                {
                    gm_b16_8 t20 = gm_l0;
                    uint64_t t21 = gm_l3;
                    uint64_t t22 = gm_f_skip_literal_17lq2p8(t20, t21);
                    gm_l3 = t22;
                }
                {
                    goto lc3;
                }
                goto ix13;
ie13:;
ix13:;
            }
            {
                uint8_t t24 = gm_l4;
                uint8_t t25 = gm_f_ident_char_pmpasvq(t24);
                if (!t25) goto ie23;
                {
                    uint64_t t27 = gm_l3;
                    gm_l5 = t27;
                }
                {
                    for (;;) {
                        uint64_t t29 = gm_l5;
                        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t31 = (t29 < t30);
                        if (!t31) goto lb28;
                        uint64_t t32 = gm_l5;
                        uint64_t t33 = (uintptr_t)t32;
                        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_srccfhjr4r9mdef, 430, 47);
                        uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                        uint8_t t36 = gm_f_ident_char_pmpasvq(t35);
                        if (!t36) goto lb28;
                        {
                            uint64_t t39 = gm_l5;
                            uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                            if (o41) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 430, 58);
                            uint64_t t42 = r40;
                            gm_l5 = t42;
                        }
lc28:;
                    }
lb28:;
                }
                {
                    uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t44 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t45 = gm_l3;
                    uint64_t t46 = (uintptr_t)t45;
                    uint64_t t47 = gm_l5;
                    uint64_t t48 = (uintptr_t)t47;
                    if (t46 > t48 || t48 > (uintptr_t)t43) gm_trap_slice(t46, t48, t43, gm_srccfhjr4r9mdef, 431, 19);
                    gm_b16_8 t49;
                    GM_ST(void *, (uint8_t *)&t49, (void *)(((uint8_t *)t44 + t46 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(t48 - t46));
                    gm_l6 = t49;
                }
                {
                    gm_b16_8 t51 = gm_l6;
                    gm_b16_8 t52;
                    GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s7ipvt5onv6ht));
                    GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(3));
                    uint8_t t53 = gm_f_starts_with_6vvt01e(t51, t52);
                    if (!t53) goto ie50;
                    uint8_t t54 = 0;
                    uint8_t t55 = gm_l4;
                    uint8_t t56 = (t55 >= ((uint8_t)48ULL));
                    if (t56) {
                        uint8_t t57 = gm_l4;
                        uint8_t t58 = (t57 <= ((uint8_t)57ULL));
                        t54 = t58;
                    } else t54 = 0;
                    uint8_t t59 = (uint8_t)!t54;
                    if (!t59) goto ie50;
                    void * t60 = (void *)((uint8_t *)&F.l2);
                    gm_b16_8 t61 = gm_l6;
                    gm_ttsm01t0d8srg t62 = gm_f_get_ece4opn(t60, t61);
                    gm_ttsm01t0d8srg t63 = t62;
                    uint8_t t64;
                    if (((((uint8_t *)&t63))[0] != 0)) goto mn66;
                    t64 = ((uint8_t)1);
                    goto me65;
mn66:;
                    t64 = ((uint8_t)0);
                    goto me65;
mn67:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 432, 74);
me65:;
                    if (!t64) goto ie50;
                    {
                        void * t69 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t70 = gm_l6;
                        gm_b0_1 t71;
                        gm_tftb1odc739t5 t72 = gm_f_put_drsjd1t(t69, t70, t71);
                        gm_tftb1odc739t5 t73 = t72;
                        if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 1, gm_srccfhjr4r9mdef, 433, 17);
                        gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 1));
                    }
                    {
                        void * t75 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t76 = gm_l6;
                        gm_tftb1odc739t5 t77 = gm_f_push_3ef746b(t75, t76);
                        gm_tftb1odc739t5 t78 = t77;
                        if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 1, gm_srccfhjr4r9mdef, 434, 17);
                        gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 1));
                    }
                    goto ix50;
ie50:;
ix50:;
                }
                {
                    uint64_t t80 = gm_l5;
                    gm_l3 = t80;
                }
                {
                    goto lc3;
                }
                goto ix23;
ie23:;
ix23:;
            }
            {
                uint64_t t81 = gm_l3;
                uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                if (o83) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 439, 9);
                uint64_t t84 = r82;
                gm_l3 = t84;
            }
lc3:;
        }
lb3:;
    }
    {
        gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t86 = t85;
        gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
        uint8_t t88 = 0;
        F.ret = t87;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t88;
        goto gm_ld1_1;
    }
    {
        void * t89 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_lehld1n(t89);
    }
    goto gm_ls90;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t91 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_lehld1n(t91);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls90:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_word_l11ltcj(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_word_l11ltcj F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l1; /* word */
    uint64_t gm_l2; /* from */
    uint64_t gm_l3; /* i */
    uint64_t gm_l4; /* at */
    uint64_t gm_l5; /* end */
    GM_STACK_CHECK(&F, 368u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l2;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t10 = gm_l2;
                uint64_t t11 = (uintptr_t)t10;
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint64_t t13 = (uintptr_t)t12;
                if (t11 > t13 || t13 > (uintptr_t)t8) gm_trap_slice(t11, t13, t8, gm_srccfhjr4r9mdef, 276, 23);
                gm_b16_8 t14;
                GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t9 + t11 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13 - t11));
                gm_b16_8 t15 = gm_l1;
                gm_tf5srh4mp4b3i t16 = gm_f_find_nab29co(t14, t15);
                gm_tf5srh4mp4b3i t17 = t16;
                uint64_t t18;
                if (((((uint8_t *)&t17))[8] != 0)) {
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t17) + 0));
                    t18 = t19;
                } else {
                    uint8_t t20 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                gm_l3 = t18;
            }
            {
                uint64_t t21 = gm_l2;
                uint64_t t22 = gm_l3;
                uint64_t r23; bool o24 = __builtin_add_overflow(t21, t22, &r23);
                if (o24) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 277, 15);
                uint64_t t25 = r23;
                gm_l4 = t25;
            }
            {
                uint64_t t26 = gm_l4;
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint64_t r28; bool o29 = __builtin_add_overflow(t26, t27, &r28);
                if (o29) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 278, 16);
                uint64_t t30 = r28;
                gm_l5 = t30;
            }
            {
                uint8_t t32 = 0;
                uint64_t t33 = gm_l4;
                uint8_t t34 = ((t33 == ((uint64_t)0ULL)));
                if (!t34) {
                    uint64_t t35 = gm_l4;
                    uint64_t r36; bool o37 = __builtin_sub_overflow(t35, ((uint64_t)1ULL), &r36);
                    if (o37) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 279, 54);
                    uint64_t t38 = r36;
                    uint64_t t39 = (uintptr_t)t38;
                    uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t39 >= (uintptr_t)(t40)) gm_trap_index(t39, t40, gm_srccfhjr4r9mdef, 279, 49);
                    uint8_t t41 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t39 * 1));
                    uint8_t t42 = gm_f_is_ident_cont_317fpdb(t41);
                    uint8_t t43 = (uint8_t)!t42;
                    t32 = t43;
                } else t32 = 1;
                if (!t32) goto ie31;
                uint8_t t44 = 0;
                uint64_t t45 = gm_l5;
                uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t47 = (t45 >= t46);
                if (!t47) {
                    uint64_t t48 = gm_l5;
                    uint64_t t49 = (uintptr_t)t48;
                    uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t49 >= (uintptr_t)(t50)) gm_trap_index(t49, t50, gm_srccfhjr4r9mdef, 279, 113);
                    uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t49 * 1));
                    uint8_t t52 = gm_f_is_ident_cont_317fpdb(t51);
                    uint8_t t53 = (uint8_t)!t52;
                    t44 = t53;
                } else t44 = 1;
                if (!t44) goto ie31;
                {
                    uint8_t t55 = 0;
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
                goto ix31;
ie31:;
ix31:;
            }
            {
                uint64_t t56 = gm_l4;
                uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                if (o58) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 280, 16);
                uint64_t t59 = r57;
                gm_l2 = t59;
            }
lc2:;
        }
lb2:;
    }
    {
        uint8_t t60 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_ident_char_pmpasvq(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ident_char_pmpasvq F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK(&F, 5u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    uint8_t t2 = gm_f_is_ident_cont_317fpdb(t1);
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_ident_set_9ajalpn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_ident_set_9ajalpn F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* j */
    GM_STACK_CHECK(&F, 242u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_hash_map_m04clhi((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l2;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                uint64_t t9 = gm_l2;
                uint64_t t10 = (uintptr_t)t9;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srccfhjr4r9mdef, 701, 23);
                uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t10 * 1));
                uint8_t t13 = gm_f_ident_char_pmpasvq(t12);
                if (!t13) goto ie8;
                {
                    uint64_t t15 = gm_l2;
                    gm_l3 = t15;
                }
                {
                    for (;;) {
                        uint64_t t17 = gm_l3;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t19 = (t17 < t18);
                        if (!t19) goto lb16;
                        uint64_t t20 = gm_l3;
                        uint64_t t21 = (uintptr_t)t20;
                        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_srccfhjr4r9mdef, 703, 47);
                        uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t21 * 1));
                        uint8_t t24 = gm_f_ident_char_pmpasvq(t23);
                        if (!t24) goto lb16;
                        {
                            uint64_t t27 = gm_l3;
                            uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                            if (o29) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 703, 58);
                            uint64_t t30 = r28;
                            gm_l3 = t30;
                        }
lc16:;
                    }
lb16:;
                }
                {
                    void * t31 = (void *)((uint8_t *)&F.l1);
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t33 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t34 = gm_l2;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = gm_l3;
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t32) gm_trap_slice(t35, t37, t32, gm_srccfhjr4r9mdef, 704, 21);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t33 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_b0_1 t39;
                    gm_tftb1odc739t5 t40 = gm_f_put_drsjd1t(t31, t38, t39);
                    gm_tftb1odc739t5 t41 = t40;
                    if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srccfhjr4r9mdef, 704, 13);
                    gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                }
                {
                    uint64_t t43 = gm_l3;
                    gm_l2 = t43;
                }
                goto ix8;
ie8:;
                {
                    uint64_t t45 = gm_l2;
                    uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                    if (o47) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 707, 13);
                    uint64_t t48 = r46;
                    gm_l2 = t48;
                }
ix8:;
            }
lc2:;
        }
lb2:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 56);
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
__attribute__((aligned(4))) void gm_f_inline_definition_6aokb9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_inline_definition_6aokb9d F;
    gm_b16_8 gm_l0; /* item */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    gm_b16_8 gm_l3; /* text */
    uint64_t gm_l4; /* first_end */
    gm_b16_8 gm_l5; /* first */
    gm_b16_8 gm_l6; /* name */
    GM_STACK_CHECK(&F, 1064u);
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
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            uint64_t t6 = gm_l1;
            uint64_t t7 = (uintptr_t)t6;
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srccfhjr4r9mdef, 196, 32);
            uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t7 * 1));
            uint8_t t10 = ((t9 == ((uint8_t)35ULL)));
            if (!t10) goto lb2;
            {
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t14 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t15 = gm_l1;
                uint64_t t16 = (uintptr_t)t15;
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint64_t t18 = (uintptr_t)t17;
                if (t16 > t18 || t18 > (uintptr_t)t13) gm_trap_slice(t16, t18, t13, gm_srccfhjr4r9mdef, 197, 25);
                gm_b16_8 t19;
                GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t14 + t16 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(t18 - t16));
                gm_b16_8 t20;
                GM_ST(void *, (uint8_t *)&t20, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
                gm_tf5srh4mp4b3i t21 = gm_f_find_nab29co(t19, t20);
                gm_tf5srh4mp4b3i t22 = t21;
                uint64_t t23;
                if (((((uint8_t *)&t22))[8] != 0)) {
                    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&t22) + 0));
                    t23 = t24;
                } else {
                    goto lb2;
                }
                gm_l2 = t23;
            }
            {
                uint64_t t25 = gm_l1;
                uint64_t t26 = gm_l2;
                uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                if (o28) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 198, 18);
                uint64_t t29 = r27;
                uint64_t r30; bool o31 = __builtin_add_overflow(t25, t29, &r30);
                if (o31) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 198, 9);
                uint64_t t32 = r30;
                gm_l1 = t32;
            }
lc2:;
        }
lb2:;
    }
    {
        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t34 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t35 = gm_l1;
        uint64_t t36 = (uintptr_t)t35;
        uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t38 = (uintptr_t)t37;
        if (t36 > t38 || t38 > (uintptr_t)t33) gm_trap_slice(t36, t38, t33, gm_srccfhjr4r9mdef, 200, 13);
        gm_b16_8 t39;
        GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t34 + t36 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(t38 - t36));
        gm_l3 = t39;
    }
    {
        gm_b16_8 t40 = gm_l3;
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t42 = gm_f_find_nab29co(t40, t41);
        gm_tf5srh4mp4b3i t43 = t42;
        uint64_t t44;
        if (((((uint8_t *)&t43))[8] != 0)) {
            uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&t43) + 0));
            t44 = t45;
        } else {
            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            t44 = t46;
        }
        gm_l4 = t44;
    }
    {
        uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t48 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t49 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t50 = gm_l4;
        uint64_t t51 = (uintptr_t)t50;
        if (t49 > t51 || t51 > (uintptr_t)t47) gm_trap_slice(t49, t51, t47, gm_srccfhjr4r9mdef, 202, 14);
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t48 + t49 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(t51 - t49));
        gm_l5 = t52;
    }
    {
        gm_b16_8 t54 = gm_l5;
        uint8_t t55 = gm_f_is_inline_aqdknmj(t54);
        uint8_t t56 = (uint8_t)!t55;
        if (!t56) goto ie53;
        {
            gm_ttu09lij36f8c t58;
            memset(&t58, 0, sizeof t58);
            uint8_t t59 = 0;
            F.ret = t58;
            goto gm_fret;
        }
        goto ix53;
ie53:;
ix53:;
    }
    {
        gm_b16_8 t60 = gm_l5;
        gm_th2lqbo77pvsc t61;
        gm_f_c_fn_name_77uspqp((void *)&t61, t60);
        gm_th2lqbo77pvsc t62 = t61;
        gm_b16_8 t63;
        if (((((uint8_t *)&t62))[16] != 0)) {
            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t62) + 0));
            t63 = t64;
        } else {
            gm_ttu09lij36f8c t65;
            memset(&t65, 0, sizeof t65);
            uint8_t t66 = 0;
            F.ret = t65;
            goto gm_fret;
        }
        gm_l6 = t63;
    }
    {
        gm_t6j6smj7qoctu t67;
        gm_t6j6smj7qoctu t68;
        uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t70 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t71 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t72 = gm_l1;
        uint64_t t73 = (uintptr_t)t72;
        if (t71 > t73 || t73 > (uintptr_t)t69) gm_trap_slice(t71, t73, t69, gm_srccfhjr4r9mdef, 205, 37);
        gm_b16_8 t74;
        GM_ST(void *, (uint8_t *)&t74, (void *)(((uint8_t *)t70 + t71 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(t73 - t71));
        GM_ST(gm_b16_8, (uint8_t *)&t68 + 0, t74);
        uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t76 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        gm_b16_8 t77;
        GM_ST(void *, (uint8_t *)&t77, (void *)(gm_soa9mnl4ahjud));
        GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(7));
        gm_b16_8 t78 = t77;
        uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&t78) + 8);
        uint64_t t80 = (uintptr_t)t79;
        uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t t82 = (uintptr_t)t81;
        if (t80 > t82 || t82 > (uintptr_t)t75) gm_trap_slice(t80, t82, t75, gm_srccfhjr4r9mdef, 205, 53);
        gm_b16_8 t83;
        GM_ST(void *, (uint8_t *)&t83, (void *)(((uint8_t *)t76 + t80 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(t82 - t80));
        GM_ST(gm_b16_8, (uint8_t *)&t68 + 16, t83);
        gm_tabb710d9mq2e t84;
        gm_f_format_8n626pi((void *)&t84, (void *)NULL, cx, NULL, &t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t85 = t84;
        if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 8, gm_srccfhjr4r9mdef, 205, 18);
        gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&t85) + 8));
        gm_b16_8 t87 = t86;
        gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
        GM_ST(gm_b16_8, (uint8_t *)&t67 + 0, t88);
        gm_b16_8 t89 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t67 + 16, t89);
        gm_ttu09lij36f8c t90;
        GM_ST(uint8_t, (uint8_t *)&t90 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t90 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t90 + 36, 0);
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t90), t67);
        ((uint8_t *)&t90)[32] = 1;
        uint8_t t91 = 0;
        F.ret = t90;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_inline_aqdknmj(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_inline_aqdknmj F;
    gm_b16_8 gm_l0; /* line */
    GM_STACK_CHECK(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soa9mnl4ahjud));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    uint8_t t4 = gm_f_starts_with_6vvt01e(t2, t3);
    if (t4) {
        gm_b16_8 t5 = gm_l0;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_so0h0lkl6bp4l));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(38));
        uint8_t t7 = gm_f_contains_gkki4kq(t5, t6);
        t1 = t7;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_items_of_q103oiv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_items_of_q103oiv F;
    gm_b16_8 gm_l0; /* code */
    uint64_t gm_l2; /* start */
    int32_t gm_l3; /* depth */
    uint64_t gm_l4; /* i */
    uint8_t gm_l5; /* line_start */
    uint8_t gm_l6; /* c */
    gm_b16_8 gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 1039u);
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
        gm_l3 = ((int32_t)0LL);
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        gm_l5 = ((uint8_t)1);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l4;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                uint64_t t8 = gm_l4;
                uint64_t t9 = (uintptr_t)t8;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srccfhjr4r9mdef, 294, 14);
                uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 1));
                gm_l6 = t11;
            }
            {
                uint8_t t13 = gm_l6;
                uint8_t t14 = ((t13 == ((uint8_t)35ULL)));
                if (!t14) goto ie12;
                uint8_t t15 = gm_l5;
                if (!t15) goto ie12;
                {
                    for (;;) {
                        uint64_t t18 = gm_l4;
                        uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t20 = (t18 < t19);
                        if (!t20) goto lb17;
                        uint64_t t21 = gm_l4;
                        uint64_t t22 = (uintptr_t)t21;
                        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_srccfhjr4r9mdef, 296, 36);
                        uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t22 * 1));
                        uint8_t t25 = (!(t24 == ((uint8_t)10ULL)));
                        if (!t25) goto lb17;
                        {
                            uint64_t t28 = gm_l4;
                            uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                            if (o30) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 296, 58);
                            uint64_t t31 = r29;
                            gm_l4 = t31;
                        }
lc17:;
                    }
lb17:;
                }
                {
                    goto lc2;
                }
                goto ix12;
ie12:;
ix12:;
            }
            {
                uint8_t t33 = 0;
                uint8_t t34 = gm_l6;
                uint8_t t35 = ((t34 == ((uint8_t)34ULL)));
                if (!t35) {
                    uint8_t t36 = gm_l6;
                    uint8_t t37 = ((t36 == ((uint8_t)39ULL)));
                    t33 = t37;
                } else t33 = 1;
                if (!t33) goto ie32;
                {
                    uint64_t t39 = gm_l4;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    if (o41) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 300, 13);
                    uint64_t t42 = r40;
                    gm_l4 = t42;
                }
                {
                    for (;;) {
                        uint64_t t44 = gm_l4;
                        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t46 = (t44 < t45);
                        if (!t46) goto lb43;
                        uint64_t t47 = gm_l4;
                        uint64_t t48 = (uintptr_t)t47;
                        uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t48 >= (uintptr_t)(t49)) gm_trap_index(t48, t49, gm_srccfhjr4r9mdef, 301, 36);
                        uint8_t t50 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t48 * 1));
                        uint8_t t51 = gm_l6;
                        uint8_t t52 = (!(t50 == t51));
                        if (!t52) goto lb43;
                        {
                            uint64_t t55 = gm_l4;
                            uint64_t t56;
                            uint64_t t58 = gm_l4;
                            uint64_t t59 = (uintptr_t)t58;
                            uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srccfhjr4r9mdef, 302, 25);
                            uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t59 * 1));
                            uint8_t t62 = ((t61 == ((uint8_t)92ULL)));
                            if (!t62) goto ie57;
                            uint64_t t64 = ((uint64_t)2ULL);
                            t56 = t64;
                            goto ix57;
ie57:;
                            uint64_t t66 = ((uint64_t)1ULL);
                            t56 = t66;
ix57:;
                            uint64_t r67; bool o68 = __builtin_add_overflow(t55, t56, &r67);
                            if (o68) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 302, 17);
                            uint64_t t69 = r67;
                            gm_l4 = t69;
                        }
lc43:;
                    }
lb43:;
                }
                goto ix32;
ie32:;
                uint8_t t71 = gm_l6;
                uint8_t t72 = ((t71 == ((uint8_t)47ULL)));
                if (!t72) goto ie70;
                uint64_t t73 = gm_l4;
                uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                if (o75) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 304, 32);
                uint64_t t76 = r74;
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t78 = (t76 < t77);
                if (!t78) goto ie70;
                uint64_t t79 = gm_l4;
                uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                if (o81) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 304, 58);
                uint64_t t82 = r80;
                uint64_t t83 = (uintptr_t)t82;
                uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t83 >= (uintptr_t)(t84)) gm_trap_index(t83, t84, gm_srccfhjr4r9mdef, 304, 53);
                uint8_t t85 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t83 * 1));
                uint8_t t86 = ((t85 == ((uint8_t)42ULL)));
                if (!t86) goto ie70;
                {
                    uint64_t t88 = gm_l4;
                    uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)2ULL), &r89);
                    if (o90) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 305, 13);
                    uint64_t t91 = r89;
                    gm_l4 = t91;
                }
                {
                    for (;;) {
                        uint64_t t93 = gm_l4;
                        uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                        if (o95) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 306, 19);
                        uint64_t t96 = r94;
                        uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t98 = (t96 < t97);
                        if (!t98) goto lb92;
                        uint8_t t99 = 0;
                        uint64_t t100 = gm_l4;
                        uint64_t t101 = (uintptr_t)t100;
                        uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t101 >= (uintptr_t)(t102)) gm_trap_index(t101, t102, gm_srccfhjr4r9mdef, 306, 45);
                        uint8_t t103 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t101 * 1));
                        uint8_t t104 = ((t103 == ((uint8_t)42ULL)));
                        if (t104) {
                            uint64_t t105 = gm_l4;
                            uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                            if (o107) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 306, 69);
                            uint64_t t108 = r106;
                            uint64_t t109 = (uintptr_t)t108;
                            uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t109 >= (uintptr_t)(t110)) gm_trap_index(t109, t110, gm_srccfhjr4r9mdef, 306, 64);
                            uint8_t t111 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t109 * 1));
                            uint8_t t112 = ((t111 == ((uint8_t)47ULL)));
                            t99 = t112;
                        } else t99 = 0;
                        uint8_t t113 = (uint8_t)!t99;
                        if (!t113) goto lb92;
                        {
                            uint64_t t116 = gm_l4;
                            uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                            if (o118) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 306, 86);
                            uint64_t t119 = r117;
                            gm_l4 = t119;
                        }
lc92:;
                    }
lb92:;
                }
                {
                    uint64_t t120 = gm_l4;
                    uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                    if (o122) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 307, 13);
                    uint64_t t123 = r121;
                    gm_l4 = t123;
                }
                goto ix70;
ie70:;
                uint8_t t125 = gm_l6;
                uint8_t t126 = ((t125 == ((uint8_t)47ULL)));
                if (!t126) goto ie124;
                uint64_t t127 = gm_l4;
                uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                if (o129) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 308, 32);
                uint64_t t130 = r128;
                uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t132 = (t130 < t131);
                if (!t132) goto ie124;
                uint64_t t133 = gm_l4;
                uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
                if (o135) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 308, 58);
                uint64_t t136 = r134;
                uint64_t t137 = (uintptr_t)t136;
                uint64_t t138 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t137 >= (uintptr_t)(t138)) gm_trap_index(t137, t138, gm_srccfhjr4r9mdef, 308, 53);
                uint8_t t139 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t137 * 1));
                uint8_t t140 = ((t139 == ((uint8_t)47ULL)));
                if (!t140) goto ie124;
                {
                    for (;;) {
                        uint64_t t143 = gm_l4;
                        uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)1ULL), &r144);
                        if (o145) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 309, 19);
                        uint64_t t146 = r144;
                        uint64_t t147 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t148 = (t146 < t147);
                        if (!t148) goto lb142;
                        uint64_t t149 = gm_l4;
                        uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
                        if (o151) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 309, 45);
                        uint64_t t152 = r150;
                        uint64_t t153 = (uintptr_t)t152;
                        uint64_t t154 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t153 >= (uintptr_t)(t154)) gm_trap_index(t153, t154, gm_srccfhjr4r9mdef, 309, 40);
                        uint8_t t155 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t153 * 1));
                        uint8_t t156 = (!(t155 == ((uint8_t)10ULL)));
                        if (!t156) goto lb142;
                        {
                            uint64_t t159 = gm_l4;
                            uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                            if (o161) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 309, 66);
                            uint64_t t162 = r160;
                            gm_l4 = t162;
                        }
lc142:;
                    }
lb142:;
                }
                goto ix124;
ie124:;
                uint8_t t164 = gm_l6;
                uint8_t t165 = ((t164 == ((uint8_t)123ULL)));
                if (!t165) goto ie163;
                {
                    int32_t t167 = gm_l3;
                    int32_t r168; bool o169 = __builtin_add_overflow(t167, ((int32_t)1LL), &r168);
                    if (o169) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 311, 13);
                    int32_t t170 = r168;
                    gm_l3 = t170;
                }
                goto ix163;
ie163:;
                uint8_t t172 = gm_l6;
                uint8_t t173 = ((t172 == ((uint8_t)125ULL)));
                if (!t173) goto ie171;
                {
                    int32_t t175 = gm_l3;
                    int32_t r176; bool o177 = __builtin_sub_overflow(t175, ((int32_t)1LL), &r176);
                    if (o177) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 313, 13);
                    int32_t t178 = r176;
                    gm_l3 = t178;
                }
                {
                    int32_t t180 = gm_l3;
                    uint8_t t181 = (t180 < ((int32_t)0LL));
                    if (!t181) goto ie179;
                    {
                        gm_th2lqbo77pvsc t183;
                        memset(&t183, 0, sizeof t183);
                        uint8_t t184 = 0;
                        F.ret = t183;
                        goto gm_fret;
                    }
                    goto ix179;
ie179:;
ix179:;
                }
                {
                    int32_t t186 = gm_l3;
                    uint8_t t187 = ((t186 == ((int32_t)0LL)));
                    if (!t187) goto ie185;
                    uint8_t t188 = 0;
                    uint64_t t189 = gm_l4;
                    uint64_t r190; bool o191 = __builtin_add_overflow(t189, ((uint64_t)1ULL), &r190);
                    if (o191) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 315, 32);
                    uint64_t t192 = r190;
                    uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t194 = (t192 >= t193);
                    if (!t194) {
                        uint64_t t195 = gm_l4;
                        uint64_t r196; bool o197 = __builtin_add_overflow(t195, ((uint64_t)1ULL), &r196);
                        if (o197) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 315, 58);
                        uint64_t t198 = r196;
                        uint64_t t199 = (uintptr_t)t198;
                        uint64_t t200 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t199 >= (uintptr_t)(t200)) gm_trap_index(t199, t200, gm_srccfhjr4r9mdef, 315, 53);
                        uint8_t t201 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t199 * 1));
                        uint8_t t202 = ((t201 == ((uint8_t)10ULL)));
                        t188 = t202;
                    } else t188 = 1;
                    if (!t188) goto ie185;
                    {
                        uint64_t t204 = gm_l4;
                        uint64_t r205; bool o206 = __builtin_add_overflow(t204, ((uint64_t)2ULL), &r205);
                        if (o206) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 316, 22);
                        uint64_t t207 = r205;
                        uint64_t t208 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint64_t t209 = (t207 < t208 ? t207 : t208);
                        gm_l4 = t209;
                    }
                    {
                        void * t210 = (void *)((uint8_t *)&F.l1);
                        uint64_t t211 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        void * t212 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                        uint64_t t213 = gm_l2;
                        uint64_t t214 = (uintptr_t)t213;
                        uint64_t t215 = gm_l4;
                        uint64_t t216 = (uintptr_t)t215;
                        if (t214 > t216 || t216 > (uintptr_t)t211) gm_trap_slice(t214, t216, t211, gm_srccfhjr4r9mdef, 317, 28);
                        gm_b16_8 t217;
                        GM_ST(void *, (uint8_t *)&t217, (void *)(((uint8_t *)t212 + t214 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(t216 - t214));
                        gm_tftb1odc739t5 t218 = gm_f_push_3ef746b(t210, t217);
                        gm_tftb1odc739t5 t219 = t218;
                        if (((((uint8_t *)&t219))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t219) + 1, gm_srccfhjr4r9mdef, 317, 17);
                        gm_b0_1 t220 = GM_LD(gm_b0_1, (((uint8_t *)&t219) + 1));
                    }
                    {
                        uint64_t t221 = gm_l4;
                        gm_l2 = t221;
                    }
                    {
                        gm_l5 = ((uint8_t)1);
                    }
                    {
                        goto lc2;
                    }
                    goto ix185;
ie185:;
ix185:;
                }
                goto ix171;
ie171:;
                uint8_t t223 = gm_l6;
                uint8_t t224 = ((t223 == ((uint8_t)59ULL)));
                if (!t224) goto ie222;
                int32_t t225 = gm_l3;
                uint8_t t226 = ((t225 == ((int32_t)0LL)));
                if (!t226) goto ie222;
                {
                    gm_th2lqbo77pvsc t228;
                    memset(&t228, 0, sizeof t228);
                    uint8_t t229 = 0;
                    F.ret = t228;
                    goto gm_fret;
                }
                goto ix222;
ie222:;
ix222:;
ix171:;
ix163:;
ix124:;
ix70:;
ix32:;
            }
            {
                uint8_t t230 = gm_l6;
                uint8_t t231 = ((t230 == ((uint8_t)10ULL)));
                gm_l5 = t231;
            }
            {
                uint64_t t232 = gm_l4;
                uint64_t r233; bool o234 = __builtin_add_overflow(t232, ((uint64_t)1ULL), &r233);
                if (o234) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 326, 9);
                uint64_t t235 = r233;
                gm_l4 = t235;
            }
lc2:;
        }
lb2:;
    }
    {
        {
            uint64_t t237 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t238 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t239 = gm_l2;
            uint64_t t240 = (uintptr_t)t239;
            uint64_t t241 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint64_t t242 = (uintptr_t)t241;
            if (t240 > t242 || t242 > (uintptr_t)t237) gm_trap_slice(t240, t242, t237, gm_srccfhjr4r9mdef, 329, 24);
            gm_b16_8 t243;
            GM_ST(void *, (uint8_t *)&t243, (void *)(((uint8_t *)t238 + t240 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t243 + 8, (uintptr_t)(t242 - t240));
            gm_l7 = t243;
        }
        gm_hval t244 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn245 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl8sdnavrdjn, .env = t244.env, .hctx = t244.cx, .with_frame = &F.h, .with_label = 5, .result = (void *)&F.w5, .fw = NULL, .next = cx};
        gm_b16_8 t248 = gm_l7;
        gm_b0_1 t249 = gm_f_lines_9uns0k2_snvhnj5((void *)NULL, (&wn245), NULL, t248);
        if (gm_unw.kind) {
            goto gm_ll247;
        }
        F.w5 = t249;
        goto gm_ls250;
gm_ll247:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 5) { gm_unw.kind = GM_NONE; goto we246; }
        goto gm_lad;
gm_ls250:;
we246:;
        gm_b0_1 t251 = F.w5;
        gm_b0_1 t252 = t251;
    }
    {
        gm_b16_8 t253 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_th2lqbo77pvsc t254;
        GM_ST(uint8_t, (uint8_t *)&t254 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t254 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t254 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t254), t253);
        ((uint8_t *)&t254)[16] = 1;
        uint8_t t255 = 0;
        F.ret = t254;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_line_reset_73tgnor(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_line_reset_73tgnor F;
    gm_b16_8 gm_l0; /* def */
    GM_STACK_CHECK(&F, 406u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = 0;
        gm_b16_8 t4 = gm_l0;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_spkrf9e7cr6h4));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(6));
        uint8_t t6 = gm_f_contains_gkki4kq(t4, t5);
        uint8_t t7 = (uint8_t)!t6;
        if (!t7) {
            gm_b16_8 t8 = gm_l0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, gm_ccmgjp6th6r);
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_tabb710d9mq2e t11;
            gm_f_format_uaq30dv((void *)&t11, (void *)NULL, cx, NULL, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t12 = t11;
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 8, gm_srccfhjr4r9mdef, 607, 57);
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
            gm_b16_8 t14 = t13;
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            uint8_t t16 = gm_f_ends_with_biriu5l(t8, t15);
            t3 = t16;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_b16_8 t18 = gm_l0;
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_t6j6smj7qoctu t20;
        gm_b16_8 t21 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 0, t21);
        gm_b16_8 t22 = GM_LD(gm_b16_8, gm_ccmgjp6th6r);
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 16, t22);
        gm_tabb710d9mq2e t23;
        gm_f_format_q8i7gsb((void *)&t23, (void *)NULL, cx, NULL, &t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t24 = t23;
        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_srccfhjr4r9mdef, 608, 12);
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
        gm_b16_8 t26 = t25;
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        uint8_t t28 = 0;
        F.ret = t27;
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
inline __attribute__((aligned(4))) uint64_t gm_f_optimized_unit_count_5mur81l(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_optimized_unit_count_5mur81l F;
    void * gm_l0; /* parts */
    uint64_t gm_l1; /* cpus */
    gm_tf5srh4mp4b3i gm_l2; /* forced */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        if (!((((uint8_t *)&gm_l2))[8] != 0)) goto ie2;
        uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        gm_l3 = t3;
        {
            uint64_t t5 = gm_l3;
            uint64_t t6 = (t5 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t5 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t5));
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 64) + 8);
        if (((uint64_t)4194304ULL) == 0) gm_trap("division by zero", gm_srccfhjr4r9mdef, 72, 13);
        uint64_t t10 = (uint64_t)(t9 / ((uint64_t)4194304ULL));
        uint64_t t11 = gm_l1;
        uint64_t t12 = (t11 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t11 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t11));
        uint64_t t13 = (t10 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t10 > t12 ? t12 : t10));
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_set_depths_1781b1p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_set_depths_1781b1p F;
    gm_b16_8 gm_l0; /* decls */
    void * gm_l1; /* by_name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint32_t gm_l6; /* depth */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* r */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* k */
    GM_STACK_CHECK(&F, 1003u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_b16_8 t4 = t3;
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
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
                    gm_t9re6l0g6jicn t12;
                    uint64_t t13 = gm_l3;
                    GM_ST(uint64_t, (uint8_t *)&t12 + 0, t13);
                    uint64_t t14 = gm_l3;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    memcpy((uint8_t *)&t12 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t15 * 56), 56);
                    gm_t9re6l0g6jicn t17 = t12;
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&t17) + 0));
                    gm_l4 = t20;
                    gm_t84tkl8vf34ko t21 = GM_LD(gm_t84tkl8vf34ko, (((uint8_t *)&t17) + 8));
                    F.l5 = t21;
                    {
                        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 48));
                        uint8_t t25 = GM_LD(uint8_t, gm_crpb2rekieb);
                        uint8_t t26 = (!(t24 == t25));
                        if (!t26) goto ie23;
                        {
                            goto lc6;
                        }
                        goto ix23;
ie23:;
ix23:;
                    }
                    {
                        gm_l6 = ((uint32_t)0ULL);
                    }
                    {
                        {
                            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 32));
                            gm_l7 = t29;
                        }
                        {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t31 = gm_l8;
                                uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                uint8_t t33 = (t31 < t32);
                                if (!t33) goto lb30;
                                {
                                    uint64_t t36 = gm_l8;
                                    uint64_t t37 = (uintptr_t)t36;
                                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t37 * 16));
                                    gm_l9 = t41;
                                    {
                                        gm_b16_8 t44 = gm_l9;
                                        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
                                        gm_b16_8 t46 = t44;
                                        gm_b16_8 t47 = t45;
                                        uint8_t t48 = (gm_eq0iqiubfm7e(((uint8_t *)&t46), ((uint8_t *)&t47)));
                                        if (!t48) goto ie43;
                                        {
                                            goto lc30;
                                        }
                                        goto ix43;
ie43:;
ix43:;
                                    }
                                    {
                                        void * t50 = gm_l1;
                                        gm_b16_8 t51 = gm_l9;
                                        gm_tqetrgo0q24b5 t52;
                                        gm_f_get_ohh8mo3((void *)&t52, t50, t51);
                                        gm_tqetrgo0q24b5 t53 = t52;
                                        gm_trqfvmjtt4sij t54;
                                        if (((((uint8_t *)&t53))[40] != 0)) {
                                            gm_trqfvmjtt4sij t55 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t53) + 0));
                                            t54 = t55;
                                        } else {
                                            goto lc30;
                                        }
                                        F.l10 = t54;
                                    }
                                    {
                                        {
                                            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                                            gm_l11 = t57;
                                        }
                                        {
                                            gm_l12 = ((uint64_t)0ULL);
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t59 = gm_l12;
                                                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                                uint8_t t61 = (t59 < t60);
                                                if (!t61) goto lb58;
                                                {
                                                    uint64_t t64 = gm_l12;
                                                    uint64_t t65 = (uintptr_t)t64;
                                                    uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                                    uint64_t t67 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t65 * 8));
                                                    uint64_t t68 = t67;
                                                    uint64_t t71 = GM_LD(uint64_t, ((uint8_t *)&t68));
                                                    gm_l13 = t71;
                                                    {
                                                        uint64_t t74 = gm_l13;
                                                        uint64_t t75 = gm_l4;
                                                        uint8_t t76 = (t74 < t75);
                                                        if (!t76) goto ie73;
                                                        uint64_t t77 = gm_l13;
                                                        uint64_t t78 = (uintptr_t)t77;
                                                        uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                                        if (t78 >= (uintptr_t)(t79)) gm_trap_index(t78, t79, gm_srccfhjr4r9mdef, 621, 30);
                                                        uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t78 * 56) + 48));
                                                        uint8_t t81 = GM_LD(uint8_t, gm_crpb2rekieb);
                                                        uint8_t t82 = ((t80 == t81));
                                                        if (!t82) goto ie73;
                                                        {
                                                            uint32_t t84 = gm_l6;
                                                            uint64_t t85 = gm_l13;
                                                            uint64_t t86 = (uintptr_t)t85;
                                                            uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                                            if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srccfhjr4r9mdef, 621, 77);
                                                            uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t86 * 56) + 52));
                                                            uint32_t r89; bool o90 = __builtin_add_overflow(t88, ((uint32_t)1ULL), &r89);
                                                            if (o90) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 621, 77);
                                                            uint32_t t91 = r89;
                                                            uint32_t t92 = (t84 > t91 ? t84 : t91);
                                                            gm_l6 = t92;
                                                        }
                                                        goto ix73;
ie73:;
ix73:;
                                                    }
                                                    goto me69;
mn70:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 620, 13);
me69:;
                                                }
lc58:;
                                                {
                                                    uint64_t t94 = gm_l12;
                                                    uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                                                    uint64_t t97 = r95;
                                                    gm_l12 = t97;
                                                }
                                            }
lb58:;
                                        }
                                    }
                                    goto me39;
mn40:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 617, 9);
me39:;
                                }
lc30:;
                                {
                                    uint64_t t99 = gm_l8;
                                    uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                                    uint64_t t102 = r100;
                                    gm_l8 = t102;
                                }
                            }
lb30:;
                        }
                    }
                    {
                        uint64_t t103 = gm_l4;
                        uint64_t t104 = (uintptr_t)t103;
                        uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srccfhjr4r9mdef, 624, 9);
                        uint32_t t106 = gm_l6;
                        GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t104 * 56) + 52), t106);
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 614, 5);
me18:;
                }
lc6:;
                {
                    uint64_t t108 = gm_l3;
                    uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                    uint64_t t111 = r109;
                    gm_l3 = t111;
                }
            }
lb6:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_skip_literal_17lq2p8(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_literal_17lq2p8 F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* i */
    uint8_t gm_l2; /* q */
    uint64_t gm_l3; /* j */
    GM_STACK_CHECK(&F, 233u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t3 >= (uintptr_t)(t4)) gm_trap_index(t3, t4, gm_srccfhjr4r9mdef, 405, 10);
        uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t3 * 1));
        gm_l2 = t5;
    }
    {
        uint64_t t6 = gm_l1;
        uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 406, 10);
        uint64_t t9 = r7;
        gm_l3 = t9;
    }
    {
        for (;;) {
            uint64_t t11 = gm_l3;
            uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t13 = (t11 < t12);
            if (!t13) goto lb10;
            uint64_t t14 = gm_l3;
            uint64_t t15 = (uintptr_t)t14;
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_srccfhjr4r9mdef, 407, 28);
            uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t15 * 1));
            uint8_t t18 = gm_l2;
            uint8_t t19 = (!(t17 == t18));
            if (!t19) goto lb10;
            {
                uint64_t t22 = gm_l3;
                uint64_t t23;
                uint64_t t25 = gm_l3;
                uint64_t t26 = (uintptr_t)t25;
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srccfhjr4r9mdef, 408, 17);
                uint8_t t28 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t26 * 1));
                uint8_t t29 = ((t28 == ((uint8_t)92ULL)));
                if (!t29) goto ie24;
                uint64_t t31 = ((uint64_t)2ULL);
                t23 = t31;
                goto ix24;
ie24:;
                uint64_t t33 = ((uint64_t)1ULL);
                t23 = t33;
ix24:;
                uint64_t r34; bool o35 = __builtin_add_overflow(t22, t23, &r34);
                if (o35) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 408, 9);
                uint64_t t36 = r34;
                gm_l3 = t36;
            }
lc10:;
        }
lb10:;
    }
    {
        uint64_t t37 = gm_l3;
        uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
        if (o39) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 410, 13);
        uint64_t t40 = r38;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t42 = (t40 < t41 ? t40 : t41);
        uint8_t t43 = 0;
        F.ret = t42;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sort_name_ro82p6b(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_name_ro82p6b F;
    void * gm_l0; /* d */
    GM_STACK_CHECK(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 16) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 16));
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 32) + 8);
        uint8_t t13 = (t12 > ((uint64_t)0ULL));
        if (!t13) goto ie10;
        {
            uint64_t t15 = (uintptr_t)((uint64_t)0ULL);
            void * t16 = gm_l0;
            uint64_t t17 = GM_LD(uintptr_t, (((uint8_t *)t16) + 32) + 8);
            if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_srccfhjr4r9mdef, 692, 32);
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t16) + 32)) + t15 * 16));
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(0));
        uint8_t t21 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_split_by_file_hgjqgsp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_split_by_file_hgjqgsp F;
    void * gm_l0; /* parts */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t6j6smj7qoctu gm_l6; /* o */
    gm_b16_8 gm_l8; /* all */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l11; /* item */
    gm_t6j6smj7qoctu gm_l12; /* d */
    gm_t6j6smj7qoctu gm_l13; /* f */
    gm_b16_8 gm_l14; /* name */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_b16_8 gm_l22; /* <tmp> */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* i */
    void * gm_l29; /* l */
    gm_b16_8 gm_l32; /* first */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    gm_b16_8 gm_l39; /* def */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* k */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    gm_t6j6smj7qoctu gm_l49; /* f */
    GM_STACK_CHECK(&F, 4592u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 96) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) goto ie2;
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)t6) + 64) + 8);
        uint8_t t8 = (t7 > ((uint64_t)0ULL));
        if (!t8) goto ie2;
        {
            gm_ttu09lij36f8c t10;
            memset(&t10, 0, sizeof t10);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_list_016fo30((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = (void *)((uint8_t *)&F.l1);
        void * t13 = gm_l0;
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 128));
        uint8_t t15 = GM_LD(uint8_t, gm_crpb2rekieb);
        gm_b0_1 t16 = gm_f_decls_of_srep9du((void *)NULL, cx, NULL, t12, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t17 = (void *)((uint8_t *)&F.l1);
        void * t18 = gm_l0;
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)t18) + 144));
        uint8_t t20 = GM_LD(uint8_t, gm_cos4c858gsq);
        gm_b0_1 t21 = gm_f_decls_of_srep9du((void *)NULL, cx, NULL, t17, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_hash_map_lljdp3h((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        gm_f_list_1g734tk((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            void * t24 = gm_l0;
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)t24) + 96));
            gm_l4 = t25;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t27 = gm_l5;
                uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t29 = (t27 < t28);
                if (!t29) goto lb26;
                {
                    uint64_t t32 = gm_l5;
                    uint64_t t33 = (uintptr_t)t32;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t6j6smj7qoctu t37 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t33 * 32));
                    gm_l6 = t37;
                    {
                        gm_f_list_c8kt75n((void *)&F.l7, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll30;
                        }
                    }
                    {
                        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 16));
                        gm_th2lqbo77pvsc t40;
                        gm_f_items_of_q103oiv((void *)&t40, (void *)NULL, cx, NULL, t39);
                        if (gm_unw.kind) {
                            goto gm_ll30;
                        }
                        gm_th2lqbo77pvsc t41 = t40;
                        gm_b16_8 t42;
                        if (((((uint8_t *)&t41))[16] != 0)) {
                            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t41) + 0));
                            t42 = t43;
                        } else {
                            gm_ttu09lij36f8c t44;
                            memset(&t44, 0, sizeof t44);
                            uint8_t t45 = 0;
                            F.ret = t44;
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t45;
                            goto gm_ll30;
                        }
                        gm_l8 = t42;
                    }
                    {
                        {
                            gm_b16_8 t47 = gm_l8;
                            gm_l9 = t47;
                        }
                        {
                            gm_l10 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t49 = gm_l10;
                                uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                uint8_t t51 = (t49 < t50);
                                if (!t51) goto lb48;
                                {
                                    uint64_t t54 = gm_l10;
                                    uint64_t t55 = (uintptr_t)t54;
                                    uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                    gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t55 * 16));
                                    gm_l11 = t59;
                                    {
                                        gm_b16_8 t62 = gm_l11;
                                        gm_ttu09lij36f8c t63;
                                        gm_f_inline_definition_6aokb9d((void *)&t63, (void *)NULL, cx, NULL, t62);
                                        if (gm_unw.kind) {
                                            goto gm_ll52;
                                        }
                                        gm_ttu09lij36f8c t64 = t63;
                                        if (!((((uint8_t *)&t64))[32] != 0)) goto ie61;
                                        gm_t6j6smj7qoctu t65 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t64));
                                        gm_l12 = t65;
                                        {
                                            void * t67 = (void *)((uint8_t *)&F.l2);
                                            gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                                            uint64_t t69 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                                            gm_tftb1odc739t5 t70 = gm_f_put_qoojoqv(t67, t68, t69);
                                            gm_tftb1odc739t5 t71 = t70;
                                            if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srccfhjr4r9mdef, 518, 17);
                                            gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                                        }
                                        {
                                            void * t73 = (void *)((uint8_t *)&F.l1);
                                            gm_t84tkl8vf34ko t74;
                                            memset((uint8_t *)&t74 + 49, 0, 3);
                                            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                                            gm_b16_8 t76 = gm_f_line_reset_73tgnor((void *)NULL, cx, NULL, t75);
                                            if (gm_unw.kind) {
                                                goto gm_ll52;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t74 + 0, t76);
                                            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                                            GM_ST(gm_b16_8, (uint8_t *)&t74 + 16, t77);
                                            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                                            gm_b16_8 t79 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t78);
                                            if (gm_unw.kind) {
                                                goto gm_ll52;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t74 + 32, t79);
                                            uint8_t t80 = GM_LD(uint8_t, gm_c30tdq2p48i);
                                            GM_ST(uint8_t, (uint8_t *)&t74 + 48, t80);
                                            GM_ST(uint32_t, (uint8_t *)&t74 + 52, ((uint32_t)0ULL));
                                            gm_tftb1odc739t5 t81 = gm_f_push_rda4o9n(t73, &t74);
                                            gm_tftb1odc739t5 t82 = t81;
                                            if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 1, gm_srccfhjr4r9mdef, 519, 17);
                                            gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 1));
                                        }
                                        {
                                            goto lc48;
                                        }
                                        goto ix61;
ie61:;
ix61:;
                                    }
                                    {
                                        gm_b16_8 t84 = gm_l11;
                                        gm_ttu09lij36f8c t85;
                                        gm_f_function_o2lv8g6((void *)&t85, (void *)NULL, cx, NULL, t84);
                                        if (gm_unw.kind) {
                                            goto gm_ll52;
                                        }
                                        gm_ttu09lij36f8c t86 = t85;
                                        gm_t6j6smj7qoctu t87;
                                        if (((((uint8_t *)&t86))[32] != 0)) {
                                            gm_t6j6smj7qoctu t88 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t86) + 0));
                                            t87 = t88;
                                        } else {
                                            gm_ttu09lij36f8c t89;
                                            memset(&t89, 0, sizeof t89);
                                            uint8_t t90 = 0;
                                            F.ret = t89;
                                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t90;
                                            goto gm_ll52;
                                        }
                                        gm_l13 = t87;
                                    }
                                    {
                                        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                                        gm_th2lqbo77pvsc t92;
                                        gm_f_c_fn_name_77uspqp((void *)&t92, t91);
                                        gm_th2lqbo77pvsc t93 = t92;
                                        gm_b16_8 t94;
                                        if (((((uint8_t *)&t93))[16] != 0)) {
                                            gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&t93) + 0));
                                            t94 = t95;
                                        } else {
                                            gm_ttu09lij36f8c t96;
                                            memset(&t96, 0, sizeof t96);
                                            uint8_t t97 = 0;
                                            F.ret = t96;
                                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t97;
                                            goto gm_ll52;
                                        }
                                        gm_l14 = t94;
                                    }
                                    {
                                        void * t98 = (void *)((uint8_t *)&F.l7);
                                        gm_t6j6smj7qoctu t99;
                                        gm_b16_8 t100 = gm_l14;
                                        GM_ST(gm_b16_8, (uint8_t *)&t99 + 0, t100);
                                        gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 16));
                                        gm_b16_8 t102 = gm_f_line_reset_73tgnor((void *)NULL, cx, NULL, t101);
                                        if (gm_unw.kind) {
                                            goto gm_ll52;
                                        }
                                        GM_ST(gm_b16_8, (uint8_t *)&t99 + 16, t102);
                                        gm_tftb1odc739t5 t103 = gm_f_push_0ek3ea4(t98, &t99);
                                        gm_tftb1odc739t5 t104 = t103;
                                        if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_srccfhjr4r9mdef, 524, 13);
                                        gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
                                    }
                                    {
                                        void * t106 = (void *)((uint8_t *)&F.l1);
                                        gm_t84tkl8vf34ko t107;
                                        memset((uint8_t *)&t107 + 49, 0, 3);
                                        gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                                        gm_tnpifv20irnph t109;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t109 + 0), t108);
                                        gm_tabb710d9mq2e t110;
                                        gm_f_format_uaq30dv((void *)&t110, (void *)NULL, cx, NULL, t109);
                                        if (gm_unw.kind) {
                                            goto gm_ll52;
                                        }
                                        gm_tabb710d9mq2e t111 = t110;
                                        if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 8, gm_srccfhjr4r9mdef, 525, 35);
                                        gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
                                        gm_b16_8 t113 = t112;
                                        gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                                        GM_ST(gm_b16_8, (uint8_t *)&t107 + 0, t114);
                                        gm_b16_8 t115 = gm_l14;
                                        GM_ST(gm_b16_8, (uint8_t *)&t107 + 16, t115);
                                        gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                                        gm_b16_8 t117 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t116);
                                        if (gm_unw.kind) {
                                            goto gm_ll52;
                                        }
                                        GM_ST(gm_b16_8, (uint8_t *)&t107 + 32, t117);
                                        uint8_t t118 = GM_LD(uint8_t, gm_c72j1g8d455);
                                        GM_ST(uint8_t, (uint8_t *)&t107 + 48, t118);
                                        GM_ST(uint32_t, (uint8_t *)&t107 + 52, ((uint32_t)0ULL));
                                        gm_tftb1odc739t5 t119 = gm_f_push_rda4o9n(t106, &t107);
                                        gm_tftb1odc739t5 t120 = t119;
                                        if (((((uint8_t *)&t120))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t120) + 1, gm_srccfhjr4r9mdef, 525, 13);
                                        gm_b0_1 t121 = GM_LD(gm_b0_1, (((uint8_t *)&t120) + 1));
                                    }
                                    goto me57;
mn58:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 516, 9);
me57:;
                                }
lc48:;
                                {
                                    uint64_t t123 = gm_l10;
                                    uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                                    uint64_t t126 = r124;
                                    gm_l10 = t126;
                                }
                            }
gm_ll52:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb48; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc48; }
                            }
                            goto gm_ll30;
lb48:;
                        }
                    }
                    {
                        void * t127 = (void *)((uint8_t *)&F.l3);
                        gm_tftb1odc739t5 t128 = gm_f_push_rlpbnuq(t127, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l7)));
                        gm_tftb1odc739t5 t129 = t128;
                        if (((((uint8_t *)&t129))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t129) + 1, gm_srccfhjr4r9mdef, 527, 9);
                        gm_b0_1 t130 = GM_LD(gm_b0_1, (((uint8_t *)&t129) + 1));
                    }
                    goto me35;
mn36:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 513, 5);
me35:;
                }
lc26:;
                {
                    uint64_t t132 = gm_l5;
                    uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                    uint64_t t135 = r133;
                    gm_l5 = t135;
                }
            }
gm_ll30:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb26; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc26; }
            }
            goto gm_ld1_1;
lb26:;
        }
    }
    {
        gm_f_list_1g734tk((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_l16 = t137;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t139 = gm_l17;
                uint64_t t140 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t141 = (t139 < t140);
                if (!t141) goto lb138;
                {
                    uint64_t t144 = gm_l17;
                    uint64_t t145 = (uintptr_t)t144;
                    uint64_t t146 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    {
                        void * t150 = (void *)((uint8_t *)&F.l15);
                        gm_trqfvmjtt4sij t151;
                        gm_f_list_c8kt75n((void *)&t151, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll142;
                        }
                        gm_tftb1odc739t5 t152 = gm_f_push_rlpbnuq(t150, &t151);
                        gm_tftb1odc739t5 t153 = t152;
                        if (((((uint8_t *)&t153))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t153) + 1, gm_srccfhjr4r9mdef, 530, 29);
                        gm_b0_1 t154 = GM_LD(gm_b0_1, (((uint8_t *)&t153) + 1));
                    }
                    goto me147;
mn148:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 530, 5);
me147:;
                }
lc138:;
                {
                    uint64_t t156 = gm_l17;
                    uint64_t r157; bool o158 = __builtin_add_overflow(t156, ((uint64_t)1ULL), &r157);
                    uint64_t t159 = r157;
                    gm_l17 = t159;
                }
            }
gm_ll142:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb138; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc138; }
            }
            goto gm_ld1_1;
lb138:;
        }
    }
    {
        {
            void * t161 = gm_l0;
            gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)t161) + 16));
            gm_l18 = t162;
        }
        gm_hval t163 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn164 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlt54s6krmq6, .env = t163.env, .hctx = t163.cx, .with_frame = &F.h, .with_label = 3, .result = (void *)&F.w3, .fw = NULL, .next = cx};
        gm_b16_8 t167 = gm_l18;
        gm_b0_1 t168 = gm_f_lines_9uns0k2_surb2to((void *)NULL, (&wn164), NULL, t167);
        if (gm_unw.kind) {
            goto gm_ll166;
        }
        F.w3 = t168;
        goto gm_ls169;
gm_ll166:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 3) { gm_unw.kind = GM_NONE; goto we165; }
        goto gm_ld1_1;
gm_ls169:;
we165:;
        gm_b0_1 t170 = F.w3;
        gm_b0_1 t171 = t170;
    }
    {
        gm_f_list_b7l80f8((void *)&F.l19, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            gm_l21 = ((uint64_t)0ULL);
        }
        {
            gm_l20 = ((uint64_t)16ULL);
        }
        {
            for (;;) {
                uint64_t t174 = gm_l21;
                uint64_t t175 = gm_l20;
                uint8_t t176 = (t174 < t175);
                if (!t176) goto lb173;
                {
                    uint64_t t179 = gm_l21;
                    uint64_t t180 = t179;
                    {
                        void * t184 = (void *)((uint8_t *)&F.l19);
                        gm_trqfvmjtt4sij t185;
                        gm_f_list_epvducg((void *)&t185, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll177;
                        }
                        gm_tftb1odc739t5 t186 = gm_f_push_vnejmrp(t184, &t185);
                        gm_tftb1odc739t5 t187 = t186;
                        if (((((uint8_t *)&t187))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t187) + 1, gm_srccfhjr4r9mdef, 547, 31);
                        gm_b0_1 t188 = GM_LD(gm_b0_1, (((uint8_t *)&t187) + 1));
                    }
                    goto me181;
mn182:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 547, 5);
me181:;
                }
lc173:;
                {
                    uint64_t t190 = gm_l21;
                    uint64_t r191; bool o192 = __builtin_add_overflow(t190, ((uint64_t)1ULL), &r191);
                    uint64_t t193 = r191;
                    gm_l21 = t193;
                }
            }
gm_ll177:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb173; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc173; }
            }
            goto gm_ld1_1;
lb173:;
        }
    }
    {
        {
            void * t195 = gm_l0;
            gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)t195) + 32));
            gm_l22 = t196;
        }
        gm_hval t197 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn198 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hln0l2hrppmf, .env = t197.env, .hctx = t197.cx, .with_frame = &F.h, .with_label = 5, .result = (void *)&F.w5, .fw = NULL, .next = cx};
        gm_b16_8 t201 = gm_l22;
        gm_b0_1 t202 = gm_f_lines_9uns0k2_scbceav((void *)NULL, (&wn198), NULL, t201);
        if (gm_unw.kind) {
            goto gm_ll200;
        }
        F.w5 = t202;
        goto gm_ls203;
gm_ll200:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 5) { gm_unw.kind = GM_NONE; goto we199; }
        goto gm_ld1_1;
gm_ls203:;
we199:;
        gm_b0_1 t204 = F.w5;
        gm_b0_1 t205 = t204;
    }
    {
        gm_f_hash_map_dklib0j((void *)&F.l23, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l24, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
        {
            gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
            gm_b16_8 t209 = t208;
            gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&t209));
            gm_l25 = t210;
        }
        {
            gm_l26 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t212 = gm_l26;
                uint64_t t213 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                uint8_t t214 = (t212 < t213);
                if (!t214) goto lb211;
                {
                    gm_t9re6l0g6jicn t217;
                    uint64_t t218 = gm_l26;
                    GM_ST(uint64_t, (uint8_t *)&t217 + 0, t218);
                    uint64_t t219 = gm_l26;
                    uint64_t t220 = (uintptr_t)t219;
                    uint64_t t221 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                    memcpy((uint8_t *)&t217 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t220 * 56), 56);
                    gm_t9re6l0g6jicn t222 = t217;
                    uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)&t222) + 0));
                    gm_l27 = t225;
                    gm_t84tkl8vf34ko t226 = GM_LD(gm_t84tkl8vf34ko, (((uint8_t *)&t222) + 8));
                    F.l28 = t226;
                    {
                        uint64_t t229 = GM_LD(uintptr_t, (((uint8_t *)&F.l28) + 16) + 8);
                        uint8_t t230 = ((t229 == ((uint64_t)0ULL)));
                        if (!t230) goto ie228;
                        {
                            void * t232 = (void *)((uint8_t *)&F.l24);
                            uint64_t t233 = gm_l27;
                            gm_tftb1odc739t5 t234 = gm_f_push_ucgmlo2(t232, t233);
                            gm_tftb1odc739t5 t235 = t234;
                            if (((((uint8_t *)&t235))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t235) + 1, gm_srccfhjr4r9mdef, 566, 13);
                            gm_b0_1 t236 = GM_LD(gm_b0_1, (((uint8_t *)&t235) + 1));
                        }
                        {
                            goto lc211;
                        }
                        goto ix228;
ie228:;
ix228:;
                    }
                    {
                        void * t238 = (void *)((uint8_t *)&F.l23);
                        gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 16));
                        void * t240 = gm_f_get_ptr_53uc4fm(t238, t239);
                        void * t241 = t240;
                        if (!(GM_LD(void *, ((uint8_t *)&t241)) != 0)) goto ie237;
                        void * t242 = GM_LD(void *, ((uint8_t *)&t241));
                        gm_l29 = t242;
                        {
                            void * t244 = gm_l29;
                            uint64_t t245 = gm_l27;
                            gm_tftb1odc739t5 t246 = gm_f_push_ucgmlo2(t244, t245);
                            gm_tftb1odc739t5 t247 = t246;
                            if (((((uint8_t *)&t247))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t247) + 1, gm_srccfhjr4r9mdef, 570, 13);
                            gm_b0_1 t248 = GM_LD(gm_b0_1, (((uint8_t *)&t247) + 1));
                        }
                        goto ix237;
ie237:;
                        {
                            gm_f_list_5dhgg4i((void *)&F.l30, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll215;
                            }
                        }
                        {
                            void * t250 = (void *)((uint8_t *)&F.l30);
                            uint64_t t251 = gm_l27;
                            gm_tftb1odc739t5 t252 = gm_f_push_ucgmlo2(t250, t251);
                            gm_tftb1odc739t5 t253 = t252;
                            if (((((uint8_t *)&t253))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t253) + 1, gm_srccfhjr4r9mdef, 573, 13);
                            gm_b0_1 t254 = GM_LD(gm_b0_1, (((uint8_t *)&t253) + 1));
                        }
                        {
                            void * t255 = (void *)((uint8_t *)&F.l23);
                            gm_b16_8 t256 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 16));
                            gm_tftb1odc739t5 t257 = gm_f_put_7o5vpta(t255, t256, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l30)));
                            gm_tftb1odc739t5 t258 = t257;
                            if (((((uint8_t *)&t258))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t258) + 1, gm_srccfhjr4r9mdef, 574, 13);
                            gm_b0_1 t259 = GM_LD(gm_b0_1, (((uint8_t *)&t258) + 1));
                        }
ix237:;
                    }
                    goto me223;
mn224:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 564, 5);
me223:;
                }
lc211:;
                {
                    uint64_t t261 = gm_l26;
                    uint64_t r262; bool o263 = __builtin_add_overflow(t261, ((uint64_t)1ULL), &r262);
                    uint64_t t264 = r262;
                    gm_l26 = t264;
                }
            }
gm_ll215:;
            if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb211; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc211; }
            }
            goto gm_ld1_2;
lb211:;
        }
    }
    {
        gm_b16_8 t265 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        void * t266 = (void *)((uint8_t *)&F.l23);
        gm_b0_1 t267 = gm_f_set_depths_1781b1p((void *)NULL, cx, NULL, t265, t266);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l31, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
        gm_t6j6smj7qoctu t268;
        void * t269 = gm_l0;
        gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)t269) + 48));
        GM_ST(gm_b16_8, (uint8_t *)&t268 + 0, t270);
        void * t271 = gm_l0;
        gm_b16_8 t272 = GM_LD(gm_b16_8, (((uint8_t *)t271) + 80));
        GM_ST(gm_b16_8, (uint8_t *)&t268 + 16, t272);
        gm_tabb710d9mq2e t273;
        gm_f_format_8n626pi((void *)&t273, (void *)NULL, cx, NULL, &t268);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
        gm_tabb710d9mq2e t274 = t273;
        if (((((uint8_t *)&t274))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t274) + 8, gm_srccfhjr4r9mdef, 579, 14);
        gm_b16_8 t275 = GM_LD(gm_b16_8, (((uint8_t *)&t274) + 8));
        gm_l32 = t275;
    }
    {
        void * t276 = (void *)((uint8_t *)&F.l31);
        gm_b16_8 t277 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t278 = t277;
        gm_b16_8 t279 = GM_LD(gm_b16_8, ((uint8_t *)&t278));
        void * t280 = (void *)((uint8_t *)&F.l23);
        gm_b16_8 t281 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
        gm_b16_8 t282 = t281;
        gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&t282));
        gm_b16_8 t284 = gm_l32;
        gm_b16_8 t285 = t284;
        gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&t285));
        void * t287 = gm_l0;
        gm_b16_8 t288 = GM_LD(gm_b16_8, (((uint8_t *)t287) + 160));
        gm_b16_8 t289 = gm_f_unit_text_mmejmp8((void *)NULL, cx, NULL, t279, t280, t283, t286, t288);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
        gm_tftb1odc739t5 t290 = gm_f_push_3ef746b(t276, t289);
        gm_tftb1odc739t5 t291 = t290;
        if (((((uint8_t *)&t291))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t291) + 1, gm_srccfhjr4r9mdef, 580, 5);
        gm_b0_1 t292 = GM_LD(gm_b0_1, (((uint8_t *)&t291) + 1));
    }
    {
        {
            gm_b16_8 t294 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
            gm_l33 = t294;
        }
        {
            gm_l34 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t296 = gm_l34;
                uint64_t t297 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                uint8_t t298 = (t296 < t297);
                if (!t298) goto lb295;
                {
                    uint64_t t301 = gm_l34;
                    uint64_t t302 = (uintptr_t)t301;
                    uint64_t t303 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                    gm_trqfvmjtt4sij t306 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t302 * 40));
                    F.l35 = t306;
                    {
                        uint64_t t309 = GM_LD(uintptr_t, (((uint8_t *)&F.l35) + 0) + 8);
                        uint8_t t310 = ((t309 == ((uint64_t)0ULL)));
                        if (!t310) goto ie308;
                        {
                            goto lc295;
                        }
                        goto ix308;
ie308:;
ix308:;
                    }
                    {
                        gm_b16_8 t312 = GM_LD(gm_b16_8, (((uint8_t *)&F.l35) + 0));
                        gm_f_sort_skeree7(t312);
                    }
                    {
                        gm_f_list_uvfu1k1((void *)&F.l36, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll299;
                        }
                    }
                    {
                        {
                            gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&F.l35) + 0));
                            gm_l37 = t314;
                        }
                        {
                            gm_l38 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t316 = gm_l38;
                                uint64_t t317 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                                uint8_t t318 = (t316 < t317);
                                if (!t318) goto lb315;
                                {
                                    uint64_t t321 = gm_l38;
                                    uint64_t t322 = (uintptr_t)t321;
                                    uint64_t t323 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                                    gm_b16_8 t326 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l37)) + t322 * 16));
                                    gm_l39 = t326;
                                    {
                                        void * t328 = (void *)((uint8_t *)&F.l36);
                                        gm_b16_8 t329 = gm_l39;
                                        gm_tnpifv20irnph t330;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t330 + 0), t329);
                                        gm_tftb1odc739t5 t331 = gm_f_write_cdouvhd(t328, t330);
                                        gm_tftb1odc739t5 t332 = t331;
                                        if (((((uint8_t *)&t332))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t332) + 1, gm_srccfhjr4r9mdef, 586, 30);
                                        gm_b0_1 t333 = GM_LD(gm_b0_1, (((uint8_t *)&t332) + 1));
                                    }
                                    goto me324;
mn325:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 586, 9);
me324:;
                                }
lc315:;
                                {
                                    uint64_t t335 = gm_l38;
                                    uint64_t r336; bool o337 = __builtin_add_overflow(t335, ((uint64_t)1ULL), &r336);
                                    uint64_t t338 = r336;
                                    gm_l38 = t338;
                                }
                            }
lb315:;
                        }
                    }
                    {
                        void * t339 = (void *)((uint8_t *)&F.l31);
                        gm_b16_8 t340 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
                        gm_b16_8 t341 = t340;
                        gm_b16_8 t342 = GM_LD(gm_b16_8, ((uint8_t *)&t341));
                        void * t343 = (void *)((uint8_t *)&F.l23);
                        gm_b16_8 t344 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                        gm_b16_8 t345 = t344;
                        gm_b16_8 t346 = GM_LD(gm_b16_8, ((uint8_t *)&t345));
                        gm_b16_8 t347 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 0));
                        gm_b16_8 t348 = t347;
                        gm_b16_8 t349 = GM_LD(gm_b16_8, ((uint8_t *)&t348));
                        void * t350 = gm_l0;
                        gm_b16_8 t351 = GM_LD(gm_b16_8, (((uint8_t *)t350) + 160));
                        gm_b16_8 t352 = gm_f_unit_text_mmejmp8((void *)NULL, cx, NULL, t342, t343, t346, t349, t351);
                        if (gm_unw.kind) {
                            goto gm_ll299;
                        }
                        gm_tftb1odc739t5 t353 = gm_f_push_3ef746b(t339, t352);
                        gm_tftb1odc739t5 t354 = t353;
                        if (((((uint8_t *)&t354))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t354) + 1, gm_srccfhjr4r9mdef, 587, 9);
                        gm_b0_1 t355 = GM_LD(gm_b0_1, (((uint8_t *)&t354) + 1));
                    }
                    goto me304;
mn305:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 581, 5);
me304:;
                }
lc295:;
                {
                    uint64_t t357 = gm_l34;
                    uint64_t r358; bool o359 = __builtin_add_overflow(t357, ((uint64_t)1ULL), &r358);
                    uint64_t t360 = r358;
                    gm_l34 = t360;
                }
            }
gm_ll299:;
            if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb295; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc295; }
            }
            goto gm_ld1_2;
lb295:;
        }
    }
    {
        {
            gm_b16_8 t362 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t363 = t362;
            gm_b16_8 t364 = GM_LD(gm_b16_8, ((uint8_t *)&t363));
            gm_l40 = t364;
        }
        {
            gm_l41 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t366 = gm_l41;
                uint64_t t367 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                uint8_t t368 = (t366 < t367);
                if (!t368) goto lb365;
                {
                    gm_tmupb6k0asuti t371;
                    uint64_t t372 = gm_l41;
                    GM_ST(uint64_t, (uint8_t *)&t371 + 0, t372);
                    uint64_t t373 = gm_l41;
                    uint64_t t374 = (uintptr_t)t373;
                    uint64_t t375 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                    memcpy((uint8_t *)&t371 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t374 * 40), 40);
                    gm_tmupb6k0asuti t376 = t371;
                    uint64_t t379 = GM_LD(uint64_t, (((uint8_t *)&t376) + 0));
                    gm_l42 = t379;
                    gm_trqfvmjtt4sij t380 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t376) + 8));
                    F.l43 = t380;
                    {
                        uint64_t t382 = gm_l42;
                        uint64_t t383 = (uintptr_t)t382;
                        uint64_t t384 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                        if (t383 >= (uintptr_t)(t384)) gm_trap_index(t383, t384, gm_srccfhjr4r9mdef, 590, 16);
                        gm_trqfvmjtt4sij t385 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t383 * 40));
                        F.l44 = t385;
                    }
                    {
                        uint64_t t387 = GM_LD(uintptr_t, (((uint8_t *)&F.l43) + 0) + 8);
                        uint8_t t388 = ((t387 == ((uint64_t)0ULL)));
                        if (!t388) goto ie386;
                        uint64_t t389 = GM_LD(uintptr_t, (((uint8_t *)&F.l44) + 0) + 8);
                        uint8_t t390 = ((t389 == ((uint64_t)0ULL)));
                        if (!t390) goto ie386;
                        {
                            goto lc365;
                        }
                        goto ix386;
ie386:;
ix386:;
                    }
                    {
                        gm_f_list_c8kt75n((void *)&F.l45, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll369;
                        }
                    }
                    {
                        void * t392 = (void *)((uint8_t *)&F.l45);
                        gm_b16_8 t393 = GM_LD(gm_b16_8, (((uint8_t *)&F.l43) + 0));
                        gm_b16_8 t394 = t393;
                        gm_b16_8 t395 = GM_LD(gm_b16_8, ((uint8_t *)&t394));
                        gm_tftb1odc739t5 t396 = gm_f_extend_k51ik07(t392, t395);
                        gm_tftb1odc739t5 t397 = t396;
                        if (((((uint8_t *)&t397))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t397) + 1, gm_srccfhjr4r9mdef, 593, 9);
                        gm_b0_1 t398 = GM_LD(gm_b0_1, (((uint8_t *)&t397) + 1));
                    }
                    {
                        void * t399 = (void *)((uint8_t *)&F.l45);
                        gm_b16_8 t400 = GM_LD(gm_b16_8, (((uint8_t *)&F.l44) + 0));
                        gm_b16_8 t401 = t400;
                        gm_b16_8 t402 = GM_LD(gm_b16_8, ((uint8_t *)&t401));
                        gm_tftb1odc739t5 t403 = gm_f_extend_k51ik07(t399, t402);
                        gm_tftb1odc739t5 t404 = t403;
                        if (((((uint8_t *)&t404))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t404) + 1, gm_srccfhjr4r9mdef, 594, 9);
                        gm_b0_1 t405 = GM_LD(gm_b0_1, (((uint8_t *)&t404) + 1));
                    }
                    {
                        gm_b16_8 t406 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 0));
                        gm_closure t407 = ((gm_closure){GM_PTR_TAG(gm_f_fn_if8ammt), NULL, cx});
                        gm_f_sort_by_4j6ktaf(t406, t407);
                        if (gm_unw.kind) {
                            goto gm_ll369;
                        }
                    }
                    {
                        gm_f_list_uvfu1k1((void *)&F.l46, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll369;
                        }
                    }
                    {
                        {
                            gm_b16_8 t409 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 0));
                            gm_l47 = t409;
                        }
                        {
                            gm_l48 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t411 = gm_l48;
                                uint64_t t412 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                                uint8_t t413 = (t411 < t412);
                                if (!t413) goto lb410;
                                {
                                    uint64_t t416 = gm_l48;
                                    uint64_t t417 = (uintptr_t)t416;
                                    uint64_t t418 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                                    gm_t6j6smj7qoctu t421 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l47)) + t417 * 32));
                                    gm_l49 = t421;
                                    {
                                        void * t423 = (void *)((uint8_t *)&F.l46);
                                        gm_b16_8 t424 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l49) + 16));
                                        gm_tftb1odc739t5 t425 = gm_f_extend_0cs85gs(t423, t424);
                                        gm_tftb1odc739t5 t426 = t425;
                                        if (((((uint8_t *)&t426))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t426) + 1, gm_srccfhjr4r9mdef, 597, 30);
                                        gm_b0_1 t427 = GM_LD(gm_b0_1, (((uint8_t *)&t426) + 1));
                                    }
                                    goto me419;
mn420:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 597, 9);
me419:;
                                }
lc410:;
                                {
                                    uint64_t t429 = gm_l48;
                                    uint64_t r430; bool o431 = __builtin_add_overflow(t429, ((uint64_t)1ULL), &r430);
                                    uint64_t t432 = r430;
                                    gm_l48 = t432;
                                }
                            }
lb410:;
                        }
                    }
                    {
                        void * t433 = (void *)((uint8_t *)&F.l31);
                        gm_b16_8 t434 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
                        gm_b16_8 t435 = t434;
                        gm_b16_8 t436 = GM_LD(gm_b16_8, ((uint8_t *)&t435));
                        void * t437 = (void *)((uint8_t *)&F.l23);
                        gm_b16_8 t438 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                        gm_b16_8 t439 = t438;
                        gm_b16_8 t440 = GM_LD(gm_b16_8, ((uint8_t *)&t439));
                        gm_b16_8 t441 = GM_LD(gm_b16_8, (((uint8_t *)&F.l46) + 0));
                        gm_b16_8 t442 = t441;
                        gm_b16_8 t443 = GM_LD(gm_b16_8, ((uint8_t *)&t442));
                        void * t444 = gm_l0;
                        gm_b16_8 t445 = GM_LD(gm_b16_8, (((uint8_t *)t444) + 160));
                        gm_b16_8 t446 = gm_f_unit_text_mmejmp8((void *)NULL, cx, NULL, t436, t437, t440, t443, t445);
                        if (gm_unw.kind) {
                            goto gm_ll369;
                        }
                        gm_tftb1odc739t5 t447 = gm_f_push_3ef746b(t433, t446);
                        gm_tftb1odc739t5 t448 = t447;
                        if (((((uint8_t *)&t448))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t448) + 1, gm_srccfhjr4r9mdef, 598, 9);
                        gm_b0_1 t449 = GM_LD(gm_b0_1, (((uint8_t *)&t448) + 1));
                    }
                    goto me377;
mn378:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 589, 5);
me377:;
                }
lc365:;
                {
                    uint64_t t451 = gm_l41;
                    uint64_t r452; bool o453 = __builtin_add_overflow(t451, ((uint64_t)1ULL), &r452);
                    uint64_t t454 = r452;
                    gm_l41 = t454;
                }
            }
gm_ll369:;
            if (gm_unw.frame == &F.h && gm_unw.label == 9) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb365; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc365; }
            }
            goto gm_ld1_2;
lb365:;
        }
    }
    {
        gm_t6j6smj7qoctu t455;
        void * t456 = gm_l0;
        gm_b16_8 t457 = GM_LD(gm_b16_8, (((uint8_t *)t456) + 112));
        GM_ST(gm_b16_8, (uint8_t *)&t455 + 0, t457);
        gm_b16_8 t458 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t455 + 16, t458);
        gm_ttu09lij36f8c t459;
        GM_ST(uint8_t, (uint8_t *)&t459 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t459 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t459 + 36, 0);
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t459), t455);
        ((uint8_t *)&t459)[32] = 1;
        uint8_t t460 = 0;
        F.ret = t459;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t460;
        goto gm_ld1_2;
    }
    {
        void * t461 = (void *)((uint8_t *)&F.l23);
        gm_f_deinit_vtu6170(t461);
    }
    {
        void * t462 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_nvuvq05(t462);
    }
    goto gm_ls463;
gm_ld1_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t464 = (void *)((uint8_t *)&F.l23);
        gm_f_deinit_vtu6170(t464);
        gm_unw = gm_saved;
    }
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t465 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_nvuvq05(t465);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls463:;
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
inline __attribute__((aligned(4))) void gm_f_split_jendjdm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_split_jendjdm F;
    void * gm_l0; /* parts */
    uint64_t gm_l1; /* n */
    gm_t6j6smj7qoctu gm_l2; /* s */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* u */
    GM_STACK_CHECK(&F, 575u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_ttu09lij36f8c t4;
        gm_f_split_shared_a91530r((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t5 = t4;
        gm_t6j6smj7qoctu t6;
        if (((((uint8_t *)&t5))[32] != 0)) {
            gm_t6j6smj7qoctu t7 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_th2lqbo77pvsc t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        gm_f_list_epvducg((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 16));
            gm_l4 = t11;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l5;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l5;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t19 * 16));
                    gm_l6 = t23;
                    {
                        void * t25 = (void *)((uint8_t *)&F.l3);
                        gm_t6j6smj7qoctu t26;
                        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t26 + 0, t27);
                        gm_b16_8 t28 = gm_l6;
                        GM_ST(gm_b16_8, (uint8_t *)&t26 + 16, t28);
                        gm_tabb710d9mq2e t29;
                        gm_f_format_8n626pi((void *)&t29, (void *)NULL, cx, NULL, &t26);
                        if (gm_unw.kind) {
                            goto gm_ll16;
                        }
                        gm_tabb710d9mq2e t30 = t29;
                        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_srccfhjr4r9mdef, 88, 33);
                        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
                        gm_b16_8 t32 = t31;
                        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                        gm_tftb1odc739t5 t34 = gm_f_push_3ef746b(t25, t33);
                        gm_tftb1odc739t5 t35 = t34;
                        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_srccfhjr4r9mdef, 88, 24);
                        gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 88, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t38 = gm_l5;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l5 = t41;
                }
            }
gm_ll16:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
            }
            goto gm_lad;
lb12:;
        }
    }
    {
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_th2lqbo77pvsc t43;
        GM_ST(uint8_t, (uint8_t *)&t43 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t43 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t43 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t43), t42);
        ((uint8_t *)&t43)[16] = 1;
        uint8_t t44 = 0;
        F.ret = t43;
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
__attribute__((aligned(4))) void gm_f_split_shared_a91530r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_split_shared_a91530r F;
    void * gm_l0; /* parts */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* all */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* item */
    gm_t6j6smj7qoctu gm_l8; /* d */
    gm_b16_8 gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* <tmp> */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l17; /* item */
    gm_t6j6smj7qoctu gm_l18; /* f */
    gm_b16_8 gm_l19; /* shared */
    uint64_t gm_l20; /* total */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_b16_8 gm_l23; /* item */
    uint64_t gm_l24; /* share */
    uint64_t gm_l27; /* size */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_b16_8 gm_l30; /* item */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    GM_STACK_CHECK(&F, 2463u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 64));
        gm_th2lqbo77pvsc t4;
        gm_f_items_of_q103oiv((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t5 = t4;
        gm_b16_8 t6;
        if (((((uint8_t *)&t5))[16] != 0)) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_ttu09lij36f8c t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        gm_f_hash_map_m04clhi((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            gm_b16_8 t12 = gm_l2;
            gm_l5 = t12;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t14 = gm_l6;
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l6;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t20 * 16));
                    gm_l7 = t24;
                    {
                        gm_b16_8 t27 = gm_l7;
                        gm_ttu09lij36f8c t28;
                        gm_f_inline_definition_6aokb9d((void *)&t28, (void *)NULL, cx, NULL, t27);
                        if (gm_unw.kind) {
                            goto gm_ll17;
                        }
                        gm_ttu09lij36f8c t29 = t28;
                        if (!((((uint8_t *)&t29))[32] != 0)) goto ie26;
                        gm_t6j6smj7qoctu t30 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t29));
                        gm_l8 = t30;
                        {
                            void * t32 = (void *)((uint8_t *)&F.l4);
                            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                            gm_tftb1odc739t5 t34 = gm_f_extend_0cs85gs(t32, t33);
                            gm_tftb1odc739t5 t35 = t34;
                            if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_srccfhjr4r9mdef, 107, 13);
                            gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
                        }
                        {
                            void * t37 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 16));
                            gm_b0_1 t39;
                            gm_tftb1odc739t5 t40 = gm_f_put_drsjd1t(t37, t38, t39);
                            gm_tftb1odc739t5 t41 = t40;
                            if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srccfhjr4r9mdef, 108, 13);
                            gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                        }
                        goto ix26;
ie26:;
ix26:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 105, 5);
me22:;
                }
lc13:;
                {
                    uint64_t t44 = gm_l6;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l6 = t47;
                }
            }
gm_ll17:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb13; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc13; }
            }
            goto gm_ld1_1;
lb13:;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            void * t49 = gm_l0;
            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)t49) + 16));
            gm_l12 = t50;
        }
        gm_hval t51 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn52 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hls2l2mrnqun, .env = t51.env, .hctx = t51.cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
        gm_b16_8 t55 = gm_l12;
        gm_b0_1 t56 = gm_f_lines_9uns0k2_svrdqg8((void *)NULL, (&wn52), NULL, t55);
        if (gm_unw.kind) {
            goto gm_ll54;
        }
        F.w1 = t56;
        goto gm_ls57;
gm_ll54:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we53; }
        goto gm_ld1_1;
gm_ls57:;
we53:;
        gm_b0_1 t58 = F.w1;
        gm_b0_1 t59 = t58;
    }
    {
        {
            void * t61 = gm_l0;
            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)t61) + 32));
            gm_l13 = t62;
        }
        gm_hval t63 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn64 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlbopa954cfe, .env = t63.env, .hctx = t63.cx, .with_frame = &F.h, .with_label = 2, .result = (void *)&F.w2, .fw = NULL, .next = cx};
        gm_b16_8 t67 = gm_l13;
        gm_b0_1 t68 = gm_f_lines_9uns0k2_s6rkd4t((void *)NULL, (&wn64), NULL, t67);
        if (gm_unw.kind) {
            goto gm_ll66;
        }
        F.w2 = t68;
        goto gm_ls69;
gm_ll66:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 2) { gm_unw.kind = GM_NONE; goto we65; }
        goto gm_ld1_1;
gm_ls69:;
we65:;
        gm_b0_1 t70 = F.w2;
        gm_b0_1 t71 = t70;
    }
    {
        gm_f_list_epvducg((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            gm_b16_8 t73 = gm_l2;
            gm_l15 = t73;
        }
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t75 = gm_l16;
                uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint8_t t77 = (t75 < t76);
                if (!t77) goto lb74;
                {
                    uint64_t t80 = gm_l16;
                    uint64_t t81 = (uintptr_t)t80;
                    uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t81 * 16));
                    gm_l17 = t85;
                    {
                        gm_b16_8 t88 = gm_l17;
                        gm_ttu09lij36f8c t89;
                        gm_f_inline_definition_6aokb9d((void *)&t89, (void *)NULL, cx, NULL, t88);
                        if (gm_unw.kind) {
                            goto gm_ll78;
                        }
                        gm_ttu09lij36f8c t90 = t89;
                        uint8_t t91;
                        if (((((uint8_t *)&t90))[32] != 0)) goto mn93;
                        t91 = ((uint8_t)0);
                        goto me92;
mn93:;
                        t91 = ((uint8_t)1);
                        goto me92;
mn94:;
                        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 141, 12);
me92:;
                        if (!t91) goto ie87;
                        {
                            goto lc74;
                        }
                        goto ix87;
ie87:;
ix87:;
                    }
                    {
                        gm_b16_8 t96 = gm_l17;
                        gm_ttu09lij36f8c t97;
                        gm_f_function_o2lv8g6((void *)&t97, (void *)NULL, cx, NULL, t96);
                        if (gm_unw.kind) {
                            goto gm_ll78;
                        }
                        gm_ttu09lij36f8c t98 = t97;
                        gm_t6j6smj7qoctu t99;
                        if (((((uint8_t *)&t98))[32] != 0)) {
                            gm_t6j6smj7qoctu t100 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t98) + 0));
                            t99 = t100;
                        } else {
                            gm_ttu09lij36f8c t101;
                            memset(&t101, 0, sizeof t101);
                            uint8_t t102 = 0;
                            F.ret = t101;
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t102;
                            goto gm_ll78;
                        }
                        gm_l18 = t99;
                    }
                    {
                        void * t103 = (void *)((uint8_t *)&F.l9);
                        gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                        gm_tftb1odc739t5 t105 = gm_f_extend_0cs85gs(t103, t104);
                        gm_tftb1odc739t5 t106 = t105;
                        if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 1, gm_srccfhjr4r9mdef, 143, 9);
                        gm_b0_1 t107 = GM_LD(gm_b0_1, (((uint8_t *)&t106) + 1));
                    }
                    {
                        void * t108 = (void *)((uint8_t *)&F.l9);
                        gm_tftb1odc739t5 t109 = gm_f_push_t83rg2m(t108, ((uint8_t)10ULL));
                        gm_tftb1odc739t5 t110 = t109;
                        if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 1, gm_srccfhjr4r9mdef, 144, 9);
                        gm_b0_1 t111 = GM_LD(gm_b0_1, (((uint8_t *)&t110) + 1));
                    }
                    {
                        void * t112 = (void *)((uint8_t *)&F.l14);
                        gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 16));
                        gm_tftb1odc739t5 t114 = gm_f_push_3ef746b(t112, t113);
                        gm_tftb1odc739t5 t115 = t114;
                        if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 1, gm_srccfhjr4r9mdef, 145, 9);
                        gm_b0_1 t116 = GM_LD(gm_b0_1, (((uint8_t *)&t115) + 1));
                    }
                    goto me83;
mn84:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 140, 5);
me83:;
                }
lc74:;
                {
                    uint64_t t118 = gm_l16;
                    uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                    uint64_t t121 = r119;
                    gm_l16 = t121;
                }
            }
gm_ll78:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb74; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc74; }
            }
            goto gm_ld1_1;
lb74:;
        }
    }
    {
        gm_tfeab279c16rr t122;
        void * t123 = gm_l0;
        gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)t123) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t122 + 0, t124);
        gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t122 + 16, t125);
        gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t122 + 32, t126);
        gm_tabb710d9mq2e t127;
        gm_f_format_c2lphrf((void *)&t127, (void *)NULL, cx, NULL, &t122);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_tabb710d9mq2e t128 = t127;
        if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 8, gm_srccfhjr4r9mdef, 147, 15);
        gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&t128) + 8));
        gm_l19 = t129;
    }
    {
        gm_l20 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
            gm_l21 = t131;
        }
        {
            gm_l22 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t133 = gm_l22;
                uint64_t t134 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                uint8_t t135 = (t133 < t134);
                if (!t135) goto lb132;
                {
                    uint64_t t138 = gm_l22;
                    uint64_t t139 = (uintptr_t)t138;
                    uint64_t t140 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t139 * 16));
                    gm_l23 = t143;
                    {
                        uint64_t t145 = gm_l20;
                        uint64_t t146 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                        uint64_t r147; bool o148 = __builtin_add_overflow(t145, t146, &r147);
                        if (o148) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 149, 31);
                        uint64_t t149 = r147;
                        gm_l20 = t149;
                    }
                    goto me141;
mn142:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 149, 5);
me141:;
                }
lc132:;
                {
                    uint64_t t151 = gm_l22;
                    uint64_t r152; bool o153 = __builtin_add_overflow(t151, ((uint64_t)1ULL), &r152);
                    uint64_t t154 = r152;
                    gm_l22 = t154;
                }
            }
lb132:;
        }
    }
    {
        uint64_t t155 = gm_l20;
        uint64_t t156 = gm_l1;
        uint64_t t157 = (t156 > ((uint64_t)1ULL) ? t156 : ((uint64_t)1ULL));
        uint64_t r158; bool o159 = __builtin_add_overflow(t155, t157, &r158);
        if (o159) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 150, 16);
        uint64_t t160 = r158;
        uint64_t r161; bool o162 = __builtin_sub_overflow(t160, ((uint64_t)1ULL), &r161);
        if (o162) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 150, 16);
        uint64_t t163 = r161;
        uint64_t t164 = gm_l1;
        uint64_t t165 = (t164 > ((uint64_t)1ULL) ? t164 : ((uint64_t)1ULL));
        if (t165 == 0) gm_trap("division by zero", gm_srccfhjr4r9mdef, 150, 15);
        uint64_t t166 = (uint64_t)(t163 / t165);
        uint64_t t167 = (t166 > ((uint64_t)1ULL) ? t166 : ((uint64_t)1ULL));
        gm_l24 = t167;
    }
    {
        gm_f_list_lj5vg1g((void *)&F.l25, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t168 = (void *)((uint8_t *)&F.l26);
        gm_tfeab279c16rr t169;
        void * t170 = gm_l0;
        gm_b16_8 t171 = GM_LD(gm_b16_8, (((uint8_t *)t170) + 48));
        GM_ST(gm_b16_8, (uint8_t *)&t169 + 0, t171);
        gm_b16_8 t172 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t169 + 16, t172);
        gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t169 + 32, t173);
        gm_tftb1odc739t5 t174 = gm_f_write_qm3m7a0(t168, &t169);
        gm_tftb1odc739t5 t175 = t174;
        if (((((uint8_t *)&t175))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t175) + 1, gm_srccfhjr4r9mdef, 153, 5);
        gm_b0_1 t176 = GM_LD(gm_b0_1, (((uint8_t *)&t175) + 1));
    }
    {
        void * t177 = (void *)((uint8_t *)&F.l25);
        gm_tftb1odc739t5 t178 = gm_f_push_97b8r3k(t177, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l26)));
        gm_tftb1odc739t5 t179 = t178;
        if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 1, gm_srccfhjr4r9mdef, 154, 5);
        gm_b0_1 t180 = GM_LD(gm_b0_1, (((uint8_t *)&t179) + 1));
    }
    {
        gm_l27 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
            gm_l28 = t182;
        }
        {
            gm_l29 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t184 = gm_l29;
                uint64_t t185 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                uint8_t t186 = (t184 < t185);
                if (!t186) goto lb183;
                {
                    uint64_t t189 = gm_l29;
                    uint64_t t190 = (uintptr_t)t189;
                    uint64_t t191 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                    gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t190 * 16));
                    gm_l30 = t194;
                    {
                        uint64_t t197 = gm_l27;
                        uint64_t t198 = gm_l24;
                        uint8_t t199 = (t197 >= t198);
                        if (!t199) goto ie196;
                        uint64_t t200 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
                        uint64_t t201 = gm_l1;
                        uint8_t t202 = (t200 < t201);
                        if (!t202) goto ie196;
                        {
                            void * t204 = (void *)((uint8_t *)&F.l25);
                            gm_trqfvmjtt4sij t205;
                            gm_f_list_uvfu1k1((void *)&t205, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll187;
                            }
                            gm_tftb1odc739t5 t206 = gm_f_push_97b8r3k(t204, &t205);
                            gm_tftb1odc739t5 t207 = t206;
                            if (((((uint8_t *)&t207))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t207) + 1, gm_srccfhjr4r9mdef, 158, 13);
                            gm_b0_1 t208 = GM_LD(gm_b0_1, (((uint8_t *)&t207) + 1));
                        }
                        {
                            gm_l27 = ((uint64_t)0ULL);
                        }
                        goto ix196;
ie196:;
ix196:;
                    }
                    {
                        uint64_t t209 = gm_l27;
                        uint64_t t210 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                        uint64_t r211; bool o212 = __builtin_add_overflow(t209, t210, &r211);
                        if (o212) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 161, 9);
                        uint64_t t213 = r211;
                        gm_l27 = t213;
                    }
                    {
                        uint64_t t214 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
                        uint64_t r215; bool o216 = __builtin_sub_overflow(t214, ((uint64_t)1ULL), &r215);
                        if (o216) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 162, 21);
                        uint64_t t217 = r215;
                        uint64_t t218 = (uintptr_t)t217;
                        uint64_t t219 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
                        if (t218 >= (uintptr_t)(t219)) gm_trap_index(t218, t219, gm_srccfhjr4r9mdef, 162, 9);
                        void * t220 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l25) + 0)) + t218 * 40);
                        gm_b16_8 t221 = gm_l30;
                        gm_tftb1odc739t5 t222 = gm_f_extend_0cs85gs(t220, t221);
                        gm_tftb1odc739t5 t223 = t222;
                        if (((((uint8_t *)&t223))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t223) + 1, gm_srccfhjr4r9mdef, 162, 9);
                        gm_b0_1 t224 = GM_LD(gm_b0_1, (((uint8_t *)&t223) + 1));
                    }
                    goto me192;
mn193:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 156, 5);
me192:;
                }
lc183:;
                {
                    uint64_t t226 = gm_l29;
                    uint64_t r227; bool o228 = __builtin_add_overflow(t226, ((uint64_t)1ULL), &r227);
                    uint64_t t229 = r227;
                    gm_l29 = t229;
                }
            }
gm_ll187:;
            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb183; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc183; }
            }
            goto gm_ld1_1;
lb183:;
        }
    }
    {
        uint64_t t230 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t231 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
        if (t230 >= (uintptr_t)(t231)) gm_trap_index(t230, t231, gm_srccfhjr4r9mdef, 164, 5);
        void * t232 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l25) + 0)) + t230 * 40);
        void * t233 = gm_l0;
        gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)t233) + 80));
        gm_tftb1odc739t5 t235 = gm_f_extend_0cs85gs(t232, t234);
        gm_tftb1odc739t5 t236 = t235;
        if (((((uint8_t *)&t236))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t236) + 1, gm_srccfhjr4r9mdef, 164, 5);
        gm_b0_1 t237 = GM_LD(gm_b0_1, (((uint8_t *)&t236) + 1));
    }
    {
        gm_f_list_epvducg((void *)&F.l31, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
            gm_l32 = t239;
        }
        {
            gm_l33 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t241 = gm_l33;
                uint64_t t242 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                uint8_t t243 = (t241 < t242);
                if (!t243) goto lb240;
                {
                    uint64_t t246 = gm_l33;
                    uint64_t t247 = (uintptr_t)t246;
                    uint64_t t248 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                    gm_trqfvmjtt4sij t251 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t247 * 40));
                    F.l34 = t251;
                    {
                        void * t253 = (void *)((uint8_t *)&F.l31);
                        gm_b16_8 t254 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
                        gm_b16_8 t255 = t254;
                        gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&t255));
                        gm_tftb1odc739t5 t257 = gm_f_push_3ef746b(t253, t256);
                        gm_tftb1odc739t5 t258 = t257;
                        if (((((uint8_t *)&t258))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t258) + 1, gm_srccfhjr4r9mdef, 166, 28);
                        gm_b0_1 t259 = GM_LD(gm_b0_1, (((uint8_t *)&t258) + 1));
                    }
                    goto me249;
mn250:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 166, 5);
me249:;
                }
lc240:;
                {
                    uint64_t t261 = gm_l33;
                    uint64_t r262; bool o263 = __builtin_add_overflow(t261, ((uint64_t)1ULL), &r262);
                    uint64_t t264 = r262;
                    gm_l33 = t264;
                }
            }
lb240:;
        }
    }
    {
        gm_t6j6smj7qoctu t265;
        gm_b16_8 t266 = gm_l19;
        gm_b16_8 t267 = t266;
        gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&t267));
        GM_ST(gm_b16_8, (uint8_t *)&t265 + 0, t268);
        gm_b16_8 t269 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t265 + 16, t269);
        gm_ttu09lij36f8c t270;
        GM_ST(uint8_t, (uint8_t *)&t270 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t270 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t270 + 36, 0);
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t270), t265);
        ((uint8_t *)&t270)[32] = 1;
        uint8_t t271 = 0;
        F.ret = t270;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t271;
        goto gm_ld1_1;
    }
    {
        void * t272 = (void *)((uint8_t *)&F.l3);
        gm_f_deinit_lehld1n(t272);
    }
    goto gm_ls273;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t274 = (void *)((uint8_t *)&F.l3);
        gm_f_deinit_lehld1n(t274);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls273:;
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
inline __attribute__((aligned(4))) uint64_t gm_f_unit_count_dk0d2e4(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unit_count_dk0d2e4 F;
    void * gm_l0; /* parts */
    uint64_t gm_l1; /* cpus */
    gm_tf5srh4mp4b3i gm_l2; /* forced */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        if (!((((uint8_t *)&gm_l2))[8] != 0)) goto ie2;
        uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        gm_l3 = t3;
        {
            uint64_t t5 = gm_l3;
            uint64_t t6 = (t5 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t5 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t5));
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 64) + 8);
        if (((uint64_t)98304ULL) == 0) gm_trap("division by zero", gm_srccfhjr4r9mdef, 80, 13);
        uint64_t t10 = (uint64_t)(t9 / ((uint64_t)98304ULL));
        uint64_t t11 = gm_l1;
        uint64_t t12 = (t11 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t11 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t11));
        uint64_t t13 = (t10 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t10 > t12 ? t12 : t10));
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_unit_text_mmejmp8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_unit_text_mmejmp8 F;
    void * gm_l1; /* by_name */
    gm_b16_8 gm_l2; /* nameless */
    gm_b16_8 gm_l3; /* body */
    gm_b16_8 gm_l4; /* members */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l10; /* w */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* k */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l17; /* r */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* k */
    gm_b16_8 gm_l21; /* refs */
    uint8_t gm_l22; /* all */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_b16_8 gm_l25; /* r */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* k */
    uint8_t gm_l30; /* t */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* k */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_b16_8 gm_l40; /* m */
    GM_STACK_CHECK(&F, 2546u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_f_list_s83jn2q((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = F.l0;
            gm_l6 = t3;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l7;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l7;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    {
                        void * t16 = (void *)((uint8_t *)&F.l5);
                        gm_tftb1odc739t5 t17 = gm_f_push_vd1290p(t16, ((uint8_t)0));
                        gm_tftb1odc739t5 t18 = t17;
                        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_srccfhjr4r9mdef, 632, 22);
                        gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 632, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t21 = gm_l7;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l7 = t24;
                }
            }
lb4:;
        }
    }
    {
        gm_f_hash_map_m04clhi((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        gm_f_list_epvducg((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t27 = gm_l3;
        gm_b16_8 t28 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_tftb1odc739t5 t29 = gm_f_extend_vogsd72(t26, t28);
        gm_tftb1odc739t5 t30 = t29;
        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srccfhjr4r9mdef, 636, 5);
        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
    }
    {
        for (;;) {
            void * t33 = (void *)((uint8_t *)&F.l9);
            gm_th2lqbo77pvsc t34;
            gm_f_pop_vr08sii((void *)&t34, t33);
            gm_th2lqbo77pvsc t35 = t34;
            if (!((((uint8_t *)&t35))[16] != 0)) goto lb32;
            gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
            gm_l10 = t36;
            {
                void * t40 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t41 = gm_l10;
                gm_ttsm01t0d8srg t42 = gm_f_get_ece4opn(t40, t41);
                gm_ttsm01t0d8srg t43 = t42;
                uint8_t t44;
                if (((((uint8_t *)&t43))[0] != 0)) goto mn46;
                t44 = ((uint8_t)0);
                goto me45;
mn46:;
                t44 = ((uint8_t)1);
                goto me45;
mn47:;
                gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 638, 12);
me45:;
                if (!t44) goto ie39;
                {
                    goto lc32;
                }
                goto ix39;
ie39:;
ix39:;
            }
            {
                void * t49 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t50 = gm_l10;
                gm_b0_1 t51;
                gm_tftb1odc739t5 t52 = gm_f_put_drsjd1t(t49, t50, t51);
                gm_tftb1odc739t5 t53 = t52;
                if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_srccfhjr4r9mdef, 639, 9);
                gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
            }
            {
                void * t55 = gm_l1;
                gm_b16_8 t56 = gm_l10;
                gm_tqetrgo0q24b5 t57;
                gm_f_get_ohh8mo3((void *)&t57, t55, t56);
                gm_tqetrgo0q24b5 t58 = t57;
                gm_trqfvmjtt4sij t59;
                if (((((uint8_t *)&t58))[40] != 0)) {
                    gm_trqfvmjtt4sij t60 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t58) + 0));
                    t59 = t60;
                } else {
                    goto lc32;
                }
                F.l11 = t59;
            }
            {
                {
                    gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                    gm_l12 = t62;
                }
                {
                    gm_l13 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t64 = gm_l13;
                        uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        uint8_t t66 = (t64 < t65);
                        if (!t66) goto lb63;
                        {
                            uint64_t t69 = gm_l13;
                            uint64_t t70 = (uintptr_t)t69;
                            uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                            uint64_t t72 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t70 * 8));
                            uint64_t t73 = t72;
                            uint64_t t76 = GM_LD(uint64_t, ((uint8_t *)&t73));
                            gm_l14 = t76;
                            {
                                uint64_t t79 = gm_l14;
                                uint64_t t80 = (uintptr_t)t79;
                                uint64_t t81 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                if (t80 >= (uintptr_t)(t81)) gm_trap_index(t80, t81, gm_srccfhjr4r9mdef, 642, 16);
                                uint8_t t82 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t80 * 1));
                                if (!t82) goto ie78;
                                {
                                    goto lc63;
                                }
                                goto ix78;
ie78:;
ix78:;
                            }
                            {
                                uint64_t t84 = gm_l14;
                                uint64_t t85 = (uintptr_t)t84;
                                uint64_t t86 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                if (t85 >= (uintptr_t)(t86)) gm_trap_index(t85, t86, gm_srccfhjr4r9mdef, 643, 13);
                                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t85 * 1), ((uint8_t)1));
                            }
                            {
                                {
                                    uint64_t t88 = gm_l14;
                                    uint64_t t89 = (uintptr_t)t88;
                                    uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
                                    if (t89 >= (uintptr_t)(t90)) gm_trap_index(t89, t90, gm_srccfhjr4r9mdef, 644, 22);
                                    gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l0)) + t89 * 56) + 32));
                                    gm_l15 = t91;
                                }
                                {
                                    gm_l16 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t93 = gm_l16;
                                        uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                        uint8_t t95 = (t93 < t94);
                                        if (!t95) goto lb92;
                                        {
                                            uint64_t t98 = gm_l16;
                                            uint64_t t99 = (uintptr_t)t98;
                                            uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                            gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t99 * 16));
                                            gm_l17 = t103;
                                            {
                                                void * t106 = (void *)((uint8_t *)&F.l8);
                                                gm_b16_8 t107 = gm_l17;
                                                gm_ttsm01t0d8srg t108 = gm_f_get_ece4opn(t106, t107);
                                                gm_ttsm01t0d8srg t109 = t108;
                                                uint8_t t110;
                                                if (((((uint8_t *)&t109))[0] != 0)) goto mn112;
                                                t110 = ((uint8_t)1);
                                                goto me111;
mn112:;
                                                t110 = ((uint8_t)0);
                                                goto me111;
mn113:;
                                                gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 644, 41);
me111:;
                                                if (!t110) goto ie105;
                                                {
                                                    void * t115 = (void *)((uint8_t *)&F.l9);
                                                    gm_b16_8 t116 = gm_l17;
                                                    gm_tftb1odc739t5 t117 = gm_f_push_3ef746b(t115, t116);
                                                    gm_tftb1odc739t5 t118 = t117;
                                                    if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 1, gm_srccfhjr4r9mdef, 644, 66);
                                                    gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 1));
                                                }
                                                goto ix105;
ie105:;
ix105:;
                                            }
                                            goto me101;
mn102:;
                                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 644, 13);
me101:;
                                        }
lc92:;
                                        {
                                            uint64_t t121 = gm_l16;
                                            uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                                            uint64_t t124 = r122;
                                            gm_l16 = t124;
                                        }
                                    }
lb92:;
                                }
                            }
                            goto me74;
mn75:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 641, 9);
me74:;
                        }
lc63:;
                        {
                            uint64_t t126 = gm_l13;
                            uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                            uint64_t t129 = r127;
                            gm_l13 = t129;
                        }
                    }
lb63:;
                }
            }
lc32:;
        }
lb32:;
    }
    {
        {
            gm_b16_8 t131 = gm_l2;
            gm_l18 = t131;
        }
        {
            gm_l19 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t133 = gm_l19;
                uint64_t t134 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t135 = (t133 < t134);
                if (!t135) goto lb132;
                {
                    uint64_t t138 = gm_l19;
                    uint64_t t139 = (uintptr_t)t138;
                    uint64_t t140 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    uint64_t t141 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t139 * 8));
                    uint64_t t142 = t141;
                    uint64_t t145 = GM_LD(uint64_t, ((uint8_t *)&t142));
                    gm_l20 = t145;
                    {
                        uint64_t t147 = gm_l20;
                        uint64_t t148 = (uintptr_t)t147;
                        uint64_t t149 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
                        if (t148 >= (uintptr_t)(t149)) gm_trap_index(t148, t149, gm_srccfhjr4r9mdef, 649, 17);
                        gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l0)) + t148 * 56) + 32));
                        gm_l21 = t150;
                    }
                    {
                        uint64_t t152 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                        uint8_t t153 = ((t152 == ((uint64_t)0ULL)));
                        if (!t153) goto ie151;
                        {
                            goto lc132;
                        }
                        goto ix151;
ie151:;
ix151:;
                    }
                    {
                        gm_l22 = ((uint8_t)1);
                    }
                    {
                        {
                            gm_b16_8 t156 = gm_l21;
                            gm_l23 = t156;
                        }
                        {
                            gm_l24 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t158 = gm_l24;
                                uint64_t t159 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                uint8_t t160 = (t158 < t159);
                                if (!t160) goto lb157;
                                {
                                    uint64_t t163 = gm_l24;
                                    uint64_t t164 = (uintptr_t)t163;
                                    uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                    gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t164 * 16));
                                    gm_l25 = t168;
                                    {
                                        void * t171 = gm_l1;
                                        gm_b16_8 t172 = gm_l25;
                                        gm_tqetrgo0q24b5 t173;
                                        gm_f_get_ohh8mo3((void *)&t173, t171, t172);
                                        gm_tqetrgo0q24b5 t174 = t173;
                                        uint8_t t175;
                                        if (((((uint8_t *)&t174))[40] != 0)) goto mn177;
                                        t175 = ((uint8_t)0);
                                        goto me176;
mn177:;
                                        t175 = ((uint8_t)1);
                                        goto me176;
mn178:;
                                        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 653, 16);
me176:;
                                        if (!t175) goto ie170;
                                        void * t179 = (void *)((uint8_t *)&F.l8);
                                        gm_b16_8 t180 = gm_l25;
                                        gm_ttsm01t0d8srg t181 = gm_f_get_ece4opn(t179, t180);
                                        gm_ttsm01t0d8srg t182 = t181;
                                        uint8_t t183;
                                        if (((((uint8_t *)&t182))[0] != 0)) goto mn185;
                                        t183 = ((uint8_t)1);
                                        goto me184;
mn185:;
                                        t183 = ((uint8_t)0);
                                        goto me184;
mn186:;
                                        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 653, 47);
me184:;
                                        if (!t183) goto ie170;
                                        {
                                            gm_l22 = ((uint8_t)0);
                                        }
                                        goto ix170;
ie170:;
ix170:;
                                    }
                                    goto me166;
mn167:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 652, 9);
me166:;
                                }
lc157:;
                                {
                                    uint64_t t189 = gm_l24;
                                    uint64_t r190; bool o191 = __builtin_add_overflow(t189, ((uint64_t)1ULL), &r190);
                                    uint64_t t192 = r190;
                                    gm_l24 = t192;
                                }
                            }
lb157:;
                        }
                    }
                    {
                        uint8_t t194 = gm_l22;
                        if (!t194) goto ie193;
                        {
                            uint64_t t196 = gm_l20;
                            uint64_t t197 = (uintptr_t)t196;
                            uint64_t t198 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                            if (t197 >= (uintptr_t)(t198)) gm_trap_index(t197, t198, gm_srccfhjr4r9mdef, 655, 18);
                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t197 * 1), ((uint8_t)1));
                        }
                        goto ix193;
ie193:;
ix193:;
                    }
                    goto me143;
mn144:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 648, 5);
me143:;
                }
lc132:;
                {
                    uint64_t t200 = gm_l19;
                    uint64_t r201; bool o202 = __builtin_add_overflow(t200, ((uint64_t)1ULL), &r201);
                    uint64_t t203 = r201;
                    gm_l19 = t203;
                }
            }
lb132:;
        }
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t206 = t205;
            gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&t206));
            gm_l27 = t207;
        }
        {
            gm_l28 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t209 = gm_l28;
                uint64_t t210 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                uint8_t t211 = (t209 < t210);
                if (!t211) goto lb208;
                {
                    uint64_t t214 = gm_l28;
                    uint64_t t215 = gm_l28;
                    uint64_t t216 = (uintptr_t)t215;
                    uint64_t t217 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                    uint8_t t218 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t216 * 1));
                    gm_tf5srh4mp4b3i t219;
                    GM_ST(uint8_t, (uint8_t *)&t219 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t219 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t219 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t219 + 0), t214);
                    GM_ST(uint8_t, ((uint8_t *)&t219 + 8), t218);
                    gm_tf5srh4mp4b3i t220 = t219;
                    uint64_t t223 = GM_LD(uint64_t, (((uint8_t *)&t220) + 0));
                    gm_l29 = t223;
                    uint8_t t224 = GM_LD(uint8_t, (((uint8_t *)&t220) + 8));
                    gm_l30 = t224;
                    {
                        uint8_t t227 = gm_l30;
                        if (!t227) goto ie226;
                        {
                            void * t229 = (void *)((uint8_t *)&F.l26);
                            uint64_t t230 = gm_l29;
                            gm_tftb1odc739t5 t231 = gm_f_push_ucgmlo2(t229, t230);
                            gm_tftb1odc739t5 t232 = t231;
                            if (((((uint8_t *)&t232))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t232) + 1, gm_srccfhjr4r9mdef, 658, 49);
                            gm_b0_1 t233 = GM_LD(gm_b0_1, (((uint8_t *)&t232) + 1));
                        }
                        goto ix226;
ie226:;
ix226:;
                    }
                    goto me221;
mn222:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 658, 5);
me221:;
                }
lc208:;
                {
                    uint64_t t235 = gm_l28;
                    uint64_t r236; bool o237 = __builtin_add_overflow(t235, ((uint64_t)1ULL), &r236);
                    uint64_t t238 = r236;
                    gm_l28 = t238;
                }
            }
lb208:;
        }
    }
    {
        gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
        gm_closure t240 = ((gm_closure){(void *)gm_f_fn_575gb0c, (void *)&F.h, cx});
        gm_f_sort_by_08k4kf2(t239, t240);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l31, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_f_hash_map_m04clhi((void *)&F.l32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
    }
    {
        {
            gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
            gm_l33 = t243;
        }
        {
            gm_l34 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t245 = gm_l34;
                uint64_t t246 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                uint8_t t247 = (t245 < t246);
                if (!t247) goto lb244;
                {
                    uint64_t t250 = gm_l34;
                    uint64_t t251 = (uintptr_t)t250;
                    uint64_t t252 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                    uint64_t t253 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t251 * 8));
                    uint64_t t254 = t253;
                    uint64_t t257 = GM_LD(uint64_t, ((uint8_t *)&t254));
                    gm_l35 = t257;
                    {
                        void * t260 = (void *)((uint8_t *)&F.l32);
                        uint64_t t261 = gm_l35;
                        uint64_t t262 = (uintptr_t)t261;
                        uint64_t t263 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
                        if (t262 >= (uintptr_t)(t263)) gm_trap_index(t262, t263, gm_srccfhjr4r9mdef, 665, 21);
                        gm_b16_8 t264 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l0)) + t262 * 56) + 0));
                        gm_ttsm01t0d8srg t265 = gm_f_get_ece4opn(t260, t264);
                        gm_ttsm01t0d8srg t266 = t265;
                        uint8_t t267;
                        if (((((uint8_t *)&t266))[0] != 0)) goto mn269;
                        t267 = ((uint8_t)0);
                        goto me268;
mn269:;
                        t267 = ((uint8_t)1);
                        goto me268;
mn270:;
                        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 665, 12);
me268:;
                        if (!t267) goto ie259;
                        {
                            goto lc244;
                        }
                        goto ix259;
ie259:;
ix259:;
                    }
                    {
                        void * t272 = (void *)((uint8_t *)&F.l32);
                        uint64_t t273 = gm_l35;
                        uint64_t t274 = (uintptr_t)t273;
                        uint64_t t275 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
                        if (t274 >= (uintptr_t)(t275)) gm_trap_index(t274, t275, gm_srccfhjr4r9mdef, 666, 18);
                        gm_b16_8 t276 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l0)) + t274 * 56) + 0));
                        gm_b0_1 t277;
                        gm_tftb1odc739t5 t278 = gm_f_put_drsjd1t(t272, t276, t277);
                        gm_tftb1odc739t5 t279 = t278;
                        if (((((uint8_t *)&t279))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t279) + 1, gm_srccfhjr4r9mdef, 666, 9);
                        gm_b0_1 t280 = GM_LD(gm_b0_1, (((uint8_t *)&t279) + 1));
                    }
                    {
                        void * t281 = (void *)((uint8_t *)&F.l31);
                        uint64_t t282 = gm_l35;
                        uint64_t t283 = (uintptr_t)t282;
                        uint64_t t284 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
                        if (t283 >= (uintptr_t)(t284)) gm_trap_index(t283, t284, gm_srccfhjr4r9mdef, 667, 21);
                        gm_b16_8 t285 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l0)) + t283 * 56) + 0));
                        gm_tftb1odc739t5 t286 = gm_f_extend_0cs85gs(t281, t285);
                        gm_tftb1odc739t5 t287 = t286;
                        if (((((uint8_t *)&t287))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t287) + 1, gm_srccfhjr4r9mdef, 667, 9);
                        gm_b0_1 t288 = GM_LD(gm_b0_1, (((uint8_t *)&t287) + 1));
                    }
                    goto me255;
mn256:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 663, 5);
me255:;
                }
lc244:;
                {
                    uint64_t t290 = gm_l34;
                    uint64_t r291; bool o292 = __builtin_add_overflow(t290, ((uint64_t)1ULL), &r291);
                    uint64_t t293 = r291;
                    gm_l34 = t293;
                }
            }
lb244:;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l36, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
        uint64_t t295 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t296 = (t295 > ((uint64_t)0ULL));
        if (!t296) goto ie294;
        {
            gm_b16_8 t298 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
            gm_b16_8 t299 = t298;
            gm_b16_8 t300 = GM_LD(gm_b16_8, ((uint8_t *)&t299));
            gm_f_ident_set_9ajalpn((void *)&F.l37, (void *)NULL, cx, NULL, t300);
            if (gm_unw.kind) {
                goto gm_ld1_2;
            }
        }
        {
            {
                gm_b16_8 t302 = gm_l4;
                gm_l38 = t302;
            }
            {
                gm_l39 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t304 = gm_l39;
                    uint64_t t305 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                    uint8_t t306 = (t304 < t305);
                    if (!t306) goto lb303;
                    {
                        uint64_t t309 = gm_l39;
                        uint64_t t310 = (uintptr_t)t309;
                        uint64_t t311 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                        gm_b16_8 t314 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t310 * 16));
                        gm_l40 = t314;
                        {
                            void * t317 = (void *)((uint8_t *)&F.l37);
                            gm_b16_8 t318 = gm_l40;
                            gm_ttsm01t0d8srg t319 = gm_f_get_ece4opn(t317, t318);
                            gm_ttsm01t0d8srg t320 = t319;
                            uint8_t t321;
                            if (((((uint8_t *)&t320))[0] != 0)) goto mn323;
                            t321 = ((uint8_t)0);
                            goto me322;
mn323:;
                            t321 = ((uint8_t)1);
                            goto me322;
mn324:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 672, 31);
me322:;
                            if (!t321) goto ie316;
                            {
                                void * t326 = (void *)((uint8_t *)&F.l36);
                                gm_b16_8 t327 = gm_l40;
                                gm_tftb1odc739t5 t328 = gm_f_push_3ef746b(t326, t327);
                                gm_tftb1odc739t5 t329 = t328;
                                if (((((uint8_t *)&t329))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t329) + 1, gm_srccfhjr4r9mdef, 672, 59);
                                gm_b0_1 t330 = GM_LD(gm_b0_1, (((uint8_t *)&t329) + 1));
                            }
                            goto ix316;
ie316:;
ix316:;
                        }
                        goto me312;
mn313:;
                        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 672, 9);
me312:;
                    }
lc303:;
                    {
                        uint64_t t332 = gm_l39;
                        uint64_t r333; bool o334 = __builtin_add_overflow(t332, ((uint64_t)1ULL), &r333);
                        uint64_t t335 = r333;
                        gm_l39 = t335;
                    }
                }
lb303:;
            }
        }
        goto ix294;
ie294:;
ix294:;
    }
    {
        gm_t6j6smj7qoctu t336;
        gm_b16_8 t337 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 0));
        gm_b16_8 t338 = t337;
        gm_b16_8 t339 = GM_LD(gm_b16_8, ((uint8_t *)&t338));
        gm_b16_8 t340 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
        gm_b16_8 t341 = t340;
        gm_b16_8 t342 = GM_LD(gm_b16_8, ((uint8_t *)&t341));
        gm_b16_8 t343 = gm_f_guard_o7k9a0e((void *)NULL, cx, NULL, t339, t342);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t336 + 0, t343);
        gm_b16_8 t344 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t336 + 16, t344);
        gm_tabb710d9mq2e t345;
        gm_f_format_8n626pi((void *)&t345, (void *)NULL, cx, NULL, &t336);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
        gm_tabb710d9mq2e t346 = t345;
        if (((((uint8_t *)&t346))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t346) + 8, gm_srccfhjr4r9mdef, 674, 12);
        gm_b16_8 t347 = GM_LD(gm_b16_8, (((uint8_t *)&t346) + 8));
        gm_b16_8 t348 = t347;
        gm_b16_8 t349 = GM_LD(gm_b16_8, ((uint8_t *)&t348));
        uint8_t t350 = 0;
        F.ret = t349;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t350;
        goto gm_ld1_2;
    }
    {
        void * t351 = (void *)((uint8_t *)&F.l32);
        gm_f_deinit_lehld1n(t351);
    }
    {
        void * t352 = (void *)((uint8_t *)&F.l8);
        gm_f_deinit_lehld1n(t352);
    }
    goto gm_ls353;
gm_ld1_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t354 = (void *)((uint8_t *)&F.l32);
        gm_f_deinit_lehld1n(t354);
        gm_unw = gm_saved;
    }
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t355 = (void *)((uint8_t *)&F.l8);
        gm_f_deinit_lehld1n(t355);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls353:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
