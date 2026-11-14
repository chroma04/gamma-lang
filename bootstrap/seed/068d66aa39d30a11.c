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
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
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
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; } gm_tov4mtrfp5e7v;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint64_t v1; } f1; } gm_t833u93mr4iob;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, off: u64)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tov4mtrfp5e7v) == 12 && _Alignof(gm_tov4mtrfp5e7v) == 4 && offsetof(gm_tov4mtrfp5e7v, f0) == 0 && offsetof(gm_tov4mtrfp5e7v, f1) == 4 && offsetof(gm_tov4mtrfp5e7v, f2) == 8, "layout of `BitField`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t833u93mr4iob) == 32 && _Alignof(gm_t833u93mr4iob) == 8 && offsetof(gm_t833u93mr4iob, f0) == 0 && offsetof(gm_t833u93mr4iob, f1) == 8, "layout of `Flow!u64`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
struct gm_fr_adt_of_8k5vqud {
    gm_frame h;
    void * ret;
};
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bit_width_0fd5f52 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_bits_field_vqua3kd {
    gm_frame h;
    gm_tplpbqf3duvn3 ret;
};
struct gm_fr_bits_from_raw_el4tjdh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_bits_raw_t6dqs65 {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_bits_struct_ty_22ue20s {
    gm_frame h;
    gm_tpautr4br7h3l l3; /* def */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_84b0b46 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_field_bytes_bhivr4s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_field_layout_h34salb {
    gm_frame h;
    gm_tpautr4br7h3l l5; /* def */
    gm_tfbnciphf3261 ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_get_int_pg7p4gc {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_get_uint_bq8c4b7 {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_int_info_ulcevdv {
    gm_frame h;
    gm_tg5lql6itufk7 ret;
};
struct gm_fr_is_packed_8fhislp {
    gm_frame h;
    gm_tpautr4br7h3l l3; /* def */
    uint8_t ret;
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
struct gm_fr_le64_7isadtr {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_load_word_ud3iuha {
    gm_frame h;
    gm_tpekhfs32op0f l5; /* e */
    gm_t833u93mr4iob ret;
};
struct gm_fr_make_res_085nq6f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_make_some_tvb9ubk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_make_variant_bn3d74n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_mem_trap_ecogko0 {
    gm_frame h;
    gm_tpekhfs32op0f l2; /* e */
    gm_t76123gfet73g ret;
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
struct gm_fr_opt_payload_1pbnobb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_opt_present_lb4i2ip {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_payload_offset_la0c8ft {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_push_7o6oubd {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_put_le64_uc3t56q {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_ptr_188p3mm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_slice_ko14loi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_u64_irp77r2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_uint_cddin99 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_word_tfn443i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_read_field_ua0742c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_regrow_hvk9f35 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_res_is_err_8jbgndp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_res_payload_pb2hcmh {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_res_types_pg7rvck {
    gm_frame h;
    gm_tgglga8ro3isc ret;
};
struct gm_fr_size_hr4b76n {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_slice_parts_1se5hu4 {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_union_def_h8mvf4t {
    gm_frame h;
    gm_t57euh612ligr ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_variant_index_s2p6fs4 {
    gm_frame h;
    gm_tckdobunaulee l9; /* v */
    gm_t9805qtonbsag ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_word_o2rcrhs {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_wr_word_npte9fs {
    gm_frame h;
    gm_b0_1 ret;
};
__attribute__((aligned(4))) void * gm_f_adt_of_8k5vqud(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_0fd5f52(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) gm_tplpbqf3duvn3 gm_f_bits_field_vqua3kd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_bits_from_raw_el4tjdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, unsigned __int128 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits_raw_t6dqs65(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_field_bytes_bhivr4s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3);
__attribute__((aligned(4))) gm_tfbnciphf3261 gm_f_field_layout_h34salb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_packed_8fhislp(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t53at80pfdus3 gm_f_kind_htv6093_c(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_le64_7isadtr(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_load_word_ud3iuha(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, uint64_t a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_res_085nq6f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_variant_bn3d74n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_payload_1pbnobb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_opt_present_lb4i2ip(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7o6oubd(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_put_le64_uc3t56q(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) void gm_f_put_word_tfn443i(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_read_field_ua0742c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hvk9f35(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tgglga8ro3isc gm_f_res_types_pg7rvck(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t57euh612ligr gm_f_union_def_h8mvf4t_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_variant_index_s2p6fs4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) uint64_t gm_f_word_o2rcrhs(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2);
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcq7ohh8h77bn7[];
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_q6sor4h F;
    uint64_t gm_l0; /* addr */
    uint64_t gm_l1; /* align */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        uint8_t t4 = (t3 <= ((uint64_t)1ULL));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = gm_l1;
        uint64_t t9 = gm_l1;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 17);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) goto ie7;
        {
            uint64_t t16 = gm_l0;
            uint64_t t17 = gm_l1;
            uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
            if (o19) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 50);
            uint64_t t20 = r18;
            uint64_t t21 = (uint64_t)(t16 & t20);
            uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint64_t t24 = gm_l0;
        uint64_t t25 = gm_l1;
        if (t25 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 198, 12);
        uint64_t t26 = (uint64_t)(t24 % t25);
        uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
        uint8_t t28 = 0;
        F.ret = t27;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
    uint8_t t4 = (t3 <= ((uint64_t)8ULL));
    if (!t4) goto ie2;
    void * t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    uint64_t t8 = gm_f_rd_word_8flcq6o(t6, t7);
    uint64_t t9 = t8;
    t1 = t9;
    goto ix2;
ie2:;
    void * t11 = gm_l0;
    gm_b16_8 t12 = gm_l1;
    unsigned __int128 t13 = gm_f_get_uint_bq8c4b7(t11, t12);
    uint64_t t14 = (uint64_t)t13;
    uint64_t t15 = t14;
    t1 = t15;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_packed_8fhislp(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_packed_8fhislp F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    void * gm_l2; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 119u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        void * t4 = gm_f_adt_of_8k5vqud(t2, GM_CONV(gm_csguankko4lmhd, t3));
        void * t5;
        if ((GM_LD(void *, ((uint8_t *)&t4)) != 0)) {
            void * t6 = GM_LD(void *, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        void * t9 = gm_l2;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 24) + 0));
        if (t10 != ((uint8_t)0ULL)) goto ie8;
        gm_tpautr4br7h3l t11 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t9) + 24) + 8));
        F.l3 = t11;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 16));
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix8;
ie8:;
        {
            uint8_t t16 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix8:;
    }
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_le64_7isadtr(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_le64_7isadtr F;
    gm_b16_8 gm_l0; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
    if (t1 >= (uintptr_t)(t2)) gm_trap_index(t1, t2, gm_srcnqtpfma6qe4a, 77, 41);
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t1 * 1));
    uint64_t t4 = (uint64_t)t3;
    uint64_t t5 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
    if (t5 >= (uintptr_t)(t6)) gm_trap_index(t5, t6, gm_srcnqtpfma6qe4a, 77, 54);
    uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t5 * 1));
    uint64_t t8 = (uint64_t)t7;
    unsigned __int128 n9 = (unsigned __int128)((uint64_t)8ULL);
    if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcnqtpfma6qe4a, 77, 50);
    uint64_t r10 = (uint64_t)((uint64_t)t8 << (int)n9);
    bool o11 = (uint64_t)(r10 >> (int)n9) != t8;
    if (o11) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 77, 50);
    uint64_t t12 = r10;
    uint64_t t13 = (uint64_t)(t4 | t12);
    uint64_t t14 = (uintptr_t)((uint64_t)2ULL);
    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 77, 73);
    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
    uint64_t t17 = (uint64_t)t16;
    unsigned __int128 n18 = (unsigned __int128)((uint64_t)16ULL);
    if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_srcnqtpfma6qe4a, 77, 69);
    uint64_t r19 = (uint64_t)((uint64_t)t17 << (int)n18);
    bool o20 = (uint64_t)(r19 >> (int)n18) != t17;
    if (o20) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 77, 69);
    uint64_t t21 = r19;
    uint64_t t22 = (uint64_t)(t13 | t21);
    uint64_t t23 = (uintptr_t)((uint64_t)3ULL);
    uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
    if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcnqtpfma6qe4a, 77, 93);
    uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t23 * 1));
    uint64_t t26 = (uint64_t)t25;
    unsigned __int128 n27 = (unsigned __int128)((uint64_t)24ULL);
    if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_srcnqtpfma6qe4a, 77, 89);
    uint64_t r28 = (uint64_t)((uint64_t)t26 << (int)n27);
    bool o29 = (uint64_t)(r28 >> (int)n27) != t26;
    if (o29) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 77, 89);
    uint64_t t30 = r28;
    uint64_t t31 = (uint64_t)(t22 | t30);
    uint64_t t32 = (uintptr_t)((uint64_t)4ULL);
    uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
    if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 77, 113);
    uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t32 * 1));
    uint64_t t35 = (uint64_t)t34;
    unsigned __int128 n36 = (unsigned __int128)((uint64_t)32ULL);
    if (n36 >= 64) gm_trap_shift((uint64_t)n36, "u64", gm_srcnqtpfma6qe4a, 77, 109);
    uint64_t r37 = (uint64_t)((uint64_t)t35 << (int)n36);
    bool o38 = (uint64_t)(r37 >> (int)n36) != t35;
    if (o38) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 77, 109);
    uint64_t t39 = r37;
    uint64_t t40 = (uint64_t)(t31 | t39);
    uint64_t t41 = (uintptr_t)((uint64_t)5ULL);
    uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
    if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcnqtpfma6qe4a, 77, 133);
    uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t41 * 1));
    uint64_t t44 = (uint64_t)t43;
    unsigned __int128 n45 = (unsigned __int128)((uint64_t)40ULL);
    if (n45 >= 64) gm_trap_shift((uint64_t)n45, "u64", gm_srcnqtpfma6qe4a, 77, 129);
    uint64_t r46 = (uint64_t)((uint64_t)t44 << (int)n45);
    bool o47 = (uint64_t)(r46 >> (int)n45) != t44;
    if (o47) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 77, 129);
    uint64_t t48 = r46;
    uint64_t t49 = (uint64_t)(t40 | t48);
    uint64_t t50 = (uintptr_t)((uint64_t)6ULL);
    uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
    if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 77, 153);
    uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t50 * 1));
    uint64_t t53 = (uint64_t)t52;
    unsigned __int128 n54 = (unsigned __int128)((uint64_t)48ULL);
    if (n54 >= 64) gm_trap_shift((uint64_t)n54, "u64", gm_srcnqtpfma6qe4a, 77, 149);
    uint64_t r55 = (uint64_t)((uint64_t)t53 << (int)n54);
    bool o56 = (uint64_t)(r55 >> (int)n54) != t53;
    if (o56) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 77, 149);
    uint64_t t57 = r55;
    uint64_t t58 = (uint64_t)(t49 | t57);
    uint64_t t59 = (uintptr_t)((uint64_t)7ULL);
    uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
    if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srcnqtpfma6qe4a, 77, 173);
    uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t59 * 1));
    uint64_t t62 = (uint64_t)t61;
    unsigned __int128 n63 = (unsigned __int128)((uint64_t)56ULL);
    if (n63 >= 64) gm_trap_shift((uint64_t)n63, "u64", gm_srcnqtpfma6qe4a, 77, 169);
    uint64_t r64 = (uint64_t)((uint64_t)t62 << (int)n63);
    bool o65 = (uint64_t)(r64 >> (int)n63) != t62;
    if (o65) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 77, 169);
    uint64_t t66 = r64;
    uint64_t t67 = (uint64_t)(t58 | t66);
    F.ret = t67;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_mem_trap_ecogko0 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_f_mem_message_e0go901((void *)NULL, cx, NULL, &(*(gm_tpekhfs32op0f *)((uint8_t *)a2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_of_b77b4c9 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 176));
    uint64_t t8 = gm_f_us_cuv01fp(t7);
    uint64_t t9 = (uintptr_t)t8;
    if (t5 > t9 || t9 > (uintptr_t)t3) gm_trap_slice(t5, t9, t3, gm_srcnqtpfma6qe4a, 104, 68);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t4 + t5 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9 - t5));
    uint64_t t11 = gm_f_get_u64_tgpb2vh(t1, t10);
    F.ret = t11;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7o6oubd(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_7o6oubd F;
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
            gm_b16_8 t11 = gm_f_empty_slice_84b0b46();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_hvk9f35(t10, ((uint64_t)1ULL), t12);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_put_le64_uc3t56q(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_le64_uc3t56q F;
    gm_b16_8 gm_l0; /* b */
    uint64_t gm_l1; /* v */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            gm_l2 = ((uint64_t)8ULL);
        }
        {
            for (;;) {
                uint64_t t4 = gm_l3;
                uint64_t t5 = gm_l2;
                uint8_t t6 = (t4 < t5);
                if (!t6) goto lb3;
                {
                    uint64_t t9 = gm_l3;
                    uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)&t9));
                    gm_l4 = t12;
                    {
                        uint64_t t14 = gm_l4;
                        uint64_t t15 = (uintptr_t)t14;
                        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_srcnqtpfma6qe4a, 82, 21);
                        uint64_t t17 = gm_l1;
                        uint64_t t18 = gm_l4;
                        uint64_t r19; bool o20 = __builtin_mul_overflow(((uint64_t)8ULL), t18, &r19);
                        if (o20) gm_trap("integer overflow in `*`", gm_srcnqtpfma6qe4a, 82, 38);
                        uint64_t t21 = r19;
                        unsigned __int128 n22 = (unsigned __int128)t21;
                        if (n22 >= 64) gm_trap_shift((uint64_t)n22, "u64", gm_srcnqtpfma6qe4a, 82, 29);
                        uint64_t t23 = (uint64_t)(t17 >> (int)n22);
                        uint8_t t24 = (uint8_t)t23;
                        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t15 * 1), t24);
                    }
                    goto me10;
