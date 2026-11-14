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
typedef struct gm_cs4j964akt50um { uint64_t f0; _Bool f1; _Bool f2; _Bool f3; } gm_cs4j964akt50um;
typedef struct gm_cskjv6t053tkje { uint32_t f0; uint64_t f1; } gm_cskjv6t053tkje;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t pe[5]; } gm_t81j4cfpg2pf9;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_cs4j964akt50um) == 16 && _Alignof(gm_cs4j964akt50um) == 8 && offsetof(gm_cs4j964akt50um, f0) == 0 && offsetof(gm_cs4j964akt50um, f1) == 8 && offsetof(gm_cs4j964akt50um, f2) == 9 && offsetof(gm_cs4j964akt50um, f3) == 10, "layout of `Open`");
_Static_assert(sizeof(gm_cskjv6t053tkje) == 16 && _Alignof(gm_cskjv6t053tkje) == 8 && offsetof(gm_cskjv6t053tkje, f0) == 0 && offsetof(gm_cskjv6t053tkje, f1) == 8, "layout of `Char`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t81j4cfpg2pf9) == 16 && _Alignof(gm_t81j4cfpg2pf9) == 8 && offsetof(gm_t81j4cfpg2pf9, f0) == 0 && offsetof(gm_t81j4cfpg2pf9, f1) == 8 && offsetof(gm_t81j4cfpg2pf9, f2) == 9 && offsetof(gm_t81j4cfpg2pf9, f3) == 10, "layout of `Open`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t f6; uint8_t f7; uint8_t pe[3]; } gm_t88jm1dgsmkjs;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; gm_t9qkh0rsooc8d f7; } gm_t8doqk5beq9nl;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; } gm_tk22flr9mh3ql;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_t81j4cfpg2pf9 f0; uint8_t f1; uint8_t pe[7]; } gm_toql92ug1137o;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
_Static_assert(sizeof(gm_t88jm1dgsmkjs) == 40 && _Alignof(gm_t88jm1dgsmkjs) == 8 && offsetof(gm_t88jm1dgsmkjs, f0) == 0 && offsetof(gm_t88jm1dgsmkjs, f1) == 8 && offsetof(gm_t88jm1dgsmkjs, f2) == 16 && offsetof(gm_t88jm1dgsmkjs, f3) == 32 && offsetof(gm_t88jm1dgsmkjs, f4) == 33 && offsetof(gm_t88jm1dgsmkjs, f5) == 34 && offsetof(gm_t88jm1dgsmkjs, f6) == 35 && offsetof(gm_t88jm1dgsmkjs, f7) == 36, "layout of `Info`");
_Static_assert(sizeof(gm_t8doqk5beq9nl) == 8 && _Alignof(gm_t8doqk5beq9nl) == 1 && offsetof(gm_t8doqk5beq9nl, f0) == 0 && offsetof(gm_t8doqk5beq9nl, f1) == 1 && offsetof(gm_t8doqk5beq9nl, f2) == 2 && offsetof(gm_t8doqk5beq9nl, f3) == 3 && offsetof(gm_t8doqk5beq9nl, f4) == 4 && offsetof(gm_t8doqk5beq9nl, f5) == 5 && offsetof(gm_t8doqk5beq9nl, f6) == 6 && offsetof(gm_t8doqk5beq9nl, f7) == 7, "layout of `(Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tk22flr9mh3ql) == 3 && _Alignof(gm_tk22flr9mh3ql) == 1 && offsetof(gm_tk22flr9mh3ql, f0) == 0 && offsetof(gm_tk22flr9mh3ql, f1) == 1 && offsetof(gm_tk22flr9mh3ql, f2) == 2, "layout of `(Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_toql92ug1137o) == 24 && _Alignof(gm_toql92ug1137o) == 8 && offsetof(gm_toql92ug1137o, f0) == 0 && offsetof(gm_toql92ug1137o, f1) == 16, "layout of `?Open`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
struct gm_fr_adjacent_close_065uhgl {
    gm_frame h;
    gm_tm4pc0emhrv1g l3; /* a */
    gm_tm4pc0emhrv1g l4; /* b */
    uint8_t ret;
};
struct gm_fr_classify_e96658j {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* infos */
    gm_trqfvmjtt4sij l5; /* stack */
    gm_trqfvmjtt4sij l7; /* pending_matches */
    gm_tm4pc0emhrv1g l11; /* t */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ginsuqm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rqd6vee {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_label_colon_rkt3ka8 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_punct_hnuckjb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_at_lviicpm {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_hcibotv {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_hngjb9i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_pop_j4rpvm5 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_pop_u3dhp9e {
    gm_frame h;
    gm_toql92ug1137o ret;
};
struct gm_fr_push_0uvdq16 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_n6hbl90 {
    gm_frame h;
    gm_t88jm1dgsmkjs l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3ifo8oj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_osbhhdi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_reset_colon_m7bd948 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_type_after_bracke_9p8imef {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_starts_with_non_white_o0cd8c4 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_tight_paren_keyword_rj4a6k9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_tight_paren_prev_p7h76um {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_type_follows_2br4pnf {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_adjacent_close_065uhgl(gm_b16_8 a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_all_white_1b9moim(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_classify_e96658j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_decode_jrk6b03(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_deinit_00kfno3(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_d5p9g61(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ginsuqm(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rqd6vee(void);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_label_colon_rkt3ka8(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_hnuckjb(const void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_is_punct_hnuckjb_c(gm_t3fcbuehh3bs2 a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_is_white_3njipsh(uint32_t a0);
__attribute__((aligned(4))) void gm_f_kind_at_lviicpm(void *gm_out, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_t3fcbuehh3bs2 gm_f_kind_at_lviicpm_c(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_last_line_hk6q5l4(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_hcibotv(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_hngjb9i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_u3dhp9e(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_punct_text_ov4r40u(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0uvdq16(void * a0, gm_cs4j964akt50um a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_n6hbl90(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3ifo8oj(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_osbhhdi(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) void gm_f_reset_colon_m7bd948(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_type_after_bracke_9p8imef(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_type_after_bracke_9p8imef_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_non_white_o0cd8c4(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_tight_paren_keyword_rj4a6k9(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) uint8_t gm_f_tight_paren_prev_p7h76um(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_tight_paren_prev_p7h76um_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) uint8_t gm_f_type_follows_2br4pnf(gm_b16_8 a0, uint64_t a1);
extern const uint8_t gm_sm7emfdvcb7c2[3];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_src8kcterbt0p7b[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_stugmrojdno85[6];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_f_find_nab29co(t1, t2);
    uint8_t t4;
    if (((((uint8_t *)&t3))[8] != 0)) goto mn6;
    t4 = ((uint8_t)0);
    goto me5;
mn6:;
    t4 = ((uint8_t)1);
    goto me5;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 84, 50);
me5:;
    F.ret = t4;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ginsuqm(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ginsuqm F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rqd6vee(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rqd6vee F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_hnuckjb(const void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_punct_hnuckjb F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* p */
    gm_t9qkh0rsooc8d gm_l2; /* q */
    GM_STACK_CHECK_SHALLOW(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    uint8_t t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)8ULL)) goto mn3;
    gm_t9qkh0rsooc8d t5 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l2 = t5;
    gm_t9qkh0rsooc8d t6 = gm_l2;
    gm_t9qkh0rsooc8d t7 = gm_l1;
    uint8_t t8 = (gm_eqv3tj1pu43g(((uint8_t *)&t6), ((uint8_t *)&t7)));
    t1 = t8;
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn9:;
    gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 52, 51);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_list_hcibotv(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_hcibotv F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_rqd6vee();
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
inline __attribute__((aligned(4))) void gm_f_list_hngjb9i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_hngjb9i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ginsuqm();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_u3dhp9e(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_u3dhp9e F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t81j4cfpg2pf9 gm_l2; /* x */
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
            gm_toql92ug1137o t8;
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
        gm_t81j4cfpg2pf9 t17 = GM_LD(gm_t81j4cfpg2pf9, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 16));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_t81j4cfpg2pf9 t29 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_toql92ug1137o *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_toql92ug1137o *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_toql92ug1137o *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_toql92ug1137o *)gm_out))[16] = 1;
        GM_ST(gm_t81j4cfpg2pf9, (uint8_t *)&(*(gm_toql92ug1137o *)gm_out) + 0, t29);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0uvdq16(void * a0, gm_cs4j964akt50um a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0uvdq16 F;
    void * gm_l0; /* l */
    gm_t81j4cfpg2pf9 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t81j4cfpg2pf9, a1);
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
            gm_b16_8 t11 = gm_f_empty_slice_ginsuqm();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_3ifo8oj(t10, ((uint64_t)1ULL), t12);
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
        gm_t81j4cfpg2pf9 t21 = gm_l1;
        GM_ST(gm_t81j4cfpg2pf9, ((uint8_t *)t20 + t18 * 16), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_n6hbl90(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_n6hbl90 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_rqd6vee();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_osbhhdi(t10, ((uint64_t)1ULL), t12);
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
        gm_t88jm1dgsmkjs t21 = GM_LD(gm_t88jm1dgsmkjs, ((uint8_t *)a1));
        GM_ST(gm_t88jm1dgsmkjs, ((uint8_t *)t20 + t18 * 40), t21);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3ifo8oj(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3ifo8oj F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_osbhhdi(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_osbhhdi F;
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
inline __attribute__((aligned(4))) void gm_f_reset_colon_m7bd948(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reset_colon_m7bd948 F;
    void * gm_l0; /* stack */
    void * gm_l1; /* top_colon */
    GM_STACK_CHECK_SHALLOW(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            uint64_t t8 = GM_LD(uintptr_t, (((uint8_t *)t7) + 0) + 8);
            uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
            if (o10) gm_trap("integer overflow in `-`", gm_src8kcterbt0p7b, 96, 21);
            uint64_t t11 = r9;
            uint64_t t12 = (uintptr_t)t11;
            void * t13 = gm_l0;
            uint64_t t14 = GM_LD(uintptr_t, (((uint8_t *)t13) + 0) + 8);
            if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_src8kcterbt0p7b, 96, 9);
            GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t13) + 0)) + t12 * 16) + 10), ((uint8_t)0));
        }
        goto ix2;
ie2:;
        {
            void * t16 = gm_l1;
            GM_ST(uint8_t, ((uint8_t *)t16), ((uint8_t)0));
        }
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_non_white_o0cd8c4(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_non_white_o0cd8c4 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_tb31k5l794t7p t8 = GM_CONV(gm_tb31k5l794t7p, gm_f_decode_jrk6b03(t7, ((uint64_t)0ULL)));
        uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&t8) + 0));
        uint8_t t10 = gm_f_is_white_3njipsh(t9);
        uint8_t t11 = (uint8_t)!t10;
        uint8_t t12 = 0;
        F.ret = t11;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_tight_paren_prev_p7h76um(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_tight_paren_prev_p7h76um F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* kw */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)2ULL)) goto mn3;
    gm_t9qkh0rsooc8d t5 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    gm_t9qkh0rsooc8d t6 = gm_l1;
    uint8_t t7 = gm_f_tight_paren_keyword_rj4a6k9(t6);
    t1 = t7;
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 46, 49);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_adjacent_close_065uhgl(gm_b16_8 a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adjacent_close_065uhgl F;
    gm_b16_8 gm_l0; /* src */
    gm_b16_8 gm_l1; /* toks */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 297u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = gm_l2;
        uint64_t r4; bool o5 = __builtin_add_overflow(t3, ((uint64_t)1ULL), &r4);
        if (o5) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 264, 8);
        uint64_t t6 = r4;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t8 = (t6 >= t7);
        if (!t8) goto ie2;
        {
            uint8_t t10 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t11 = gm_l2;
        uint64_t t12 = (uintptr_t)t11;
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_src8kcterbt0p7b, 265, 10);
        gm_tm4pc0emhrv1g t14 = GM_LD(gm_tm4pc0emhrv1g, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t12 * 48));
        F.l3 = t14;
    }
    {
        uint64_t t15 = gm_l2;
        uint64_t r16; bool o17 = __builtin_add_overflow(t15, ((uint64_t)1ULL), &r16);
        if (o17) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 266, 15);
        uint64_t t18 = r16;
        uint64_t t19 = (uintptr_t)t18;
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_src8kcterbt0p7b, 266, 10);
        gm_tm4pc0emhrv1g t21 = GM_LD(gm_tm4pc0emhrv1g, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t19 * 48));
        F.l4 = t21;
    }
    {
        uint8_t t22 = 0;
        gm_t9qkh0rsooc8d t23;
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)66ULL));
        uint8_t t24 = gm_f_is_punct_hnuckjb(&(*(gm_t3fcbuehh3bs2 *)(((uint8_t *)&F.l4) + 0)), t23);
        if (t24) {
            uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t26 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint32_t t27 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 32) + 8));
            uint64_t t28 = (uint64_t)t27;
            uint64_t t29 = (uintptr_t)t28;
            uint32_t t30 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 32) + 4));
            uint64_t t31 = (uint64_t)t30;
            uint64_t t32 = (uintptr_t)t31;
            if (t29 > t32 || t32 > (uintptr_t)t25) gm_trap_slice(t29, t32, t25, gm_src8kcterbt0p7b, 267, 52);
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t26 + t29 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t29));
            uint8_t t34 = gm_f_all_white_1b9moim(t33);
            t22 = t34;
        } else t22 = 0;
        uint8_t t35 = 0;
        F.ret = t22;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_classify_e96658j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_classify_e96658j F;
    gm_b16_8 gm_l0; /* src */
    gm_b16_8 gm_l1; /* toks */
    uint8_t gm_l3; /* ends */
    gm_tf5srh4mp4b3i gm_l4; /* prev */
    uint8_t gm_l6; /* top_colon */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint64_t gm_l12; /* lo */
    uint64_t gm_l13; /* hi */
    uint8_t gm_l14; /* tb */
    uint8_t gm_l15; /* ta */
    uint8_t gm_l16; /* open */
    uint8_t gm_l17; /* close */
    uint8_t gm_l18; /* match_body */
    gm_b16_8 gm_l19; /* txt */
    uint64_t gm_l20; /* p */
    gm_t3fcbuehh3bs2 gm_l21; /* prev_kind */
    gm_b16_8 gm_l22; /* s */
    gm_t9qkh0rsooc8d gm_l23; /* k */
    gm_t9qkh0rsooc8d gm_l24; /* p */
    uint8_t gm_l25; /* attr */
    gm_toql92ug1137o gm_l26; /* popped */
    gm_t81j4cfpg2pf9 gm_l27; /* o */
    uint8_t gm_l28; /* starts_line */
    uint8_t gm_l29; /* seen */
    gm_b16_8 gm_l30; /* word */
    GM_STACK_CHECK(&F, 1964u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_hcibotv((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l3 = ((uint8_t)0);
    }
    {
        gm_tf5srh4mp4b3i t2;
        memset(&t2, 0, sizeof t2);
        gm_l4 = t2;
    }
    {
        gm_f_list_hngjb9i((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        gm_l6 = ((uint8_t)0);
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
    }
    {
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l8 = t6;
        }
        {
            for (;;) {
                uint64_t t8 = gm_l9;
                uint64_t t9 = gm_l8;
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint64_t t13 = gm_l9;
                    uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&t13));
                    gm_l10 = t16;
                    {
                        uint64_t t18 = gm_l10;
                        uint64_t t19 = (uintptr_t)t18;
                        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_src8kcterbt0p7b, 122, 14);
                        gm_tm4pc0emhrv1g t21 = GM_LD(gm_tm4pc0emhrv1g, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t19 * 48));
                        F.l11 = t21;
                    }
                    {
                        uint32_t t22 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 32) + 4));
                        uint64_t t23 = (uint64_t)t22;
                        gm_l12 = t23;
                    }
                    {
                        uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 32) + 8));
                        uint64_t t25 = (uint64_t)t24;
                        gm_l13 = t25;
                    }
                    {
                        gm_l14 = ((uint8_t)0);
                    }
                    {
                        gm_l15 = ((uint8_t)0);
                    }
                    {
                        gm_l16 = ((uint8_t)0);
                    }
                    {
                        gm_l17 = ((uint8_t)0);
                    }
                    {
                        gm_l18 = ((uint8_t)0);
                    }
                    {
                        gm_b16_8 t26;
                        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(0));
                        gm_l19 = t26;
                    }
                    {
                        gm_t3fcbuehh3bs2 t27;
                        if (!((((uint8_t *)&gm_l4))[8] != 0)) goto ie28;
                        uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
                        gm_l20 = t29;
                        uint64_t t31 = gm_l20;
                        uint64_t t32 = (uintptr_t)t31;
                        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_src8kcterbt0p7b, 131, 37);
                        gm_t3fcbuehh3bs2 t34 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 48) + 0));
                        gm_t3fcbuehh3bs2 t35 = t34;
                        t27 = t35;
                        goto ix28;
ie28:;
                        gm_t3fcbuehh3bs2 t37;
                        GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t37 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t37 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t37 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)9ULL));
                        gm_t3fcbuehh3bs2 t38 = t37;
                        t27 = t38;
ix28:;
                        gm_l21 = t27;
                    }
                    {
                        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 0) + 0));
                        switch (t48) { case ((uint8_t)0ULL): goto ma42; case ((uint8_t)1ULL): goto ma43; case ((uint8_t)2ULL): goto ma45; case ((uint8_t)3ULL): goto ma43; case ((uint8_t)4ULL): goto ma43; case ((uint8_t)5ULL): goto ma43; case ((uint8_t)6ULL): goto ma43; case ((uint8_t)7ULL): goto ma44; case ((uint8_t)8ULL): goto ma46; case ((uint8_t)9ULL): goto ma40; case ((uint8_t)10ULL): goto ma41; default: goto ma47; }
ma40:;
                        uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 0) + 0));
                        if (t50 != ((uint8_t)9ULL)) goto mn49;
                        {
                            gm_l3 = ((uint8_t)0);
                        }
                        {
                            gm_tf5srh4mp4b3i t52;
                            memset(&t52, 0, sizeof t52);
                            gm_l4 = t52;
                        }
                        {
                            void * t53 = (void *)((uint8_t *)&F.l5);
                            void * t54 = (void *)((uint8_t *)&gm_l6);
                            gm_f_reset_colon_m7bd948(t53, t54);
                        }
                        {
                            goto lc7;
                        }
                        goto me39;
mn49:;
ma41:;
                        uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 0) + 0));
                        if (t56 != ((uint8_t)10ULL)) goto mn55;
                        goto lb7;
                        goto me39;
mn55:;
ma42:;
                        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 0) + 0));
                        if (t58 != ((uint8_t)0ULL)) goto mn57;
                        gm_b16_8 t59 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l11) + 0) + 8));
                        gm_l22 = t59;
                        {
                            uint8_t t61 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l22)), gm_stugmrojdno85, 5) == 0));
                            gm_l3 = t61;
                        }
                        goto me39;
mn57:;
ma43:;
                        uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 0) + 0));
                        if (t65 != ((uint8_t)1ULL)) goto pa64;
                        goto po63;
pa64:;
                        uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 0) + 0));
                        if (t67 != ((uint8_t)3ULL)) goto pa66;
                        goto po63;
pa66:;
                        uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 0) + 0));
                        if (t69 != ((uint8_t)4ULL)) goto pa68;
                        goto po63;
pa68:;
                        uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 0) + 0));
                        if (t71 != ((uint8_t)5ULL)) goto pa70;
                        goto po63;
pa70:;
                        uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 0) + 0));
                        if (t73 != ((uint8_t)6ULL)) goto pa72;
                        goto po63;
pa72:;
                        goto mn62;
po63:;
                        {
                            gm_l3 = ((uint8_t)1);
                        }
                        goto me39;
mn62:;
ma44:;
                        uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 0) + 0));
                        if (t76 != ((uint8_t)7ULL)) goto mn75;
                        {
                            gm_l3 = ((uint8_t)0);
                        }
                        goto me39;
mn75:;
ma45:;
                        uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 0) + 0));
                        if (t79 != ((uint8_t)2ULL)) goto mn78;
                        gm_t9qkh0rsooc8d t80 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&F.l11) + 0) + 8));
                        gm_l23 = t80;
                        {
                            uint8_t t83 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l23) + 0) == ((uint8_t)17ULL)));
                            if (!t83) goto ie82;
                            {
                                void * t85 = (void *)((uint8_t *)&F.l7);
                                uint64_t t86 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                gm_tftb1odc739t5 t87 = gm_f_push_ucgmlo2(t85, t86);
                                if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_src8kcterbt0p7b, 144, 34);
                                gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
                            }
                            goto ix82;
ie82:;
ix82:;
                        }
                        {
                            gm_t9qkh0rsooc8d t89 = gm_l23;
                            gm_t9qkh0rsooc8d t90;
                            GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)25ULL));
                            gm_t9qkh0rsooc8d t91;
                            GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)10ULL));
                            gm_t9qkh0rsooc8d t92;
                            GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)19ULL));
                            gm_tk22flr9mh3ql t93;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t93 + 0), t90);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t93 + 1), t91);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t93 + 2), t92);
                            uint8_t t94 = 0;
                            if (gm_eq2ceic3f8rp(((uint8_t *)&t93) + 0, ((uint8_t *)&t89))) t94 = 1;
                            if (gm_eq2ceic3f8rp(((uint8_t *)&t93) + 1, ((uint8_t *)&t89))) t94 = 1;
                            if (gm_eq2ceic3f8rp(((uint8_t *)&t93) + 2, ((uint8_t *)&t89))) t94 = 1;
                            uint8_t t95 = (uint8_t)(t94);
                            gm_l3 = t95;
                        }
                        goto me39;
mn78:;
ma46:;
                        uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 0) + 0));
                        if (t97 != ((uint8_t)8ULL)) goto mn96;
                        gm_t9qkh0rsooc8d t98 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&F.l11) + 0) + 8));
                        gm_l24 = t98;
                        uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        switch (t115) { case ((uint8_t)0ULL): goto ma113; case ((uint8_t)1ULL): goto ma109; case ((uint8_t)2ULL): goto ma113; case ((uint8_t)3ULL): goto ma113; case ((uint8_t)4ULL): goto ma113; case ((uint8_t)5ULL): goto ma113; case ((uint8_t)6ULL): goto ma113; case ((uint8_t)7ULL): goto ma109; case ((uint8_t)8ULL): goto ma113; case ((uint8_t)9ULL): goto ma109; case ((uint8_t)10ULL): goto ma113; case ((uint8_t)11ULL): goto ma113; case ((uint8_t)12ULL): goto ma113; case ((uint8_t)13ULL): goto ma113; case ((uint8_t)14ULL): goto ma113; case ((uint8_t)15ULL): goto ma113; case ((uint8_t)16ULL): goto ma113; case ((uint8_t)17ULL): goto ma113; case ((uint8_t)18ULL): goto ma113; case ((uint8_t)19ULL): goto ma113; case ((uint8_t)20ULL): goto ma113; case ((uint8_t)21ULL): goto ma113; case ((uint8_t)22ULL): goto ma113; case ((uint8_t)23ULL): goto ma113; case ((uint8_t)24ULL): goto ma113; case ((uint8_t)25ULL): goto ma113; case ((uint8_t)26ULL): goto ma113; case ((uint8_t)27ULL): goto ma113; case ((uint8_t)28ULL): goto ma113; case ((uint8_t)29ULL): goto ma113; case ((uint8_t)30ULL): goto ma113; case ((uint8_t)31ULL): goto ma113; case ((uint8_t)32ULL): goto ma113; case ((uint8_t)33ULL): goto ma113; case ((uint8_t)34ULL): goto ma113; case ((uint8_t)35ULL): goto ma113; case ((uint8_t)36ULL): goto ma113; case ((uint8_t)37ULL): goto ma113; case ((uint8_t)38ULL): goto ma113; case ((uint8_t)39ULL): goto ma113; case ((uint8_t)40ULL): goto ma113; case ((uint8_t)41ULL): goto ma113; case ((uint8_t)42ULL): goto ma113; case ((uint8_t)43ULL): goto ma113; case ((uint8_t)44ULL): goto ma113; case ((uint8_t)45ULL): goto ma107; case ((uint8_t)46ULL): goto ma108; case ((uint8_t)47ULL): goto ma107; case ((uint8_t)48ULL): goto ma110; case ((uint8_t)49ULL): goto ma110; case ((uint8_t)50ULL): goto ma105; case ((uint8_t)51ULL): goto ma105; case ((uint8_t)52ULL): goto ma113; case ((uint8_t)53ULL): goto ma111; case ((uint8_t)54ULL): goto ma113; case ((uint8_t)55ULL): goto ma113; case ((uint8_t)56ULL): goto ma106; case ((uint8_t)57ULL): goto ma104; case ((uint8_t)58ULL): goto ma105; case ((uint8_t)59ULL): goto ma113; case ((uint8_t)60ULL): goto ma113; case ((uint8_t)61ULL): goto ma100; case ((uint8_t)62ULL): goto ma103; case ((uint8_t)63ULL): goto ma101; case ((uint8_t)64ULL): goto ma103; case ((uint8_t)65ULL): goto ma102; case ((uint8_t)66ULL): goto ma103; case ((uint8_t)67ULL): goto ma112; case ((uint8_t)68ULL): goto ma112; default: goto ma113; }
ma100:;
                        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t117 != ((uint8_t)61ULL)) goto mn116;
                        {
                            uint8_t t119 = 0;
                            uint8_t t120 = 0;
                            uint8_t t121 = 0;
                            uint8_t t122 = gm_l3;
                            if (!t122) {
                                uint8_t t123 = gm_f_tight_paren_prev_p7h76um(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l21)));
                                t121 = t123;
                            } else t121 = 1;
                            if (!t121) {
                                uint8_t t124;
                                uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                switch (t130) { case ((uint8_t)0ULL): goto ma128; case ((uint8_t)1ULL): goto ma128; case ((uint8_t)2ULL): goto ma128; case ((uint8_t)3ULL): goto ma128; case ((uint8_t)4ULL): goto ma128; case ((uint8_t)5ULL): goto ma128; case ((uint8_t)6ULL): goto ma127; case ((uint8_t)7ULL): goto ma126; case ((uint8_t)8ULL): goto ma128; case ((uint8_t)9ULL): goto ma128; case ((uint8_t)10ULL): goto ma128; default: goto ma128; }
ma126:;
                                uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                if (t132 != ((uint8_t)7ULL)) goto mn131;
                                t124 = ((uint8_t)1);
                                goto me125;
mn131:;
ma127:;
                                uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                if (t134 != ((uint8_t)6ULL)) goto mn133;
                                t124 = ((uint8_t)1);
                                goto me125;
mn133:;
ma128:;
                                t124 = ((uint8_t)0);
                                goto me125;
mn135:;
ma129:;
                                gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 149, 65);
me125:;
                                t120 = t124;
                            } else t120 = 1;
                            if (!t120) {
                                gm_t9qkh0rsooc8d t136;
                                GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)46ULL));
                                uint8_t t137 = gm_f_is_punct_hnuckjb(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l21)), t136);
                                t119 = t137;
                            } else t119 = 1;
                            gm_l14 = t119;
                        }
                        {
                            gm_l15 = ((uint8_t)1);
                        }
                        {
                            gm_l16 = ((uint8_t)1);
                        }
                        {
                            uint8_t t138;
                            uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                            if (t141 != ((uint8_t)7ULL)) goto mn140;
                            t138 = ((uint8_t)1);
                            goto me139;
mn140:;
                            t138 = ((uint8_t)0);
                            goto me139;
mn142:;
                            gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 152, 29);
me139:;
                            gm_l25 = t138;
                        }
                        {
                            void * t143 = (void *)((uint8_t *)&F.l5);
                            uint64_t t144 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                            uint8_t t145 = gm_l25;
                            gm_t81j4cfpg2pf9 t146;
                            GM_ST(uint8_t, (uint8_t *)&t146 + 11, 0);
                            GM_ST(uint32_t, (uint8_t *)&t146 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t146 + 0), t144);
                            GM_ST(uint8_t, ((uint8_t *)&t146 + 8), ((uint8_t)0));
                            GM_ST(uint8_t, ((uint8_t *)&t146 + 9), t145);
                            GM_ST(uint8_t, ((uint8_t *)&t146 + 10), ((uint8_t)0));
                            gm_tftb1odc739t5 t147 = gm_f_push_0uvdq16(t143, GM_CONV(gm_cs4j964akt50um, t146));
                            if (((((uint8_t *)&t147))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t147) + 1, gm_src8kcterbt0p7b, 153, 21);
                            gm_b0_1 t148 = GM_LD(gm_b0_1, (((uint8_t *)&t147) + 1));
                        }
                        {
                            gm_l3 = ((uint8_t)0);
                        }
                        goto me99;
mn116:;
ma101:;
                        uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t150 != ((uint8_t)63ULL)) goto mn149;
                        {
                            uint8_t t152 = gm_l3;
                            gm_l14 = t152;
                        }
                        {
                            gm_l15 = ((uint8_t)1);
                        }
                        {
                            gm_l16 = ((uint8_t)1);
                        }
                        {
                            void * t153 = (void *)((uint8_t *)&F.l5);
                            uint64_t t154 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                            uint8_t t155 = gm_l3;
                            uint8_t t156 = (uint8_t)!t155;
                            gm_t81j4cfpg2pf9 t157;
                            GM_ST(uint8_t, (uint8_t *)&t157 + 11, 0);
                            GM_ST(uint32_t, (uint8_t *)&t157 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t157 + 0), t154);
                            GM_ST(uint8_t, ((uint8_t *)&t157 + 8), t156);
                            GM_ST(uint8_t, ((uint8_t *)&t157 + 9), ((uint8_t)0));
                            GM_ST(uint8_t, ((uint8_t *)&t157 + 10), ((uint8_t)0));
                            gm_tftb1odc739t5 t158 = gm_f_push_0uvdq16(t153, GM_CONV(gm_cs4j964akt50um, t157));
                            if (((((uint8_t *)&t158))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t158) + 1, gm_src8kcterbt0p7b, 160, 21);
                            gm_b0_1 t159 = GM_LD(gm_b0_1, (((uint8_t *)&t158) + 1));
                        }
                        {
                            gm_l3 = ((uint8_t)0);
                        }
                        goto me99;
mn149:;
ma102:;
                        uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t161 != ((uint8_t)65ULL)) goto mn160;
                        {
                            uint64_t t164 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                            uint8_t t165 = (t164 > ((uint64_t)0ULL));
                            if (!t165) goto ie163;
                            uint64_t t166 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                            uint64_t r167; bool o168 = __builtin_sub_overflow(t166, ((uint64_t)1ULL), &r167);
                            if (o168) gm_trap("integer overflow in `-`", gm_src8kcterbt0p7b, 164, 80);
                            uint64_t t169 = r167;
                            uint64_t t170 = (uintptr_t)t169;
                            uint64_t t171 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                            if (t170 >= (uintptr_t)(t171)) gm_trap_index(t170, t171, gm_src8kcterbt0p7b, 164, 58);
                            uint64_t t172 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)) + t170 * 8));
                            uint64_t t173 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                            uint8_t t174 = ((t172 == t173));
                            if (!t174) goto ie163;
                            {
                                {
                                    void * t177 = (void *)((uint8_t *)&F.l7);
                                    gm_tf5srh4mp4b3i t178 = gm_f_pop_j4rpvm5(t177);
                                }
                            }
                            {
                                gm_l18 = ((uint8_t)1);
                            }
                            goto ix163;
ie163:;
ix163:;
                        }
                        {
                            gm_b16_8 t179 = gm_l0;
                            gm_b16_8 t180 = gm_l1;
                            uint64_t t181 = gm_l10;
                            uint8_t t182 = gm_f_adjacent_close_065uhgl(t179, t180, t181);
                            gm_l15 = t182;
                        }
                        {
                            gm_l16 = ((uint8_t)1);
                        }
                        {
                            void * t183 = (void *)((uint8_t *)&F.l5);
                            uint64_t t184 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                            gm_t81j4cfpg2pf9 t185;
                            GM_ST(uint8_t, (uint8_t *)&t185 + 11, 0);
                            GM_ST(uint32_t, (uint8_t *)&t185 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t185 + 0), t184);
                            GM_ST(uint8_t, ((uint8_t *)&t185 + 8), ((uint8_t)0));
                            GM_ST(uint8_t, ((uint8_t *)&t185 + 9), ((uint8_t)0));
                            GM_ST(uint8_t, ((uint8_t *)&t185 + 10), ((uint8_t)0));
                            gm_tftb1odc739t5 t186 = gm_f_push_0uvdq16(t183, GM_CONV(gm_cs4j964akt50um, t185));
                            if (((((uint8_t *)&t186))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t186) + 1, gm_src8kcterbt0p7b, 170, 21);
                            gm_b0_1 t187 = GM_LD(gm_b0_1, (((uint8_t *)&t186) + 1));
                        }
                        {
                            gm_l3 = ((uint8_t)0);
                        }
                        goto me99;
mn160:;
ma103:;
                        uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t191 != ((uint8_t)62ULL)) goto pa190;
                        goto po189;
pa190:;
                        uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t193 != ((uint8_t)64ULL)) goto pa192;
                        goto po189;
pa192:;
                        uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t195 != ((uint8_t)66ULL)) goto pa194;
                        goto po189;
pa194:;
                        goto mn188;
po189:;
                        {
                            gm_l17 = ((uint8_t)1);
                        }
                        {
                            gm_l14 = ((uint8_t)1);
                        }
                        {
                            void * t197 = (void *)((uint8_t *)&F.l5);
                            gm_f_pop_u3dhp9e((void *)&gm_l26, t197);
                        }
                        {
                            gm_l3 = ((uint8_t)1);
                        }
                        {
                            if (!((((uint8_t *)&gm_l26))[16] != 0)) goto ie198;
                            gm_t81j4cfpg2pf9 t199 = GM_LD(gm_t81j4cfpg2pf9, ((uint8_t *)&gm_l26));
                            gm_l27 = t199;
                            {
                                uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 9));
                                uint8_t t202 = (uint8_t)!t201;
                                gm_l3 = t202;
                            }
                            {
                                uint8_t t204 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l24) + 0) == ((uint8_t)64ULL)));
                                if (!t204) goto ie203;
                                uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 8));
                                if (!t205) goto ie203;
                                gm_b16_8 t206 = gm_l1;
                                uint64_t t207 = gm_l10;
                                uint64_t r208; bool o209 = __builtin_add_overflow(t207, ((uint64_t)1ULL), &r208);
                                if (o209) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 180, 100);
                                uint64_t t210 = r208;
                                gm_t3fcbuehh3bs2 t211;
                                gm_f_kind_at_lviicpm((void *)&t211, t206, t210);
                                uint8_t t212 = gm_f_starts_type_after_bracke_9p8imef(&t211);
                                if (!t212) goto ie203;
                                {
                                    gm_l15 = ((uint8_t)1);
                                }
                                {
                                    gm_l3 = ((uint8_t)0);
                                }
                                goto ix203;
ie203:;
ix203:;
                            }
                            {
                                uint8_t t215 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l24) + 0) == ((uint8_t)66ULL)));
                                if (!t215) goto ie214;
                                {
                                    uint8_t t217 = 0;
                                    uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 0));
                                    uint64_t r219; bool o220 = __builtin_add_overflow(t218, ((uint64_t)1ULL), &r219);
                                    if (o220) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 185, 34);
                                    uint64_t t221 = r219;
                                    uint64_t t222 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                    uint8_t t223 = ((t221 == t222));
                                    if (t223) {
                                        uint64_t t224 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                        void * t225 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                                        uint64_t t226 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 0));
                                        uint64_t t227 = (uintptr_t)t226;
                                        uint64_t t228 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                        if (t227 >= (uintptr_t)(t228)) gm_trap_index(t227, t228, gm_src8kcterbt0p7b, 185, 82);
                                        uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t227 * 40) + 8));
                                        uint64_t t230 = (uintptr_t)t229;
                                        uint64_t t231 = gm_l12;
                                        uint64_t t232 = (uintptr_t)t231;
                                        if (t230 > t232 || t232 > (uintptr_t)t224) gm_trap_slice(t230, t232, t224, gm_src8kcterbt0p7b, 185, 78);
                                        gm_b16_8 t233;
                                        GM_ST(void *, (uint8_t *)&t233, (void *)(((uint8_t *)t225 + t230 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t233 + 8, (uintptr_t)(t232 - t230));
                                        uint8_t t234 = gm_f_all_white_1b9moim(t233);
                                        t217 = t234;
                                    } else t217 = 0;
                                    gm_l14 = t217;
                                }
                                goto ix214;
ie214:;
ix214:;
                            }
                            goto ix198;
ie198:;
ix198:;
                        }
                        goto me99;
mn188:;
ma104:;
                        uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t236 != ((uint8_t)57ULL)) goto mn235;
                        {
                            gm_l14 = ((uint8_t)1);
                        }
                        {
                            gm_l3 = ((uint8_t)0);
                        }
                        {
                            void * t238 = (void *)((uint8_t *)&F.l5);
                            void * t239 = (void *)((uint8_t *)&gm_l6);
                            gm_f_reset_colon_m7bd948(t238, t239);
                        }
                        goto me99;
mn235:;
ma105:;
                        uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t243 != ((uint8_t)58ULL)) goto pa242;
                        goto po241;
pa242:;
                        uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t245 != ((uint8_t)50ULL)) goto pa244;
                        goto po241;
pa244:;
                        uint8_t t247 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t247 != ((uint8_t)51ULL)) goto pa246;
                        goto po241;
pa246:;
                        goto mn240;
po241:;
                        {
                            uint8_t t249 = gm_l3;
                            gm_l14 = t249;
                        }
                        {
                            gm_l15 = ((uint8_t)1);
                        }
                        {
                            uint8_t t250 = 0;
                            uint8_t t251;
                            if (((((uint8_t *)&gm_l4))[8] != 0)) goto mn253;
                            t251 = ((uint8_t)1);
                            goto me252;
mn253:;
                            t251 = ((uint8_t)0);
                            goto me252;
mn254:;
                            gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 200, 36);
me252:;
                            if (!t251) {
                                uint64_t t255 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                void * t256 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                                uint64_t t257 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t258 = gm_l12;
                                uint64_t t259 = (uintptr_t)t258;
                                if (t257 > t259 || t259 > (uintptr_t)t255) gm_trap_slice(t257, t259, t255, gm_src8kcterbt0p7b, 200, 72);
                                gm_b16_8 t260;
                                GM_ST(void *, (uint8_t *)&t260, (void *)(((uint8_t *)t256 + t257 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t260 + 8, (uintptr_t)(t259 - t257));
                                gm_b16_8 t261 = gm_f_last_line_hk6q5l4(t260);
                                uint8_t t262 = gm_f_all_white_1b9moim(t261);
                                t250 = t262;
                            } else t250 = 1;
                            gm_l28 = t250;
                        }
                        {
                            uint8_t t264 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l24) + 0) == ((uint8_t)58ULL)));
                            if (!t264) goto ie263;
                            uint8_t t265 = gm_l28;
                            if (!t265) goto ie263;
                            uint64_t t266 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            void * t267 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                            uint64_t t268 = gm_l13;
                            uint64_t t269 = (uintptr_t)t268;
                            if (t269 > (uintptr_t)t266 || (uintptr_t)t266 > (uintptr_t)t266) gm_trap_slice(t269, (uintptr_t)t266, t266, gm_src8kcterbt0p7b, 201, 80);
                            gm_b16_8 t270;
                            GM_ST(void *, (uint8_t *)&t270, (void *)(((uint8_t *)t267 + t269 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t270 + 8, (uintptr_t)((uintptr_t)t266 - t269));
                            uint8_t t271 = gm_f_starts_with_non_white_o0cd8c4(t270);
                            uint8_t t272 = (uint8_t)!t271;
                            if (!t272) goto ie263;
                            {
                                gm_l15 = ((uint8_t)0);
                            }
                            goto ix263;
ie263:;
ix263:;
                        }
                        {
                            uint8_t t275 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l24) + 0) == ((uint8_t)58ULL)));
                            if (!t275) goto ie274;
                            uint8_t t276;
                            uint8_t t279 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                            if (t279 != ((uint8_t)3ULL)) goto mn278;
                            t276 = ((uint8_t)1);
                            goto me277;
mn278:;
                            t276 = ((uint8_t)0);
                            goto me277;
mn280:;
                            gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 205, 38);
me277:;
                            if (!t276) goto ie274;
                            {
                                uint8_t t282 = 0;
                                uint64_t t283 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                uint8_t t284 = (t283 > ((uint64_t)0ULL));
                                if (t284) {
                                    uint64_t t285 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                    uint64_t r286; bool o287 = __builtin_sub_overflow(t285, ((uint64_t)1ULL), &r286);
                                    if (o287) gm_trap("integer overflow in `-`", gm_src8kcterbt0p7b, 206, 66);
                                    uint64_t t288 = r286;
                                    uint64_t t289 = (uintptr_t)t288;
                                    uint64_t t290 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                    if (t289 >= (uintptr_t)(t290)) gm_trap_index(t289, t290, gm_src8kcterbt0p7b, 206, 54);
                                    uint64_t t291 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t289 * 40) + 8));
                                    uint64_t t292 = gm_l12;
                                    uint8_t t293 = ((t291 == t292));
                                    t282 = t293;
                                } else t282 = 0;
                                gm_l14 = t282;
                            }
                            {
                                uint64_t t294 = gm_l10;
                                uint64_t r295; bool o296 = __builtin_add_overflow(t294, ((uint64_t)1ULL), &r295);
                                if (o296) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 207, 41);
                                uint64_t t297 = r295;
                                uint64_t t298 = (uintptr_t)t297;
                                uint64_t t299 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                if (t298 >= (uintptr_t)(t299)) gm_trap_index(t298, t299, gm_src8kcterbt0p7b, 207, 36);
                                uint32_t t300 = GM_LD(uint32_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t298 * 48) + 32) + 4));
                                uint64_t t301 = (uint64_t)t300;
                                uint64_t t302 = gm_l13;
                                uint8_t t303 = ((t301 == t302));
                                gm_l15 = t303;
                            }
                            goto ix274;
ie274:;
ix274:;
                        }
                        {
                            gm_l3 = ((uint8_t)0);
                        }
                        goto me99;
mn240:;
ma106:;
                        uint8_t t305 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t305 != ((uint8_t)56ULL)) goto mn304;
                        {
                            gm_b16_8 t308 = gm_l1;
                            uint64_t t309 = gm_l10;
                            uint8_t t310 = gm_f_is_label_colon_rkt3ka8(t308, t309);
                            if (!t310) goto ie307;
                            {
                                gm_l14 = ((uint8_t)1);
                            }
                            {
                                gm_l3 = ((uint8_t)0);
                            }
                            goto ix307;
ie307:;
                            {
                                uint8_t t313;
                                uint64_t t315 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                uint8_t t316 = (t315 > ((uint64_t)0ULL));
                                if (!t316) goto ie314;
                                uint64_t t318 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                uint64_t r319; bool o320 = __builtin_sub_overflow(t318, ((uint64_t)1ULL), &r319);
                                if (o320) gm_trap("integer overflow in `-`", gm_src8kcterbt0p7b, 217, 70);
                                uint64_t t321 = r319;
                                uint64_t t322 = (uintptr_t)t321;
                                uint64_t t323 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                if (t322 >= (uintptr_t)(t323)) gm_trap_index(t322, t323, gm_src8kcterbt0p7b, 217, 58);
                                uint8_t t324 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t322 * 16) + 10));
                                uint8_t t325 = t324;
                                t313 = t325;
                                goto ix314;
ie314:;
                                uint8_t t327 = gm_l6;
                                uint8_t t328 = t327;
                                t313 = t328;
ix314:;
                                gm_l29 = t313;
                            }
                            {
                                uint8_t t329 = gm_l29;
                                uint8_t t330 = (uint8_t)!t329;
                                gm_l14 = t330;
                            }
                            {
                                gm_l15 = ((uint8_t)0);
                            }
                            {
                                uint64_t t332 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                uint8_t t333 = (t332 > ((uint64_t)0ULL));
                                if (!t333) goto ie331;
                                {
                                    uint64_t t335 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                    uint64_t r336; bool o337 = __builtin_sub_overflow(t335, ((uint64_t)1ULL), &r336);
                                    if (o337) gm_trap("integer overflow in `-`", gm_src8kcterbt0p7b, 220, 62);
                                    uint64_t t338 = r336;
                                    uint64_t t339 = (uintptr_t)t338;
                                    uint64_t t340 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                    if (t339 >= (uintptr_t)(t340)) gm_trap_index(t339, t340, gm_src8kcterbt0p7b, 220, 50);
                                    GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t339 * 16) + 10), ((uint8_t)1));
                                }
                                goto ix331;
ie331:;
                                {
                                    gm_l6 = ((uint8_t)1);
                                }
ix331:;
                            }
                            {
                                gm_l3 = ((uint8_t)0);
                            }
ix307:;
                        }
                        goto me99;
mn304:;
ma107:;
                        uint8_t t345 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t345 != ((uint8_t)45ULL)) goto pa344;
                        goto po343;
pa344:;
                        uint8_t t347 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t347 != ((uint8_t)47ULL)) goto pa346;
                        goto po343;
pa346:;
                        goto mn342;
po343:;
                        {
                            uint8_t t349 = gm_l3;
                            gm_l14 = t349;
                        }
                        {
                            uint8_t t350 = gm_l3;
                            uint8_t t351 = (uint8_t)!t350;
                            gm_l15 = t351;
                        }
                        goto me99;
mn342:;
ma108:;
                        uint8_t t353 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t353 != ((uint8_t)46ULL)) goto mn352;
                        {
                            uint8_t t356 = gm_l3;
                            uint8_t t357 = (uint8_t)!t356;
                            if (!t357) goto ie355;
                            {
                                gm_l15 = ((uint8_t)1);
                            }
                            goto ix355;
ie355:;
                            gm_b16_8 t360 = gm_l1;
                            uint64_t t361 = gm_l10;
                            uint64_t r362; bool o363 = __builtin_add_overflow(t361, ((uint64_t)1ULL), &r362);
                            if (o363) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 232, 50);
                            uint64_t t364 = r362;
                            uint8_t t365 = gm_f_type_follows_2br4pnf(t360, t364);
                            if (!t365) goto ie359;
                            {
                                gm_l14 = ((uint8_t)1);
                            }
                            {
                                gm_l15 = ((uint8_t)1);
                            }
                            {
                                gm_l3 = ((uint8_t)0);
                            }
                            goto ix359;
ie359:;
                            {
                                gm_l14 = ((uint8_t)1);
                            }
ix359:;
ix355:;
                        }
                        goto me99;
mn352:;
ma109:;
                        uint8_t t371 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t371 != ((uint8_t)1ULL)) goto pa370;
                        goto po369;
pa370:;
                        uint8_t t373 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t373 != ((uint8_t)7ULL)) goto pa372;
                        goto po369;
pa372:;
                        uint8_t t375 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t375 != ((uint8_t)9ULL)) goto pa374;
                        goto po369;
pa374:;
                        goto mn368;
po369:;
                        {
                            uint8_t t377 = gm_l3;
                            uint8_t t378 = (uint8_t)!t377;
                            gm_l15 = t378;
                        }
                        {
                            gm_l3 = ((uint8_t)0);
                        }
                        goto me99;
mn368:;
ma110:;
                        uint8_t t382 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t382 != ((uint8_t)48ULL)) goto pa381;
                        goto po380;
pa381:;
                        uint8_t t384 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t384 != ((uint8_t)49ULL)) goto pa383;
                        goto po380;
pa383:;
                        goto mn379;
po380:;
                        {
                            gm_l3 = ((uint8_t)0);
                        }
                        goto me99;
mn379:;
ma111:;
                        uint8_t t387 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t387 != ((uint8_t)53ULL)) goto mn386;
                        {
                            gm_l3 = ((uint8_t)1);
                        }
                        goto me99;
mn386:;
ma112:;
                        uint8_t t392 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t392 != ((uint8_t)67ULL)) goto pa391;
                        goto po390;
pa391:;
                        uint8_t t394 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                        if (t394 != ((uint8_t)68ULL)) goto pa393;
                        goto po390;
pa393:;
                        goto mn389;
po390:;
                        {
                            uint64_t t396 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            void * t397 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                            uint64_t t398 = gm_l12;
                            uint64_t t399 = (uintptr_t)t398;
                            uint64_t t400 = gm_l13;
                            uint64_t t401 = (uintptr_t)t400;
                            if (t399 > t401 || t401 > (uintptr_t)t396) gm_trap_slice(t399, t401, t396, gm_src8kcterbt0p7b, 248, 29);
                            gm_b16_8 t402;
                            GM_ST(void *, (uint8_t *)&t402, (void *)(((uint8_t *)t397 + t399 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t402 + 8, (uintptr_t)(t401 - t399));
                            gm_l30 = t402;
                        }
                        {
                            gm_b16_8 t404 = gm_l30;
                            gm_b16_8 t405;
                            GM_ST(void *, (uint8_t *)&t405, (void *)(gm_sm7emfdvcb7c2));
                            GM_ST(uintptr_t, (uint8_t *)&t405 + 8, (uintptr_t)(2));
                            uint8_t t406 = gm_f_contains_gkki4kq(t404, t405);
                            uint8_t t407 = (uint8_t)!t406;
                            if (!t407) goto ie403;
                            gm_b16_8 t408 = gm_l30;
                            gm_tf5srh4mp4b3i t409 = gm_f_index_of_hdeg351(t408, ((uint8_t)10ULL));
                            uint8_t t410;
                            if (((((uint8_t *)&t409))[8] != 0)) goto mn412;
                            t410 = ((uint8_t)1);
                            goto me411;
mn412:;
                            t410 = ((uint8_t)0);
                            goto me411;
mn413:;
                            gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 249, 57);
me411:;
                            if (!t410) goto ie403;
                            {
                                gm_t9qkh0rsooc8d t415 = gm_l24;
                                gm_b16_8 t416 = gm_f_punct_text_ov4r40u(t415);
                                gm_l19 = t416;
                            }
                            goto ix403;
ie403:;
ix403:;
                        }
                        {
                            gm_l3 = ((uint8_t)0);
                        }
                        goto me99;
mn389:;
ma113:;
                        {
                            gm_l3 = ((uint8_t)0);
                        }
                        goto me99;
mn417:;
ma114:;
                        gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 147, 26);
me99:;
                        goto me39;
mn96:;
ma47:;
                        gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 132, 9);
me39:;
                    }
                    {
                        uint64_t t420 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                        uint8_t t421 = ((t420 == ((uint64_t)0ULL)));
                        if (!t421) goto ie419;
                        {
                            uint64_t t423 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            void * t424 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                            uint64_t t425 = gm_l12;
                            uint64_t t426 = (uintptr_t)t425;
                            uint64_t t427 = gm_l13;
                            uint64_t t428 = (uintptr_t)t427;
                            if (t426 > t428 || t428 > (uintptr_t)t423) gm_trap_slice(t426, t428, t423, gm_src8kcterbt0p7b, 255, 33);
                            gm_b16_8 t429;
                            GM_ST(void *, (uint8_t *)&t429, (void *)(((uint8_t *)t424 + t426 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t429 + 8, (uintptr_t)(t428 - t426));
                            gm_l19 = t429;
                        }
                        goto ix419;
ie419:;
ix419:;
                    }
                    {
                        void * t430 = (void *)((uint8_t *)&F.l2);
                        gm_t88jm1dgsmkjs t431;
                        memset((uint8_t *)&t431 + 37, 0, 3);
                        uint64_t t432 = gm_l12;
                        GM_ST(uint64_t, (uint8_t *)&t431 + 0, t432);
                        uint64_t t433 = gm_l13;
                        GM_ST(uint64_t, (uint8_t *)&t431 + 8, t433);
                        gm_b16_8 t434 = gm_l19;
                        GM_ST(gm_b16_8, (uint8_t *)&t431 + 16, t434);
                        uint8_t t435 = gm_l14;
                        GM_ST(uint8_t, (uint8_t *)&t431 + 32, t435);
                        uint8_t t436 = gm_l15;
                        GM_ST(uint8_t, (uint8_t *)&t431 + 33, t436);
                        uint8_t t437 = gm_l16;
                        GM_ST(uint8_t, (uint8_t *)&t431 + 34, t437);
                        uint8_t t438 = gm_l17;
                        GM_ST(uint8_t, (uint8_t *)&t431 + 35, t438);
                        uint8_t t439 = gm_l18;
                        GM_ST(uint8_t, (uint8_t *)&t431 + 36, t439);
                        gm_tftb1odc739t5 t440 = gm_f_push_n6hbl90(t430, &t431);
                        if (((((uint8_t *)&t440))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t440) + 1, gm_src8kcterbt0p7b, 256, 9);
                        gm_b0_1 t441 = GM_LD(gm_b0_1, (((uint8_t *)&t440) + 1));
                    }
                    {
                        uint64_t t442 = gm_l10;
                        gm_tf5srh4mp4b3i t443;
                        GM_ST(uint8_t, (uint8_t *)&t443 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t443 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t443 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t443), t442);
                        ((uint8_t *)&t443)[8] = 1;
                        gm_l4 = t443;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 121, 5);
me14:;
                }
lc7:;
                {
                    uint64_t t445 = gm_l9;
                    uint64_t r446; bool o447 = __builtin_add_overflow(t445, ((uint64_t)1ULL), &r446);
                    uint64_t t448 = r446;
                    gm_l9 = t448;
                }
            }
lb7:;
        }
    }
    {
        uint8_t t449 = 0;
        F.ret = (*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l2));
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t449;
        goto gm_ld1_2;
    }
    {
        void * t450 = (void *)((uint8_t *)&F.l7);
        gm_f_deinit_d5p9g61(t450);
    }
    {
        void * t451 = (void *)((uint8_t *)&F.l5);
        gm_f_deinit_00kfno3(t451);
    }
    goto gm_ls452;
gm_ld1_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t453 = (void *)((uint8_t *)&F.l7);
        gm_f_deinit_d5p9g61(t453);
        gm_unw = gm_saved;
    }
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t454 = (void *)((uint8_t *)&F.l5);
        gm_f_deinit_00kfno3(t454);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls452:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_label_colon_rkt3ka8(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_label_colon_rkt3ka8 F;
    gm_b16_8 gm_l0; /* toks */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    uint64_t t2 = gm_l1;
    uint64_t r3; bool o4 = __builtin_add_overflow(t2, ((uint64_t)1ULL), &r3);
    if (o4) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 103, 84);
    uint64_t t5 = r3;
    gm_t3fcbuehh3bs2 t6;
    gm_f_kind_at_lviicpm((void *)&t6, t1, t5);
    uint8_t t7;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    if (t12 != ((uint8_t)2ULL)) goto pa11;
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)&t6) + 8) + 0));
    if (t13 != ((uint8_t)31ULL)) goto pa11;
    goto po10;
pa11:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    if (t15 != ((uint8_t)2ULL)) goto pa14;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)&t6) + 8) + 0));
    if (t16 != ((uint8_t)12ULL)) goto pa14;
    goto po10;
pa14:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    if (t18 != ((uint8_t)2ULL)) goto pa17;
    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)&t6) + 8) + 0));
    if (t19 != ((uint8_t)30ULL)) goto pa17;
    goto po10;
pa17:;
    goto mn9;
po10:;
    t7 = ((uint8_t)1);
    goto me8;
mn9:;
    t7 = ((uint8_t)0);
    goto me8;
mn20:;
    gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 103, 64);
me8:;
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_punct_hnuckjb(const void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_is_punct_hnuckjb_c(gm_t3fcbuehh3bs2 a0, gm_t9qkh0rsooc8d a1) { return gm_f_is_punct_hnuckjb(&a0, a1); }
inline __attribute__((aligned(4))) void gm_f_kind_at_lviicpm(void *gm_out, gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_at_lviicpm F;
    gm_b16_8 gm_l0; /* toks */
    uint64_t gm_l1; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t3 < t4);
        if (!t5) goto ie2;
        {
            uint64_t t7 = gm_l1;
            uint64_t t8 = (uintptr_t)t7;
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_src8kcterbt0p7b, 29, 30);
            memmove(gm_out, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 48) + 0), 32);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3fcbuehh3bs2 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3fcbuehh3bs2 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3fcbuehh3bs2 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t3fcbuehh3bs2 *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t3fcbuehh3bs2 *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t3fcbuehh3bs2 *)gm_out) + 24, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3fcbuehh3bs2 *)gm_out) + 0, ((uint8_t)10ULL));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t3fcbuehh3bs2 gm_f_kind_at_lviicpm_c(gm_b16_8 a0, uint64_t a1) { gm_t3fcbuehh3bs2 gm_r; gm_f_kind_at_lviicpm(&gm_r, a0, a1); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_reset_colon_m7bd948(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_type_after_bracke_9p8imef(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_type_after_bracke_9p8imef F;
    gm_t3fcbuehh3bs2 gm_l0; /* next */
    GM_STACK_CHECK_SHALLOW(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t6 != ((uint8_t)0ULL)) goto pa5;
    goto po4;
pa5:;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t8 != ((uint8_t)1ULL)) goto pa7;
    goto po4;
pa7:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t10 != ((uint8_t)6ULL)) goto pa9;
    goto po4;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t12 != ((uint8_t)2ULL)) goto pa11;
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t13 != ((uint8_t)11ULL)) goto pa11;
    goto po4;
pa11:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t15 != ((uint8_t)8ULL)) goto pa14;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t16 != ((uint8_t)45ULL)) goto pa14;
    goto po4;
pa14:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t18 != ((uint8_t)8ULL)) goto pa17;
    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t19 != ((uint8_t)47ULL)) goto pa17;
    goto po4;
pa17:;
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t21 != ((uint8_t)8ULL)) goto pa20;
    uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t22 != ((uint8_t)63ULL)) goto pa20;
    goto po4;
pa20:;
    goto mn3;
po4:;
    t1 = ((uint8_t)1);
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn23:;
    gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 78, 61);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_starts_type_after_bracke_9p8imef_c(gm_t3fcbuehh3bs2 a0) { return gm_f_starts_type_after_bracke_9p8imef(&a0); }
inline __attribute__((aligned(4))) uint8_t gm_f_tight_paren_keyword_rj4a6k9(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_tight_paren_keyword_rj4a6k9 F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 22u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t1 = gm_l0;
    gm_t9qkh0rsooc8d t2;
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)11ULL));
    gm_t9qkh0rsooc8d t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)24ULL));
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)27ULL));
    gm_t9qkh0rsooc8d t5;
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)6ULL));
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)4ULL));
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)9ULL));
    gm_t9qkh0rsooc8d t8;
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)14ULL));
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)21ULL));
    gm_t8doqk5beq9nl t10;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 0), t2);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 1), t3);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 2), t4);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 3), t5);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 4), t6);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 5), t7);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 6), t8);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 7), t9);
    uint8_t t11 = 0;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t10) + 0, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t10) + 1, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t10) + 2, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t10) + 3, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t10) + 4, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t10) + 5, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t10) + 6, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t10) + 7, ((uint8_t *)&t1))) t11 = 1;
    uint8_t t12 = (uint8_t)(t11);
    F.ret = t12;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_tight_paren_prev_p7h76um(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_tight_paren_prev_p7h76um_c(gm_t3fcbuehh3bs2 a0) { return gm_f_tight_paren_prev_p7h76um(&a0); }
__attribute__((aligned(4))) uint8_t gm_f_type_follows_2br4pnf(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_type_follows_2br4pnf F;
    gm_b16_8 gm_l0; /* toks */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* j */
    gm_t3fcbuehh3bs2 gm_l3; /* k1 */
    gm_t3fcbuehh3bs2 gm_l4; /* k2 */
    GM_STACK_CHECK_SHALLOW(&F, 332u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        gm_l2 = t2;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb3;
            {
                gm_b16_8 t6 = gm_l0;
                uint64_t t7 = gm_l2;
                gm_t3fcbuehh3bs2 t8;
                gm_f_kind_at_lviicpm((void *)&t8, t6, t7);
                uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
                switch (t15) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma13; default: goto ma13; }
ma10:;
                uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
                if (t19 != ((uint8_t)0ULL)) goto pa18;
                goto po17;
pa18:;
                uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
                if (t21 != ((uint8_t)1ULL)) goto pa20;
                goto po17;
pa20:;
                goto mn16;
po17:;
                uint8_t t22 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
                goto me9;
mn16:;
ma11:;
                uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
                if (t26 != ((uint8_t)8ULL)) goto pa25;
                uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
                if (t27 != ((uint8_t)45ULL)) goto pa25;
                goto po24;
pa25:;
                uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
                if (t29 != ((uint8_t)8ULL)) goto pa28;
                uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
                if (t30 != ((uint8_t)47ULL)) goto pa28;
                goto po24;
pa28:;
                goto mn23;
po24:;
                {
                    uint64_t t32 = gm_l2;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    if (o34) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 63, 52);
                    uint64_t t35 = r33;
                    gm_l2 = t35;
                }
                goto me9;
mn23:;
ma12:;
                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
                if (t37 != ((uint8_t)8ULL)) goto mn36;
                uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
                if (t38 != ((uint8_t)63ULL)) goto mn36;
                {
                    gm_b16_8 t40 = gm_l0;
                    uint64_t t41 = gm_l2;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    if (o43) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 65, 37);
                    uint64_t t44 = r42;
                    gm_f_kind_at_lviicpm((void *)&gm_l3, t40, t44);
                }
                {
                    gm_b16_8 t45 = gm_l0;
                    uint64_t t46 = gm_l2;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)2ULL), &r47);
                    if (o48) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 66, 37);
                    uint64_t t49 = r47;
                    gm_f_kind_at_lviicpm((void *)&gm_l4, t45, t49);
                }
                {
                    gm_t9qkh0rsooc8d t51;
                    GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)64ULL));
                    uint8_t t52 = gm_f_is_punct_hnuckjb(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l3)), t51);
                    if (!t52) goto ie50;
                    {
                        uint8_t t54 = 0;
                        F.ret = ((uint8_t)1);
                        goto gm_fret;
                    }
                    goto ix50;
ie50:;
ix50:;
                }
                {
                    gm_t9qkh0rsooc8d t56;
                    GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)47ULL));
                    uint8_t t57 = gm_f_is_punct_hnuckjb(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l3)), t56);
                    if (!t57) goto ie55;
                    gm_t9qkh0rsooc8d t58;
                    GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)64ULL));
                    uint8_t t59 = gm_f_is_punct_hnuckjb(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l4)), t58);
                    if (!t59) goto ie55;
                    {
                        uint8_t t61 = 0;
                        F.ret = ((uint8_t)1);
                        goto gm_fret;
                    }
                    goto ix55;
ie55:;
ix55:;
                }
                {
                    uint8_t t63;
                    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    if (t66 != ((uint8_t)3ULL)) goto mn65;
                    t63 = ((uint8_t)1);
                    goto me64;
mn65:;
                    t63 = ((uint8_t)0);
                    goto me64;
mn67:;
                    gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 69, 20);
me64:;
                    if (!t63) goto ie62;
                    gm_t9qkh0rsooc8d t68;
                    GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)64ULL));
                    uint8_t t69 = gm_f_is_punct_hnuckjb(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l4)), t68);
                    if (!t69) goto ie62;
                    {
                        gm_b16_8 t71 = gm_l0;
                        uint64_t t72 = gm_l2;
                        uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)3ULL), &r73);
                        if (o74) gm_trap("integer overflow in `+`", gm_src8kcterbt0p7b, 69, 111);
                        uint64_t t75 = r73;
                        gm_t3fcbuehh3bs2 t76;
                        gm_f_kind_at_lviicpm((void *)&t76, t71, t75);
                        uint8_t t77 = gm_f_starts_type_after_bracke_9p8imef(&t76);
                        uint8_t t78 = 0;
                        F.ret = t77;
                        goto gm_fret;
                    }
                    goto ix62;
ie62:;
ix62:;
                }
                {
                    uint8_t t79 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                goto me9;
mn36:;
ma13:;
                uint8_t t81 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
                goto me9;
mn80:;
ma14:;
                gm_trap("no match arm matched the scrutinee", gm_src8kcterbt0p7b, 61, 9);
me9:;
            }
lc3:;
        }
lb3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