mn11:;
                    gm_trap("no match arm matched the scrutinee", gm_srcnqtpfma6qe4a, 82, 5);
me10:;
                }
lc3:;
                {
                    uint64_t t26 = gm_l3;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l3 = t29;
                }
            }
lb3:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
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
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 316, 36);
                uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1));
                uint64_t t13 = (uint64_t)t12;
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 316, 49);
                uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1));
                uint64_t t17 = (uint64_t)t16;
                unsigned __int128 n18 = (unsigned __int128)((uint64_t)8ULL);
                if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t r19 = (uint64_t)((uint64_t)t17 << (int)n18);
                bool o20 = (uint64_t)(r19 >> (int)n18) != t17;
                if (o20) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t t21 = r19;
                uint64_t t22 = (uint64_t)(t13 | t21);
                uint64_t t23 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcnqtpfma6qe4a, 316, 68);
                uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t23 * 1));
                uint64_t t26 = (uint64_t)t25;
                unsigned __int128 n27 = (unsigned __int128)((uint64_t)16ULL);
                if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t r28 = (uint64_t)((uint64_t)t26 << (int)n27);
                bool o29 = (uint64_t)(r28 >> (int)n27) != t26;
                if (o29) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t22 | t30);
                uint64_t t32 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 316, 88);
                uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1));
                uint64_t t35 = (uint64_t)t34;
                unsigned __int128 n36 = (unsigned __int128)((uint64_t)24ULL);
                if (n36 >= 64) gm_trap_shift((uint64_t)n36, "u64", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t r37 = (uint64_t)((uint64_t)t35 << (int)n36);
                bool o38 = (uint64_t)(r37 >> (int)n36) != t35;
                if (o38) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t t39 = r37;
                uint64_t t40 = (uint64_t)(t31 | t39);
                uint64_t t41 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcnqtpfma6qe4a, 316, 108);
                uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t41 * 1));
                uint64_t t44 = (uint64_t)t43;
                unsigned __int128 n45 = (unsigned __int128)((uint64_t)32ULL);
                if (n45 >= 64) gm_trap_shift((uint64_t)n45, "u64", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t r46 = (uint64_t)((uint64_t)t44 << (int)n45);
                bool o47 = (uint64_t)(r46 >> (int)n45) != t44;
                if (o47) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t t48 = r46;
                uint64_t t49 = (uint64_t)(t40 | t48);
                uint64_t t50 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 316, 128);
                uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1));
                uint64_t t53 = (uint64_t)t52;
                unsigned __int128 n54 = (unsigned __int128)((uint64_t)40ULL);
                if (n54 >= 64) gm_trap_shift((uint64_t)n54, "u64", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t r55 = (uint64_t)((uint64_t)t53 << (int)n54);
                bool o56 = (uint64_t)(r55 >> (int)n54) != t53;
                if (o56) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t t57 = r55;
                uint64_t t58 = (uint64_t)(t49 | t57);
                uint64_t t59 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srcnqtpfma6qe4a, 316, 148);
                uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t59 * 1));
                uint64_t t62 = (uint64_t)t61;
                unsigned __int128 n63 = (unsigned __int128)((uint64_t)48ULL);
                if (n63 >= 64) gm_trap_shift((uint64_t)n63, "u64", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t r64 = (uint64_t)((uint64_t)t62 << (int)n63);
                bool o65 = (uint64_t)(r64 >> (int)n63) != t62;
                if (o65) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t t66 = r64;
                uint64_t t67 = (uint64_t)(t58 | t66);
                uint64_t t68 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_srcnqtpfma6qe4a, 316, 168);
                uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t68 * 1));
                uint64_t t71 = (uint64_t)t70;
                unsigned __int128 n72 = (unsigned __int128)((uint64_t)56ULL);
                if (n72 >= 64) gm_trap_shift((uint64_t)n72, "u64", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t r73 = (uint64_t)((uint64_t)t71 << (int)n72);
                bool o74 = (uint64_t)(r73 >> (int)n72) != t71;
                if (o74) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t t75 = r73;
                uint64_t t76 = (uint64_t)(t67 | t75);
                uint8_t t77 = 0;
                F.ret = t76;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t80 = ((t79 == ((uint64_t)4ULL)));
            if (!t80) goto ie78;
            {
                uint64_t t82 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcnqtpfma6qe4a, 317, 36);
                uint8_t t84 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1));
                uint64_t t85 = (uint64_t)t84;
                uint64_t t86 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcnqtpfma6qe4a, 317, 49);
                uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t86 * 1));
                uint64_t t89 = (uint64_t)t88;
                unsigned __int128 n90 = (unsigned __int128)((uint64_t)8ULL);
                if (n90 >= 64) gm_trap_shift((uint64_t)n90, "u64", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t r91 = (uint64_t)((uint64_t)t89 << (int)n90);
                bool o92 = (uint64_t)(r91 >> (int)n90) != t89;
                if (o92) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t t93 = r91;
                uint64_t t94 = (uint64_t)(t85 | t93);
                uint64_t t95 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t95 >= (uintptr_t)(t96)) gm_trap_index(t95, t96, gm_srcnqtpfma6qe4a, 317, 68);
                uint8_t t97 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t95 * 1));
                uint64_t t98 = (uint64_t)t97;
                unsigned __int128 n99 = (unsigned __int128)((uint64_t)16ULL);
                if (n99 >= 64) gm_trap_shift((uint64_t)n99, "u64", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t r100 = (uint64_t)((uint64_t)t98 << (int)n99);
                bool o101 = (uint64_t)(r100 >> (int)n99) != t98;
                if (o101) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t t102 = r100;
                uint64_t t103 = (uint64_t)(t94 | t102);
                uint64_t t104 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcnqtpfma6qe4a, 317, 88);
                uint8_t t106 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 1));
                uint64_t t107 = (uint64_t)t106;
                unsigned __int128 n108 = (unsigned __int128)((uint64_t)24ULL);
                if (n108 >= 64) gm_trap_shift((uint64_t)n108, "u64", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t r109 = (uint64_t)((uint64_t)t107 << (int)n108);
                bool o110 = (uint64_t)(r109 >> (int)n108) != t107;
                if (o110) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t t111 = r109;
                uint64_t t112 = (uint64_t)(t103 | t111);
                uint8_t t113 = 0;
                F.ret = t112;
                goto gm_fret;
            }
            goto ix78;
ie78:;
ix78:;
        }
        {
            uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t116 = ((t115 == ((uint64_t)1ULL)));
            if (!t116) goto ie114;
            {
                uint64_t t118 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_srcnqtpfma6qe4a, 318, 36);
                uint8_t t120 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t118 * 1));
                uint64_t t121 = (uint64_t)t120;
                uint8_t t122 = 0;
                F.ret = t121;
                goto gm_fret;
            }
            goto ix114;
ie114:;
ix114:;
        }
        {
            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t125 = ((t124 == ((uint64_t)2ULL)));
            if (!t125) goto ie123;
            {
                uint64_t t127 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcnqtpfma6qe4a, 319, 36);
                uint8_t t129 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t127 * 1));
                uint64_t t130 = (uint64_t)t129;
                uint64_t t131 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t131 >= (uintptr_t)(t132)) gm_trap_index(t131, t132, gm_srcnqtpfma6qe4a, 319, 49);
                uint8_t t133 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t131 * 1));
                uint64_t t134 = (uint64_t)t133;
                unsigned __int128 n135 = (unsigned __int128)((uint64_t)8ULL);
                if (n135 >= 64) gm_trap_shift((uint64_t)n135, "u64", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t r136 = (uint64_t)((uint64_t)t134 << (int)n135);
                bool o137 = (uint64_t)(r136 >> (int)n135) != t134;
                if (o137) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t t138 = r136;
                uint64_t t139 = (uint64_t)(t130 | t138);
                uint8_t t140 = 0;
                F.ret = t139;
                goto gm_fret;
            }
            goto ix123;
ie123:;
ix123:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t141 = gm_l0;
        gm_b16_8 t142 = gm_l1;
        unsigned __int128 t143 = gm_f_get_uint_bq8c4b7(t141, t142);
        uint64_t t144 = (uint64_t)t143;
        uint8_t t145 = 0;
        F.ret = t144;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_u6r4vns F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 267u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = gm_l2;
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = gm_l3;
        uint8_t t6 = (t5 <= ((uint64_t)1ULL));
        if (!t6) goto ie2;
        {
            gm_b16_8 t8 = gm_f_empty_slice_ijtp9lo();
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t8);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t10 = gm_l1;
        uint64_t t11 = gm_l3;
        uint8_t t12 = gm_f_aligned_q6sor4h(t10, t11);
        if (!t12) goto ie9;
        {
            void * t15 = gm_l0;
            uint64_t t16 = gm_l1;
            uint64_t t17 = gm_l2;
            void * t18 = gm_f_view_qalkbv1(t15, t16, t17, ((uint8_t)0));
            if (!(GM_LD(void *, ((uint8_t *)&t18)) != 0)) goto ie14;
            void * t19 = GM_LD(void *, ((uint8_t *)&t18));
            gm_l4 = t19;
            {
                void * t21 = gm_l4;
                void * t22 = ((uint8_t *)t21);
                uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t24 = gm_l2;
                uint64_t t25 = gm_f_us_cuv01fp(t24);
                uint64_t t26 = (uintptr_t)t25;
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t22 + t23 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26 - t23));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t27);
                goto gm_fdone;
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
        void * t28 = gm_l0;
        uint64_t t29 = gm_l1;
        uint64_t t30 = gm_l2;
        uint64_t t31 = gm_l3;
        gm_tpekhfs32op0f t32;
        gm_f_fault_u31b92l((void *)&t32, t28, t29, t30, t31, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 1;
        GM_ST(gm_tpekhfs32op0f, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hvk9f35(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hvk9f35 F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_res_is_err_8jbgndp F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l2;
    gm_b16_8 t2 = t1;
    uint64_t t3 = (uintptr_t)((uint64_t)0ULL);
    void * t4 = gm_l0;
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&t6) + 8);
    if (t3 >= (uintptr_t)(t7)) gm_trap_index(t3, t7, gm_srcnqtpfma6qe4a, 174, 81);
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t6)) + t3 * 8));
    uint64_t t9 = gm_f_us_cuv01fp(t8);
    uint64_t t10 = (uintptr_t)t9;
    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 174, 76);
    uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)) + t10 * 1));
    uint8_t t13 = (!(t12 == ((uint8_t)0ULL)));
    F.ret = t13;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
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
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_qalkbv1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint8_t gm_l3; /* write */
    uint64_t gm_l4; /* p */
    void * gm_l5; /* pg */
    void * gm_l6; /* ss */
    uint64_t gm_l7; /* rel */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* s */
    uint64_t gm_l10; /* off */
    GM_STACK_CHECK_SHALLOW(&F, 561u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t2 = gm_l1;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
        uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
        uint64_t t7 = r5;
        uint64_t t8 = GM_LD(uint64_t, gm_c6p4chcutca);
        unsigned __int128 n9 = (unsigned __int128)t8;
        if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 206, 10);
        uint64_t t10 = (uint64_t)(t7 >> (int)n9);
        gm_l4 = t10;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 0) + 0) + 8);
        uint64_t t15 = (uint64_t)t14;
        uint8_t t16 = (t12 >= t15);
        if (!t16) goto ie11;
        {
            uint8_t t18 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((((uint8_t *)t19) + 0) + 0));
        void * t21 = t20;
        uint64_t t22 = gm_l4;
        uint64_t t23 = gm_f_us_cuv01fp(t22);
        uint64_t t24 = (uintptr_t)t23;
        void * t25 = (void *)((uint8_t *)t21 + t24 * 32);
        gm_l5 = t25;
    }
    {
        void * t26 = gm_l5;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
        void * t28;
        if ((GM_LD(void *, ((uint8_t *)&t27)) != 0)) {
            void * t29 = GM_LD(void *, (((uint8_t *)&t27) + 0));
            t28 = t29;
        } else {
            uint8_t t30 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l6 = t28;
    }
    {
        uint64_t t31 = gm_l1;
        void * t32 = gm_l5;
        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 8));
        uint64_t r34; bool o35 = __builtin_sub_overflow(t31, t33, &r34);
        if (o35) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
        uint64_t t36 = r34;
        gm_l7 = t36;
    }
    {
        uint64_t t37 = gm_l7;
        void * t38 = gm_l5;
        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 16));
        unsigned __int128 n40 = (unsigned __int128)t39;
        if (n40 >= 64) gm_trap_shift((uint64_t)n40, "u64", gm_srcq7ohh8h77bn7, 211, 10);
        uint64_t t41 = (uint64_t)(t37 >> (int)n40);
        gm_l8 = t41;
    }
    {
        void * t42 = gm_l6;
        void * t43 = t42;
        uint64_t t44 = gm_l8;
        uint64_t t45 = gm_f_us_cuv01fp(t44);
        uint64_t t46 = (uintptr_t)t45;
        void * t47 = (void *)((uint8_t *)t43 + t46 * 24);
        gm_l9 = t47;
    }
    {
        uint64_t t48 = gm_l7;
        uint64_t t49 = gm_l8;
        void * t50 = gm_l5;
        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 16));
        unsigned __int128 n52 = (unsigned __int128)t51;
        if (n52 >= 64) gm_trap_shift((uint64_t)n52, "u64", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t r53 = (uint64_t)((uint64_t)t49 << (int)n52);
        bool o54 = (uint64_t)(r53 >> (int)n52) != t49;
        if (o54) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t t55 = r53;
        uint64_t r56; bool o57 = __builtin_sub_overflow(t48, t55, &r56);
        if (o57) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
        uint64_t t58 = r56;
        gm_l10 = t58;
    }
    {
        uint8_t t60 = 0;
        uint8_t t61 = 0;
        uint8_t t62 = 0;
        void * t63 = gm_l9;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)t63) + 17));
        uint8_t t65 = (uint8_t)!t64;
        if (!t65) {
            uint64_t t66 = gm_l2;
            void * t67 = gm_l9;
            uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)t67) + 8));
            uint8_t t69 = (t66 > t68);
            t62 = t69;
        } else t62 = 1;
        if (!t62) {
            uint64_t t70 = gm_l10;
            void * t71 = gm_l9;
            uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)t71) + 8));
            uint64_t t73 = gm_l2;
            uint64_t r74; bool o75 = __builtin_sub_overflow(t72, t73, &r74);
            if (o75) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
            uint64_t t76 = r74;
            uint8_t t77 = (t70 > t76);
            t61 = t77;
        } else t61 = 1;
        if (!t61) {
            uint8_t t78 = 0;
            uint8_t t79 = gm_l3;
            if (t79) {
                void * t80 = gm_l9;
                uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t80) + 18));
                uint8_t t82 = (uint8_t)!t81;
                t78 = t82;
            } else t78 = 0;
            t60 = t78;
        } else t60 = 1;
        if (!t60) goto ie59;
        {
            uint8_t t84 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        void * t85 = gm_l9;
        void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
        void * t87 = t86;
        uint64_t t88 = gm_l10;
        uint64_t t89 = gm_f_us_cuv01fp(t88);
        uint64_t t90 = (uintptr_t)t89;
        void * t91 = (void *)((uint8_t *)t87 + t90 * 1);
        void * t92 = t91;
        uint8_t t93 = 0;
        F.ret = t92;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) void * gm_f_adt_of_8k5vqud(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_of_8k5vqud F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 183u);
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
        if (t6 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t7 = GM_LD(uint32_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l2 = t7;
        {
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 8));
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((((uint8_t *)t10) + 1088) + 0));
            gm_b16_8 t12 = t11;
            uint32_t t13 = gm_l2;
            uint64_t t14 = (uint64_t)t13;
            uint64_t t15 = gm_f_us_cuv01fp(t14);
            uint64_t t16 = (uintptr_t)t15;
            uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&t12) + 8);
            if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_srcnqtpfma6qe4a, 202, 13);
            void * t18 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t12)) + t16 * 184);
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t21 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) uint32_t gm_f_bit_width_0fd5f52(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_tplpbqf3duvn3 gm_f_bits_field_vqua3kd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2) {
    struct gm_fr_bits_field_vqua3kd F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* i */
    gm_tcmkompbl09il gm_l3; /* bl */
    gm_tov4mtrfp5e7v gm_l4; /* f */
    GM_STACK_CHECK(&F, 238u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t57euh612ligr t5;
        gm_f_bits_layout_u6kkccp((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcmkompbl09il t6;
        if (((((uint8_t *)&t5))[24] != 0)) {
            t6 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t5) + 0));
        } else {
            gm_t88rcucg1b7uk t7 = gm_l1;
            gm_tplpbqf3duvn3 t8;
            GM_ST(uint32_t, ((uint8_t *)&t8 + 0), ((uint32_t)0ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t8 + 4), t7);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l3 = t6;
    }
    {
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        gm_b16_8 t11 = t10;
        uint32_t t12 = gm_l2;
        uint64_t t13 = (uint64_t)t12;
        uint64_t t14 = gm_f_us_cuv01fp(t13);
        uint64_t t15 = (uintptr_t)t14;
        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&t11) + 8);
        if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_srcnqtpfma6qe4a, 266, 10);
        gm_tov4mtrfp5e7v t17 = GM_LD(gm_tov4mtrfp5e7v, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t11)) + t15 * 12));
        gm_l4 = t17;
    }
    {
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
        gm_tplpbqf3duvn3 t20;
        GM_ST(uint32_t, ((uint8_t *)&t20 + 0), t18);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t20 + 4), t19);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_bits_from_raw_el4tjdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, unsigned __int128 a2, gm_b16_8 a3) {
    struct gm_fr_bits_from_raw_el4tjdh F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    unsigned __int128 gm_l2; /* raw */
    gm_b16_8 gm_l3; /* dst */
    uint32_t gm_l4; /* w */
    unsigned __int128 gm_l5; /* v */
    gm_t88rcucg1b7uk gm_l6; /* under */
    gm_tcmkompbl09il gm_l7; /* u */
    gm_t88rcucg1b7uk gm_l8; /* u */
    gm_ttovhfee8mf28 gm_l9; /* info */
    GM_STACK_CHECK(&F, 581u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_htv6093((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = ((GM_LD(uint8_t, ((uint8_t *)&t5) + 0) == ((uint8_t)4ULL)));
        if (!t6) goto ie2;
        {
            uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srcnqtpfma6qe4a, 280, 9);
            uint8_t *t10 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t8 * 1);
            uint8_t t11;
            unsigned __int128 t13 = gm_l2;
            uint8_t t14 = ((t13 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
            if (!t14) goto ie12;
            uint8_t t16 = ((uint8_t)0ULL);
            t11 = t16;
            goto ix12;
ie12:;
            uint8_t t18 = ((uint8_t)1ULL);
            t11 = t18;
ix12:;
            GM_ST(uint8_t, t10, t11);
        }
        {
            gm_b0_1 t19;
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t21 = gm_l0;
        gm_t88rcucg1b7uk t22 = gm_l1;
        uint32_t t23 = gm_f_bit_width_0fd5f52((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t23;
    }
    {
        unsigned __int128 t24 = gm_l2;
        gm_l5 = t24;
    }
    {
        gm_t88rcucg1b7uk t25 = gm_l1;
        gm_l6 = t25;
    }
    {
        void * t27 = gm_l0;
        gm_t88rcucg1b7uk t28 = gm_l1;
        gm_t57euh612ligr t29;
        gm_f_union_def_h8mvf4t((void *)&t29, t27, GM_CONV(gm_csguankko4lmhd, t28));
        if (!((((uint8_t *)&t29))[24] != 0)) goto ie26;
        gm_l7 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t29) + 0));
        {
            gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 0));
            gm_l6 = t31;
        }
        goto ix26;
ie26:;
        void * t33 = gm_l0;
        gm_t88rcucg1b7uk t34 = gm_l1;
        gm_tkn42u7na3t81 t35 = gm_f_bits_struct_ty_22ue20s(t33, GM_CONV(gm_csguankko4lmhd, t34));
        if (!((((uint8_t *)&t35))[4] != 0)) goto ie32;
        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t35));
        gm_l8 = t36;
        {
            gm_t88rcucg1b7uk t38 = gm_l8;
            gm_l6 = t38;
        }
        goto ix32;
ie32:;
ix32:;
ix26:;
    }
    {
        void * t40 = gm_l0;
        gm_t88rcucg1b7uk t41 = gm_l6;
        gm_tg5lql6itufk7 t42 = gm_f_int_info_ulcevdv(t40, GM_CONV(gm_csguankko4lmhd, t41));
        if (!((((uint8_t *)&t42))[8] != 0)) goto ie39;
        gm_ttovhfee8mf28 t43 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t42));
        gm_l9 = t43;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
        if (!t44) goto ie39;
        uint32_t t45 = gm_l4;
        uint8_t t46 = (t45 < ((uint32_t)128ULL));
        if (!t46) goto ie39;
        unsigned __int128 t47 = gm_l5;
        uint32_t t48 = gm_l4;
        uint32_t r49; bool o50 = __builtin_sub_overflow(t48, ((uint32_t)1ULL), &r49);
        if (o50) gm_trap("integer overflow in `-`", gm_srcnqtpfma6qe4a, 287, 77);
        uint32_t t51 = r49;
        unsigned __int128 t52 = (unsigned __int128)t51;
        unsigned __int128 n53 = (unsigned __int128)t52;
        if (n53 >= 128) gm_trap_shift((uint64_t)n53, "u128", gm_srcnqtpfma6qe4a, 287, 67);
        unsigned __int128 t54 = (unsigned __int128)(t47 >> (int)n53);
        unsigned __int128 t55 = (unsigned __int128)(t54 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)));
        uint8_t t56 = ((t55 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
        if (!t56) goto ie39;
        {
            unsigned __int128 t58 = gm_l5;
            uint32_t t59 = gm_l4;
            unsigned __int128 t60 = (unsigned __int128)t59;
            unsigned __int128 n61 = (unsigned __int128)t60;
            if (n61 >= 128) gm_trap_shift((uint64_t)n61, "u128", gm_srcnqtpfma6qe4a, 287, 104);
            unsigned __int128 r62 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n61);
            bool o63 = (unsigned __int128)(r62 >> (int)n61) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
            if (o63) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 287, 104);
            unsigned __int128 t64 = r62;
            unsigned __int128 r65; bool o66 = __builtin_sub_overflow(t64, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r65);
            if (o66) gm_trap("integer overflow in `-`", gm_srcnqtpfma6qe4a, 287, 103);
            unsigned __int128 t67 = r65;
            unsigned __int128 t68 = (unsigned __int128)~t67;
            unsigned __int128 t69 = (unsigned __int128)(t58 | t68);
            gm_l5 = t69;
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        void * t70 = gm_l0;
        gm_b16_8 t71 = gm_l3;
        unsigned __int128 t72 = gm_l5;
        gm_f_put_uint_cddin99(t70, t71, t72);
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
inline __attribute__((aligned(4))) unsigned __int128 gm_f_bits_raw_t6dqs65(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_bits_raw_t6dqs65 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    uint32_t gm_l3; /* w */
    unsigned __int128 gm_l4; /* v */
    GM_STACK_CHECK(&F, 290u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        uint32_t t4 = gm_f_bit_width_0fd5f52((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        gm_b16_8 t6 = gm_l2;
        unsigned __int128 t7 = gm_f_get_uint_bq8c4b7(t5, t6);
        gm_l4 = t7;
    }
    {
        unsigned __int128 t8;
        uint32_t t10 = gm_l3;
        uint8_t t11 = (t10 >= ((uint32_t)128ULL));
        if (!t11) goto ie9;
        unsigned __int128 t13 = gm_l4;
        unsigned __int128 t14 = t13;
        t8 = t14;
        goto ix9;
ie9:;
        unsigned __int128 t16 = gm_l4;
        uint32_t t17 = gm_l3;
        unsigned __int128 t18 = (unsigned __int128)t17;
        unsigned __int128 n19 = (unsigned __int128)t18;
        if (n19 >= 128) gm_trap_shift((uint64_t)n19, "u128", gm_srcnqtpfma6qe4a, 274, 43);
        unsigned __int128 r20 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n19);
        bool o21 = (unsigned __int128)(r20 >> (int)n19) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
        if (o21) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 274, 43);
        unsigned __int128 t22 = r20;
        unsigned __int128 r23; bool o24 = __builtin_sub_overflow(t22, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r23);
        if (o24) gm_trap("integer overflow in `-`", gm_srcnqtpfma6qe4a, 274, 42);
        unsigned __int128 t25 = r23;
        unsigned __int128 t26 = (unsigned __int128)(t16 & t25);
        unsigned __int128 t27 = t26;
        t8 = t27;
ix9:;
        uint8_t t28 = 0;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bits_struct_ty_22ue20s F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    void * gm_l2; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        void * t4 = gm_f_adt_of_8k5vqud(t2, GM_CONV(gm_csguankko4lmhd, t3));
        void * t5;
        if ((GM_LD(void *, ((uint8_t *)&t4)) != 0)) {
            void * t6 = GM_LD(void *, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tkn42u7na3t81 t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        void * t10 = gm_l2;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t10) + 24) + 0));
        if (t11 != ((uint8_t)0ULL)) goto ie9;
        gm_tpautr4br7h3l t12 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t10) + 24) + 8));
        F.l3 = t12;
        {
            gm_tkn42u7na3t81 t14 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l3) + 40));
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix9;
ie9:;
        {
            gm_tkn42u7na3t81 t17;
            memset(&t17, 0, sizeof t17);
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
ix9:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_field_bytes_bhivr4s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3) {
    struct gm_fr_field_bytes_bhivr4s F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    uint32_t gm_l3; /* i */
    gm_tfbnciphf3261 gm_l4; /* fl */
    uint64_t gm_l5; /* off */
    GM_STACK_CHECK(&F, 253u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        uint32_t t4 = gm_l3;
        gm_tfbnciphf3261 t5 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t5;
    }
    {
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        uint64_t t7 = gm_f_us_cuv01fp(t6);
        gm_l5 = t7;
    }
    {
        gm_b16_8 t8 = gm_l2;
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&t8) + 8);
        void * t10 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t8)));
        uint64_t t11 = gm_l5;
        uint64_t t12 = (uintptr_t)t11;
        uint64_t t13 = gm_l5;
        void * t14 = gm_l0;
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 0));
        uint64_t t16 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t17 = gm_f_us_cuv01fp(t16);
        uint64_t r18; bool o19 = __builtin_add_overflow(t13, t17, &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcnqtpfma6qe4a, 295, 19);
        uint64_t t20 = r18;
        uint64_t t21 = (uintptr_t)t20;
        if (t12 > t21 || t21 > (uintptr_t)t9) gm_trap_slice(t12, t21, t9, gm_srcnqtpfma6qe4a, 295, 12);
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t10 + t12 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(t21 - t12));
        uint8_t t23 = 0;
        F.ret = t22;
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
__attribute__((aligned(4))) gm_tfbnciphf3261 gm_f_field_layout_h34salb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2) {
    struct gm_fr_field_layout_h34salb F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* elems */
    uint32_t gm_l4; /* id */
    gm_b16_8 gm_l6; /* fields */
    gm_b16_8 gm_l7; /* variants */
    GM_STACK_CHECK(&F, 836u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t53at80pfdus3 t4;
        gm_f_kind_htv6093((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; default: goto ma8; }
ma6:;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t12 != ((uint8_t)12ULL)) goto mn11;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((((uint8_t *)&t4) + 8) + 0));
        gm_l3 = t13;
        gm_b16_8 t14 = gm_l3;
        gm_b16_8 t15 = t14;
        uint32_t t16 = gm_l2;
        uint64_t t17 = (uint64_t)t16;
        uint64_t t18 = gm_f_us_cuv01fp(t17);
        uint64_t t19 = (uintptr_t)t18;
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&t15) + 8);
        if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_srcnqtpfma6qe4a, 250, 41);
        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t15)) + t19 * 4));
        void * t22 = gm_l0;
        gm_t88rcucg1b7uk t23 = gm_l1;
        gm_b16_8 t24 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t25 = t24;
        uint32_t t26 = gm_l2;
        uint64_t t27 = (uint64_t)t26;
        uint64_t t28 = gm_f_us_cuv01fp(t27);
        uint64_t t29 = (uintptr_t)t28;
        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&t25) + 8);
        if (t29 >= (uintptr_t)(t30)) gm_trap_index(t29, t30, gm_srcnqtpfma6qe4a, 250, 60);
        uint64_t t31 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t25)) + t29 * 8));
        gm_tfbnciphf3261 t32;
        GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t32 + 0), t21);
        GM_ST(uint64_t, ((uint8_t *)&t32 + 8), t31);
        uint8_t t33 = 0;
        F.ret = t32;
        goto gm_fret;
        goto me5;
mn11:;
ma7:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t35 != ((uint8_t)17ULL)) goto mn34;
        uint32_t t36 = GM_LD(uint32_t, ((((uint8_t *)&t4) + 8) + 0));
        gm_l4 = t36;
        void * t37 = gm_l0;
        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 8));
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((((uint8_t *)t38) + 1088) + 0));
        gm_b16_8 t40 = t39;
        uint32_t t41 = gm_l4;
        uint64_t t42 = (uint64_t)t41;
        uint64_t t43 = gm_f_us_cuv01fp(t42);
        uint64_t t44 = (uintptr_t)t43;
        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&t40) + 8);
        if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srcnqtpfma6qe4a, 251, 30);
        uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t40)) + t44 * 184) + 24) + 0));
        switch (t51) { case ((uint8_t)0ULL): goto ma47; case ((uint8_t)1ULL): goto ma49; case ((uint8_t)2ULL): goto ma48; default: goto ma50; }
ma47:;
        uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t40)) + t44 * 184) + 24) + 0));
        if (t53 != ((uint8_t)0ULL)) goto mn52;
        gm_tpautr4br7h3l t54 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t40)) + t44 * 184) + 24) + 8));
        F.l5 = t54;
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t56 = t55;
        uint32_t t57 = gm_l2;
        uint64_t t58 = (uint64_t)t57;
        uint64_t t59 = gm_f_us_cuv01fp(t58);
        uint64_t t60 = (uintptr_t)t59;
        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&t56) + 8);
        if (t60 >= (uintptr_t)(t61)) gm_trap_index(t60, t61, gm_srcnqtpfma6qe4a, 252, 41);
        gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t56)) + t60 * 40) + 16));
        void * t63 = gm_l0;
        gm_t88rcucg1b7uk t64 = gm_l1;
        gm_b16_8 t65 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t63, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t66 = t65;
        uint32_t t67 = gm_l2;
        uint64_t t68 = (uint64_t)t67;
        uint64_t t69 = gm_f_us_cuv01fp(t68);
        uint64_t t70 = (uintptr_t)t69;
        uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&t66) + 8);
        if (t70 >= (uintptr_t)(t71)) gm_trap_index(t70, t71, gm_srcnqtpfma6qe4a, 252, 68);
        uint64_t t72 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t66)) + t70 * 8));
        gm_tfbnciphf3261 t73;
        GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t73 + 0), t62);
        GM_ST(uint64_t, ((uint8_t *)&t73 + 8), t72);
        uint8_t t74 = 0;
        F.ret = t73;
        goto gm_fret;
        goto me46;
mn52:;
ma48:;
        uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t40)) + t44 * 184) + 24) + 0));
        if (t76 != ((uint8_t)2ULL)) goto mn75;
        gm_b16_8 t77 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t40)) + t44 * 184) + 24) + 8));
        gm_l6 = t77;
        gm_b16_8 t78 = gm_l6;
        gm_b16_8 t79 = t78;
        uint32_t t80 = gm_l2;
        uint64_t t81 = (uint64_t)t80;
        uint64_t t82 = gm_f_us_cuv01fp(t81);
        uint64_t t83 = (uintptr_t)t82;
        uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&t79) + 8);
        if (t83 >= (uintptr_t)(t84)) gm_trap_index(t83, t84, gm_srcnqtpfma6qe4a, 253, 44);
        gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t79)) + t83 * 40) + 16));
        gm_tfbnciphf3261 t86;
        GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t86 + 0), t85);
        GM_ST(uint64_t, ((uint8_t *)&t86 + 8), ((uint64_t)0ULL));
        uint8_t t87 = 0;
        F.ret = t86;
        goto gm_fret;
        goto me46;
mn75:;
ma49:;
        uint8_t t89 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t40)) + t44 * 184) + 24) + 0));
        if (t89 != ((uint8_t)1ULL)) goto mn88;
        gm_b16_8 t90 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((uint8_t *)&t40)) + t44 * 184) + 24) + 8) + 8));
        gm_l7 = t90;
        gm_b16_8 t91 = gm_l7;
        gm_b16_8 t92 = t91;
        uint32_t t93 = gm_l2;
        uint64_t t94 = (uint64_t)t93;
        uint64_t t95 = gm_f_us_cuv01fp(t94);
        uint64_t t96 = (uintptr_t)t95;
        uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&t92) + 8);
        if (t96 >= (uintptr_t)(t97)) gm_trap_index(t96, t97, gm_srcnqtpfma6qe4a, 254, 48);
        gm_tkn42u7na3t81 t98 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t92)) + t96 * 72) + 16));
        gm_t88rcucg1b7uk t99;
        if (((((uint8_t *)&t98))[4] != 0)) {
            gm_t88rcucg1b7uk t100 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t98) + 0));
            t99 = t100;
        } else {
            gm_t88rcucg1b7uk t101 = gm_l1;
            t99 = t101;
        }
        void * t102 = gm_l0;
        gm_t88rcucg1b7uk t103 = gm_l1;
        uint64_t t104 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t102, t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfbnciphf3261 t105;
        GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t105 + 0), t99);
        GM_ST(uint64_t, ((uint8_t *)&t105 + 8), t104);
        uint8_t t106 = 0;
        F.ret = t105;
        goto gm_fret;
        goto me46;
mn88:;
ma50:;
        gm_trap("no match arm matched the scrutinee", gm_srcnqtpfma6qe4a, 251, 24);
me46:;
        goto me5;
mn34:;
ma8:;
        gm_t88rcucg1b7uk t108 = gm_l1;
        gm_tfbnciphf3261 t109;
        GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t109 + 0), t108);
        GM_ST(uint64_t, ((uint8_t *)&t109 + 8), ((uint64_t)0ULL));
        uint8_t t110 = 0;
        F.ret = t109;
        goto gm_fret;
        goto me5;
mn107:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcnqtpfma6qe4a, 249, 5);
me5:;
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
inline __attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_int_pg7p4gc F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    unsigned __int128 gm_l3; /* v */
    gm_ttovhfee8mf28 gm_l4; /* info */
    GM_STACK_CHECK_SHALLOW(&F, 468u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        unsigned __int128 t4 = gm_f_get_uint_bq8c4b7(t2, t3);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_tg5lql6itufk7 t7 = gm_f_int_info_ulcevdv(t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_ttovhfee8mf28 t8;
        if (((((uint8_t *)&t7))[8] != 0)) {
            gm_ttovhfee8mf28 t9 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            unsigned __int128 t10 = gm_l3;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        gm_l4 = t8;
    }
    {
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
        if (!t13) goto ie12;
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
        uint8_t t15 = (t14 < ((uint32_t)128ULL));
        if (!t15) goto ie12;
        unsigned __int128 t16 = gm_l3;
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
        uint32_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint32_t)1ULL), &r18);
        if (o19) gm_trap("integer overflow in `-`", gm_srcnqtpfma6qe4a, 99, 55);
        uint32_t t20 = r18;
        unsigned __int128 t21 = (unsigned __int128)t20;
        unsigned __int128 n22 = (unsigned __int128)t21;
        if (n22 >= 128) gm_trap_shift((uint64_t)n22, "u128", gm_srcnqtpfma6qe4a, 99, 45);
        unsigned __int128 t23 = (unsigned __int128)(t16 >> (int)n22);
        unsigned __int128 t24 = (unsigned __int128)(t23 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)));
        uint8_t t25 = ((t24 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
        if (!t25) goto ie12;
        {
            unsigned __int128 t27 = gm_l3;
            uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
            unsigned __int128 t29 = (unsigned __int128)t28;
            unsigned __int128 n30 = (unsigned __int128)t29;
            if (n30 >= 128) gm_trap_shift((uint64_t)n30, "u128", gm_srcnqtpfma6qe4a, 99, 96);
            unsigned __int128 r31 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n30);
            bool o32 = (unsigned __int128)(r31 >> (int)n30) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
            if (o32) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 99, 96);
            unsigned __int128 t33 = r31;
            unsigned __int128 r34; bool o35 = __builtin_sub_overflow(t33, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r34);
            if (o35) gm_trap("integer overflow in `-`", gm_srcnqtpfma6qe4a, 99, 95);
            unsigned __int128 t36 = r34;
            unsigned __int128 t37 = (unsigned __int128)~t36;
            unsigned __int128 t38 = (unsigned __int128)(t27 | t37);
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
        uint8_t t42 = (t41 < ((uint32_t)128ULL));
        if (!t42) goto ie40;
        {
            unsigned __int128 t44 = gm_l3;
            uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
            unsigned __int128 t46 = (unsigned __int128)t45;
            unsigned __int128 n47 = (unsigned __int128)t46;
            if (n47 >= 128) gm_trap_shift((uint64_t)n47, "u128", gm_srcnqtpfma6qe4a, 100, 39);
            unsigned __int128 r48 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n47);
            bool o49 = (unsigned __int128)(r48 >> (int)n47) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
            if (o49) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 100, 39);
            unsigned __int128 t50 = r48;
            unsigned __int128 r51; bool o52 = __builtin_sub_overflow(t50, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r51);
            if (o52) gm_trap("integer overflow in `-`", gm_srcnqtpfma6qe4a, 100, 38);
            unsigned __int128 t53 = r51;
            unsigned __int128 t54 = (unsigned __int128)(t44 & t53);
            uint8_t t55 = 0;
            F.ret = t54;
            goto gm_fret;
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        unsigned __int128 t56 = gm_l3;
        uint8_t t57 = 0;
        F.ret = t56;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_uint_bq8c4b7 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    unsigned __int128 gm_l2; /* v */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* i */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 658u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
    }
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t3 = (t2 < ((uint64_t)16ULL) ? t2 : ((uint64_t)16ULL));
        gm_l3 = t3;
    }
    {
        void * t5 = gm_l0;
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t5) + 89));
        if (!t6) goto ie4;
        {
            uint64_t t9 = gm_l3;
            uint8_t t10 = ((t9 == ((uint64_t)16ULL)));
            if (!t10) goto ie8;
            {
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                void * t13 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                uint64_t t14 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t15 = (uintptr_t)((uint64_t)8ULL);
                if (t14 > t15 || t15 > (uintptr_t)t12) gm_trap_slice(t14, t15, t12, gm_srcnqtpfma6qe4a, 34, 39);
                gm_b16_8 t16;
                GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t13 + t14 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(t15 - t14));
                uint64_t t17 = gm_f_le64_7isadtr(t16);
                unsigned __int128 t18 = (unsigned __int128)t17;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                void * t20 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                uint64_t t21 = (uintptr_t)((uint64_t)8ULL);
                uint64_t t22 = (uintptr_t)((uint64_t)16ULL);
                if (t21 > t22 || t22 > (uintptr_t)t19) gm_trap_slice(t21, t22, t19, gm_srcnqtpfma6qe4a, 34, 62);
                gm_b16_8 t23;
                GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t20 + t21 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22 - t21));
                uint64_t t24 = gm_f_le64_7isadtr(t23);
                unsigned __int128 t25 = (unsigned __int128)t24;
                unsigned __int128 n26 = (unsigned __int128)((uint64_t)64ULL);
                if (n26 >= 128) gm_trap_shift((uint64_t)n26, "u128", gm_srcnqtpfma6qe4a, 34, 52);
                unsigned __int128 r27 = (unsigned __int128)((unsigned __int128)t25 << (int)n26);
                bool o28 = (unsigned __int128)(r27 >> (int)n26) != t25;
                if (o28) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 34, 52);
                unsigned __int128 t29 = r27;
                unsigned __int128 t30 = (unsigned __int128)(t18 | t29);
                uint8_t t31 = 0;
                F.ret = t30;
                goto gm_fret;
            }
            goto ix8;
ie8:;
ix8:;
        }
        {
            uint64_t t33 = gm_l3;
            uint8_t t34 = ((t33 == ((uint64_t)8ULL)));
            if (!t34) goto ie32;
            {
                gm_b16_8 t36 = gm_l1;
                uint64_t t37 = gm_f_le64_7isadtr(t36);
                unsigned __int128 t38 = (unsigned __int128)t37;
                uint8_t t39 = 0;
                F.ret = t38;
                goto gm_fret;
            }
            goto ix32;
ie32:;
ix32:;
        }
        {
            uint64_t t40 = gm_l3;
            gm_l4 = t40;
        }
        {
            for (;;) {
                uint64_t t42 = gm_l4;
                uint8_t t43 = (t42 > ((uint64_t)0ULL));
                if (!t43) goto lb41;
                {
                    uint64_t t46 = gm_l4;
                    uint64_t r47; bool o48 = __builtin_sub_overflow(t46, ((uint64_t)1ULL), &r47);
                    if (o48) gm_trap("integer overflow in `-`", gm_srcnqtpfma6qe4a, 38, 13);
                    uint64_t t49 = r47;
                    gm_l4 = t49;
                }
                {
                    unsigned __int128 t50 = gm_l2;
                    unsigned __int128 n51 = (unsigned __int128)((uint64_t)8ULL);
                    if (n51 >= 128) gm_trap_shift((uint64_t)n51, "u128", gm_srcnqtpfma6qe4a, 39, 18);
                    unsigned __int128 r52 = (unsigned __int128)((unsigned __int128)t50 << (int)n51);
                    bool o53 = (unsigned __int128)(r52 >> (int)n51) != t50;
                    if (o53) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 39, 18);
                    unsigned __int128 t54 = r52;
                    uint64_t t55 = gm_l4;
                    uint64_t t56 = (uintptr_t)t55;
                    uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    if (t56 >= (uintptr_t)(t57)) gm_trap_index(t56, t57, gm_srcnqtpfma6qe4a, 39, 33);
                    uint8_t t58 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t56 * 1));
                    unsigned __int128 t59 = (unsigned __int128)t58;
                    unsigned __int128 t60 = (unsigned __int128)(t54 | t59);
                    gm_l2 = t60;
                }
lc41:;
            }
lb41:;
        }
        goto ix4;
ie4:;
        {
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                uint64_t t63 = gm_l3;
                gm_l5 = t63;
            }
            {
                for (;;) {
                    uint64_t t65 = gm_l6;
                    uint64_t t66 = gm_l5;
                    uint8_t t67 = (t65 < t66);
                    if (!t67) goto lb64;
                    {
                        uint64_t t70 = gm_l6;
                        uint64_t t73 = GM_LD(uint64_t, ((uint8_t *)&t70));
                        gm_l7 = t73;
                        {
                            unsigned __int128 t75 = gm_l2;
                            unsigned __int128 n76 = (unsigned __int128)((uint64_t)8ULL);
                            if (n76 >= 128) gm_trap_shift((uint64_t)n76, "u128", gm_srcnqtpfma6qe4a, 42, 30);
                            unsigned __int128 r77 = (unsigned __int128)((unsigned __int128)t75 << (int)n76);
                            bool o78 = (unsigned __int128)(r77 >> (int)n76) != t75;
                            if (o78) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 42, 30);
                            unsigned __int128 t79 = r77;
                            uint64_t t80 = gm_l7;
                            uint64_t t81 = (uintptr_t)t80;
                            uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t81 >= (uintptr_t)(t82)) gm_trap_index(t81, t82, gm_srcnqtpfma6qe4a, 42, 45);
                            uint8_t t83 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t81 * 1));
                            unsigned __int128 t84 = (unsigned __int128)t83;
                            unsigned __int128 t85 = (unsigned __int128)(t79 | t84);
                            gm_l2 = t85;
                        }
                        goto me71;
mn72:;
                        gm_trap("no match arm matched the scrutinee", gm_srcnqtpfma6qe4a, 42, 9);
me71:;
                    }
lc64:;
                    {
                        uint64_t t87 = gm_l6;
                        uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                        uint64_t t90 = r88;
                        gm_l6 = t90;
                    }
                }
lb64:;
            }
        }
ix4:;
    }
    {
        unsigned __int128 t91 = gm_l2;
        uint8_t t92 = 0;
        F.ret = t91;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_info_ulcevdv F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* signed */
    uint16_t gm_l3; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 277u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 8));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_t53at80pfdus3 t5;
    gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
    gm_tg5lql6itufk7 t6;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    switch (t13) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma8:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t15 != ((uint8_t)0ULL)) goto mn14;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)&t5) + 8) + 0));
    gm_l2 = t16;
    uint16_t t17 = GM_LD(uint16_t, ((((uint8_t *)&t5) + 8) + 2));
    gm_l3 = t17;
    uint8_t t18 = gm_l2;
    uint16_t t19 = gm_l3;
    uint32_t t20 = (uint32_t)t19;
    gm_ttovhfee8mf28 t21;
    GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t21 + 0), t18);
    GM_ST(uint32_t, ((uint8_t *)&t21 + 4), t20);
    gm_tg5lql6itufk7 t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t22 + 10, 0);
    GM_ST(gm_ttovhfee8mf28, ((uint8_t *)&t22), t21);
    ((uint8_t *)&t22)[8] = 1;
    t6 = t22;
    goto me7;
mn14:;
ma9:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t24 != ((uint8_t)1ULL)) goto mn23;
    void * t25 = gm_l0;
    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 176));
    uint64_t r27; bool o28 = __builtin_mul_overflow(t26, ((uint64_t)8ULL), &r27);
    if (o28) gm_trap("integer overflow in `*`", gm_srcnqtpfma6qe4a, 23, 38);
    uint64_t t29 = r27;
    uint32_t t30 = (uint32_t)t29;
    gm_ttovhfee8mf28 t31;
    GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)0));
    GM_ST(uint32_t, ((uint8_t *)&t31 + 4), t30);
    gm_tg5lql6itufk7 t32;
    GM_ST(uint8_t, (uint8_t *)&t32 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 10, 0);
    GM_ST(gm_ttovhfee8mf28, ((uint8_t *)&t32), t31);
    ((uint8_t *)&t32)[8] = 1;
    t6 = t32;
    goto me7;
mn23:;
ma10:;
    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t34 != ((uint8_t)2ULL)) goto mn33;
    void * t35 = gm_l0;
    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 176));
    uint64_t r37; bool o38 = __builtin_mul_overflow(t36, ((uint64_t)8ULL), &r37);
    if (o38) gm_trap("integer overflow in `*`", gm_srcnqtpfma6qe4a, 24, 37);
    uint64_t t39 = r37;
    uint32_t t40 = (uint32_t)t39;
    gm_ttovhfee8mf28 t41;
    GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)1));
    GM_ST(uint32_t, ((uint8_t *)&t41 + 4), t40);
    gm_tg5lql6itufk7 t42;
    GM_ST(uint8_t, (uint8_t *)&t42 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t42 + 10, 0);
    GM_ST(gm_ttovhfee8mf28, ((uint8_t *)&t42), t41);
    ((uint8_t *)&t42)[8] = 1;
    t6 = t42;
    goto me7;
mn33:;
ma11:;
    gm_tg5lql6itufk7 t44;
    memset(&t44, 0, sizeof t44);
    t6 = t44;
    goto me7;
mn43:;
ma12:;
    gm_trap("no match arm matched the scrutinee", gm_srcnqtpfma6qe4a, 21, 75);
me7:;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_packed_8fhislp(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t53at80pfdus3 gm_f_kind_htv6093_c(void * a0, gm_csguankko4lmhd a1) { gm_t53at80pfdus3 gm_r; gm_f_kind_htv6093(&gm_r, a0, a1); return gm_r; }
extern __attribute__((aligned(4))) uint64_t gm_f_le64_7isadtr(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_load_word_ud3iuha(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    struct gm_fr_load_word_ud3iuha F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint64_t gm_l2; /* addr */
    uint64_t gm_l3; /* align */
    uint64_t gm_l4; /* size */
    gm_b16_8 gm_l6; /* b */
    GM_STACK_CHECK(&F, 281u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint64_t t3 = gm_l4;
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, ((uint64_t)0ULL));
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 184);
        uint64_t t8 = gm_l2;
        uint64_t t9 = gm_l4;
        uint64_t t10 = gm_l3;
        gm_t934k1j7u70ct t11;
        gm_f_read_u6r4vns((void *)&t11, t7, t8, t9, t10);
        gm_b16_8 t12;
        if (!((((uint8_t *)&t11))[0] != 0)) {
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
            t12 = t13;
        } else {
            F.l5 = (*(gm_tpekhfs32op0f *)(((uint8_t *)&t11) + 8));
            void * t14 = gm_l0;
            gm_tl874t2atnlh3 t15 = gm_l1;
            gm_t76123gfet73g t16;
            gm_f_mem_trap_ecogko0((void *)&t16, (void *)NULL, cx, NULL, t14, t15, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l5)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t16);
            goto gm_fdone;
        }
        gm_l6 = t12;
    }
    {
        void * t17 = gm_l0;
        gm_b16_8 t18 = gm_l6;
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        uint64_t t20 = gm_f_rd_word_8flcq6o(t17, t19);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t20);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_res_085nq6f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint8_t a3, gm_b16_8 a4) {
    struct gm_fr_make_res_085nq6f F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* dst */
    uint8_t gm_l3; /* is_err */
    gm_b16_8 gm_l4; /* payload */
    gm_b16_8 gm_l5; /* offs */
    uint64_t gm_l6; /* off */
    uint64_t gm_l7; /* n */
    GM_STACK_CHECK(&F, 405u);
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
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t4;
    }
    {
        gm_b16_8 t5 = gm_l2;
        gm_f_fill_abmf5g5(t5, ((uint8_t)0ULL));
    }
    {
        gm_b16_8 t6 = gm_l2;
        gm_b16_8 t7 = t6;
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srcnqtpfma6qe4a, 187, 12);
        uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t8 * 8));
        uint64_t t11 = gm_f_us_cuv01fp(t10);
        uint64_t t12 = (uintptr_t)t11;
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&t7) + 8);
        if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_srcnqtpfma6qe4a, 187, 5);
        uint8_t *t14 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t7)) + t12 * 1);
        uint8_t t15;
        uint8_t t17 = gm_l3;
        if (!t17) goto ie16;
        uint8_t t19 = ((uint8_t)1ULL);
        t15 = t19;
        goto ix16;
ie16:;
        uint8_t t21 = ((uint8_t)0ULL);
        t15 = t21;
ix16:;
        GM_ST(uint8_t, t14, t15);
    }
    {
        uint64_t t22 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_srcnqtpfma6qe4a, 188, 15);
        uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t22 * 8));
        uint64_t t25 = gm_f_us_cuv01fp(t24);
        gm_l6 = t25;
    }
    {
        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint64_t t28 = gm_l6;
        uint64_t r29; bool o30 = __builtin_sub_overflow(t27, t28, &r29);
        if (o30) gm_trap("integer overflow in `-`", gm_srcnqtpfma6qe4a, 189, 26);
        uint64_t t31 = r29;
        uint64_t t32 = (t26 < t31 ? t26 : t31);
        gm_l7 = t32;
    }
    {
        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t34 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t35 = gm_l6;
        uint64_t t36 = (uintptr_t)t35;
        uint64_t t37 = gm_l6;
        uint64_t t38 = gm_l7;
        uint64_t r39; bool o40 = __builtin_add_overflow(t37, t38, &r39);
        if (o40) gm_trap("integer overflow in `+`", gm_srcnqtpfma6qe4a, 190, 14);
        uint64_t t41 = r39;
        uint64_t t42 = (uintptr_t)t41;
        if (t36 > t42 || t42 > (uintptr_t)t33) gm_trap_slice(t36, t42, t33, gm_srcnqtpfma6qe4a, 190, 5);
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(((uint8_t *)t34 + t36 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(t42 - t36));
        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        void * t45 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
        uint64_t t46 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t47 = gm_l7;
        uint64_t t48 = (uintptr_t)t47;
        if (t46 > t48 || t48 > (uintptr_t)t44) gm_trap_slice(t46, t48, t44, gm_srcnqtpfma6qe4a, 190, 33);
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(((uint8_t *)t45 + t46 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(t48 - t46));
        gm_f_copy_from_f7j6p05(t43, t49);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_make_some_tvb9ubk F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* dst */
    gm_b16_8 gm_l3; /* payload */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    uint64_t gm_l5; /* n */
    gm_b16_8 gm_l6; /* offs */
    uint64_t gm_l7; /* off */
    uint64_t gm_l8; /* n */
    GM_STACK_CHECK(&F, 578u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t2, GM_CONV(gm_csguankko4lmhd, t3)));
        gm_l4 = t4;
    }
    {
        gm_b16_8 t5 = gm_l2;
        gm_f_fill_abmf5g5(t5, ((uint8_t)0ULL));
    }
    {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 8));
        gm_t88rcucg1b7uk t9 = gm_l4;
        uint8_t t10 = gm_f_nullable_pointer_7qfmn6o(t8, GM_CONV(gm_csguankko4lmhd, t9));
        if (!t10) goto ie6;
        {
            uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t t14 = (t12 < t13 ? t12 : t13);
            gm_l5 = t14;
        }
        {
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            void * t16 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
            uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t18 = gm_l5;
            uint64_t t19 = (uintptr_t)t18;
            if (t17 > t19 || t19 > (uintptr_t)t15) gm_trap_slice(t17, t19, t15, gm_srcnqtpfma6qe4a, 159, 9);
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(((uint8_t *)t16 + t17 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(t19 - t17));
            uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
            uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t24 = gm_l5;
            uint64_t t25 = (uintptr_t)t24;
            if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_srcnqtpfma6qe4a, 159, 29);
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
            gm_f_copy_from_f7j6p05(t20, t26);
        }
        {
            gm_b0_1 t27;
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t29 = gm_l0;
        gm_t88rcucg1b7uk t30 = gm_l1;
        gm_b16_8 t31 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t31;
    }
    {
        uint64_t t32 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
        if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 163, 15);
        uint64_t t34 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t32 * 8));
        uint64_t t35 = gm_f_us_cuv01fp(t34);
        gm_l7 = t35;
    }
    {
        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint64_t t38 = gm_l7;
        uint64_t r39; bool o40 = __builtin_sub_overflow(t37, t38, &r39);
        if (o40) gm_trap("integer overflow in `-`", gm_srcnqtpfma6qe4a, 164, 26);
        uint64_t t41 = r39;
        uint64_t t42 = (t36 < t41 ? t36 : t41);
        gm_l8 = t42;
    }
    {
        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t44 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t45 = gm_l7;
        uint64_t t46 = (uintptr_t)t45;
        uint64_t t47 = gm_l7;
        uint64_t t48 = gm_l8;
        uint64_t r49; bool o50 = __builtin_add_overflow(t47, t48, &r49);
        if (o50) gm_trap("integer overflow in `+`", gm_srcnqtpfma6qe4a, 165, 14);
        uint64_t t51 = r49;
        uint64_t t52 = (uintptr_t)t51;
        if (t46 > t52 || t52 > (uintptr_t)t43) gm_trap_slice(t46, t52, t43, gm_srcnqtpfma6qe4a, 165, 5);
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(((uint8_t *)t44 + t46 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(t52 - t46));
        uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t55 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t56 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t57 = gm_l8;
        uint64_t t58 = (uintptr_t)t57;
        if (t56 > t58 || t58 > (uintptr_t)t54) gm_trap_slice(t56, t58, t54, gm_srcnqtpfma6qe4a, 165, 33);
        gm_b16_8 t59;
        GM_ST(void *, (uint8_t *)&t59, (void *)(((uint8_t *)t55 + t56 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(t58 - t56));
        gm_f_copy_from_f7j6p05(t53, t59);
    }
    {
        gm_b16_8 t60 = gm_l2;
        gm_b16_8 t61 = t60;
        uint64_t t62 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
        if (t62 >= (uintptr_t)(t63)) gm_trap_index(t62, t63, gm_srcnqtpfma6qe4a, 166, 12);
        uint64_t t64 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t62 * 8));
        uint64_t t65 = gm_f_us_cuv01fp(t64);
        uint64_t t66 = (uintptr_t)t65;
        uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&t61) + 8);
        if (t66 >= (uintptr_t)(t67)) gm_trap_index(t66, t67, gm_srcnqtpfma6qe4a, 166, 5);
        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t61)) + t66 * 1), ((uint8_t)1ULL));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_variant_bn3d74n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4) {
    struct gm_fr_make_variant_bn3d74n F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* dst */
    uint32_t gm_l3; /* variant */
    gm_b16_8 gm_l4; /* payload */
    gm_tcmkompbl09il gm_l5; /* u */
    uint64_t gm_l6; /* n */
    uint64_t gm_l7; /* off */
    uint64_t gm_l8; /* k */
    GM_STACK_CHECK(&F, 602u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_b16_8 t2 = gm_l2;
        gm_f_fill_abmf5g5(t2, ((uint8_t)0ULL));
    }
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t57euh612ligr t5;
        gm_f_union_def_h8mvf4t((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_tcmkompbl09il t6;
        if (((((uint8_t *)&t5))[24] != 0)) {
            t6 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t5) + 0));
        } else {
            gm_b0_1 t7;
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l5 = t6;
    }
    {
        void * t9 = gm_l0;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 0));
        uint64_t t11 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t12 = gm_f_us_cuv01fp(t11);
        gm_l6 = t12;
    }
    {
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t15 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t16 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t17 = gm_l6;
        uint64_t t18 = (uintptr_t)t17;
        if (t16 > t18 || t18 > (uintptr_t)t14) gm_trap_slice(t16, t18, t14, gm_srcnqtpfma6qe4a, 239, 16);
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t15 + t16 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(t18 - t16));
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
        gm_b16_8 t21 = t20;
        uint32_t t22 = gm_l3;
        uint64_t t23 = (uint64_t)t22;
        uint64_t t24 = gm_f_us_cuv01fp(t23);
        uint64_t t25 = (uintptr_t)t24;
        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&t21) + 8);
        if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srcnqtpfma6qe4a, 239, 42);
        gm_tbci5ospk5nnn t27 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t21)) + t25 * 72) + 24));
        unsigned __int128 t28 = gm_f_bits128_jigkui4(&t27);
        gm_f_put_uint_cddin99(t13, t19, t28);
    }
    {
        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t31 = (t30 > ((uint64_t)0ULL));
        if (!t31) goto ie29;
        {
            void * t33 = gm_l0;
            gm_t88rcucg1b7uk t34 = gm_l1;
            uint64_t t35 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t33, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t36 = gm_f_us_cuv01fp(t35);
            gm_l7 = t36;
        }
        {
            uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t t39 = gm_l7;
            uint64_t r40; bool o41 = __builtin_sub_overflow(t38, t39, &r40);
            if (o41) gm_trap("integer overflow in `-`", gm_srcnqtpfma6qe4a, 242, 30);
            uint64_t t42 = r40;
            uint64_t t43 = (t37 < t42 ? t37 : t42);
            gm_l8 = t43;
        }
        {
            uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            void * t45 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
            uint64_t t46 = gm_l7;
            uint64_t t47 = (uintptr_t)t46;
            uint64_t t48 = gm_l7;
            uint64_t t49 = gm_l8;
            uint64_t r50; bool o51 = __builtin_add_overflow(t48, t49, &r50);
            if (o51) gm_trap("integer overflow in `+`", gm_srcnqtpfma6qe4a, 243, 18);
            uint64_t t52 = r50;
            uint64_t t53 = (uintptr_t)t52;
            if (t47 > t53 || t53 > (uintptr_t)t44) gm_trap_slice(t47, t53, t44, gm_srcnqtpfma6qe4a, 243, 9);
            gm_b16_8 t54;
            GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t45 + t47 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53 - t47));
            uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            void * t56 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
            uint64_t t57 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t58 = gm_l8;
            uint64_t t59 = (uintptr_t)t58;
            if (t57 > t59 || t59 > (uintptr_t)t55) gm_trap_slice(t57, t59, t55, gm_srcnqtpfma6qe4a, 243, 37);
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(((uint8_t *)t56 + t57 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(t59 - t57));
            gm_f_copy_from_f7j6p05(t54, t60);
        }
        goto ix29;
ie29:;
ix29:;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_opt_payload_1pbnobb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_opt_payload_1pbnobb F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    gm_t88rcucg1b7uk gm_l3; /* inner */
    uint64_t gm_l4; /* n */
    uint64_t gm_l5; /* off */
    GM_STACK_CHECK(&F, 343u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t2, GM_CONV(gm_csguankko4lmhd, t3)));
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l3;
        uint64_t t7 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t8 = gm_f_us_cuv01fp(t7);
        gm_l4 = t8;
    }
    {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 8));
        gm_t88rcucg1b7uk t12 = gm_l3;
        uint8_t t13 = gm_f_nullable_pointer_7qfmn6o(t11, GM_CONV(gm_csguankko4lmhd, t12));
        if (!t13) goto ie9;
        {
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            void * t16 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
            uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t18 = gm_l4;
            uint64_t t19 = (uintptr_t)t18;
            if (t17 > t19 || t19 > (uintptr_t)t15) gm_trap_slice(t17, t19, t15, gm_srcnqtpfma6qe4a, 148, 51);
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(((uint8_t *)t16 + t17 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(t19 - t17));
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        void * t23 = gm_l0;
        gm_t88rcucg1b7uk t24 = gm_l1;
        gm_b16_8 t25 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&t25) + 8);
        if (t22 >= (uintptr_t)(t26)) gm_trap_index(t22, t26, gm_srcnqtpfma6qe4a, 149, 15);
        uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t25)) + t22 * 8));
        uint64_t t28 = gm_f_us_cuv01fp(t27);
        gm_l5 = t28;
    }
    {
        uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t30 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t31 = gm_l5;
        uint64_t t32 = (uintptr_t)t31;
        uint64_t t33 = gm_l5;
        uint64_t t34 = gm_l4;
        uint64_t r35; bool o36 = __builtin_add_overflow(t33, t34, &r35);
        if (o36) gm_trap("integer overflow in `+`", gm_srcnqtpfma6qe4a, 150, 19);
        uint64_t t37 = r35;
        uint64_t t38 = (uintptr_t)t37;
        if (t32 > t38 || t38 > (uintptr_t)t29) gm_trap_slice(t32, t38, t29, gm_srcnqtpfma6qe4a, 150, 12);
        gm_b16_8 t39;
        GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t30 + t32 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(t38 - t32));
        uint8_t t40 = 0;
        F.ret = t39;
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
inline __attribute__((aligned(4))) uint8_t gm_f_opt_present_lb4i2ip(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_opt_present_lb4i2ip F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    gm_t88rcucg1b7uk gm_l3; /* inner */
    GM_STACK_CHECK(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t2, GM_CONV(gm_csguankko4lmhd, t3)));
        gm_l3 = t4;
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 8));
        gm_t88rcucg1b7uk t8 = gm_l3;
        uint8_t t9 = gm_f_nullable_pointer_7qfmn6o(t7, GM_CONV(gm_csguankko4lmhd, t8));
        if (!t9) goto ie5;
        {
            void * t11 = gm_l0;
            gm_b16_8 t12 = gm_l2;
            uint64_t t13 = gm_f_ptr_of_b77b4c9(t11, t12);
            uint8_t t14 = (!(t13 == ((uint64_t)0ULL)));
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_b16_8 t16 = gm_l2;
        gm_b16_8 t17 = t16;
        uint64_t t18 = (uintptr_t)((uint64_t)1ULL);
        void * t19 = gm_l0;
        gm_t88rcucg1b7uk t20 = gm_l1;
        gm_b16_8 t21 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&t21) + 8);
        if (t18 >= (uintptr_t)(t22)) gm_trap_index(t18, t22, gm_srcnqtpfma6qe4a, 141, 17);
        uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t21)) + t18 * 8));
        uint64_t t24 = gm_f_us_cuv01fp(t23);
        uint64_t t25 = (uintptr_t)t24;
        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&t17) + 8);
        if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srcnqtpfma6qe4a, 141, 12);
        uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t17)) + t25 * 1));
        uint8_t t28 = (!(t27 == ((uint8_t)0ULL)));
        uint8_t t29 = 0;
        F.ret = t28;
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
extern __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) void gm_f_put_le64_uc3t56q(gm_b16_8 a0, uint64_t a1);
extern __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2);
inline __attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_slice_ko14loi F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* ptr */
    uint64_t gm_l3; /* len */
    uint64_t gm_l4; /* p */
    GM_STACK_CHECK(&F, 232u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 176));
        uint64_t t4 = gm_f_us_cuv01fp(t3);
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t7 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t9 = gm_l4;
        uint64_t t10 = (uintptr_t)t9;
        if (t8 > t10 || t10 > (uintptr_t)t6) gm_trap_slice(t8, t10, t6, gm_srcnqtpfma6qe4a, 116, 15);
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t7 + t8 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10 - t8));
        uint64_t t12 = gm_l2;
        gm_f_put_u64_irp77r2(t5, t11, t12);
    }
    {
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t15 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t16 = gm_l4;
        uint64_t t17 = (uintptr_t)t16;
        uint64_t t18 = gm_l4;
        uint64_t r19; bool o20 = __builtin_mul_overflow(((uint64_t)2ULL), t18, &r19);
        if (o20) gm_trap("integer overflow in `*`", gm_srcnqtpfma6qe4a, 117, 20);
        uint64_t t21 = r19;
        uint64_t t22 = (uintptr_t)t21;
        if (t17 > t22 || t22 > (uintptr_t)t14) gm_trap_slice(t17, t22, t14, gm_srcnqtpfma6qe4a, 117, 15);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t15 + t17 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22 - t17));
        uint64_t t24 = gm_l3;
        gm_f_put_u64_irp77r2(t13, t23, t24);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_uint_cddin99 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    unsigned __int128 gm_l2; /* v */
    uint64_t gm_l3; /* n */
    unsigned __int128 gm_l4; /* x */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint64_t gm_l8; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 593u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t3 = (t2 < ((uint64_t)16ULL) ? t2 : ((uint64_t)16ULL));
        gm_l3 = t3;
    }
    {
        unsigned __int128 t4 = gm_l2;
        gm_l4 = t4;
    }
    {
        void * t6 = gm_l0;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t6) + 89));
        if (!t7) goto ie5;
        {
            uint64_t t10 = gm_l3;
            uint8_t t11 = ((t10 == ((uint64_t)16ULL)));
            if (!t11) goto ie9;
            {
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                void * t14 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                uint64_t t15 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t16 = (uintptr_t)((uint64_t)8ULL);
                if (t15 > t16 || t16 > (uintptr_t)t13) gm_trap_slice(t15, t16, t13, gm_srcnqtpfma6qe4a, 53, 22);
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t14 + t15 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(t16 - t15));
                unsigned __int128 t18 = gm_l2;
                uint64_t t19 = (uint64_t)t18;
                gm_f_put_le64_uc3t56q(t17, t19);
            }
            {
                uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                void * t21 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                uint64_t t22 = (uintptr_t)((uint64_t)8ULL);
                uint64_t t23 = (uintptr_t)((uint64_t)16ULL);
                if (t22 > t23 || t23 > (uintptr_t)t20) gm_trap_slice(t22, t23, t20, gm_srcnqtpfma6qe4a, 54, 22);
                gm_b16_8 t24;
                GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t21 + t22 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23 - t22));
                unsigned __int128 t25 = gm_l2;
                unsigned __int128 n26 = (unsigned __int128)((uint64_t)64ULL);
                if (n26 >= 128) gm_trap_shift((uint64_t)n26, "u128", gm_srcnqtpfma6qe4a, 54, 33);
                unsigned __int128 t27 = (unsigned __int128)(t25 >> (int)n26);
                uint64_t t28 = (uint64_t)t27;
                gm_f_put_le64_uc3t56q(t24, t28);
            }
            {
                gm_b0_1 t29;
                uint8_t t30 = 0;
                F.ret = t29;
                goto gm_fret;
            }
            goto ix9;
ie9:;
ix9:;
        }
        {
            uint64_t t32 = gm_l3;
            uint8_t t33 = ((t32 == ((uint64_t)8ULL)));
            if (!t33) goto ie31;
            {
                gm_b16_8 t35 = gm_l1;
                unsigned __int128 t36 = gm_l2;
                uint64_t t37 = (uint64_t)t36;
                gm_f_put_le64_uc3t56q(t35, t37);
            }
            {
                gm_b0_1 t38;
                uint8_t t39 = 0;
                F.ret = t38;
                goto gm_fret;
            }
            goto ix31;
ie31:;
ix31:;
        }
        {
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                uint64_t t41 = gm_l3;
                gm_l5 = t41;
            }
            {
                for (;;) {
                    uint64_t t43 = gm_l6;
                    uint64_t t44 = gm_l5;
                    uint8_t t45 = (t43 < t44);
                    if (!t45) goto lb42;
                    {
                        uint64_t t48 = gm_l6;
                        uint64_t t51 = GM_LD(uint64_t, ((uint8_t *)&t48));
                        gm_l7 = t51;
                        {
                            uint64_t t53 = gm_l7;
                            uint64_t t54 = (uintptr_t)t53;
                            uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t54 >= (uintptr_t)(t55)) gm_trap_index(t54, t55, gm_srcnqtpfma6qe4a, 62, 13);
                            unsigned __int128 t56 = gm_l4;
                            uint8_t t57 = (uint8_t)t56;
                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t54 * 1), t57);
                        }
                        {
                            unsigned __int128 t58 = gm_l4;
                            unsigned __int128 n59 = (unsigned __int128)((uint64_t)8ULL);
                            if (n59 >= 128) gm_trap_shift((uint64_t)n59, "u128", gm_srcnqtpfma6qe4a, 63, 13);
                            unsigned __int128 t60 = (unsigned __int128)(t58 >> (int)n59);
                            gm_l4 = t60;
                        }
                        goto me49;
mn50:;
                        gm_trap("no match arm matched the scrutinee", gm_srcnqtpfma6qe4a, 61, 9);
me49:;
                    }
lc42:;
                    {
                        uint64_t t62 = gm_l6;
                        uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                        uint64_t t65 = r63;
                        gm_l6 = t65;
                    }
                }
lb42:;
            }
        }
        goto ix5;
ie5:;
        {
            uint64_t t67 = gm_l3;
            gm_l8 = t67;
        }
        {
            for (;;) {
                uint64_t t69 = gm_l8;
                uint8_t t70 = (t69 > ((uint64_t)0ULL));
                if (!t70) goto lb68;
                {
                    uint64_t t73 = gm_l8;
                    uint64_t r74; bool o75 = __builtin_sub_overflow(t73, ((uint64_t)1ULL), &r74);
                    if (o75) gm_trap("integer overflow in `-`", gm_srcnqtpfma6qe4a, 68, 13);
                    uint64_t t76 = r74;
                    gm_l8 = t76;
                }
                {
                    uint64_t t77 = gm_l8;
                    uint64_t t78 = (uintptr_t)t77;
                    uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    if (t78 >= (uintptr_t)(t79)) gm_trap_index(t78, t79, gm_srcnqtpfma6qe4a, 69, 13);
                    unsigned __int128 t80 = gm_l4;
                    uint8_t t81 = (uint8_t)t80;
                    GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t78 * 1), t81);
                }
                {
                    unsigned __int128 t82 = gm_l4;
                    unsigned __int128 n83 = (unsigned __int128)((uint64_t)8ULL);
                    if (n83 >= 128) gm_trap_shift((uint64_t)n83, "u128", gm_srcnqtpfma6qe4a, 70, 13);
                    unsigned __int128 t84 = (unsigned __int128)(t82 >> (int)n83);
                    gm_l4 = t84;
                }
lc68:;
            }
lb68:;
        }
ix5:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_put_word_tfn443i(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_word_tfn443i F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* v */
    uint64_t gm_l4; /* p */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 176));
        uint64_t t4 = gm_f_us_cuv01fp(t3);
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t7 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t8 = gm_l2;
        uint64_t t9 = gm_l4;
        uint64_t r10; bool o11 = __builtin_mul_overflow(t8, t9, &r10);
        if (o11) gm_trap("integer overflow in `*`", gm_srcnqtpfma6qe4a, 128, 17);
        uint64_t t12 = r10;
        uint64_t t13 = (uintptr_t)t12;
        uint64_t t14 = gm_l2;
        uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcnqtpfma6qe4a, 128, 25);
        uint64_t t17 = r15;
        uint64_t t18 = gm_l4;
        uint64_t r19; bool o20 = __builtin_mul_overflow(t17, t18, &r19);
        if (o20) gm_trap("integer overflow in `*`", gm_srcnqtpfma6qe4a, 128, 24);
        uint64_t t21 = r19;
        uint64_t t22 = (uintptr_t)t21;
        if (t13 > t22 || t22 > (uintptr_t)t6) gm_trap_slice(t13, t22, t6, gm_srcnqtpfma6qe4a, 128, 15);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t7 + t13 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22 - t13));
        uint64_t t24 = gm_l3;
        gm_f_put_u64_irp77r2(t5, t23, t24);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_read_field_ua0742c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4) {
    struct gm_fr_read_field_ua0742c F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    uint32_t gm_l3; /* i */
    gm_b16_8 gm_l4; /* dst */
    gm_t88rcucg1b7uk gm_l5; /* u */
    gm_tplpbqf3duvn3 gm_l6; /* bf */
    unsigned __int128 gm_l7; /* whole */
    uint32_t gm_l8; /* w */
    unsigned __int128 gm_l9; /* raw */
    GM_STACK_CHECK(&F, 606u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_tkn42u7na3t81 t5 = gm_f_bits_struct_ty_22ue20s(t3, GM_CONV(gm_csguankko4lmhd, t4));
        if (!((((uint8_t *)&t5))[4] != 0)) goto ie2;
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t5));
        gm_l5 = t6;
        {
            void * t8 = gm_l0;
            gm_t88rcucg1b7uk t9 = gm_l1;
            uint32_t t10 = gm_l3;
            gm_tplpbqf3duvn3 t11 = gm_f_bits_field_vqua3kd((void *)NULL, cx, NULL, t8, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t11;
        }
        {
            void * t12 = gm_l0;
            gm_b16_8 t13 = gm_l2;
            uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&t13) + 8);
            void * t15 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t13)));
            uint64_t t16 = (uintptr_t)((uint64_t)0ULL);
            void * t17 = gm_l0;
            gm_t88rcucg1b7uk t18 = gm_l5;
            uint64_t t19 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t20 = gm_f_us_cuv01fp(t19);
            uint64_t t21 = (uintptr_t)t20;
            if (t16 > t21 || t21 > (uintptr_t)t14) gm_trap_slice(t16, t21, t14, gm_srcnqtpfma6qe4a, 303, 29);
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t15 + t16 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(t21 - t16));
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
            unsigned __int128 t24 = gm_f_get_uint_bq8c4b7(t12, t23);
            uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
            unsigned __int128 t26 = (unsigned __int128)t25;
            unsigned __int128 n27 = (unsigned __int128)t26;
            if (n27 >= 128) gm_trap_shift((uint64_t)n27, "u128", gm_srcnqtpfma6qe4a, 303, 18);
            unsigned __int128 t28 = (unsigned __int128)(t24 >> (int)n27);
            gm_l7 = t28;
        }
        {
            void * t29 = gm_l0;
            gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 4));
            uint32_t t31 = gm_f_bit_width_0fd5f52((void *)NULL, cx, NULL, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t31;
        }
        {
            unsigned __int128 t32;
            uint32_t t34 = gm_l8;
            uint8_t t35 = (t34 >= ((uint32_t)128ULL));
            if (!t35) goto ie33;
            unsigned __int128 t37 = gm_l7;
            unsigned __int128 t38 = t37;
            t32 = t38;
            goto ix33;
ie33:;
            unsigned __int128 t40 = gm_l7;
            uint32_t t41 = gm_l8;
            unsigned __int128 t42 = (unsigned __int128)t41;
            unsigned __int128 n43 = (unsigned __int128)t42;
            if (n43 >= 128) gm_trap_shift((uint64_t)n43, "u128", gm_srcnqtpfma6qe4a, 305, 55);
            unsigned __int128 r44 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n43);
            bool o45 = (unsigned __int128)(r44 >> (int)n43) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
            if (o45) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 305, 55);
            unsigned __int128 t46 = r44;
            unsigned __int128 r47; bool o48 = __builtin_sub_overflow(t46, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r47);
            if (o48) gm_trap("integer overflow in `-`", gm_srcnqtpfma6qe4a, 305, 54);
            unsigned __int128 t49 = r47;
            unsigned __int128 t50 = (unsigned __int128)(t40 & t49);
            unsigned __int128 t51 = t50;
            t32 = t51;
ix33:;
            gm_l9 = t32;
        }
        {
            void * t52 = gm_l0;
            gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 4));
            unsigned __int128 t54 = gm_l9;
            gm_b16_8 t55 = gm_l4;
            gm_b0_1 t56 = gm_f_bits_from_raw_el4tjdh((void *)NULL, cx, NULL, t52, t53, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t57;
            uint8_t t58 = 0;
            F.ret = t57;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t59 = gm_l4;
        void * t60 = gm_l0;
        gm_t88rcucg1b7uk t61 = gm_l1;
        gm_b16_8 t62 = gm_l2;
        uint32_t t63 = gm_l3;
        gm_b16_8 t64 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t60, t61, t62, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
        gm_f_copy_from_f7j6p05(t59, t65);
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
extern __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_res_payload_pb2hcmh F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    gm_tgglga8ro3isc gm_l3; /* rt */
    uint64_t gm_l4; /* n */
    uint64_t gm_l5; /* off */
    GM_STACK_CHECK(&F, 334u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_tgglga8ro3isc t4 = gm_f_res_types_pg7rvck(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6;
        void * t8 = gm_l0;
        gm_t88rcucg1b7uk t9 = gm_l1;
        gm_b16_8 t10 = gm_l2;
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        uint8_t t12 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t8, t9, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t12) goto ie7;
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 0));
        gm_t88rcucg1b7uk t15 = t14;
        t6 = t15;
        goto ix7;
ie7:;
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 4));
        gm_t88rcucg1b7uk t18 = t17;
        t6 = t18;
ix7:;
        uint64_t t19 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t20 = gm_f_us_cuv01fp(t19);
        gm_l4 = t20;
    }
    {
        uint64_t t21 = (uintptr_t)((uint64_t)1ULL);
        void * t22 = gm_l0;
        gm_t88rcucg1b7uk t23 = gm_l1;
        gm_b16_8 t24 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&t24) + 8);
        if (t21 >= (uintptr_t)(t25)) gm_trap_index(t21, t25, gm_srcnqtpfma6qe4a, 180, 15);
        uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t24)) + t21 * 8));
        uint64_t t27 = gm_f_us_cuv01fp(t26);
        gm_l5 = t27;
    }
    {
        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t30 = gm_l5;
        uint64_t t31 = (uintptr_t)t30;
        uint64_t t32 = gm_l5;
        uint64_t t33 = gm_l4;
        uint64_t r34; bool o35 = __builtin_add_overflow(t32, t33, &r34);
        if (o35) gm_trap("integer overflow in `+`", gm_srcnqtpfma6qe4a, 181, 19);
        uint64_t t36 = r34;
        uint64_t t37 = (uintptr_t)t36;
        if (t31 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t31, t37, t28, gm_srcnqtpfma6qe4a, 181, 12);
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t31 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t31));
        uint8_t t39 = 0;
        F.ret = t38;
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
inline __attribute__((aligned(4))) gm_tgglga8ro3isc gm_f_res_types_pg7rvck(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_res_types_pg7rvck F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* err */
    gm_t88rcucg1b7uk gm_l3; /* ok */
    GM_STACK_CHECK_SHALLOW(&F, 147u);
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
        if (t6 != ((uint8_t)14ULL)) goto ie2;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 0));
        gm_l2 = t7;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l3 = t8;
        {
            gm_t88rcucg1b7uk t10 = gm_l2;
            gm_t88rcucg1b7uk t11 = gm_l3;
            gm_tgglga8ro3isc t12;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t12 + 0), t10);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t12 + 4), t11);
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_t88rcucg1b7uk t15 = gm_l1;
            gm_t88rcucg1b7uk t16 = gm_l1;
            gm_tgglga8ro3isc t17;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t17 + 0), t15);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t17 + 4), t16);
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_hr4b76n F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK(&F, 299u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        uint64_t t3 = (uint64_t)t2;
        gm_l2 = t3;
    }
    {
        uint64_t t5 = gm_l2;
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 496) + 0) + 8);
        uint8_t t8 = (t5 < t7);
        if (!t8) goto ie4;
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 496) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcnqtpfma6qe4a, 9, 34);
        uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t11) + 496) + 0)) + t10 * 8));
        uint64_t t14 = (uint64_t)~((uint64_t)0ULL);
        uint8_t t15 = (!(t13 == t14));
        if (!t15) goto ie4;
        {
            uint64_t t17 = gm_l2;
            uint64_t t18 = (uintptr_t)t17;
            void * t19 = gm_l0;
            uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 496) + 0) + 8);
            if (t18 >= (uintptr_t)(t20)) gm_trap_index(t18, t20, gm_srcnqtpfma6qe4a, 9, 75);
            uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 496) + 0)) + t18 * 8));
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        for (;;) {
            void * t24 = gm_l0;
            uint64_t t25 = GM_LD(uintptr_t, ((((uint8_t *)t24) + 496) + 0) + 8);
            uint64_t t26 = gm_l2;
            uint8_t t27 = (t25 <= t26);
            if (!t27) goto lb23;
            {
                void * t30 = gm_l0;
                void * t31 = (void *)(((uint8_t *)t30) + 496);
                uint64_t t32 = (uint64_t)~((uint64_t)0ULL);
                gm_tftb1odc739t5 t33 = gm_f_push_7o6oubd(t31, t32);
                if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcnqtpfma6qe4a, 10, 36);
                gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
            }
lc23:;
        }
lb23:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = (void *)(((uint8_t *)t35) + 16);
        gm_t88rcucg1b7uk t37 = gm_l1;
        uint64_t t38 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t38;
    }
    {
        uint64_t t39 = gm_l2;
        uint64_t t40 = (uintptr_t)t39;
        void * t41 = gm_l0;
        uint64_t t42 = GM_LD(uintptr_t, ((((uint8_t *)t41) + 496) + 0) + 8);
        if (t40 >= (uintptr_t)(t42)) gm_trap_index(t40, t42, gm_srcnqtpfma6qe4a, 12, 5);
        uint64_t t43 = gm_l3;
        GM_ST(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t41) + 496) + 0)) + t40 * 8), t43);
    }
    {
        uint64_t t44 = gm_l3;
        uint8_t t45 = 0;
        F.ret = t44;
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
inline __attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_slice_parts_1se5hu4 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* p */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 176));
        uint64_t t4 = gm_f_us_cuv01fp(t3);
        gm_l2 = t4;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t7 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        if (t8 > t10 || t10 > (uintptr_t)t6) gm_trap_slice(t8, t10, t6, gm_srcnqtpfma6qe4a, 111, 28);
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t7 + t8 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10 - t8));
        uint64_t t12 = gm_f_get_u64_tgpb2vh(t5, t11);
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t15 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t16 = gm_l2;
        uint64_t t17 = (uintptr_t)t16;
        uint64_t t18 = gm_l2;
        uint64_t r19; bool o20 = __builtin_mul_overflow(((uint64_t)2ULL), t18, &r19);
        if (o20) gm_trap("integer overflow in `*`", gm_srcnqtpfma6qe4a, 111, 58);
        uint64_t t21 = r19;
        uint64_t t22 = (uintptr_t)t21;
        if (t17 > t22 || t22 > (uintptr_t)t14) gm_trap_slice(t17, t22, t14, gm_srcnqtpfma6qe4a, 111, 53);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t15 + t17 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22 - t17));
        uint64_t t24 = gm_f_get_u64_tgpb2vh(t13, t23);
        gm_t2lqu8jsj511d t25;
        GM_ST(uint64_t, ((uint8_t *)&t25 + 0), t12);
        GM_ST(uint64_t, ((uint8_t *)&t25 + 8), t24);
        uint8_t t26 = 0;
        F.ret = t25;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_union_def_h8mvf4t F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    gm_t88rcucg1b7uk gm_l3; /* tag */
    gm_b16_8 gm_l4; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 308u);
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
        if (t6 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t7 = GM_LD(uint32_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l2 = t7;
        {
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 8));
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)t11) + 1088) + 0));
            gm_b16_8 t13 = t12;
            uint32_t t14 = gm_l2;
            uint64_t t15 = (uint64_t)t14;
            uint64_t t16 = gm_f_us_cuv01fp(t15);
            uint64_t t17 = (uintptr_t)t16;
            uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&t13) + 8);
            if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srcnqtpfma6qe4a, 196, 30);
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t13)) + t17 * 184) + 24) + 0));
            if (t19 != ((uint8_t)1ULL)) goto ie9;
            gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)GM_LD(void *, ((uint8_t *)&t13)) + t17 * 184) + 24) + 8) + 0));
            gm_l3 = t20;
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((uint8_t *)&t13)) + t17 * 184) + 24) + 8) + 8));
            gm_l4 = t21;
            {
                gm_tcmkompbl09il t23;
                memset((uint8_t *)&t23 + 4, 0, 4);
                gm_t88rcucg1b7uk t24 = gm_l3;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t23 + 0, t24);
                gm_b16_8 t25 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t23 + 8, t25);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t57euh612ligr *)gm_out) + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t57euh612ligr *)gm_out) + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t57euh612ligr *)gm_out) + 28, 0);
                ((uint8_t *)&(*(gm_t57euh612ligr *)gm_out))[24] = 1;
                GM_ST(gm_tcmkompbl09il, (uint8_t *)&(*(gm_t57euh612ligr *)gm_out) + 0, t23);
                goto gm_fdone;
            }
            goto ix9;
ie9:;
            {
                gm_t57euh612ligr t27;
                memset(&t27, 0, sizeof t27);
                uint8_t t28 = 0;
                F.ret = t27;
                goto gm_fret;
            }
ix9:;
        }
        goto ix2;
ie2:;
        {
            gm_t57euh612ligr t30;
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
__attribute__((aligned(4))) gm_t57euh612ligr gm_f_union_def_h8mvf4t_c(void * a0, gm_csguankko4lmhd a1) { gm_t57euh612ligr gm_r; gm_f_union_def_h8mvf4t(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_variant_index_s2p6fs4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_variant_index_s2p6fs4 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    gm_tcmkompbl09il gm_l3; /* u */
    uint64_t gm_l4; /* n */
    unsigned __int128 gm_l5; /* tag */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    GM_STACK_CHECK(&F, 561u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t57euh612ligr t4;
        gm_f_union_def_h8mvf4t((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_tcmkompbl09il t5;
        if (((((uint8_t *)&t4))[24] != 0)) {
            t5 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t4) + 0));
        } else {
            gm_t9805qtonbsag t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l3 = t5;
    }
    {
        void * t8 = gm_l0;
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 0));
        uint64_t t10 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t11 = gm_f_us_cuv01fp(t10);
        gm_l4 = t11;
    }
    {
        void * t12 = gm_l0;
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 0));
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t15 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t16 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t17 = gm_l4;
        uint64_t t18 = (uintptr_t)t17;
        if (t16 > t18 || t18 > (uintptr_t)t14) gm_trap_slice(t16, t18, t14, gm_srcnqtpfma6qe4a, 222, 29);
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t15 + t16 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(t18 - t16));
        unsigned __int128 t20 = gm_f_get_int_pg7p4gc(t12, GM_CONV(gm_csguankko4lmhd, t13), t19);
        gm_l5 = t20;
    }
    {
        {
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
            gm_l6 = t23;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t25 = gm_l7;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    gm_t3l8idb2m6c1r t30;
                    uint64_t t31 = gm_l7;
                    GM_ST(uint64_t, (uint8_t *)&t30 + 0, t31);
                    uint64_t t32 = gm_l7;
                    uint64_t t33 = (uintptr_t)t32;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    memcpy((uint8_t *)&t30 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t33 * 72), 72);
                    uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&t30) + 0));
                    gm_l8 = t37;
                    F.l9 = (*(gm_tckdobunaulee *)(((uint8_t *)&t30) + 8));
                    {
                        unsigned __int128 t40 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l9) + 24)));
                        unsigned __int128 t41 = gm_l5;
                        uint8_t t42 = ((t40 == t41));
                        if (!t42) goto ie39;
                        {
                            uint64_t t44 = gm_l8;
                            uint32_t t45 = (uint32_t)t44;
                            gm_t9805qtonbsag t46;
                            GM_ST(uint8_t, (uint8_t *)&t46 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t46 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t46), t45);
                            ((uint8_t *)&t46)[4] = 1;
                            uint8_t t47 = 0;
                            F.ret = t46;
                            goto gm_fret;
                        }
                        goto ix39;
ie39:;
ix39:;
                    }
                    goto me35;
mn36:;
                    gm_trap("no match arm matched the scrutinee", gm_srcnqtpfma6qe4a, 223, 5);
me35:;
                }
lc24:;
                {
                    uint64_t t49 = gm_l7;
                    uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                    uint64_t t52 = r50;
                    gm_l7 = t52;
                }
            }
lb24:;
        }
    }
    {
        gm_t9805qtonbsag t53;
        memset(&t53, 0, sizeof t53);
        uint8_t t54 = 0;
        F.ret = t53;
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
inline __attribute__((aligned(4))) uint64_t gm_f_word_o2rcrhs(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_word_o2rcrhs F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* p */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 176));
        uint64_t t4 = gm_f_us_cuv01fp(t3);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t7 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t8 = gm_l2;
        uint64_t t9 = gm_l3;
        uint64_t r10; bool o11 = __builtin_mul_overflow(t8, t9, &r10);
        if (o11) gm_trap("integer overflow in `*`", gm_srcnqtpfma6qe4a, 123, 24);
        uint64_t t12 = r10;
        uint64_t t13 = (uintptr_t)t12;
        uint64_t t14 = gm_l2;
        uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcnqtpfma6qe4a, 123, 32);
        uint64_t t17 = r15;
        uint64_t t18 = gm_l3;
        uint64_t r19; bool o20 = __builtin_mul_overflow(t17, t18, &r19);
        if (o20) gm_trap("integer overflow in `*`", gm_srcnqtpfma6qe4a, 123, 31);
        uint64_t t21 = r19;
        uint64_t t22 = (uintptr_t)t21;
        if (t13 > t22 || t22 > (uintptr_t)t6) gm_trap_slice(t13, t22, t6, gm_srcnqtpfma6qe4a, 123, 22);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t7 + t13 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22 - t13));
        uint64_t t24 = gm_f_get_u64_tgpb2vh(t5, t23);
        uint8_t t25 = 0;
        F.ret = t24;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2);
