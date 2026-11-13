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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t f1; } gm_ttsm01t0d8srg;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; int64_t f1; } gm_t4d55u65a2eb7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; uint8_t f2; uint8_t pe[7]; } gm_th5c3ge61oapm;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttsm01t0d8srg) == 1 && _Alignof(gm_ttsm01t0d8srg) == 1 && offsetof(gm_ttsm01t0d8srg, f1) == 0, "layout of `?()`");
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { uint64_t f0; gm_th2lqbo77pvsc f1; } gm_t13urt52695sh;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t4d55u65a2eb7) == 24 && _Alignof(gm_t4d55u65a2eb7) == 8 && offsetof(gm_t4d55u65a2eb7, f0) == 0 && offsetof(gm_t4d55u65a2eb7, f1) == 16, "layout of `([]u8, i64)`");
typedef struct { gm_th5c3ge61oapm f0; uint8_t f1; uint8_t pe[7]; } gm_t4ml5v7ugcjrt;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_t4c7ipjj9s3fp f0; gm_trqfvmjtt4sij f1; uint8_t f2; uint8_t p3[7]; gm_trqfvmjtt4sij f3; gm_t4c7ipjj9s3fp f4; gm_t4c7ipjj9s3fp f5; } gm_tbpdf5ej15iof;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th5c3ge61oapm) == 32 && _Alignof(gm_th5c3ge61oapm) == 8 && offsetof(gm_th5c3ge61oapm, f0) == 0 && offsetof(gm_th5c3ge61oapm, f1) == 16 && offsetof(gm_th5c3ge61oapm, f2) == 24, "layout of `EnumInfo`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_t13urt52695sh) == 32 && _Alignof(gm_t13urt52695sh) == 8 && offsetof(gm_t13urt52695sh, f0) == 0 && offsetof(gm_t13urt52695sh, f1) == 8, "layout of `(i: usize, x: ?string)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4ml5v7ugcjrt) == 40 && _Alignof(gm_t4ml5v7ugcjrt) == 8 && offsetof(gm_t4ml5v7ugcjrt, f0) == 0 && offsetof(gm_t4ml5v7ugcjrt, f1) == 32, "layout of `?EnumInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbpdf5ej15iof) == 256 && _Alignof(gm_tbpdf5ej15iof) == 8 && offsetof(gm_tbpdf5ej15iof, f0) == 0 && offsetof(gm_tbpdf5ej15iof, f1) == 56 && offsetof(gm_tbpdf5ej15iof, f2) == 96 && offsetof(gm_tbpdf5ej15iof, f3) == 104 && offsetof(gm_tbpdf5ej15iof, f4) == 144 && offsetof(gm_tbpdf5ej15iof, f5) == 200, "layout of `Views`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { gm_ttu09lij36f8c f0; uint8_t f1; uint8_t pe[7]; } gm_tu3261i9p5758;
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tu3261i9p5758) == 48 && _Alignof(gm_tu3261i9p5758) == 8 && offsetof(gm_tu3261i9p5758, f0) == 0 && offsetof(gm_tu3261i9p5758, f1) == 40, "layout of `??View`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
struct gm_fr_debug_view_gp38euq {
    gm_frame h;
    gm_ttu09lij36f8c l2; /* v */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_define_enum_v780plg {
    gm_frame h;
    gm_tckdobunaulee l14; /* v */
    gm_trqfvmjtt4sij l15; /* items */
    gm_tckdobunaulee l20; /* v */
    gm_b16_8 ret;
};
struct gm_fr_define_view_sob497j {
    gm_frame h;
    gm_ttu09lij36f8c l2; /* cached */
    gm_trqfvmjtt4sij l7; /* members */
    gm_t4c7ipjj9s3fp l8; /* taken */
    gm_trqfvmjtt4sij l11; /* labels */
    gm_trqfvmjtt4sij l30; /* arms */
    gm_tpautr4br7h3l l31; /* def */
    gm_trqfvmjtt4sij l32; /* names */
    gm_t0mglksrkfa5e l35; /* f */
    gm_t0mglksrkfa5e l40; /* f */
    gm_trqfvmjtt4sij l43; /* names */
    gm_t0mglksrkfa5e l46; /* f */
    gm_trqfvmjtt4sij l48; /* arms */
    gm_t0mglksrkfa5e l52; /* f */
    gm_trqfvmjtt4sij l56; /* names */
    gm_tckdobunaulee l59; /* vd */
    gm_trqfvmjtt4sij l61; /* arms */
    gm_tckdobunaulee l65; /* vd */
    gm_trqfvmjtt4sij l67; /* body */
    gm_b0_1 ret;
};
struct gm_fr_enum_info_61n2nkp {
    gm_frame h;
    gm_t4ml5v7ugcjrt ret;
};
struct gm_fr_is_aggregate_buh1uic {
    gm_frame h;
    gm_tpautr4br7h3l l2; /* def */
    gm_tckdobunaulee l6; /* v */
    uint8_t ret;
};
struct gm_fr_packed_v79gj1r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pad_juftt9c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_pointer_view_efkbf4q {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pop_knqt0be {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_push_member_eubf7s6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_push_union_m2cqbk2 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* decls */
    gm_b0_1 ret;
};
struct gm_fr_reserve_40g0de6 {
    gm_frame h;
    gm_ttu09lij36f8c l4; /* v */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_scalar_f0e206b {
    gm_frame h;
    gm_ttu09lij36f8c ret;
};
struct gm_fr_static_assert_15uo2jf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_target_view_ams6b9s {
    gm_frame h;
    gm_ttu09lij36f8c l2; /* cached */
    gm_b16_8 ret;
};
struct gm_fr_view_defs_u461okc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_view_kind_pth6nol {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_view_number_29hi30b {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qkqm3in {
    gm_frame h;
    gm_ttu09lij36f8c l2; /* cached */
    gm_ttu09lij36f8c l7; /* r */
    gm_ttu09lij36f8c l11; /* r */
    gm_ttu09lij36f8c l12; /* v */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_views_bc1b3r4 {
    gm_frame h;
    gm_tbpdf5ej15iof ret;
};
__attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_debug_view_gp38euq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_define_enum_v780plg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_define_view_sob497j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_enum_info_61n2nkp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_08mmoe2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_0bd7vot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_3qphnbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6a91mh8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7c4i91d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_7nu71mn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8ibn6ei(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_cn4968h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hn5gv7p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ivs3cj0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jk4dii4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kl4ha3s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lfjb9aq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_of6149p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_phh9f1s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_tc7tn98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ubnq9mq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_v301q4v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_get_fq78978(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_p217beu(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_v0dhnfj(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint64_t gm_f_hash64_apkb7bo(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_hash_map_5kr0r9p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hash_map_9mnq5mu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_aggregate_buh1uic(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_ige9b74(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_at7dsst(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_member_name_mj2bvs3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_packed_v79gj1r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_pad_juftt9c(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_pointer_view_efkbf4q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tkn42u7na3t81 gm_f_pop_knqt0be(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_m7lva8q(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_member_eubf7s6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_union_m2cqbk2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_00pon0c(void * a0, uint64_t a1, gm_b0_1 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_25ngckq(void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_remove_6blu1gh(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_reserve_40g0de6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_scalar_f0e206b(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_ttu09lij36f8c gm_f_scalar_f0e206b_c(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_static_assert_15uo2jf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_target_view_ams6b9s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_to_i64_4cgpvak(const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_view_members_3bvsjcm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_view_defs_u461okc(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_view_kind_pth6nol(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_view_number_29hi30b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_view_qkqm3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_views_bc1b3r4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dqbiqk0(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ier753o(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u1no4ia(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ue93873(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vfiknt5(void * a0, const void * a1);
extern uint8_t gm_cgsmfcq937e_3[16] __attribute__((aligned(8)));
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3p17h7dbk8us[8];
extern const uint8_t gm_s420tm9ua7ggg[4];
extern const uint8_t gm_s44ek1gecbajs[7];
extern const uint8_t gm_s4v09sn49l08n[7];
extern const uint8_t gm_s557bv2sfqcom[6];
extern const uint8_t gm_s5qkai093c1q0[6];
extern const uint8_t gm_sakkgen9m77d6[16];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_shhr1hlq1ethc[4];
extern const uint8_t gm_si68bogf7eu32[8];
extern const uint8_t gm_sk2tgsi47prt7[14];
extern const uint8_t gm_sk8hn2ar9njah[15];
extern const uint8_t gm_smg3p90in08sb[10];
extern const uint8_t gm_snokcvhtidt97[6];
extern const uint8_t gm_soim4k70n6bc7[3];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcs9q7ghsrcgv5[];
extern const uint8_t gm_stntm4cqqrhf7[7];
extern const uint8_t gm_sts5j6f5pkov0[5];
extern const uint8_t gm_sumv38t63phct[14];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tkn42u7na3t81 gm_f_pop_knqt0be(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_knqt0be F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t88rcucg1b7uk gm_l2; /* x */
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
            gm_tkn42u7na3t81 t8;
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
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 4));
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
        gm_t88rcucg1b7uk t29 = gm_l2;
        gm_tkn42u7na3t81 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t30), t29);
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
inline __attribute__((aligned(4))) void gm_f_debug_view_gp38euq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_debug_view_gp38euq F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l3; /* p */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 872) + 96));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            gm_t88rcucg1b7uk t7 = gm_l1;
            gm_f_view_qkqm3in((void *)&(*(gm_ttu09lij36f8c *)gm_out), (void *)NULL, cx, NULL, t6, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        GM_ST(uint8_t, ((((uint8_t *)t8) + 872) + 96), ((uint8_t)1));
    }
    {
        void * t9 = gm_l0;
        gm_t88rcucg1b7uk t10 = gm_l1;
        gm_f_view_qkqm3in((void *)&F.l2, (void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            void * t12 = gm_l0;
            void * t13 = (void *)((((uint8_t *)t12) + 872) + 56);
            gm_tkn42u7na3t81 t14 = gm_f_pop_knqt0be(t13);
            gm_tkn42u7na3t81 t15 = t14;
            if (!((((uint8_t *)&t15))[4] != 0)) goto lb11;
            gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t15));
            gm_l3 = t16;
            {
                void * t19 = gm_l0;
                gm_t88rcucg1b7uk t20 = gm_l3;
                gm_b0_1 t21 = gm_f_define_view_sob497j((void *)NULL, cx, NULL, t19, t20);
                if (gm_unw.kind) {
                    goto gm_ll17;
                }
            }
lc11:;
        }
gm_ll17:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
        }
        goto gm_lad;
lb11:;
    }
    {
        void * t22 = gm_l0;
        GM_ST(uint8_t, ((((uint8_t *)t22) + 872) + 96), ((uint8_t)0));
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
__attribute__((aligned(4))) gm_b16_8 gm_f_define_enum_v780plg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_define_enum_v780plg F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* view */
    void * gm_l3; /* def */
    gm_t88rcucg1b7uk gm_l4; /* tag */
    gm_b16_8 gm_l5; /* variants */
    gm_th5c3ge61oapm gm_l6; /* info */
    gm_b16_8 gm_l7; /* name */
    uint64_t gm_l8; /* n */
    gm_b16_8 gm_l9; /* prefix */
    uint64_t gm_l10; /* k */
    uint8_t gm_l11; /* clash */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    int64_t gm_l16; /* lo */
    int64_t gm_l17; /* hi */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_b16_8 gm_l21; /* e */
    int64_t gm_l22; /* value */
    uint64_t gm_l23; /* natural */
    gm_tc0nbpdhofi37 gm_l24; /* <tmp> */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* s */
    uint64_t gm_l28; /* bits */
    uint8_t gm_l29; /* fits */
    uint64_t gm_l30; /* bits */
    int64_t gm_l31; /* wide */
    gm_b16_8 gm_l32; /* e */
    GM_STACK_CHECK(&F, 3136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        void * t4 = gm_f_adt_of_gej99vn(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_l3 = t4;
    }
    {
        void * t6 = gm_l3;
        uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 24) + 0));
        if (t7 != ((uint8_t)1ULL)) goto ie5;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t6) + 24) + 8) + 0));
        gm_l4 = t8;
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((((uint8_t *)t6) + 24) + 8) + 8));
        gm_l5 = t9;
        {
            void * t11 = gm_l0;
            gm_t88rcucg1b7uk t12 = gm_l1;
            gm_t4ml5v7ugcjrt t13;
            gm_f_enum_info_61n2nkp((void *)&t13, (void *)NULL, cx, NULL, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t4ml5v7ugcjrt t14 = t13;
            gm_th5c3ge61oapm t15;
            if (((((uint8_t *)&t14))[32] != 0)) {
                gm_th5c3ge61oapm t16 = GM_LD(gm_th5c3ge61oapm, (((uint8_t *)&t14) + 0));
                t15 = t16;
            } else {
                void * t17 = gm_l0;
                gm_t88rcucg1b7uk t18 = gm_l4;
                gm_b16_8 t19 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t17, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t20 = 0;
                F.ret = t19;
                goto gm_fret;
            }
            gm_l6 = t15;
        }
        {
            gm_b16_8 t21 = gm_l2;
            gm_l7 = t21;
        }
        {
            gm_b16_8 t23 = gm_l2;
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_s557bv2sfqcom));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(5));
            uint8_t t25 = gm_f_starts_with_6vvt01e(t23, t24);
            uint8_t t26 = (uint8_t)!t25;
            if (!t26) goto ie22;
            {
                void * t28 = gm_l0;
                void * t29 = gm_l0;
                gm_t88rcucg1b7uk t30 = gm_l1;
                gm_b16_8 t31 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t29, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t32;
                GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t31);
                gm_tabb710d9mq2e t33;
                gm_f_format_kl4ha3s((void *)&t33, (void *)NULL, cx, NULL, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t34 = t33;
                if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srcs9q7ghsrcgv5, 377, 29);
                gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
                gm_b16_8 t36 = t35;
                gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                uint64_t t38 = gm_f_view_number_29hi30b((void *)NULL, cx, NULL, t28, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t38;
            }
            {
                uint64_t t39 = gm_l8;
                gm_t4ce0jd3tdro4 t40;
                GM_ST(uint64_t, ((uint8_t *)&t40 + 0), t39);
                gm_tabb710d9mq2e t41;
                gm_f_format_8ibn6ei((void *)&t41, (void *)NULL, cx, NULL, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t42 = t41;
                if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_srcs9q7ghsrcgv5, 378, 16);
                gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
                gm_b16_8 t44 = t43;
                gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                gm_l7 = t45;
            }
            goto ix22;
ie22:;
ix22:;
        }
        {
            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
            gm_l9 = t46;
        }
        {
            gm_l10 = ((uint64_t)3ULL);
        }
        {
            for (;;) {
                if (!((uint8_t)1)) goto lb47;
                {
                    gm_l11 = ((uint8_t)0);
                }
                {
                    {
                        gm_b16_8 t51 = gm_l5;
                        gm_l12 = t51;
                    }
                    {
                        gm_l13 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t53 = gm_l13;
                            uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                            uint8_t t55 = (t53 < t54);
                            if (!t55) goto lb52;
                            {
                                uint64_t t58 = gm_l13;
                                uint64_t t59 = (uintptr_t)t58;
                                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                gm_tckdobunaulee t63 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t59 * 72));
                                F.l14 = t63;
                                {
                                    void * t66 = gm_l0;
                                    void * t67 = (void *)((((uint8_t *)t66) + 872) + 200);
                                    gm_t6j6smj7qoctu t68;
                                    gm_b16_8 t69 = gm_l9;
                                    GM_ST(gm_b16_8, (uint8_t *)&t68 + 0, t69);
                                    gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                                    GM_ST(gm_b16_8, (uint8_t *)&t68 + 16, t70);
                                    gm_tabb710d9mq2e t71;
                                    gm_f_format_of6149p((void *)&t71, (void *)NULL, cx, NULL, &t68);
                                    if (gm_unw.kind) {
                                        goto gm_ll56;
                                    }
                                    gm_tabb710d9mq2e t72 = t71;
                                    if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_srcs9q7ghsrcgv5, 387, 41);
                                    gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
                                    gm_b16_8 t74 = t73;
                                    gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                                    gm_tqs5p4n2tq19v t76 = gm_f_get_p217beu(t67, t75);
                                    gm_tqs5p4n2tq19v t77 = t76;
                                    uint8_t t78;
                                    if (((((uint8_t *)&t77))[1] != 0)) goto mn80;
                                    t78 = ((uint8_t)0);
                                    goto me79;
mn80:;
                                    t78 = ((uint8_t)1);
                                    goto me79;
mn81:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 387, 16);
me79:;
                                    if (!t78) goto ie65;
                                    {
                                        gm_l11 = ((uint8_t)1);
                                    }
                                    goto ix65;
ie65:;
ix65:;
                                }
                                goto me61;
mn62:;
                                gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 386, 9);
me61:;
                            }
lc52:;
                            {
                                uint64_t t84 = gm_l13;
                                uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                                uint64_t t87 = r85;
                                gm_l13 = t87;
                            }
                        }
gm_ll56:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb52; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc52; }
                        }
                        goto gm_ll48;
lb52:;
                    }
                }
                {
                    uint8_t t89 = gm_l11;
                    uint8_t t90 = (uint8_t)!t89;
                    if (!t90) goto ie88;
                    {
                        goto lb47;
                    }
                    goto ix88;
ie88:;
ix88:;
                }
                {
                    gm_t6j6smj7qoctu t92;
                    gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t92 + 0, t93);
                    void * t94 = gm_l0;
                    gm_t88rcucg1b7uk t95 = gm_l1;
                    gm_b16_8 t96 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t94, t95);
                    if (gm_unw.kind) {
                        goto gm_ll48;
                    }
                    uint64_t t97 = gm_l10;
                    gm_b16_8 t98 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t96, t97);
                    if (gm_unw.kind) {
                        goto gm_ll48;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t92 + 16, t98);
                    gm_tabb710d9mq2e t99;
                    gm_f_format_of6149p((void *)&t99, (void *)NULL, cx, NULL, &t92);
                    if (gm_unw.kind) {
                        goto gm_ll48;
                    }
                    gm_tabb710d9mq2e t100 = t99;
                    if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 8, gm_srcs9q7ghsrcgv5, 390, 18);
                    gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&t100) + 8));
                    gm_b16_8 t102 = t101;
                    gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
                    gm_l9 = t103;
                }
                {
                    uint64_t t104 = gm_l10;
                    uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                    if (o106) gm_trap("integer overflow in `+`", gm_srcs9q7ghsrcgv5, 391, 9);
                    uint64_t t107 = r105;
                    gm_l10 = t107;
                }
lc47:;
            }
gm_ll48:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb47; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc47; }
            }
            goto gm_lad;
lb47:;
        }
        {
            gm_f_list_epvducg((void *)&F.l15, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l16 = ((int64_t)0LL);
        }
        {
            gm_l17 = ((int64_t)0LL);
        }
        {
            {
                gm_b16_8 t109 = gm_l5;
                gm_l18 = t109;
            }
            {
                gm_l19 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t111 = gm_l19;
                    uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    uint8_t t113 = (t111 < t112);
                    if (!t113) goto lb110;
                    {
                        uint64_t t116 = gm_l19;
                        uint64_t t117 = (uintptr_t)t116;
                        uint64_t t118 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                        gm_tckdobunaulee t121 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t117 * 72));
                        F.l20 = t121;
                        {
                            gm_t6j6smj7qoctu t123;
                            gm_b16_8 t124 = gm_l9;
                            GM_ST(gm_b16_8, (uint8_t *)&t123 + 0, t124);
                            gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t123 + 16, t125);
                            gm_tabb710d9mq2e t126;
                            gm_f_format_of6149p((void *)&t126, (void *)NULL, cx, NULL, &t123);
                            if (gm_unw.kind) {
                                goto gm_ll114;
                            }
                            gm_tabb710d9mq2e t127 = t126;
                            if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 8, gm_srcs9q7ghsrcgv5, 397, 14);
                            gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&t127) + 8));
                            gm_l21 = t128;
                        }
                        {
                            gm_t913bb735pvt5 t129 = gm_f_to_i64_4cgpvak(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l20) + 24)));
                            gm_t913bb735pvt5 t130 = t129;
                            int64_t t131;
                            if (((((uint8_t *)&t130))[8] != 0)) {
                                int64_t t132 = GM_LD(int64_t, (((uint8_t *)&t130) + 0));
                                t131 = t132;
                            } else {
                                t131 = ((int64_t)0LL);
                            }
                            gm_l22 = t131;
                        }
                        {
                            int64_t t133 = gm_l16;
                            int64_t t134 = gm_l22;
                            int64_t t135 = (t133 < t134 ? t133 : t134);
                            gm_l16 = t135;
                        }
                        {
                            int64_t t136 = gm_l17;
                            int64_t t137 = gm_l22;
                            int64_t t138 = (t136 > t137 ? t136 : t137);
                            gm_l17 = t138;
                        }
                        {
                            void * t139 = gm_l0;
                            void * t140 = (void *)((((uint8_t *)t139) + 872) + 200);
                            gm_b16_8 t141 = gm_l21;
                            gm_b16_8 t142 = t141;
                            gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
                            gm_tftb1odc739t5 t144 = gm_f_put_5c6qgui(t140, t143, ((uint8_t)1));
                            gm_tftb1odc739t5 t145 = t144;
                            if (((((uint8_t *)&t145))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t145) + 1, gm_srcs9q7ghsrcgv5, 401, 9);
                            gm_b0_1 t146 = GM_LD(gm_b0_1, (((uint8_t *)&t145) + 1));
                        }
                        {
                            void * t147 = gm_l0;
                            gm_b16_8 t148 = gm_l21;
                            gm_b16_8 t149 = t148;
                            gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                            gm_b0_1 t151 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t147, t150);
                            if (gm_unw.kind) {
                                goto gm_ll114;
                            }
                        }
                        {
                            void * t152 = (void *)((uint8_t *)&F.l15);
                            gm_t4d55u65a2eb7 t153;
                            gm_b16_8 t154 = gm_l21;
                            GM_ST(gm_b16_8, (uint8_t *)&t153 + 0, t154);
                            int64_t t155 = gm_l22;
                            GM_ST(int64_t, (uint8_t *)&t153 + 16, t155);
                            gm_tabb710d9mq2e t156;
                            gm_f_format_3qphnbe((void *)&t156, (void *)NULL, cx, NULL, &t153);
                            if (gm_unw.kind) {
                                goto gm_ll114;
                            }
                            gm_tabb710d9mq2e t157 = t156;
                            if (((((uint8_t *)&t157))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t157) + 8, gm_srcs9q7ghsrcgv5, 403, 20);
                            gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&t157) + 8));
                            gm_b16_8 t159 = t158;
                            gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&t159));
                            gm_tftb1odc739t5 t161 = gm_f_push_3ef746b(t152, t160);
                            gm_tftb1odc739t5 t162 = t161;
                            if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 1, gm_srcs9q7ghsrcgv5, 403, 9);
                            gm_b0_1 t163 = GM_LD(gm_b0_1, (((uint8_t *)&t162) + 1));
                        }
                        goto me119;
mn120:;
                        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 396, 5);
me119:;
                    }
lc110:;
                    {
                        uint64_t t165 = gm_l19;
                        uint64_t r166; bool o167 = __builtin_add_overflow(t165, ((uint64_t)1ULL), &r166);
                        uint64_t t168 = r166;
                        gm_l19 = t168;
                    }
                }
gm_ll114:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb110; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc110; }
                }
                goto gm_lad;
lb110:;
            }
        }
        {
            gm_l23 = ((uint64_t)4ULL);
        }
        {
            {
                {
                    GM_ST(uint64_t, (uint8_t *)&gm_l24 + 0, ((uint64_t)1ULL));
                    GM_ST(uint64_t, (uint8_t *)&gm_l24 + 8, ((uint64_t)2ULL));
                    GM_ST(uint64_t, (uint8_t *)&gm_l24 + 16, ((uint64_t)4ULL));
                }
                void * t171 = ((uint8_t *)&gm_l24);
                if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcs9q7ghsrcgv5, 408, 5);
                gm_b16_8 t172;
                GM_ST(void *, (uint8_t *)&t172, (void *)(((uint8_t *)t171 + (uintptr_t)0 * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
                gm_b16_8 t173 = t172;
                gm_l25 = t173;
            }
            {
                gm_l26 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t175 = gm_l26;
                    uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                    uint8_t t177 = (t175 < t176);
                    if (!t177) goto lb174;
                    {
                        uint64_t t180 = gm_l26;
                        uint64_t t181 = (uintptr_t)t180;
                        uint64_t t182 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                        uint64_t t183 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t181 * 8));
                        uint64_t t184 = t183;
                        uint64_t t187 = GM_LD(uint64_t, ((uint8_t *)&t184));
                        gm_l27 = t187;
                        {
                            uint64_t t189 = gm_l27;
                            uint64_t r190; bool o191 = __builtin_mul_overflow(((uint64_t)8ULL), t189, &r190);
                            if (o191) gm_trap("integer overflow in `*`", gm_srcs9q7ghsrcgv5, 409, 17);
                            uint64_t t192 = r190;
                            gm_l28 = t192;
                        }
                        {
                            uint8_t t193;
                            int64_t t195 = gm_l16;
                            uint8_t t196 = (t195 < ((int64_t)0LL));
                            if (!t196) goto ie194;
                            uint8_t t198 = 0;
                            int64_t t199 = gm_l16;
                            uint64_t t200 = gm_l28;
                            uint64_t r201; bool o202 = __builtin_sub_overflow(t200, ((uint64_t)1ULL), &r201);
                            if (o202) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 410, 48);
                            uint64_t t203 = r201;
                            unsigned __int128 n204 = (unsigned __int128)t203;
                            if (n204 >= 64) gm_trap_shift((uint64_t)n204, "i64", gm_srcs9q7ghsrcgv5, 410, 37);
                            int64_t r205 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n204);
                            bool o206 = (int64_t)(r205 >> (int)n204) != ((int64_t)1LL);
                            if (o206) gm_trap("integer overflow in `<<`", gm_srcs9q7ghsrcgv5, 410, 37);
                            int64_t t207 = r205;
                            if (t207 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 410, 35);
                            int64_t t208 = (int64_t)((uint64_t)0 - (uint64_t)t207);
                            uint8_t t209 = (t199 >= t208);
                            if (t209) {
                                int64_t t210 = gm_l17;
                                uint64_t t211 = gm_l28;
                                uint64_t r212; bool o213 = __builtin_sub_overflow(t211, ((uint64_t)1ULL), &r212);
                                if (o213) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 410, 80);
                                uint64_t t214 = r212;
                                unsigned __int128 n215 = (unsigned __int128)t214;
                                if (n215 >= 64) gm_trap_shift((uint64_t)n215, "i64", gm_srcs9q7ghsrcgv5, 410, 69);
                                int64_t r216 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n215);
                                bool o217 = (int64_t)(r216 >> (int)n215) != ((int64_t)1LL);
                                if (o217) gm_trap("integer overflow in `<<`", gm_srcs9q7ghsrcgv5, 410, 69);
                                int64_t t218 = r216;
                                uint8_t t219 = (t210 < t218);
                                t198 = t219;
                            } else t198 = 0;
                            uint8_t t220 = t198;
                            t193 = t220;
                            goto ix194;
ie194:;
                            int64_t t222 = gm_l17;
                            uint64_t t223 = gm_l28;
                            unsigned __int128 n224 = (unsigned __int128)t223;
                            if (n224 >= 64) gm_trap_shift((uint64_t)n224, "i64", gm_srcs9q7ghsrcgv5, 410, 106);
                            int64_t r225 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n224);
                            bool o226 = (int64_t)(r225 >> (int)n224) != ((int64_t)1LL);
                            if (o226) gm_trap("integer overflow in `<<`", gm_srcs9q7ghsrcgv5, 410, 106);
                            int64_t t227 = r225;
                            uint8_t t228 = (t222 < t227);
                            uint8_t t229 = t228;
                            t193 = t229;
ix194:;
                            gm_l29 = t193;
                        }
                        {
                            uint8_t t231 = gm_l29;
                            if (!t231) goto ie230;
                            {
                                uint64_t t233 = gm_l27;
                                gm_l23 = t233;
                            }
                            {
                                goto lb174;
                            }
                            goto ix230;
ie230:;
ix230:;
                        }
                        goto me185;
mn186:;
                        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 408, 5);
me185:;
                    }
lc174:;
                    {
                        uint64_t t235 = gm_l26;
                        uint64_t r236; bool o237 = __builtin_add_overflow(t235, ((uint64_t)1ULL), &r236);
                        uint64_t t238 = r236;
                        gm_l26 = t238;
                    }
                }
lb174:;
            }
        }
        {
            uint64_t t240 = gm_l23;
            uint64_t t241 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
            uint8_t t242 = (t240 < t241);
            if (!t242) goto ie239;
            {
                uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcs9q7ghsrcgv5, 417, 22);
                uint64_t t245 = (uint64_t)(t244 / ((uint64_t)2ULL));
                uint64_t r246; bool o247 = __builtin_mul_overflow(((uint64_t)8ULL), t245, &r246);
                if (o247) gm_trap("integer overflow in `*`", gm_srcs9q7ghsrcgv5, 417, 17);
                uint64_t t248 = r246;
                gm_l30 = t248;
            }
            {
                int64_t t249;
                uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 24));
                if (!t251) goto ie250;
                uint64_t t253 = gm_l30;
                uint64_t r254; bool o255 = __builtin_sub_overflow(t253, ((uint64_t)1ULL), &r254);
                if (o255) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 418, 51);
                uint64_t t256 = r254;
                unsigned __int128 n257 = (unsigned __int128)t256;
                if (n257 >= 64) gm_trap_shift((uint64_t)n257, "i64", gm_srcs9q7ghsrcgv5, 418, 40);
                int64_t r258 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n257);
                bool o259 = (int64_t)(r258 >> (int)n257) != ((int64_t)1LL);
                if (o259) gm_trap("integer overflow in `<<`", gm_srcs9q7ghsrcgv5, 418, 40);
                int64_t t260 = r258;
                if (t260 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 418, 38);
                int64_t t261 = (int64_t)((uint64_t)0 - (uint64_t)t260);
                int64_t r262; bool o263 = __builtin_sub_overflow(t261, ((int64_t)1LL), &r262);
                if (o263) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 418, 38);
                int64_t t264 = r262;
                int64_t t265 = t264;
                t249 = t265;
                goto ix250;
ie250:;
                uint64_t t267 = gm_l30;
                unsigned __int128 n268 = (unsigned __int128)t267;
                if (n268 >= 64) gm_trap_shift((uint64_t)n268, "i64", gm_srcs9q7ghsrcgv5, 418, 75);
                int64_t r269 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n268);
                bool o270 = (int64_t)(r269 >> (int)n268) != ((int64_t)1LL);
                if (o270) gm_trap("integer overflow in `<<`", gm_srcs9q7ghsrcgv5, 418, 75);
                int64_t t271 = r269;
                int64_t t272 = t271;
                t249 = t272;
ix250:;
                gm_l31 = t249;
            }
            {
                gm_b16_8 t273 = gm_l9;
                gm_tnpifv20irnph t274;
                GM_ST(gm_b16_8, ((uint8_t *)&t274 + 0), t273);
                gm_tabb710d9mq2e t275;
                gm_f_format_ubnq9mq((void *)&t275, (void *)NULL, cx, NULL, t274);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t276 = t275;
                if (((((uint8_t *)&t276))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t276) + 8, gm_srcs9q7ghsrcgv5, 419, 14);
                gm_b16_8 t277 = GM_LD(gm_b16_8, (((uint8_t *)&t276) + 8));
                gm_l32 = t277;
            }
            {
                void * t278 = gm_l0;
                void * t279 = (void *)((((uint8_t *)t278) + 872) + 200);
                gm_b16_8 t280 = gm_l32;
                gm_b16_8 t281 = t280;
                gm_b16_8 t282 = GM_LD(gm_b16_8, ((uint8_t *)&t281));
                gm_tftb1odc739t5 t283 = gm_f_put_5c6qgui(t279, t282, ((uint8_t)1));
                gm_tftb1odc739t5 t284 = t283;
                if (((((uint8_t *)&t284))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t284) + 1, gm_srcs9q7ghsrcgv5, 420, 9);
                gm_b0_1 t285 = GM_LD(gm_b0_1, (((uint8_t *)&t284) + 1));
            }
            {
                void * t286 = gm_l0;
                gm_b16_8 t287 = gm_l32;
                gm_b16_8 t288 = t287;
                gm_b16_8 t289 = GM_LD(gm_b16_8, ((uint8_t *)&t288));
                gm_b0_1 t290 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t286, t289);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t291 = (void *)((uint8_t *)&F.l15);
                gm_t4d55u65a2eb7 t292;
                gm_b16_8 t293 = gm_l32;
                GM_ST(gm_b16_8, (uint8_t *)&t292 + 0, t293);
                int64_t t294 = gm_l31;
                GM_ST(int64_t, (uint8_t *)&t292 + 16, t294);
                gm_tabb710d9mq2e t295;
                gm_f_format_3qphnbe((void *)&t295, (void *)NULL, cx, NULL, &t292);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t296 = t295;
                if (((((uint8_t *)&t296))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t296) + 8, gm_srcs9q7ghsrcgv5, 422, 20);
                gm_b16_8 t297 = GM_LD(gm_b16_8, (((uint8_t *)&t296) + 8));
                gm_b16_8 t298 = t297;
                gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)&t298));
                gm_tftb1odc739t5 t300 = gm_f_push_3ef746b(t291, t299);
                gm_tftb1odc739t5 t301 = t300;
                if (((((uint8_t *)&t301))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t301) + 1, gm_srcs9q7ghsrcgv5, 422, 9);
                gm_b0_1 t302 = GM_LD(gm_b0_1, (((uint8_t *)&t301) + 1));
            }
            goto ix239;
ie239:;
ix239:;
        }
        {
            void * t303 = gm_l0;
            void * t304 = (void *)((((uint8_t *)t303) + 872) + 104);
            gm_t6j6smj7qoctu t305;
            gm_b16_8 t306 = gm_l7;
            gm_b16_8 t307 = gm_f_packed_v79gj1r((void *)NULL, cx, NULL, t306);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t305 + 0, t307);
            gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            gm_b16_8 t309 = t308;
            gm_b16_8 t310 = GM_LD(gm_b16_8, ((uint8_t *)&t309));
            gm_b16_8 t311;
            GM_ST(void *, (uint8_t *)&t311, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t311 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t312;
            gm_f_join_sfknljb((void *)&t312, (void *)NULL, cx, NULL, t310, t311);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t313 = t312;
            if (((((uint8_t *)&t313))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t313) + 8, gm_srcs9q7ghsrcgv5, 424, 57);
            gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&t313) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t305 + 16, t314);
            gm_tftb1odc739t5 t315 = gm_f_write_ier753o(t304, &t305);
            gm_tftb1odc739t5 t316 = t315;
            if (((((uint8_t *)&t316))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t316) + 1, gm_srcs9q7ghsrcgv5, 424, 5);
            gm_b0_1 t317 = GM_LD(gm_b0_1, (((uint8_t *)&t316) + 1));
        }
        {
            void * t318 = gm_l0;
            gm_b16_8 t319 = gm_l7;
            uint64_t t320 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
            gm_b0_1 t321 = gm_f_static_assert_15uo2jf((void *)NULL, cx, NULL, t318, t319, t320);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t322 = gm_l7;
            uint8_t t323 = 0;
            F.ret = t322;
            goto gm_fret;
        }
        goto ix5;
ie5:;
        {
            gm_trap("reached `@unreachable()`", gm_srcs9q7ghsrcgv5, 373, 44);
        }
ix5:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_define_view_sob497j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_define_view_sob497j F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t6j6smj7qoctu gm_l3; /* v */
    gm_b16_8 gm_l4; /* cty */
    uint64_t gm_l5; /* size */
    gm_b16_8 gm_l6; /* offs */
    gm_b16_8 gm_l9; /* elems */
    gm_b16_8 gm_l10; /* names */
    gm_b16_8 gm_l12; /* chosen */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* k */
    gm_th2lqbo77pvsc gm_l16; /* n */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* k */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* k */
    gm_t88rcucg1b7uk gm_l23; /* e */
    gm_t88rcucg1b7uk gm_l24; /* elem */
    gm_b16_8 gm_l25; /* target */
    gm_b16_8 gm_l26; /* len */
    gm_t88rcucg1b7uk gm_l27; /* inner */
    gm_t88rcucg1b7uk gm_l28; /* err */
    gm_t88rcucg1b7uk gm_l29; /* ok */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    gm_b16_8 gm_l36; /* chosen */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* k */
    gm_b16_8 gm_l41; /* name */
    gm_b16_8 gm_l42; /* fields */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_b16_8 gm_l47; /* chosen */
    gm_b16_8 gm_l49; /* <tmp> */
    uint64_t gm_l50; /* <tmp> */
    uint64_t gm_l51; /* k */
    gm_t88rcucg1b7uk gm_l53; /* tag */
    gm_b16_8 gm_l54; /* variants */
    gm_b16_8 gm_l55; /* tag_cty */
    gm_b16_8 gm_l57; /* <tmp> */
    uint64_t gm_l58; /* <tmp> */
    gm_b16_8 gm_l60; /* chosen */
    gm_b16_8 gm_l62; /* <tmp> */
    uint64_t gm_l63; /* <tmp> */
    uint64_t gm_l64; /* k */
    gm_t88rcucg1b7uk gm_l66; /* p */
    uint64_t gm_l68; /* at */
    gm_b16_8 gm_l69; /* <tmp> */
    uint64_t gm_l70; /* <tmp> */
    gm_tbccvikg6b96e gm_l71; /* m */
    GM_STACK_CHECK(&F, 6343u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((((uint8_t *)t2) + 872) + 0);
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_tu3261i9p5758 t5;
        gm_f_get_v0dhnfj((void *)&t5, t3, t4);
        gm_tu3261i9p5758 t6 = t5;
        gm_ttu09lij36f8c t7;
        if (((((uint8_t *)&t6))[40] != 0)) {
            gm_ttu09lij36f8c t8 = GM_LD(gm_ttu09lij36f8c, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        F.l2 = t7;
    }
    {
        gm_ttu09lij36f8c t11 = F.l2;
        gm_ttu09lij36f8c t12 = t11;
        gm_t6j6smj7qoctu t13;
        if (((((uint8_t *)&t12))[32] != 0)) {
            gm_t6j6smj7qoctu t14 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t12) + 0));
            t13 = t14;
        } else {
            gm_b0_1 t15;
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        gm_l3 = t13;
    }
    {
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
        gm_l4 = t17;
    }
    {
        void * t18 = gm_l0;
        gm_t88rcucg1b7uk t19 = gm_l1;
        uint64_t t20 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t20;
    }
    {
        void * t21 = gm_l0;
        gm_t88rcucg1b7uk t22 = gm_l1;
        gm_b16_8 t23 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t23;
    }
    {
        gm_f_list_at7dsst((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_hash_map_cuh4fs9((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t24 = gm_l0;
        gm_t88rcucg1b7uk t25 = gm_l1;
        gm_t53at80pfdus3 t26;
        gm_f_kind_ghvav22((void *)&t26, t24, GM_CONV(gm_csguankko4lmhd, t25));
        gm_t53at80pfdus3 t27 = t26;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
        switch (t36) { case ((uint8_t)0ULL): goto ma34; case ((uint8_t)1ULL): goto ma34; case ((uint8_t)2ULL): goto ma34; case ((uint8_t)3ULL): goto ma34; case ((uint8_t)4ULL): goto ma34; case ((uint8_t)5ULL): goto ma34; case ((uint8_t)6ULL): goto ma34; case ((uint8_t)7ULL): goto ma34; case ((uint8_t)8ULL): goto ma34; case ((uint8_t)9ULL): goto ma34; case ((uint8_t)10ULL): goto ma30; case ((uint8_t)11ULL): goto ma34; case ((uint8_t)12ULL): goto ma29; case ((uint8_t)13ULL): goto ma31; case ((uint8_t)14ULL): goto ma32; case ((uint8_t)15ULL): goto ma34; case ((uint8_t)16ULL): goto ma34; case ((uint8_t)17ULL): goto ma33; case ((uint8_t)18ULL): goto ma34; case ((uint8_t)19ULL): goto ma34; case ((uint8_t)20ULL): goto ma34; case ((uint8_t)21ULL): goto ma34; case ((uint8_t)22ULL): goto ma34; case ((uint8_t)23ULL): goto ma34; case ((uint8_t)24ULL): goto ma34; case ((uint8_t)25ULL): goto ma34; case ((uint8_t)26ULL): goto ma34; default: goto ma34; }
ma29:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
        if (t38 != ((uint8_t)12ULL)) goto mn37;
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((((uint8_t *)&t27) + 8) + 0));
        gm_l9 = t39;
        gm_b16_8 t40 = GM_LD(gm_b16_8, ((((uint8_t *)&t27) + 8) + 16));
        gm_l10 = t40;
        {
            gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
            uint8_t t44 = (t43 > ((uint64_t)0ULL));
            if (!t44) goto ie42;
            {
                gm_b16_8 t46 = gm_l10;
                void * t47 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t48 = gm_f_unique_view_members_3bvsjcm((void *)NULL, cx, NULL, t46, t47);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t48;
            }
            {
                {
                    gm_b16_8 t50 = gm_l12;
                    gm_b16_8 t51 = t50;
                    gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                    gm_l13 = t52;
                }
                {
                    gm_l14 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t54 = gm_l14;
                        uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        uint8_t t56 = (t54 < t55);
                        if (!t56) goto lb53;
                        {
                            gm_t13urt52695sh t59;
                            uint64_t t60 = gm_l14;
                            GM_ST(uint64_t, (uint8_t *)&t59 + 0, t60);
                            uint64_t t61 = gm_l14;
                            uint64_t t62 = (uintptr_t)t61;
                            uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                            memcpy((uint8_t *)&t59 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t62 * 24), 24);
                            gm_t13urt52695sh t64 = t59;
                            uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&t64) + 0));
                            gm_l15 = t67;
                            gm_th2lqbo77pvsc t68 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t64) + 8));
                            gm_l16 = t68;
                            {
                                void * t70 = (void *)((uint8_t *)&F.l11);
                                gm_th2lqbo77pvsc t71 = gm_l16;
                                gm_th2lqbo77pvsc t72 = t71;
                                gm_b16_8 t73;
                                if (((((uint8_t *)&t72))[16] != 0)) {
                                    gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 0));
                                    t73 = t74;
                                } else {
                                    uint64_t t75 = gm_l15;
                                    gm_t4ce0jd3tdro4 t76;
                                    GM_ST(uint64_t, ((uint8_t *)&t76 + 0), t75);
                                    gm_tabb710d9mq2e t77;
                                    gm_f_format_7c4i91d((void *)&t77, (void *)NULL, cx, NULL, t76);
                                    if (gm_unw.kind) {
                                        goto gm_ll57;
                                    }
                                    gm_tabb710d9mq2e t78 = t77;
                                    if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 8, gm_srcs9q7ghsrcgv5, 239, 68);
                                    gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&t78) + 8));
                                    gm_b16_8 t80 = t79;
                                    gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
                                    t73 = t81;
                                }
                                gm_tftb1odc739t5 t82 = gm_f_push_3ef746b(t70, t73);
                                gm_tftb1odc739t5 t83 = t82;
                                if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 1, gm_srcs9q7ghsrcgv5, 239, 49);
                                gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 1));
                            }
                            goto me65;
mn66:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 239, 17);
me65:;
                        }
lc53:;
                        {
                            uint64_t t86 = gm_l14;
                            uint64_t r87; bool o88 = __builtin_add_overflow(t86, ((uint64_t)1ULL), &r87);
                            uint64_t t89 = r87;
                            gm_l14 = t89;
                        }
                    }
gm_ll57:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb53; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc53; }
                    }
                    goto gm_lad;
lb53:;
                }
            }
            goto ix42;
ie42:;
            {
                {
                    gm_l18 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    gm_l17 = t92;
                }
                {
                    for (;;) {
                        uint64_t t94 = gm_l18;
                        uint64_t t95 = gm_l17;
                        uint8_t t96 = (t94 < t95);
                        if (!t96) goto lb93;
                        {
                            uint64_t t99 = gm_l18;
                            uint64_t t100 = t99;
                            uint64_t t103 = GM_LD(uint64_t, ((uint8_t *)&t100));
                            gm_l19 = t103;
                            {
                                void * t105 = (void *)((uint8_t *)&F.l11);
                                uint64_t t106 = gm_l19;
                                gm_t4ce0jd3tdro4 t107;
                                GM_ST(uint64_t, ((uint8_t *)&t107 + 0), t106);
                                gm_tabb710d9mq2e t108;
                                gm_f_format_7c4i91d((void *)&t108, (void *)NULL, cx, NULL, t107);
                                if (gm_unw.kind) {
                                    goto gm_ll97;
                                }
                                gm_tabb710d9mq2e t109 = t108;
                                if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 8, gm_srcs9q7ghsrcgv5, 241, 53);
                                gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
                                gm_b16_8 t111 = t110;
                                gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                                gm_tftb1odc739t5 t113 = gm_f_push_3ef746b(t105, t112);
                                gm_tftb1odc739t5 t114 = t113;
                                if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 1, gm_srcs9q7ghsrcgv5, 241, 41);
                                gm_b0_1 t115 = GM_LD(gm_b0_1, (((uint8_t *)&t114) + 1));
                            }
                            goto me101;
mn102:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 241, 17);
me101:;
                        }
lc93:;
                        {
                            uint64_t t117 = gm_l18;
                            uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                            uint64_t t120 = r118;
                            gm_l18 = t120;
                        }
                    }
gm_ll97:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb93; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc93; }
                    }
                    goto gm_lad;
lb93:;
                }
            }
ix42:;
        }
        {
            {
                gm_b16_8 t122 = gm_l9;
                gm_l20 = t122;
            }
            {
                gm_l21 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t124 = gm_l21;
                    uint64_t t125 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    uint8_t t126 = (t124 < t125);
                    if (!t126) goto lb123;
                    {
                        uint64_t t129 = gm_l21;
                        uint64_t t130 = gm_l21;
                        uint64_t t131 = (uintptr_t)t130;
                        uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        gm_t88rcucg1b7uk t133 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t131 * 4));
                        gm_tlcm4lvu3g1a8 t134;
                        GM_ST(uint32_t, (uint8_t *)&t134 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t134 + 0), t129);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t134 + 8), t133);
                        gm_tlcm4lvu3g1a8 t135 = t134;
                        uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&t135) + 0));
                        gm_l22 = t138;
                        gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t135) + 8));
                        gm_l23 = t139;
                        {
                            void * t141 = gm_l0;
                            void * t142 = (void *)((uint8_t *)&F.l7);
                            uint64_t t143 = gm_l22;
                            uint64_t t144 = (uintptr_t)t143;
                            uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            if (t144 >= (uintptr_t)(t145)) gm_trap_index(t144, t145, gm_srcs9q7ghsrcgv5, 243, 69);
                            uint64_t t146 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t144 * 8));
                            gm_t88rcucg1b7uk t147 = gm_l23;
                            uint64_t t148 = gm_l22;
                            uint64_t t149 = (uintptr_t)t148;
                            uint64_t t150 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                            if (t149 >= (uintptr_t)(t150)) gm_trap_index(t149, t150, gm_srcs9q7ghsrcgv5, 243, 81);
                            gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t149 * 16));
                            gm_b0_1 t152 = gm_f_push_member_eubf7s6((void *)NULL, cx, NULL, t141, t142, t146, t147, t151);
                            if (gm_unw.kind) {
                                goto gm_ll127;
                            }
                        }
                        goto me136;
mn137:;
                        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 243, 13);
me136:;
                    }
lc123:;
                    {
                        uint64_t t154 = gm_l21;
                        uint64_t r155; bool o156 = __builtin_add_overflow(t154, ((uint64_t)1ULL), &r155);
                        uint64_t t157 = r155;
                        gm_l21 = t157;
                    }
                }
gm_ll127:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb123; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc123; }
                }
                goto gm_lad;
lb123:;
            }
        }
        goto me28;
mn37:;
ma30:;
        uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
        if (t159 != ((uint8_t)10ULL)) goto mn158;
        gm_t88rcucg1b7uk t160 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t27) + 8) + 4));
        gm_l24 = t160;
        {
            void * t162 = gm_l0;
            gm_t88rcucg1b7uk t163 = gm_l24;
            gm_b16_8 t164 = gm_f_target_view_ams6b9s((void *)NULL, cx, NULL, t162, t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l25 = t164;
        }
        {
            void * t165 = gm_l0;
            void * t166 = gm_l0;
            gm_t88rcucg1b7uk t167 = gm_f_usize_ty_9mijhtc((void *)NULL, cx, NULL, t166);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t168 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t165, t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l26 = t168;
        }
        {
            void * t169 = (void *)((uint8_t *)&F.l7);
            gm_tbccvikg6b96e t170;
            uint64_t t171 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t171 >= (uintptr_t)(t172)) gm_trap_index(t171, t172, gm_srcs9q7ghsrcgv5, 248, 45);
            uint64_t t173 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t171 * 8));
            GM_ST(uint64_t, (uint8_t *)&t170 + 0, t173);
            uint64_t t174 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t175 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t174 >= (uintptr_t)(t175)) gm_trap_index(t174, t175, gm_srcs9q7ghsrcgv5, 248, 60);
            uint64_t t176 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t174 * 8));
            GM_ST(uint64_t, (uint8_t *)&t170 + 8, t176);
            gm_b16_8 t177 = gm_l25;
            gm_tnpifv20irnph t178;
            GM_ST(gm_b16_8, ((uint8_t *)&t178 + 0), t177);
            gm_tabb710d9mq2e t179;
            gm_f_format_cn4968h((void *)&t179, (void *)NULL, cx, NULL, t178);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t180 = t179;
            if (((((uint8_t *)&t180))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t180) + 8, gm_srcs9q7ghsrcgv5, 248, 75);
            gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&t180) + 8));
            gm_b16_8 t182 = t181;
            gm_b16_8 t183 = GM_LD(gm_b16_8, ((uint8_t *)&t182));
            GM_ST(gm_b16_8, (uint8_t *)&t170 + 16, t183);
            gm_tftb1odc739t5 t184 = gm_f_push_m7lva8q(t169, &t170);
            gm_tftb1odc739t5 t185 = t184;
            if (((((uint8_t *)&t185))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t185) + 1, gm_srcs9q7ghsrcgv5, 248, 13);
            gm_b0_1 t186 = GM_LD(gm_b0_1, (((uint8_t *)&t185) + 1));
        }
        {
            void * t187 = (void *)((uint8_t *)&F.l7);
            gm_tbccvikg6b96e t188;
            uint64_t t189 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t190 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t189 >= (uintptr_t)(t190)) gm_trap_index(t189, t190, gm_srcs9q7ghsrcgv5, 249, 45);
            uint64_t t191 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t189 * 8));
            GM_ST(uint64_t, (uint8_t *)&t188 + 0, t191);
            uint64_t t192 = gm_l5;
            uint64_t t193 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t194 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t193 >= (uintptr_t)(t194)) gm_trap_index(t193, t194, gm_srcs9q7ghsrcgv5, 249, 67);
            uint64_t t195 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t193 * 8));
            uint64_t r196; bool o197 = __builtin_sub_overflow(t192, t195, &r196);
            if (o197) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 249, 60);
            uint64_t t198 = r196;
            GM_ST(uint64_t, (uint8_t *)&t188 + 8, t198);
            gm_b16_8 t199 = gm_l26;
            gm_tnpifv20irnph t200;
            GM_ST(gm_b16_8, ((uint8_t *)&t200 + 0), t199);
            gm_tabb710d9mq2e t201;
            gm_f_format_jk4dii4((void *)&t201, (void *)NULL, cx, NULL, t200);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t202 = t201;
            if (((((uint8_t *)&t202))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t202) + 8, gm_srcs9q7ghsrcgv5, 249, 82);
            gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)&t202) + 8));
            gm_b16_8 t204 = t203;
            gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&t204));
            GM_ST(gm_b16_8, (uint8_t *)&t188 + 16, t205);
            gm_tftb1odc739t5 t206 = gm_f_push_m7lva8q(t187, &t188);
            gm_tftb1odc739t5 t207 = t206;
            if (((((uint8_t *)&t207))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t207) + 1, gm_srcs9q7ghsrcgv5, 249, 13);
            gm_b0_1 t208 = GM_LD(gm_b0_1, (((uint8_t *)&t207) + 1));
        }
        goto me28;
mn158:;
ma31:;
        uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
        if (t210 != ((uint8_t)13ULL)) goto mn209;
        gm_t88rcucg1b7uk t211 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t27) + 8));
        gm_l27 = t211;
        {
            void * t213 = gm_l0;
            void * t214 = (void *)((uint8_t *)&F.l7);
            uint64_t t215 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t216 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t215 >= (uintptr_t)(t216)) gm_trap_index(t215, t216, gm_srcs9q7ghsrcgv5, 252, 38);
            uint64_t t217 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t215 * 8));
            gm_t88rcucg1b7uk t218 = gm_l27;
            gm_b16_8 t219;
            GM_ST(void *, (uint8_t *)&t219, (void *)(gm_s5qkai093c1q0));
            GM_ST(uintptr_t, (uint8_t *)&t219 + 8, (uintptr_t)(5));
            gm_b0_1 t220 = gm_f_push_member_eubf7s6((void *)NULL, cx, NULL, t213, t214, t217, t218, t219);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t221 = (void *)((uint8_t *)&F.l7);
            gm_tbccvikg6b96e t222;
            uint64_t t223 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t224 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t223 >= (uintptr_t)(t224)) gm_trap_index(t223, t224, gm_srcs9q7ghsrcgv5, 253, 45);
            uint64_t t225 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t223 * 8));
            GM_ST(uint64_t, (uint8_t *)&t222 + 0, t225);
            GM_ST(uint64_t, (uint8_t *)&t222 + 8, ((uint64_t)1ULL));
            gm_b16_8 t226;
            GM_ST(void *, (uint8_t *)&t226, (void *)(gm_sk8hn2ar9njah));
            GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(14));
            GM_ST(gm_b16_8, (uint8_t *)&t222 + 16, t226);
            gm_tftb1odc739t5 t227 = gm_f_push_m7lva8q(t221, &t222);
            gm_tftb1odc739t5 t228 = t227;
            if (((((uint8_t *)&t228))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t228) + 1, gm_srcs9q7ghsrcgv5, 253, 13);
            gm_b0_1 t229 = GM_LD(gm_b0_1, (((uint8_t *)&t228) + 1));
        }
        goto me28;
mn209:;
ma32:;
        uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
        if (t231 != ((uint8_t)14ULL)) goto mn230;
        gm_t88rcucg1b7uk t232 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t27) + 8) + 0));
        gm_l28 = t232;
        gm_t88rcucg1b7uk t233 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t27) + 8) + 4));
        gm_l29 = t233;
        {
            void * t235 = (void *)((uint8_t *)&F.l7);
            gm_tbccvikg6b96e t236;
            uint64_t t237 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t238 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t237 >= (uintptr_t)(t238)) gm_trap_index(t237, t238, gm_srcs9q7ghsrcgv5, 256, 45);
            uint64_t t239 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t237 * 8));
            GM_ST(uint64_t, (uint8_t *)&t236 + 0, t239);
            GM_ST(uint64_t, (uint8_t *)&t236 + 8, ((uint64_t)1ULL));
            gm_b16_8 t240;
            GM_ST(void *, (uint8_t *)&t240, (void *)(gm_sk2tgsi47prt7));
            GM_ST(uintptr_t, (uint8_t *)&t240 + 8, (uintptr_t)(13));
            GM_ST(gm_b16_8, (uint8_t *)&t236 + 16, t240);
            gm_tftb1odc739t5 t241 = gm_f_push_m7lva8q(t235, &t236);
            gm_tftb1odc739t5 t242 = t241;
            if (((((uint8_t *)&t242))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t242) + 1, gm_srcs9q7ghsrcgv5, 256, 13);
            gm_b0_1 t243 = GM_LD(gm_b0_1, (((uint8_t *)&t242) + 1));
        }
        {
            gm_f_list_58nr42v_2((void *)&F.l30, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t244 = (void *)((uint8_t *)&F.l30);
            gm_tcmkompbl09il t245;
            memset((uint8_t *)&t245 + 4, 0, 4);
            gm_t88rcucg1b7uk t246 = gm_l29;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t245 + 0, t246);
            gm_b16_8 t247;
            GM_ST(void *, (uint8_t *)&t247, (void *)(gm_soim4k70n6bc7));
            GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(2));
            GM_ST(gm_b16_8, (uint8_t *)&t245 + 8, t247);
            gm_tftb1odc739t5 t248 = gm_f_push_bpovr2k_2(t244, &t245);
            gm_tftb1odc739t5 t249 = t248;
            if (((((uint8_t *)&t249))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t249) + 1, gm_srcs9q7ghsrcgv5, 258, 13);
            gm_b0_1 t250 = GM_LD(gm_b0_1, (((uint8_t *)&t249) + 1));
        }
        {
            void * t251 = (void *)((uint8_t *)&F.l30);
            gm_tcmkompbl09il t252;
            memset((uint8_t *)&t252 + 4, 0, 4);
            gm_t88rcucg1b7uk t253 = gm_l28;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t252 + 0, t253);
            gm_b16_8 t254;
            GM_ST(void *, (uint8_t *)&t254, (void *)(gm_shhr1hlq1ethc));
            GM_ST(uintptr_t, (uint8_t *)&t254 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&t252 + 8, t254);
            gm_tftb1odc739t5 t255 = gm_f_push_bpovr2k_2(t251, &t252);
            gm_tftb1odc739t5 t256 = t255;
            if (((((uint8_t *)&t256))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t256) + 1, gm_srcs9q7ghsrcgv5, 259, 13);
            gm_b0_1 t257 = GM_LD(gm_b0_1, (((uint8_t *)&t256) + 1));
        }
        {
            void * t258 = gm_l0;
            void * t259 = (void *)((uint8_t *)&F.l7);
            uint64_t t260 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t261 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t260 >= (uintptr_t)(t261)) gm_trap_index(t260, t261, gm_srcs9q7ghsrcgv5, 260, 37);
            uint64_t t262 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t260 * 8));
            gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 0));
            gm_b16_8 t264 = t263;
            gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&t264));
            gm_b0_1 t266 = gm_f_push_union_m2cqbk2((void *)NULL, cx, NULL, t258, t259, t262, t265);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me28;
mn230:;
ma33:;
        uint8_t t268 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
        if (t268 != ((uint8_t)17ULL)) goto mn267;
        {
            void * t270 = gm_l0;
            gm_t88rcucg1b7uk t271 = gm_l1;
            void * t272 = gm_f_adt_of_gej99vn(t270, GM_CONV(gm_csguankko4lmhd, t271));
            uint8_t t278 = GM_LD(uint8_t, ((((uint8_t *)t272) + 24) + 0));
            switch (t278) { case ((uint8_t)0ULL): goto ma274; case ((uint8_t)1ULL): goto ma276; case ((uint8_t)2ULL): goto ma275; default: goto ma277; }
ma274:;
            uint8_t t280 = GM_LD(uint8_t, ((((uint8_t *)t272) + 24) + 0));
            if (t280 != ((uint8_t)0ULL)) goto mn279;
            gm_tpautr4br7h3l t281 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t272) + 24) + 8));
            F.l31 = t281;
            {
                gm_f_list_epvducg((void *)&F.l32, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t284 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                    gm_l33 = t284;
                }
                {
                    gm_l34 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t286 = gm_l34;
                        uint64_t t287 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                        uint8_t t288 = (t286 < t287);
                        if (!t288) goto lb285;
                        {
                            uint64_t t291 = gm_l34;
                            uint64_t t292 = (uintptr_t)t291;
                            uint64_t t293 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                            gm_t0mglksrkfa5e t296 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t292 * 40));
                            F.l35 = t296;
                            {
                                void * t298 = (void *)((uint8_t *)&F.l32);
                                gm_b16_8 t299 = GM_LD(gm_b16_8, (((uint8_t *)&F.l35) + 0));
                                gm_tftb1odc739t5 t300 = gm_f_push_3ef746b(t298, t299);
                                gm_tftb1odc739t5 t301 = t300;
                                if (((((uint8_t *)&t301))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t301) + 1, gm_srcs9q7ghsrcgv5, 266, 43);
                                gm_b0_1 t302 = GM_LD(gm_b0_1, (((uint8_t *)&t301) + 1));
                            }
                            goto me294;
mn295:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 266, 21);
me294:;
                        }
lc285:;
                        {
                            uint64_t t304 = gm_l34;
                            uint64_t r305; bool o306 = __builtin_add_overflow(t304, ((uint64_t)1ULL), &r305);
                            uint64_t t307 = r305;
                            gm_l34 = t307;
                        }
                    }
lb285:;
                }
            }
            {
                gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&F.l32) + 0));
                gm_b16_8 t309 = t308;
                gm_b16_8 t310 = GM_LD(gm_b16_8, ((uint8_t *)&t309));
                void * t311 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t312 = gm_f_unique_view_members_3bvsjcm((void *)NULL, cx, NULL, t310, t311);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l36 = t312;
            }
            {
                {
                    gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                    gm_b16_8 t315 = t314;
                    gm_b16_8 t316 = GM_LD(gm_b16_8, ((uint8_t *)&t315));
                    gm_l37 = t316;
                }
                {
                    gm_l38 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t318 = gm_l38;
                        uint64_t t319 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                        uint8_t t320 = (t318 < t319);
                        if (!t320) goto lb317;
                        {
                            gm_t8ta482bl6be0 t323;
                            uint64_t t324 = gm_l38;
                            GM_ST(uint64_t, (uint8_t *)&t323 + 0, t324);
                            uint64_t t325 = gm_l38;
                            uint64_t t326 = (uintptr_t)t325;
                            uint64_t t327 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                            memcpy((uint8_t *)&t323 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l37)) + t326 * 40), 40);
                            gm_t8ta482bl6be0 t328 = t323;
                            uint64_t t331 = GM_LD(uint64_t, (((uint8_t *)&t328) + 0));
                            gm_l39 = t331;
                            gm_t0mglksrkfa5e t332 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t328) + 8));
                            F.l40 = t332;
                            {
                                uint64_t t334 = gm_l39;
                                uint64_t t335 = (uintptr_t)t334;
                                uint64_t t336 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                                if (t335 >= (uintptr_t)(t336)) gm_trap_index(t335, t336, gm_srcs9q7ghsrcgv5, 269, 33);
                                gm_th2lqbo77pvsc t337 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t335 * 24));
                                gm_th2lqbo77pvsc t338 = t337;
                                gm_b16_8 t339;
                                if (((((uint8_t *)&t338))[16] != 0)) {
                                    gm_b16_8 t340 = GM_LD(gm_b16_8, (((uint8_t *)&t338) + 0));
                                    t339 = t340;
                                } else {
                                    uint64_t t341 = gm_l39;
                                    gm_t4ce0jd3tdro4 t342;
                                    GM_ST(uint64_t, ((uint8_t *)&t342 + 0), t341);
                                    gm_tabb710d9mq2e t343;
                                    gm_f_format_tc7tn98((void *)&t343, (void *)NULL, cx, NULL, t342);
                                    if (gm_unw.kind) {
                                        goto gm_ll321;
                                    }
                                    gm_tabb710d9mq2e t344 = t343;
                                    if (((((uint8_t *)&t344))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t344) + 8, gm_srcs9q7ghsrcgv5, 269, 48);
                                    gm_b16_8 t345 = GM_LD(gm_b16_8, (((uint8_t *)&t344) + 8));
                                    gm_b16_8 t346 = t345;
                                    gm_b16_8 t347 = GM_LD(gm_b16_8, ((uint8_t *)&t346));
                                    t339 = t347;
                                }
                                gm_l41 = t339;
                            }
                            {
                                void * t348 = gm_l0;
                                void * t349 = (void *)((uint8_t *)&F.l7);
                                uint64_t t350 = gm_l39;
                                uint64_t t351 = (uintptr_t)t350;
                                uint64_t t352 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                if (t351 >= (uintptr_t)(t352)) gm_trap_index(t351, t352, gm_srcs9q7ghsrcgv5, 270, 50);
                                uint64_t t353 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t351 * 8));
                                gm_t88rcucg1b7uk t354 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l40) + 16));
                                gm_b16_8 t355 = gm_l41;
                                gm_b0_1 t356 = gm_f_push_member_eubf7s6((void *)NULL, cx, NULL, t348, t349, t353, t354, t355);
                                if (gm_unw.kind) {
                                    goto gm_ll321;
                                }
                            }
                            goto me329;
mn330:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 268, 21);
me329:;
                        }
lc317:;
                        {
                            uint64_t t358 = gm_l38;
                            uint64_t r359; bool o360 = __builtin_add_overflow(t358, ((uint64_t)1ULL), &r359);
                            uint64_t t361 = r359;
                            gm_l38 = t361;
                        }
                    }
gm_ll321:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb317; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc317; }
                    }
                    goto gm_lad;
lb317:;
                }
            }
            goto me273;
mn279:;
ma275:;
            uint8_t t363 = GM_LD(uint8_t, ((((uint8_t *)t272) + 24) + 0));
            if (t363 != ((uint8_t)2ULL)) goto mn362;
            gm_b16_8 t364 = GM_LD(gm_b16_8, ((((uint8_t *)t272) + 24) + 8));
            gm_l42 = t364;
            {
                gm_f_list_epvducg((void *)&F.l43, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t367 = gm_l42;
                    gm_l44 = t367;
                }
                {
                    gm_l45 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t369 = gm_l45;
                        uint64_t t370 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                        uint8_t t371 = (t369 < t370);
                        if (!t371) goto lb368;
                        {
                            uint64_t t374 = gm_l45;
                            uint64_t t375 = (uintptr_t)t374;
                            uint64_t t376 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                            gm_t0mglksrkfa5e t379 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l44)) + t375 * 40));
                            F.l46 = t379;
                            {
                                void * t381 = (void *)((uint8_t *)&F.l43);
                                gm_b16_8 t382 = GM_LD(gm_b16_8, (((uint8_t *)&F.l46) + 0));
                                gm_tftb1odc739t5 t383 = gm_f_push_3ef746b(t381, t382);
                                gm_tftb1odc739t5 t384 = t383;
                                if (((((uint8_t *)&t384))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t384) + 1, gm_srcs9q7ghsrcgv5, 275, 39);
                                gm_b0_1 t385 = GM_LD(gm_b0_1, (((uint8_t *)&t384) + 1));
                            }
                            goto me377;
mn378:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 275, 21);
me377:;
                        }
lc368:;
                        {
                            uint64_t t387 = gm_l45;
                            uint64_t r388; bool o389 = __builtin_add_overflow(t387, ((uint64_t)1ULL), &r388);
                            uint64_t t390 = r388;
                            gm_l45 = t390;
                        }
                    }
lb368:;
                }
            }
            {
                gm_b16_8 t391 = GM_LD(gm_b16_8, (((uint8_t *)&F.l43) + 0));
                gm_b16_8 t392 = t391;
                gm_b16_8 t393 = GM_LD(gm_b16_8, ((uint8_t *)&t392));
                void * t394 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t395 = gm_f_unique_view_members_3bvsjcm((void *)NULL, cx, NULL, t393, t394);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l47 = t395;
            }
            {
                gm_f_list_58nr42v_2((void *)&F.l48, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t397 = gm_l42;
                    gm_b16_8 t398 = t397;
                    gm_b16_8 t399 = GM_LD(gm_b16_8, ((uint8_t *)&t398));
                    gm_l49 = t399;
                }
                {
                    gm_l50 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t401 = gm_l50;
                        uint64_t t402 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                        uint8_t t403 = (t401 < t402);
                        if (!t403) goto lb400;
                        {
                            gm_t8ta482bl6be0 t406;
                            uint64_t t407 = gm_l50;
                            GM_ST(uint64_t, (uint8_t *)&t406 + 0, t407);
                            uint64_t t408 = gm_l50;
                            uint64_t t409 = (uintptr_t)t408;
                            uint64_t t410 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                            memcpy((uint8_t *)&t406 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l49)) + t409 * 40), 40);
                            gm_t8ta482bl6be0 t411 = t406;
                            uint64_t t414 = GM_LD(uint64_t, (((uint8_t *)&t411) + 0));
                            gm_l51 = t414;
                            gm_t0mglksrkfa5e t415 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t411) + 8));
                            F.l52 = t415;
                            {
                                void * t417 = (void *)((uint8_t *)&F.l48);
                                gm_tcmkompbl09il t418;
                                memset((uint8_t *)&t418 + 4, 0, 4);
                                gm_t88rcucg1b7uk t419 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l52) + 16));
                                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t418 + 0, t419);
                                uint64_t t420 = gm_l51;
                                uint64_t t421 = (uintptr_t)t420;
                                uint64_t t422 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                                if (t421 >= (uintptr_t)(t422)) gm_trap_index(t421, t422, gm_srcs9q7ghsrcgv5, 279, 55);
                                gm_th2lqbo77pvsc t423 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l47)) + t421 * 24));
                                gm_th2lqbo77pvsc t424 = t423;
                                gm_b16_8 t425;
                                if (((((uint8_t *)&t424))[16] != 0)) {
                                    gm_b16_8 t426 = GM_LD(gm_b16_8, (((uint8_t *)&t424) + 0));
                                    t425 = t426;
                                } else {
                                    uint64_t t427 = gm_l51;
                                    gm_t4ce0jd3tdro4 t428;
                                    GM_ST(uint64_t, ((uint8_t *)&t428 + 0), t427);
                                    gm_tabb710d9mq2e t429;
                                    gm_f_format_tc7tn98((void *)&t429, (void *)NULL, cx, NULL, t428);
                                    if (gm_unw.kind) {
                                        goto gm_ll404;
                                    }
                                    gm_tabb710d9mq2e t430 = t429;
                                    if (((((uint8_t *)&t430))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t430) + 8, gm_srcs9q7ghsrcgv5, 279, 70);
                                    gm_b16_8 t431 = GM_LD(gm_b16_8, (((uint8_t *)&t430) + 8));
                                    gm_b16_8 t432 = t431;
                                    gm_b16_8 t433 = GM_LD(gm_b16_8, ((uint8_t *)&t432));
                                    t425 = t433;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t418 + 8, t425);
                                gm_tftb1odc739t5 t434 = gm_f_push_bpovr2k_2(t417, &t418);
                                gm_tftb1odc739t5 t435 = t434;
                                if (((((uint8_t *)&t435))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t435) + 1, gm_srcs9q7ghsrcgv5, 279, 25);
                                gm_b0_1 t436 = GM_LD(gm_b0_1, (((uint8_t *)&t435) + 1));
                            }
                            goto me412;
mn413:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 278, 21);
me412:;
                        }
lc400:;
                        {
                            uint64_t t438 = gm_l50;
                            uint64_t r439; bool o440 = __builtin_add_overflow(t438, ((uint64_t)1ULL), &r439);
                            uint64_t t441 = r439;
                            gm_l50 = t441;
                        }
                    }
gm_ll404:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb400; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc400; }
                    }
                    goto gm_lad;
lb400:;
                }
            }
            {
                void * t442 = gm_l0;
                void * t443 = (void *)((uint8_t *)&F.l7);
                gm_b16_8 t444 = GM_LD(gm_b16_8, (((uint8_t *)&F.l48) + 0));
                gm_b16_8 t445 = t444;
                gm_b16_8 t446 = GM_LD(gm_b16_8, ((uint8_t *)&t445));
                gm_b0_1 t447 = gm_f_push_union_m2cqbk2((void *)NULL, cx, NULL, t442, t443, ((uint64_t)0ULL), t446);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me273;
mn362:;
ma276:;
            uint8_t t449 = GM_LD(uint8_t, ((((uint8_t *)t272) + 24) + 0));
            if (t449 != ((uint8_t)1ULL)) goto mn448;
            gm_t88rcucg1b7uk t450 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t272) + 24) + 8) + 0));
            gm_l53 = t450;
            gm_b16_8 t451 = GM_LD(gm_b16_8, (((((uint8_t *)t272) + 24) + 8) + 8));
            gm_l54 = t451;
            {
                void * t453 = gm_l0;
                gm_t88rcucg1b7uk t454 = gm_l1;
                gm_b16_8 t455 = gm_l4;
                gm_b16_8 t456 = gm_f_define_enum_v780plg((void *)NULL, cx, NULL, t453, t454, t455);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l55 = t456;
            }
            {
                void * t458 = gm_l0;
                gm_t88rcucg1b7uk t459 = gm_l1;
                uint8_t t460 = gm_f_is_aggregate_buh1uic((void *)NULL, cx, NULL, t458, t459);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t461 = (uint8_t)!t460;
                if (!t461) goto ie457;
                {
                    gm_b0_1 t463;
                    uint8_t t464 = 0;
                    F.ret = t463;
                    goto gm_fret;
                }
                goto ix457;
ie457:;
ix457:;
            }
            {
                void * t465 = (void *)((uint8_t *)&F.l7);
                gm_tbccvikg6b96e t466;
                uint64_t t467 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t468 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t467 >= (uintptr_t)(t468)) gm_trap_index(t467, t468, gm_srcs9q7ghsrcgv5, 286, 53);
                uint64_t t469 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t467 * 8));
                GM_ST(uint64_t, (uint8_t *)&t466 + 0, t469);
                void * t470 = gm_l0;
                gm_t88rcucg1b7uk t471 = gm_l53;
                uint64_t t472 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t470, t471);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint64_t, (uint8_t *)&t466 + 8, t472);
                gm_b16_8 t473 = gm_l55;
                gm_tnpifv20irnph t474;
                GM_ST(gm_b16_8, ((uint8_t *)&t474 + 0), t473);
                gm_tabb710d9mq2e t475;
                gm_f_format_7nu71mn((void *)&t475, (void *)NULL, cx, NULL, t474);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t476 = t475;
                if (((((uint8_t *)&t476))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t476) + 8, gm_srcs9q7ghsrcgv5, 286, 88);
                gm_b16_8 t477 = GM_LD(gm_b16_8, (((uint8_t *)&t476) + 8));
                gm_b16_8 t478 = t477;
                gm_b16_8 t479 = GM_LD(gm_b16_8, ((uint8_t *)&t478));
                GM_ST(gm_b16_8, (uint8_t *)&t466 + 16, t479);
                gm_tftb1odc739t5 t480 = gm_f_push_m7lva8q(t465, &t466);
                gm_tftb1odc739t5 t481 = t480;
                if (((((uint8_t *)&t481))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t481) + 1, gm_srcs9q7ghsrcgv5, 286, 21);
                gm_b0_1 t482 = GM_LD(gm_b0_1, (((uint8_t *)&t481) + 1));
            }
            {
                void * t483 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t484;
                GM_ST(void *, (uint8_t *)&t484, (void *)(gm_s420tm9ua7ggg));
                GM_ST(uintptr_t, (uint8_t *)&t484 + 8, (uintptr_t)(3));
                gm_tftb1odc739t5 t485 = gm_f_put_5c6qgui(t483, t484, ((uint8_t)1));
                gm_tftb1odc739t5 t486 = t485;
                if (((((uint8_t *)&t486))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t486) + 1, gm_srcs9q7ghsrcgv5, 287, 21);
                gm_b0_1 t487 = GM_LD(gm_b0_1, (((uint8_t *)&t486) + 1));
            }
            {
                gm_f_list_epvducg((void *)&F.l56, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t489 = gm_l54;
                    gm_l57 = t489;
                }
                {
                    gm_l58 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t491 = gm_l58;
                        uint64_t t492 = GM_LD(uintptr_t, ((uint8_t *)&gm_l57) + 8);
                        uint8_t t493 = (t491 < t492);
                        if (!t493) goto lb490;
                        {
                            uint64_t t496 = gm_l58;
                            uint64_t t497 = (uintptr_t)t496;
                            uint64_t t498 = GM_LD(uintptr_t, ((uint8_t *)&gm_l57) + 8);
                            gm_tckdobunaulee t501 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l57)) + t497 * 72));
                            F.l59 = t501;
                            {
                                void * t503 = (void *)((uint8_t *)&F.l56);
                                gm_b16_8 t504 = GM_LD(gm_b16_8, (((uint8_t *)&F.l59) + 0));
                                gm_tftb1odc739t5 t505 = gm_f_push_3ef746b(t503, t504);
                                gm_tftb1odc739t5 t506 = t505;
                                if (((((uint8_t *)&t506))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t506) + 1, gm_srcs9q7ghsrcgv5, 289, 42);
                                gm_b0_1 t507 = GM_LD(gm_b0_1, (((uint8_t *)&t506) + 1));
                            }
                            goto me499;
mn500:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 289, 21);
me499:;
                        }
lc490:;
                        {
                            uint64_t t509 = gm_l58;
                            uint64_t r510; bool o511 = __builtin_add_overflow(t509, ((uint64_t)1ULL), &r510);
                            uint64_t t512 = r510;
                            gm_l58 = t512;
                        }
                    }
lb490:;
                }
            }
            {
                gm_b16_8 t513 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 0));
                gm_b16_8 t514 = t513;
                gm_b16_8 t515 = GM_LD(gm_b16_8, ((uint8_t *)&t514));
                void * t516 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t517 = gm_f_unique_view_members_3bvsjcm((void *)NULL, cx, NULL, t515, t516);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l60 = t517;
            }
            {
                gm_f_list_58nr42v_2((void *)&F.l61, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t519 = gm_l54;
                    gm_b16_8 t520 = t519;
                    gm_b16_8 t521 = GM_LD(gm_b16_8, ((uint8_t *)&t520));
                    gm_l62 = t521;
                }
                {
                    gm_l63 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t523 = gm_l63;
                        uint64_t t524 = GM_LD(uintptr_t, ((uint8_t *)&gm_l62) + 8);
                        uint8_t t525 = (t523 < t524);
                        if (!t525) goto lb522;
                        {
                            gm_t3l8idb2m6c1r t528;
                            uint64_t t529 = gm_l63;
                            GM_ST(uint64_t, (uint8_t *)&t528 + 0, t529);
                            uint64_t t530 = gm_l63;
                            uint64_t t531 = (uintptr_t)t530;
                            uint64_t t532 = GM_LD(uintptr_t, ((uint8_t *)&gm_l62) + 8);
                            memcpy((uint8_t *)&t528 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l62)) + t531 * 72), 72);
                            gm_t3l8idb2m6c1r t533 = t528;
                            uint64_t t536 = GM_LD(uint64_t, (((uint8_t *)&t533) + 0));
                            gm_l64 = t536;
                            gm_tckdobunaulee t537 = GM_LD(gm_tckdobunaulee, (((uint8_t *)&t533) + 8));
                            F.l65 = t537;
                            {
                                gm_tkn42u7na3t81 t539 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l65) + 16));
                                gm_tkn42u7na3t81 t540 = t539;
                                gm_t88rcucg1b7uk t541;
                                if (((((uint8_t *)&t540))[4] != 0)) {
                                    gm_t88rcucg1b7uk t542 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t540) + 0));
                                    t541 = t542;
                                } else {
                                    goto lc522;
                                }
                                gm_l66 = t541;
                            }
                            {
                                void * t543 = (void *)((uint8_t *)&F.l61);
                                gm_tcmkompbl09il t544;
                                memset((uint8_t *)&t544 + 4, 0, 4);
                                gm_t88rcucg1b7uk t545 = gm_l66;
                                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t544 + 0, t545);
                                uint64_t t546 = gm_l64;
                                uint64_t t547 = (uintptr_t)t546;
                                uint64_t t548 = GM_LD(uintptr_t, ((uint8_t *)&gm_l60) + 8);
                                if (t547 >= (uintptr_t)(t548)) gm_trap_index(t547, t548, gm_srcs9q7ghsrcgv5, 294, 52);
                                gm_th2lqbo77pvsc t549 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l60)) + t547 * 24));
                                gm_th2lqbo77pvsc t550 = t549;
                                gm_b16_8 t551;
                                if (((((uint8_t *)&t550))[16] != 0)) {
                                    gm_b16_8 t552 = GM_LD(gm_b16_8, (((uint8_t *)&t550) + 0));
                                    t551 = t552;
                                } else {
                                    uint64_t t553 = gm_l64;
                                    gm_t4ce0jd3tdro4 t554;
                                    GM_ST(uint64_t, ((uint8_t *)&t554 + 0), t553);
                                    gm_tabb710d9mq2e t555;
                                    gm_f_format_0bd7vot((void *)&t555, (void *)NULL, cx, NULL, t554);
                                    if (gm_unw.kind) {
                                        goto gm_ll526;
                                    }
                                    gm_tabb710d9mq2e t556 = t555;
                                    if (((((uint8_t *)&t556))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t556) + 8, gm_srcs9q7ghsrcgv5, 294, 67);
                                    gm_b16_8 t557 = GM_LD(gm_b16_8, (((uint8_t *)&t556) + 8));
                                    gm_b16_8 t558 = t557;
                                    gm_b16_8 t559 = GM_LD(gm_b16_8, ((uint8_t *)&t558));
                                    t551 = t559;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t544 + 8, t551);
                                gm_tftb1odc739t5 t560 = gm_f_push_bpovr2k_2(t543, &t544);
                                gm_tftb1odc739t5 t561 = t560;
                                if (((((uint8_t *)&t561))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t561) + 1, gm_srcs9q7ghsrcgv5, 294, 25);
                                gm_b0_1 t562 = GM_LD(gm_b0_1, (((uint8_t *)&t561) + 1));
                            }
                            goto me534;
mn535:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 292, 21);
me534:;
                        }
lc522:;
                        {
                            uint64_t t564 = gm_l63;
                            uint64_t r565; bool o566 = __builtin_add_overflow(t564, ((uint64_t)1ULL), &r565);
                            uint64_t t567 = r565;
                            gm_l63 = t567;
                        }
                    }
gm_ll526:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 8) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb522; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc522; }
                    }
                    goto gm_lad;
lb522:;
                }
            }
            {
                void * t568 = gm_l0;
                void * t569 = (void *)((uint8_t *)&F.l7);
                uint64_t t570 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t571 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t570 >= (uintptr_t)(t571)) gm_trap_index(t570, t571, gm_srcs9q7ghsrcgv5, 296, 45);
                uint64_t t572 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t570 * 8));
                gm_b16_8 t573 = GM_LD(gm_b16_8, (((uint8_t *)&F.l61) + 0));
                gm_b16_8 t574 = t573;
                gm_b16_8 t575 = GM_LD(gm_b16_8, ((uint8_t *)&t574));
                gm_b0_1 t576 = gm_f_push_union_m2cqbk2((void *)NULL, cx, NULL, t568, t569, t572, t575);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me273;
mn448:;
ma277:;
            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 263, 13);
me273:;
        }
        goto me28;
mn267:;
ma34:;
        gm_b0_1 t578;
        uint8_t t579 = 0;
        F.ret = t578;
        goto gm_fret;
        goto me28;
mn577:;
ma35:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 234, 5);
me28:;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l67, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l68 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t581 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_l69 = t581;
        }
        {
            gm_l70 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t583 = gm_l70;
                uint64_t t584 = GM_LD(uintptr_t, ((uint8_t *)&gm_l69) + 8);
                uint8_t t585 = (t583 < t584);
                if (!t585) goto lb582;
                {
                    uint64_t t588 = gm_l70;
                    uint64_t t589 = (uintptr_t)t588;
                    uint64_t t590 = GM_LD(uintptr_t, ((uint8_t *)&gm_l69) + 8);
                    gm_tbccvikg6b96e t593 = GM_LD(gm_tbccvikg6b96e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l69)) + t589 * 32));
                    gm_l71 = t593;
                    {
                        void * t595 = (void *)((uint8_t *)&F.l67);
                        uint64_t t596 = GM_LD(uint64_t, (((uint8_t *)&gm_l71) + 0));
                        uint64_t t597 = gm_l68;
                        uint64_t r598; bool o599 = __builtin_sub_overflow(t596, t597, &r598);
                        if (o599) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 305, 20);
                        uint64_t t600 = r598;
                        gm_b0_1 t601 = gm_f_pad_juftt9c((void *)NULL, cx, NULL, t595, t600);
                        if (gm_unw.kind) {
                            goto gm_ll586;
                        }
                    }
                    {
                        void * t602 = (void *)((uint8_t *)&F.l67);
                        gm_b16_8 t603 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l71) + 16));
                        gm_tnpifv20irnph t604;
                        GM_ST(gm_b16_8, ((uint8_t *)&t604 + 0), t603);
                        gm_tftb1odc739t5 t605 = gm_f_write_dqbiqk0(t602, t604);
                        gm_tftb1odc739t5 t606 = t605;
                        if (((((uint8_t *)&t606))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t606) + 1, gm_srcs9q7ghsrcgv5, 306, 9);
                        gm_b0_1 t607 = GM_LD(gm_b0_1, (((uint8_t *)&t606) + 1));
                    }
                    {
                        uint64_t t608 = GM_LD(uint64_t, (((uint8_t *)&gm_l71) + 0));
                        uint64_t t609 = GM_LD(uint64_t, (((uint8_t *)&gm_l71) + 8));
                        uint64_t r610; bool o611 = __builtin_add_overflow(t608, t609, &r610);
                        if (o611) gm_trap("integer overflow in `+`", gm_srcs9q7ghsrcgv5, 307, 14);
                        uint64_t t612 = r610;
                        gm_l68 = t612;
                    }
                    goto me591;
mn592:;
                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 304, 5);
me591:;
                }
lc582:;
                {
                    uint64_t t614 = gm_l70;
                    uint64_t r615; bool o616 = __builtin_add_overflow(t614, ((uint64_t)1ULL), &r615);
                    uint64_t t617 = r615;
                    gm_l70 = t617;
                }
            }
gm_ll586:;
            if (gm_unw.frame == &F.h && gm_unw.label == 9) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb582; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc582; }
            }
            goto gm_lad;
lb582:;
        }
    }
    {
        void * t618 = (void *)((uint8_t *)&F.l67);
        uint64_t t619 = gm_l5;
        uint64_t t620 = gm_l68;
        uint64_t r621; bool o622 = __builtin_sub_overflow(t619, t620, &r621);
        if (o622) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 309, 16);
        uint64_t t623 = r621;
        gm_b0_1 t624 = gm_f_pad_juftt9c((void *)NULL, cx, NULL, t618, t623);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t625 = gm_l0;
        void * t626 = (void *)((((uint8_t *)t625) + 872) + 104);
        gm_t6j6smj7qoctu t627;
        gm_b16_8 t628 = gm_l4;
        gm_b16_8 t629 = gm_f_packed_v79gj1r((void *)NULL, cx, NULL, t628);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t627 + 0, t629);
        gm_b16_8 t630 = GM_LD(gm_b16_8, (((uint8_t *)&F.l67) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t627 + 16, t630);
        gm_tftb1odc739t5 t631 = gm_f_write_ue93873(t626, &t627);
        gm_tftb1odc739t5 t632 = t631;
        if (((((uint8_t *)&t632))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t632) + 1, gm_srcs9q7ghsrcgv5, 310, 5);
        gm_b0_1 t633 = GM_LD(gm_b0_1, (((uint8_t *)&t632) + 1));
    }
    {
        void * t634 = gm_l0;
        gm_b16_8 t635 = gm_l4;
        uint64_t t636 = gm_l5;
        gm_b0_1 t637 = gm_f_static_assert_15uo2jf((void *)NULL, cx, NULL, t634, t635, t636);
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
__attribute__((aligned(4))) void gm_f_enum_info_61n2nkp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_enum_info_61n2nkp F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    void * gm_l2; /* def */
    gm_t88rcucg1b7uk gm_l3; /* tag */
    uint64_t gm_l4; /* size */
    uint64_t gm_l5; /* n */
    uint8_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* base */
    GM_STACK_CHECK(&F, 844u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_ghvav22((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)17ULL)) goto ie2;
        {
            void * t9 = gm_l0;
            gm_t88rcucg1b7uk t10 = gm_l1;
            void * t11 = gm_f_adt_of_gej99vn(t9, GM_CONV(gm_csguankko4lmhd, t10));
            gm_l2 = t11;
        }
        {
            void * t13 = gm_l2;
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t13) + 24) + 0));
            if (t14 != ((uint8_t)1ULL)) goto ie12;
            gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t13) + 24) + 8) + 0));
            gm_l3 = t15;
            {
                void * t17 = gm_l0;
                gm_t88rcucg1b7uk t18 = gm_l3;
                uint64_t t19 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t17, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t19;
            }
            {
                uint64_t t21 = gm_l4;
                uint8_t t22 = (t21 > ((uint64_t)4ULL));
                if (!t22) goto ie20;
                {
                    gm_t4ml5v7ugcjrt t24;
                    memset(&t24, 0, sizeof t24);
                    uint8_t t25 = 0;
                    F.ret = t24;
                    goto gm_fret;
                }
                goto ix20;
ie20:;
ix20:;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t27 = gm_l5;
                    void * t28 = gm_l2;
                    uint64_t t29 = GM_LD(uintptr_t, (((uint8_t *)t28) + 0) + 8);
                    uint8_t t30 = (t27 < t29);
                    if (!t30) goto lb26;
                    uint8_t t31 = 0;
                    uint8_t t32 = 0;
                    uint8_t t33 = 0;
                    {
                        uint64_t t35 = gm_l5;
                        uint64_t t36 = (uintptr_t)t35;
                        void * t37 = gm_l2;
                        uint64_t t38 = GM_LD(uintptr_t, (((uint8_t *)t37) + 0) + 8);
                        if (t36 >= (uintptr_t)(t38)) gm_trap_index(t36, t38, gm_srcs9q7ghsrcgv5, 362, 33);
                        uint8_t t39 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t37) + 0)) + t36 * 1));
                        gm_l6 = t39;
                    }
                    uint8_t t40 = 0;
                    uint8_t t41 = gm_l6;
                    uint8_t t42 = (((uint8_t)97ULL) <= t41);
                    if (t42) {
                        uint8_t t43 = gm_l6;
                        uint8_t t44 = (t43 <= ((uint8_t)122ULL));
                        t40 = t44;
                    } else t40 = 0;
                    uint8_t t45 = t40;
                    if (!t45) {
                        {
                            uint64_t t47 = gm_l5;
                            uint64_t t48 = (uintptr_t)t47;
                            void * t49 = gm_l2;
                            uint64_t t50 = GM_LD(uintptr_t, (((uint8_t *)t49) + 0) + 8);
                            if (t48 >= (uintptr_t)(t50)) gm_trap_index(t48, t50, gm_srcs9q7ghsrcgv5, 362, 61);
                            uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t49) + 0)) + t48 * 1));
                            gm_l7 = t51;
                        }
                        uint8_t t52 = 0;
                        uint8_t t53 = gm_l7;
                        uint8_t t54 = (((uint8_t)65ULL) <= t53);
                        if (t54) {
                            uint8_t t55 = gm_l7;
                            uint8_t t56 = (t55 <= ((uint8_t)90ULL));
                            t52 = t56;
                        } else t52 = 0;
                        uint8_t t57 = t52;
                        t33 = t57;
                    } else t33 = 1;
                    if (!t33) {
                        {
                            uint64_t t59 = gm_l5;
                            uint64_t t60 = (uintptr_t)t59;
                            void * t61 = gm_l2;
                            uint64_t t62 = GM_LD(uintptr_t, (((uint8_t *)t61) + 0) + 8);
                            if (t60 >= (uintptr_t)(t62)) gm_trap_index(t60, t62, gm_srcs9q7ghsrcgv5, 362, 89);
                            uint8_t t63 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t61) + 0)) + t60 * 1));
                            gm_l8 = t63;
                        }
                        uint8_t t64 = 0;
                        uint8_t t65 = gm_l8;
                        uint8_t t66 = (((uint8_t)48ULL) <= t65);
                        if (t66) {
                            uint8_t t67 = gm_l8;
                            uint8_t t68 = (t67 <= ((uint8_t)57ULL));
                            t64 = t68;
                        } else t64 = 0;
                        uint8_t t69 = t64;
                        t32 = t69;
                    } else t32 = 1;
                    if (!t32) {
                        uint64_t t70 = gm_l5;
                        uint64_t t71 = (uintptr_t)t70;
                        void * t72 = gm_l2;
                        uint64_t t73 = GM_LD(uintptr_t, (((uint8_t *)t72) + 0) + 8);
                        if (t71 >= (uintptr_t)(t73)) gm_trap_index(t71, t73, gm_srcs9q7ghsrcgv5, 362, 117);
                        uint8_t t74 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t72) + 0)) + t71 * 1));
                        uint8_t t75 = ((t74 == ((uint8_t)95ULL)));
                        t31 = t75;
                    } else t31 = 1;
                    if (!t31) goto lb26;
                    {
                        uint64_t t78 = gm_l5;
                        uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                        if (o80) gm_trap("integer overflow in `+`", gm_srcs9q7ghsrcgv5, 362, 139);
                        uint64_t t81 = r79;
                        gm_l5 = t81;
                    }
lc26:;
                }
lb26:;
            }
            {
                void * t82 = gm_l2;
                uint64_t t83 = GM_LD(uintptr_t, (((uint8_t *)t82) + 0) + 8);
                void * t84 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t82) + 0)));
                uint64_t t85 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t86 = gm_l5;
                uint64_t t87 = (uintptr_t)t86;
                if (t85 > t87 || t87 > (uintptr_t)t83) gm_trap_slice(t85, t87, t83, gm_srcs9q7ghsrcgv5, 363, 13);
                gm_b16_8 t88;
                GM_ST(void *, (uint8_t *)&t88, (void *)(((uint8_t *)t84 + t85 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(t87 - t85));
                gm_l9 = t88;
            }
            {
                gm_b16_8 t90 = gm_l9;
                gm_th2lqbo77pvsc t91;
                gm_f_member_name_mj2bvs3((void *)&t91, (void *)NULL, cx, NULL, t90);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t92 = t91;
                uint8_t t93;
                if (((((uint8_t *)&t92))[16] != 0)) goto mn95;
                t93 = ((uint8_t)1);
                goto me94;
mn95:;
                t93 = ((uint8_t)0);
                goto me94;
mn96:;
                gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 364, 8);
me94:;
                if (!t93) goto ie89;
                {
                    gm_t4ml5v7ugcjrt t98;
                    memset(&t98, 0, sizeof t98);
                    uint8_t t99 = 0;
                    F.ret = t98;
                    goto gm_fret;
                }
                goto ix89;
ie89:;
ix89:;
            }
            {
                gm_th5c3ge61oapm t100;
                memset((uint8_t *)&t100 + 25, 0, 7);
                gm_b16_8 t101 = gm_l9;
                GM_ST(gm_b16_8, (uint8_t *)&t100 + 0, t101);
                uint64_t t102 = gm_l4;
                GM_ST(uint64_t, (uint8_t *)&t100 + 16, t102);
                void * t103 = gm_l0;
                gm_t88rcucg1b7uk t104 = gm_l3;
                uint8_t t105 = gm_f_is_signed_integer_ige9b74(t103, GM_CONV(gm_csguankko4lmhd, t104));
                GM_ST(uint8_t, (uint8_t *)&t100 + 24, t105);
                gm_t4ml5v7ugcjrt t106;
                GM_ST(uint8_t, (uint8_t *)&t106 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t106 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t106 + 36, 0);
                GM_ST(gm_th5c3ge61oapm, ((uint8_t *)&t106), t100);
                ((uint8_t *)&t106)[32] = 1;
                uint8_t t107 = 0;
                F.ret = t106;
                goto gm_fret;
            }
            goto ix12;
ie12:;
            {
                gm_t4ml5v7ugcjrt t109;
                memset(&t109, 0, sizeof t109);
                uint8_t t110 = 0;
                F.ret = t109;
                goto gm_fret;
            }
ix12:;
        }
        goto ix2;
ie2:;
        {
            gm_t4ml5v7ugcjrt t112;
            memset(&t112, 0, sizeof t112);
            uint8_t t113 = 0;
            F.ret = t112;
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
__attribute__((aligned(4))) uint8_t gm_f_is_aggregate_buh1uic(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_aggregate_buh1uic F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l3; /* variants */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* p */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t53at80pfdus3 t4;
        gm_f_kind_ghvav22((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_t53at80pfdus3 t5 = t4;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma7:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t16 != ((uint8_t)12ULL)) goto pa15;
        goto po14;
pa15:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t18 != ((uint8_t)10ULL)) goto pa17;
        goto po14;
pa17:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t20 != ((uint8_t)14ULL)) goto pa19;
        goto po14;
pa19:;
        goto mn13;
po14:;
        uint8_t t21 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me6;
mn13:;
ma8:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t23 != ((uint8_t)13ULL)) goto mn22;
        void * t24 = gm_l0;
        gm_t88rcucg1b7uk t25 = gm_l1;
        gm_t5v8n6qrn4rh4 t26;
        gm_f_repr_53quqki((void *)&t26, (void *)NULL, cx, NULL, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5v8n6qrn4rh4 t27 = t26;
        uint8_t t28 = (!(GM_LD(uint8_t, ((uint8_t *)&t27) + 0) == ((uint8_t)3ULL)));
        uint8_t t29 = 0;
        F.ret = t28;
        goto gm_fret;
        goto me6;
mn22:;
ma9:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t31 != ((uint8_t)17ULL)) goto mn30;
        {
            void * t33 = gm_l0;
            gm_t88rcucg1b7uk t34 = gm_l1;
            void * t35 = gm_f_adt_of_gej99vn(t33, GM_CONV(gm_csguankko4lmhd, t34));
            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t35) + 24) + 0));
            switch (t41) { case ((uint8_t)0ULL): goto ma37; case ((uint8_t)1ULL): goto ma39; case ((uint8_t)2ULL): goto ma38; default: goto ma40; }
ma37:;
            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t35) + 24) + 0));
            if (t43 != ((uint8_t)0ULL)) goto mn42;
            gm_tpautr4br7h3l t44 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t35) + 24) + 8));
            F.l2 = t44;
            uint8_t t45;
            if ((((((uint8_t *)&F.l2) + 40))[4] != 0)) goto mn47;
            t45 = ((uint8_t)1);
            goto me46;
mn47:;
            t45 = ((uint8_t)0);
            goto me46;
mn48:;
            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 143, 40);
me46:;
            uint8_t t49 = 0;
            F.ret = t45;
            goto gm_fret;
            goto me36;
mn42:;
ma38:;
            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t35) + 24) + 0));
            if (t51 != ((uint8_t)2ULL)) goto mn50;
            uint8_t t52 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
            goto me36;
mn50:;
ma39:;
            uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t35) + 24) + 0));
            if (t54 != ((uint8_t)1ULL)) goto mn53;
            gm_b16_8 t55 = GM_LD(gm_b16_8, (((((uint8_t *)t35) + 24) + 8) + 8));
            gm_l3 = t55;
            {
                {
                    gm_b16_8 t58 = gm_l3;
                    gm_l4 = t58;
                }
                {
                    gm_l5 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t60 = gm_l5;
                        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        uint8_t t62 = (t60 < t61);
                        if (!t62) goto lb59;
                        {
                            uint64_t t65 = gm_l5;
                            uint64_t t66 = (uintptr_t)t65;
                            uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            gm_tckdobunaulee t70 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t66 * 72));
                            F.l6 = t70;
                            {
                                if (!(((((uint8_t *)&F.l6) + 16))[4] != 0)) goto ie72;
                                gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 16));
                                gm_l7 = t73;
                                {
                                    void * t76 = gm_l0;
                                    gm_t88rcucg1b7uk t77 = gm_l7;
                                    uint64_t t78 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t76, t77);
                                    if (gm_unw.kind) {
                                        goto gm_ll63;
                                    }
                                    uint8_t t79 = (t78 > ((uint64_t)0ULL));
                                    if (!t79) goto ie75;
                                    {
                                        uint8_t t81 = 0;
                                        F.ret = ((uint8_t)1);
                                        goto gm_fret;
                                    }
                                    goto ix75;
ie75:;
ix75:;
                                }
                                goto ix72;
ie72:;
ix72:;
                            }
                            goto me68;
mn69:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 146, 21);
me68:;
                        }
lc59:;
                        {
                            uint64_t t83 = gm_l5;
                            uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                            uint64_t t86 = r84;
                            gm_l5 = t86;
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
                uint8_t t87 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto me36;
mn53:;
ma40:;
            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 142, 13);
me36:;
        }
        goto me6;
mn30:;
ma10:;
        uint8_t t89 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me6;
mn88:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 137, 5);
me6:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_packed_v79gj1r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_packed_v79gj1r F;
    gm_b16_8 gm_l0; /* cty */
    gm_t6j6smj7qoctu gm_l1; /* sp */
    GM_STACK_CHECK(&F, 514u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(1));
        gm_ttu09lij36f8c t5;
        gm_f_split_once_mqcu1ce((void *)&t5, t3, t4);
        gm_ttu09lij36f8c t6 = t5;
        if (!((((uint8_t *)&t6))[32] != 0)) goto ie2;
        gm_t6j6smj7qoctu t7 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t6));
        gm_l1 = t7;
        {
            gm_t6j6smj7qoctu t9;
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t9 + 0, t10);
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 16));
            GM_ST(gm_b16_8, (uint8_t *)&t9 + 16, t11);
            gm_tabb710d9mq2e t12;
            gm_f_format_v301q4v((void *)&t12, (void *)NULL, cx, NULL, &t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t13 = t12;
            if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_srcs9q7ghsrcgv5, 431, 48);
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
            gm_b16_8 t15 = t14;
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t18 = gm_l0;
        gm_tnpifv20irnph t19;
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t18);
        gm_tabb710d9mq2e t20;
        gm_f_format_ivs3cj0((void *)&t20, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t21 = t20;
        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 8, gm_srcs9q7ghsrcgv5, 432, 12);
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
        gm_b16_8 t23 = t22;
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_pad_juftt9c(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_pad_juftt9c F;
    void * gm_l0; /* body */
    uint64_t gm_l1; /* bytes */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 234u);
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
            uint64_t t3 = gm_l1;
            if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcs9q7ghsrcgv5, 437, 17);
            uint64_t t4 = (uint64_t)(t3 / ((uint64_t)8ULL));
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
                    uint64_t t12 = t11;
                    {
                        void * t16 = gm_l0;
                        gm_b16_8 t17;
                        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sakkgen9m77d6));
                        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(15));
                        gm_tftb1odc739t5 t18 = gm_f_extend_0cs85gs(t16, t17);
                        gm_tftb1odc739t5 t19 = t18;
                        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 1, gm_srcs9q7ghsrcgv5, 437, 29);
                        gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 437, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t22 = gm_l3;
                    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                    uint64_t t25 = r23;
                    gm_l3 = t25;
                }
            }
lb5:;
        }
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t27 = gm_l1;
            if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcs9q7ghsrcgv5, 438, 17);
            uint64_t t28 = (uint64_t)(t27 % ((uint64_t)8ULL));
            gm_l4 = t28;
        }
        {
            for (;;) {
                uint64_t t30 = gm_l5;
                uint64_t t31 = gm_l4;
                uint8_t t32 = (t30 < t31);
                if (!t32) goto lb29;
                {
                    uint64_t t35 = gm_l5;
                    uint64_t t36 = t35;
                    {
                        void * t40 = gm_l0;
                        gm_b16_8 t41;
                        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sumv38t63phct));
                        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(13));
                        gm_tftb1odc739t5 t42 = gm_f_extend_0cs85gs(t40, t41);
                        gm_tftb1odc739t5 t43 = t42;
                        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcs9q7ghsrcgv5, 438, 29);
                        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
                    }
                    goto me37;
mn38:;
                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 438, 5);
me37:;
                }
lc29:;
                {
                    uint64_t t46 = gm_l5;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l5 = t49;
                }
            }
lb29:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_pointer_view_efkbf4q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_pointer_view_efkbf4q F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    gm_t88rcucg1b7uk gm_l5; /* target */
    GM_STACK_CHECK(&F, 399u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t53at80pfdus3 t4;
        gm_f_kind_ghvav22((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_t53at80pfdus3 t5 = t4;
        gm_t88rcucg1b7uk t6;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma8:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t15 != ((uint8_t)8ULL)) goto mn14;
        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l2 = t16;
        gm_t88rcucg1b7uk t17 = gm_l2;
        t6 = t17;
        goto me7;
mn14:;
ma9:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t19 != ((uint8_t)9ULL)) goto mn18;
        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l3 = t20;
        gm_t88rcucg1b7uk t21 = gm_l3;
        t6 = t21;
        goto me7;
mn18:;
ma10:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t23 != ((uint8_t)13ULL)) goto mn22;
        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 8));
        gm_l4 = t24;
        void * t25 = gm_l0;
        gm_t88rcucg1b7uk t26 = gm_l4;
        gm_b16_8 t27 = gm_f_pointer_view_efkbf4q((void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t28 = 0;
        F.ret = t27;
        goto gm_fret;
        goto me7;
mn22:;
ma11:;
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s44ek1gecbajs));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(6));
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
        goto me7;
mn29:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 194, 18);
me7:;
        gm_l5 = t6;
    }
    {
        void * t32 = gm_l0;
        gm_t88rcucg1b7uk t33 = gm_l5;
        gm_b16_8 t34 = gm_f_target_view_ams6b9s((void *)NULL, cx, NULL, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t35;
        GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t34);
        gm_tabb710d9mq2e t36;
        gm_f_format_lfjb9aq((void *)&t36, (void *)NULL, cx, NULL, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t37 = t36;
        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 8, gm_srcs9q7ghsrcgv5, 200, 12);
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
        gm_b16_8 t39 = t38;
        gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_member_eubf7s6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_push_member_eubf7s6 F;
    void * gm_l0; /* ty */
    void * gm_l1; /* members */
    uint64_t gm_l2; /* offset */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* name */
    gm_t6j6smj7qoctu gm_l5; /* v */
    GM_STACK_CHECK(&F, 521u);
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
        gm_t88rcucg1b7uk t3 = gm_l3;
        gm_ttu09lij36f8c t4;
        gm_f_view_qkqm3in((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t5 = t4;
        gm_t6j6smj7qoctu t6;
        if (((((uint8_t *)&t5))[32] != 0)) {
            gm_t6j6smj7qoctu t7 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l5 = t6;
    }
    {
        void * t10 = gm_l0;
        gm_b16_8 t11 = gm_l4;
        gm_b0_1 t12 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = gm_l1;
        gm_tbccvikg6b96e t14;
        uint64_t t15 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t14 + 0, t15);
        void * t16 = gm_l0;
        gm_t88rcucg1b7uk t17 = gm_l3;
        uint64_t t18 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint64_t, (uint8_t *)&t14 + 8, t18);
        gm_tfeab279c16rr t19;
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 0, t20);
        gm_b16_8 t21 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 16, t21);
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 32, t22);
        gm_tabb710d9mq2e t23;
        gm_f_format_hn5gv7p((void *)&t23, (void *)NULL, cx, NULL, &t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t24 = t23;
        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_srcs9q7ghsrcgv5, 324, 69);
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
        gm_b16_8 t26 = t25;
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        GM_ST(gm_b16_8, (uint8_t *)&t14 + 16, t27);
        gm_tftb1odc739t5 t28 = gm_f_push_m7lva8q(t13, &t14);
        gm_tftb1odc739t5 t29 = t28;
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srcs9q7ghsrcgv5, 324, 5);
        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_union_m2cqbk2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_b16_8 a3) {
    struct gm_fr_push_union_m2cqbk2 F;
    void * gm_l0; /* ty */
    void * gm_l1; /* members */
    uint64_t gm_l2; /* offset */
    gm_b16_8 gm_l3; /* arms */
    uint64_t gm_l5; /* size */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tcmkompbl09il gm_l8; /* arm */
    gm_t6j6smj7qoctu gm_l9; /* v */
    GM_STACK_CHECK(&F, 746u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_list_uvfu1k1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t3 = gm_l3;
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
                    gm_tcmkompbl09il t15 = GM_LD(gm_tcmkompbl09il, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t11 * 24));
                    gm_l8 = t15;
                    {
                        void * t17 = gm_l0;
                        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l8) + 0));
                        gm_ttu09lij36f8c t19;
                        gm_f_view_qkqm3in((void *)&t19, (void *)NULL, cx, NULL, t17, t18);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_ttu09lij36f8c t20 = t19;
                        gm_t6j6smj7qoctu t21;
                        if (((((uint8_t *)&t20))[32] != 0)) {
                            gm_t6j6smj7qoctu t22 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t20) + 0));
                            t21 = t22;
                        } else {
                            goto lc4;
                        }
                        gm_l9 = t21;
                    }
                    {
                        void * t23 = gm_l0;
                        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                        gm_b0_1 t25 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t23, t24);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        void * t26 = (void *)((uint8_t *)&F.l4);
                        gm_tfeab279c16rr t27;
                        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t27 + 0, t28);
                        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                        GM_ST(gm_b16_8, (uint8_t *)&t27 + 16, t29);
                        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 16));
                        GM_ST(gm_b16_8, (uint8_t *)&t27 + 32, t30);
                        gm_tftb1odc739t5 t31 = gm_f_write_vfiknt5(t26, &t27);
                        gm_tftb1odc739t5 t32 = t31;
                        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srcs9q7ghsrcgv5, 334, 9);
                        gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
                    }
                    {
                        uint64_t t34 = gm_l5;
                        void * t35 = gm_l0;
                        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l8) + 0));
                        uint64_t t37 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t35, t36);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        uint64_t t38 = (t34 > t37 ? t34 : t37);
                        gm_l5 = t38;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 331, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t40 = gm_l7;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l7 = t43;
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
        uint64_t t45 = gm_l5;
        uint8_t t46 = (t45 > ((uint64_t)0ULL));
        if (!t46) goto ie44;
        {
            void * t48 = gm_l1;
            gm_tbccvikg6b96e t49;
            uint64_t t50 = gm_l2;
            GM_ST(uint64_t, (uint8_t *)&t49 + 0, t50);
            uint64_t t51 = gm_l5;
            GM_ST(uint64_t, (uint8_t *)&t49 + 8, t51);
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_tnpifv20irnph t53;
            GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
            gm_tabb710d9mq2e t54;
            gm_f_format_phh9f1s((void *)&t54, (void *)NULL, cx, NULL, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t55 = t54;
            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_srcs9q7ghsrcgv5, 338, 67);
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
            gm_b16_8 t57 = t56;
            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
            GM_ST(gm_b16_8, (uint8_t *)&t49 + 16, t58);
            gm_tftb1odc739t5 t59 = gm_f_push_m7lva8q(t48, &t49);
            gm_tftb1odc739t5 t60 = t59;
            if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_srcs9q7ghsrcgv5, 338, 9);
            gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
        }
        goto ix44;
ie44:;
ix44:;
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
inline __attribute__((aligned(4))) void gm_f_reserve_40g0de6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_reserve_40g0de6 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* cty */
    GM_STACK_CHECK(&F, 449u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_b16_8 t5 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t6 = gm_f_view_number_29hi30b((void *)NULL, cx, NULL, t2, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t6;
    }
    {
        gm_b16_8 t7;
        void * t9 = gm_l0;
        gm_t88rcucg1b7uk t10 = gm_l1;
        uint8_t t11 = gm_f_is_aggregate_buh1uic((void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t11) goto ie8;
        gm_tf1bjg81u0glv t13;
        uint64_t t14 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t13 + 0, t14);
        void * t15 = gm_l0;
        gm_t88rcucg1b7uk t16 = gm_l1;
        gm_b16_8 t17 = gm_f_view_kind_pth6nol((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t13 + 8, t17);
        gm_tabb710d9mq2e t18;
        gm_f_format_08mmoe2((void *)&t18, (void *)NULL, cx, NULL, &t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t19 = t18;
        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_srcs9q7ghsrcgv5, 186, 43);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        gm_b16_8 t21 = t20;
        gm_b16_8 t22 = t21;
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
        t7 = t23;
        goto ix8;
ie8:;
        uint64_t t25 = gm_l2;
        gm_t4ce0jd3tdro4 t26;
        GM_ST(uint64_t, ((uint8_t *)&t26 + 0), t25);
        gm_tabb710d9mq2e t27;
        gm_f_format_8ibn6ei((void *)&t27, (void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t28 = t27;
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_srcs9q7ghsrcgv5, 186, 104);
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
        gm_b16_8 t30 = t29;
        gm_b16_8 t31 = t30;
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        t7 = t32;
ix8:;
        gm_l3 = t7;
    }
    {
        gm_b16_8 t33 = gm_l3;
        gm_f_scalar_f0e206b((void *)&F.l4, t33);
    }
    {
        void * t34 = gm_l0;
        void * t35 = (void *)((((uint8_t *)t34) + 872) + 0);
        uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_tftb1odc739t5 t37 = gm_f_put_25ngckq(t35, t36, &(*(gm_ttu09lij36f8c *)((uint8_t *)&F.l4)));
        gm_tftb1odc739t5 t38 = t37;
        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srcs9q7ghsrcgv5, 188, 5);
        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l4), 40);
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
inline __attribute__((aligned(4))) void gm_f_scalar_f0e206b(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scalar_f0e206b F;
    gm_b16_8 gm_l0; /* cty */
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t6j6smj7qoctu t1;
    gm_b16_8 t2 = gm_l0;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    gm_b16_8 t3 = GM_LD(gm_b16_8, gm_cgsmfcq937e_3);
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 16, t3);
    gm_ttu09lij36f8c t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 33, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 34, 0);
    GM_ST(uint32_t, (uint8_t *)&t4 + 36, 0);
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t4), t1);
    ((uint8_t *)&t4)[32] = 1;
    F.ret = t4;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_ttu09lij36f8c gm_f_scalar_f0e206b_c(gm_b16_8 a0) { gm_ttu09lij36f8c gm_r; gm_f_scalar_f0e206b(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_static_assert_15uo2jf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2) {
    struct gm_fr_static_assert_15uo2jf F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* cty */
    uint64_t gm_l2; /* size */
    GM_STACK_CHECK(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((((uint8_t *)t2) + 872) + 104);
        gm_t3877pk7lv748 t4;
        gm_b16_8 t5 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        uint64_t t6 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t4 + 16, t6);
        gm_tftb1odc739t5 t7 = gm_f_write_u1no4ia(t3, &t4);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcs9q7ghsrcgv5, 343, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_target_view_ams6b9s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_target_view_ams6b9s F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t6j6smj7qoctu gm_l3; /* v */
    gm_t6j6smj7qoctu gm_l4; /* v */
    gm_t6j6smj7qoctu gm_l5; /* v */
    GM_STACK_CHECK(&F, 822u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((((uint8_t *)t3) + 872) + 0);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_tu3261i9p5758 t6;
        gm_f_get_v0dhnfj((void *)&t6, t4, t5);
        gm_tu3261i9p5758 t7 = t6;
        if (!((((uint8_t *)&t7))[40] != 0)) goto ie2;
        gm_ttu09lij36f8c t8 = GM_LD(gm_ttu09lij36f8c, ((uint8_t *)&t7));
        F.l2 = t8;
        {
            if (!((((uint8_t *)&F.l2))[32] != 0)) goto ie10;
            gm_t6j6smj7qoctu t11 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&F.l2));
            gm_l3 = t11;
            {
                uint64_t t14 = GM_LD(uintptr_t, (((uint8_t *)&gm_l3) + 16) + 8);
                uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
                if (!t15) goto ie13;
                gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s557bv2sfqcom));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(5));
                uint8_t t18 = gm_f_starts_with_6vvt01e(t16, t17);
                uint8_t t19 = (uint8_t)!t18;
                if (!t19) goto ie13;
                {
                    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t22 = 0;
                    F.ret = t21;
                    goto gm_fret;
                }
                goto ix13;
ie13:;
ix13:;
            }
            goto ix10;
ie10:;
ix10:;
        }
        {
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(4));
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t26 = gm_l0;
        gm_t88rcucg1b7uk t27 = gm_l1;
        uint64_t t28 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t29 = ((t28 == ((uint64_t)0ULL)));
        if (!t29) goto ie25;
        {
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(4));
            uint8_t t32 = 0;
            F.ret = t31;
            goto gm_fret;
        }
        goto ix25;
ie25:;
ix25:;
    }
    {
        void * t34 = gm_l0;
        gm_t88rcucg1b7uk t35 = gm_l1;
        uint8_t t36 = gm_f_is_aggregate_buh1uic((void *)NULL, cx, NULL, t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t36) goto ie33;
        {
            void * t38 = gm_l0;
            gm_t88rcucg1b7uk t39 = gm_l1;
            gm_ttu09lij36f8c t40;
            gm_f_reserve_40g0de6((void *)&t40, (void *)NULL, cx, NULL, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttu09lij36f8c t41 = t40;
            if (!((((uint8_t *)&t41))[32] != 0)) gm_trap("unwrap of null", gm_srcs9q7ghsrcgv5, 214, 14);
            gm_t6j6smj7qoctu t42 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t41));
            gm_l4 = t42;
        }
        {
            void * t43 = gm_l0;
            void * t44 = (void *)((((uint8_t *)t43) + 872) + 56);
            gm_t88rcucg1b7uk t45 = gm_l1;
            gm_tftb1odc739t5 t46 = gm_f_push_k3l9nic(t44, GM_CONV(gm_csguankko4lmhd, t45));
            gm_tftb1odc739t5 t47 = t46;
            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srcs9q7ghsrcgv5, 215, 9);
            gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
        }
        {
            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            uint8_t t50 = 0;
            F.ret = t49;
            goto gm_fret;
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        void * t52 = gm_l0;
        gm_t88rcucg1b7uk t53 = gm_l1;
        gm_ttu09lij36f8c t54;
        gm_f_view_qkqm3in((void *)&t54, (void *)NULL, cx, NULL, t52, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t55 = t54;
        if (!((((uint8_t *)&t55))[32] != 0)) goto ie51;
        gm_t6j6smj7qoctu t56 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t55));
        gm_l5 = t56;
        {
            uint64_t t59 = GM_LD(uintptr_t, (((uint8_t *)&gm_l5) + 16) + 8);
            uint8_t t60 = ((t59 == ((uint64_t)0ULL)));
            if (!t60) goto ie58;
            gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_b16_8 t62;
            GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s557bv2sfqcom));
            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(5));
            uint8_t t63 = gm_f_starts_with_6vvt01e(t61, t62);
            uint8_t t64 = (uint8_t)!t63;
            if (!t64) goto ie58;
            {
                gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
                uint8_t t67 = 0;
                F.ret = t66;
                goto gm_fret;
            }
            goto ix58;
ie58:;
ix58:;
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        gm_b16_8 t68;
        GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sts5j6f5pkov0));
        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(4));
        uint8_t t69 = 0;
        F.ret = t68;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_view_defs_u461okc(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_defs_u461okc F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((((uint8_t *)t1) + 872) + 104) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_view_kind_pth6nol(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_view_kind_pth6nol F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 568u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t53at80pfdus3 t4;
        gm_f_kind_ghvav22((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_t53at80pfdus3 t5 = t4;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma7:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t15 != ((uint8_t)10ULL)) goto mn14;
        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l2 = t16;
        {
            void * t19 = gm_l0;
            gm_t88rcucg1b7uk t20 = gm_l2;
            gm_t53at80pfdus3 t21;
            gm_f_kind_ghvav22((void *)&t21, t19, GM_CONV(gm_csguankko4lmhd, t20));
            gm_t53at80pfdus3 t22 = t21;
            uint8_t t23;
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            if (t26 != ((uint8_t)0ULL)) goto mn25;
            uint16_t t27 = GM_LD(uint16_t, ((((uint8_t *)&t22) + 8) + 2));
            if (!(t27 == ((uint16_t)8ULL))) goto mn25;
            t23 = ((uint8_t)1);
            goto me24;
mn25:;
            t23 = ((uint8_t)0);
            goto me24;
mn28:;
            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 164, 16);
me24:;
            if (!t23) goto ie18;
            {
                gm_b16_8 t30;
                GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s3p17h7dbk8us));
                GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(7));
                uint8_t t31 = 0;
                F.ret = t30;
                goto gm_fret;
            }
            goto ix18;
ie18:;
ix18:;
        }
        {
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_stntm4cqqrhf7));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(6));
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        goto me6;
mn14:;
ma8:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t35 != ((uint8_t)13ULL)) goto mn34;
        gm_b16_8 t36;
        GM_ST(void *, (uint8_t *)&t36, (void *)(gm_smg3p90in08sb));
        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(9));
        uint8_t t37 = 0;
        F.ret = t36;
        goto gm_fret;
        goto me6;
mn34:;
ma9:;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t39 != ((uint8_t)14ULL)) goto mn38;
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_si68bogf7eu32));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(7));
        uint8_t t41 = 0;
        F.ret = t40;
        goto gm_fret;
        goto me6;
mn38:;
ma10:;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t43 != ((uint8_t)17ULL)) goto mn42;
        gm_b16_8 t44;
        void * t46 = gm_l0;
        gm_t88rcucg1b7uk t47 = gm_l1;
        gm_t4ml5v7ugcjrt t48;
        gm_f_enum_info_61n2nkp((void *)&t48, (void *)NULL, cx, NULL, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4ml5v7ugcjrt t49 = t48;
        uint8_t t50;
        if (((((uint8_t *)&t49))[32] != 0)) goto mn52;
        t50 = ((uint8_t)0);
        goto me51;
mn52:;
        t50 = ((uint8_t)1);
        goto me51;
mn53:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 169, 33);
me51:;
        if (!t50) goto ie45;
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_s4v09sn49l08n));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(6));
        gm_b16_8 t56 = t55;
        t44 = t56;
        goto ix45;
ie45:;
        gm_b16_8 t58;
        GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(0));
        gm_b16_8 t59 = t58;
        t44 = t59;
ix45:;
        uint8_t t60 = 0;
        F.ret = t44;
        goto gm_fret;
        goto me6;
mn42:;
ma11:;
        gm_b16_8 t62;
        GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(0));
        uint8_t t63 = 0;
        F.ret = t62;
        goto gm_fret;
        goto me6;
mn61:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 162, 5);
me6:;
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
inline __attribute__((aligned(4))) uint64_t gm_f_view_number_29hi30b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_view_number_29hi30b F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* key */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        uint64_t t3 = gm_f_hash64_apkb7bo(t2);
        if (((uint64_t)1000000000000ULL) == 0) gm_trap("division by zero", gm_srcs9q7ghsrcgv5, 177, 10);
        uint64_t t4 = (uint64_t)(t3 % ((uint64_t)1000000000000ULL));
        gm_l2 = t4;
    }
    {
        for (;;) {
            void * t6 = gm_l0;
            void * t7 = (void *)((((uint8_t *)t6) + 872) + 144);
            uint64_t t8 = gm_l2;
            gm_ttsm01t0d8srg t9 = gm_f_get_fq78978(t7, t8);
            gm_ttsm01t0d8srg t10 = t9;
            uint8_t t11;
            if (((((uint8_t *)&t10))[0] != 0)) goto mn13;
            t11 = ((uint8_t)0);
            goto me12;
mn13:;
            t11 = ((uint8_t)1);
            goto me12;
mn14:;
            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 178, 11);
me12:;
            if (!t11) goto lb5;
            {
                uint64_t t17 = gm_l2;
                uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
                if (o19) gm_trap("integer overflow in `+`", gm_srcs9q7ghsrcgv5, 178, 54);
                uint64_t t20 = r18;
                if (((uint64_t)1000000000000ULL) == 0) gm_trap("division by zero", gm_srcs9q7ghsrcgv5, 178, 53);
                uint64_t t21 = (uint64_t)(t20 % ((uint64_t)1000000000000ULL));
                gm_l2 = t21;
            }
lc5:;
        }
lb5:;
    }
    {
        void * t22 = gm_l0;
        void * t23 = (void *)((((uint8_t *)t22) + 872) + 144);
        uint64_t t24 = gm_l2;
        gm_b0_1 t25;
        gm_tftb1odc739t5 t26 = gm_f_put_00pon0c(t23, t24, t25);
        gm_tftb1odc739t5 t27 = t26;
        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srcs9q7ghsrcgv5, 179, 5);
        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
    }
    {
        uint64_t t29 = gm_l2;
        uint8_t t30 = 0;
        F.ret = t29;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_view_qkqm3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_view_qkqm3in F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* k */
    gm_t88rcucg1b7uk gm_l6; /* p */
    uint64_t gm_l8; /* len */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    gm_t6j6smj7qoctu gm_l10; /* e */
    GM_STACK_CHECK(&F, 2169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((((uint8_t *)t3) + 872) + 0);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_tu3261i9p5758 t6;
        gm_f_get_v0dhnfj((void *)&t6, t4, t5);
        gm_tu3261i9p5758 t7 = t6;
        if (!((((uint8_t *)&t7))[40] != 0)) goto ie2;
        gm_ttu09lij36f8c t8 = GM_LD(gm_ttu09lij36f8c, ((uint8_t *)&t7));
        F.l2 = t8;
        {
            {
                void * t11 = gm_l0;
                gm_b16_8 t12 = GM_LD(gm_b16_8, (((((uint8_t *)t11) + 872) + 56) + 0));
                gm_b16_8 t13 = t12;
                gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
                gm_l3 = t14;
            }
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t16 = gm_l4;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t18 = (t16 < t17);
                    if (!t18) goto lb15;
                    {
                        uint64_t t21 = gm_l4;
                        uint64_t t22 = gm_l4;
                        uint64_t t23 = (uintptr_t)t22;
                        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t23 * 4));
                        gm_tlcm4lvu3g1a8 t26;
                        GM_ST(uint32_t, (uint8_t *)&t26 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t26 + 0), t21);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t26 + 8), t25);
                        gm_tlcm4lvu3g1a8 t27 = t26;
                        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&t27) + 0));
                        gm_l5 = t30;
                        gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t27) + 8));
                        gm_l6 = t31;
                        {
                            uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                            uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t36 = ((t34 == t35));
                            if (!t36) goto ie33;
                            {
                                {
                                    void * t39 = gm_l0;
                                    void * t40 = (void *)((((uint8_t *)t39) + 872) + 56);
                                    uint64_t t41 = gm_l5;
                                    gm_t88rcucg1b7uk t42 = GM_CONV(gm_t88rcucg1b7uk, gm_f_remove_6blu1gh(t40, t41));
                                }
                            }
                            {
                                void * t43 = gm_l0;
                                gm_t88rcucg1b7uk t44 = gm_l1;
                                gm_b0_1 t45 = gm_f_define_view_sob497j((void *)NULL, cx, NULL, t43, t44);
                                if (gm_unw.kind) {
                                    goto gm_ll19;
                                }
                            }
                            {
                                goto lb15;
                            }
                            goto ix33;
ie33:;
ix33:;
                        }
                        goto me28;
mn29:;
                        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 89, 9);
me28:;
                    }
lc15:;
                    {
                        uint64_t t47 = gm_l4;
                        uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                        uint64_t t50 = r48;
                        gm_l4 = t50;
                    }
                }
gm_ll19:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb15; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc15; }
                }
                goto gm_lad;
lb15:;
            }
        }
        {
            memmove(gm_out, ((uint8_t *)&F.l2), 40);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t52 = gm_l0;
        gm_t88rcucg1b7uk t53 = gm_l1;
        uint64_t t54 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t52, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t55 = ((t54 == ((uint64_t)0ULL)));
        if (!t55) goto ie51;
        {
            void * t57 = gm_l0;
            void * t58 = (void *)((((uint8_t *)t57) + 872) + 0);
            uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
            gm_ttu09lij36f8c t60;
            memset(&t60, 0, sizeof t60);
            gm_tftb1odc739t5 t61 = gm_f_put_25ngckq(t58, t59, &t60);
            gm_tftb1odc739t5 t62 = t61;
            if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srcs9q7ghsrcgv5, 99, 9);
            gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
        }
        {
            gm_ttu09lij36f8c t64;
            memset(&t64, 0, sizeof t64);
            uint8_t t65 = 0;
            F.ret = t64;
            goto gm_fret;
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        void * t66 = gm_l0;
        gm_t88rcucg1b7uk t67 = gm_l1;
        gm_t5v8n6qrn4rh4 t68;
        gm_f_repr_53quqki((void *)&t68, (void *)NULL, cx, NULL, t66, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5v8n6qrn4rh4 t69 = t68;
        gm_ttu09lij36f8c t70;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&t69) + 0));
        switch (t77) { case ((uint8_t)0ULL): goto ma75; case ((uint8_t)1ULL): goto ma75; case ((uint8_t)2ULL): goto ma72; case ((uint8_t)3ULL): goto ma73; case ((uint8_t)4ULL): goto ma75; case ((uint8_t)5ULL): goto ma75; case ((uint8_t)6ULL): goto ma74; default: goto ma75; }
ma72:;
        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&t69) + 0));
        if (t79 != ((uint8_t)2ULL)) goto mn78;
        gm_b16_8 t80;
        GM_ST(void *, (uint8_t *)&t80, (void *)(gm_snokcvhtidt97));
        GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(5));
        gm_ttu09lij36f8c t81;
        gm_f_scalar_f0e206b((void *)&t81, t80);
        t70 = t81;
        goto me71;
mn78:;
ma73:;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t69) + 0));
        if (t83 != ((uint8_t)3ULL)) goto mn82;
        void * t84 = gm_l0;
        gm_t88rcucg1b7uk t85 = gm_l1;
        gm_b16_8 t86 = gm_f_pointer_view_efkbf4q((void *)NULL, cx, NULL, t84, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t87;
        gm_f_scalar_f0e206b((void *)&t87, t86);
        t70 = t87;
        goto me71;
mn82:;
ma74:;
        uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&t69) + 0));
        if (t89 != ((uint8_t)6ULL)) goto mn88;
        gm_ttu09lij36f8c t91;
        void * t93 = gm_l0;
        gm_t88rcucg1b7uk t94 = gm_l1;
        uint8_t t95 = gm_f_is_aggregate_buh1uic((void *)NULL, cx, NULL, t93, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t95) goto ie92;
        {
            void * t97 = gm_l0;
            gm_t88rcucg1b7uk t98 = gm_l1;
            gm_f_reserve_40g0de6((void *)&F.l7, (void *)NULL, cx, NULL, t97, t98);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t99 = gm_l0;
            gm_t88rcucg1b7uk t100 = gm_l1;
            gm_b0_1 t101 = gm_f_define_view_sob497j((void *)NULL, cx, NULL, t99, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_ttu09lij36f8c t102 = F.l7;
        gm_ttu09lij36f8c t103 = t102;
        t91 = t103;
        goto ix92;
ie92:;
        void * t105 = gm_l0;
        gm_t88rcucg1b7uk t106 = gm_l1;
        gm_t53at80pfdus3 t107;
        gm_f_kind_ghvav22((void *)&t107, t105, GM_CONV(gm_csguankko4lmhd, t106));
        gm_t53at80pfdus3 t108 = t107;
        gm_ttu09lij36f8c t109;
        uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&t108) + 0));
        switch (t115) { case ((uint8_t)0ULL): goto ma113; case ((uint8_t)1ULL): goto ma113; case ((uint8_t)2ULL): goto ma113; case ((uint8_t)3ULL): goto ma113; case ((uint8_t)4ULL): goto ma113; case ((uint8_t)5ULL): goto ma113; case ((uint8_t)6ULL): goto ma113; case ((uint8_t)7ULL): goto ma113; case ((uint8_t)8ULL): goto ma113; case ((uint8_t)9ULL): goto ma113; case ((uint8_t)10ULL): goto ma113; case ((uint8_t)11ULL): goto ma111; case ((uint8_t)12ULL): goto ma113; case ((uint8_t)13ULL): goto ma113; case ((uint8_t)14ULL): goto ma113; case ((uint8_t)15ULL): goto ma113; case ((uint8_t)16ULL): goto ma113; case ((uint8_t)17ULL): goto ma112; case ((uint8_t)18ULL): goto ma113; case ((uint8_t)19ULL): goto ma113; case ((uint8_t)20ULL): goto ma113; case ((uint8_t)21ULL): goto ma113; case ((uint8_t)22ULL): goto ma113; case ((uint8_t)23ULL): goto ma113; case ((uint8_t)24ULL): goto ma113; case ((uint8_t)25ULL): goto ma113; case ((uint8_t)26ULL): goto ma113; default: goto ma113; }
ma111:;
        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&t108) + 0));
        if (t117 != ((uint8_t)11ULL)) goto mn116;
        uint64_t t118 = GM_LD(uint64_t, ((((uint8_t *)&t108) + 8) + 0));
        gm_l8 = t118;
        gm_t88rcucg1b7uk t119 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t108) + 8) + 8));
        gm_l9 = t119;
        gm_ttu09lij36f8c t121;
        void * t123 = gm_l0;
        gm_t88rcucg1b7uk t124 = gm_l9;
        gm_ttu09lij36f8c t125;
        gm_f_view_qkqm3in((void *)&t125, (void *)NULL, cx, NULL, t123, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t126 = t125;
        if (!((((uint8_t *)&t126))[32] != 0)) goto ie122;
        gm_t6j6smj7qoctu t127 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t126));
        gm_l10 = t127;
        gm_t6j6smj7qoctu t129;
        gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t129 + 0, t130);
        gm_tf1bjg81u0glv t131;
        uint64_t t132 = gm_l8;
        GM_ST(uint64_t, (uint8_t *)&t131 + 0, t132);
        gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t131 + 8, t133);
        gm_tabb710d9mq2e t134;
        gm_f_format_6a91mh8((void *)&t134, (void *)NULL, cx, NULL, &t131);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t135 = t134;
        if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 8, gm_srcs9q7ghsrcgv5, 114, 52);
        gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&t135) + 8));
        gm_b16_8 t137 = t136;
        gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
        GM_ST(gm_b16_8, (uint8_t *)&t129 + 16, t138);
        gm_t6j6smj7qoctu t139 = t129;
        gm_ttu09lij36f8c t140;
        GM_ST(uint8_t, (uint8_t *)&t140 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t140 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t140 + 36, 0);
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t140), t139);
        ((uint8_t *)&t140)[32] = 1;
        t121 = t140;
        goto ix122;
ie122:;
        void * t142 = gm_l0;
        gm_t88rcucg1b7uk t143 = gm_l1;
        gm_b16_8 t144 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t142, t143);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t145;
        gm_f_scalar_f0e206b((void *)&t145, t144);
        gm_ttu09lij36f8c t146 = t145;
        t121 = t146;
ix122:;
        gm_ttu09lij36f8c t147 = t121;
        t109 = t147;
        goto me110;
mn116:;
ma112:;
        uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&t108) + 0));
        if (t149 != ((uint8_t)17ULL)) goto mn148;
        void * t150 = gm_l0;
        gm_t88rcucg1b7uk t151 = gm_l1;
        gm_t4ml5v7ugcjrt t152;
        gm_f_enum_info_61n2nkp((void *)&t152, (void *)NULL, cx, NULL, t150, t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4ml5v7ugcjrt t153 = t152;
        uint8_t t154;
        if (((((uint8_t *)&t153))[32] != 0)) goto mn156;
        t154 = ((uint8_t)0);
        goto me155;
mn156:;
        t154 = ((uint8_t)1);
        goto me155;
mn157:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 119, 35);
me155:;
        if (!t154) goto mn148;
        {
            void * t159 = gm_l0;
            gm_t88rcucg1b7uk t160 = gm_l1;
            gm_f_reserve_40g0de6((void *)&F.l11, (void *)NULL, cx, NULL, t159, t160);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t161 = gm_l0;
            gm_t88rcucg1b7uk t162 = gm_l1;
            gm_b0_1 t163 = gm_f_define_view_sob497j((void *)NULL, cx, NULL, t161, t162);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_ttu09lij36f8c t164 = F.l11;
        gm_ttu09lij36f8c t165 = t164;
        t109 = t165;
        goto me110;
mn148:;
ma113:;
        void * t167 = gm_l0;
        gm_t88rcucg1b7uk t168 = gm_l1;
        gm_b16_8 t169 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t167, t168);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t170;
        gm_f_scalar_f0e206b((void *)&t170, t169);
        t109 = t170;
        goto me110;
mn166:;
ma114:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 111, 17);
me110:;
        gm_ttu09lij36f8c t171 = t109;
        t91 = t171;
ix92:;
        gm_ttu09lij36f8c t172 = t91;
        t70 = t172;
        goto me71;
mn88:;
ma75:;
        void * t174 = gm_l0;
        gm_t88rcucg1b7uk t175 = gm_l1;
        gm_b16_8 t176 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t174, t175);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t177;
        gm_f_scalar_f0e206b((void *)&t177, t176);
        t70 = t177;
        goto me71;
mn173:;
ma76:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 102, 16);
me71:;
        F.l12 = t70;
    }
    {
        void * t178 = gm_l0;
        void * t179 = (void *)((((uint8_t *)t178) + 872) + 0);
        uint32_t t180 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_tftb1odc739t5 t181 = gm_f_put_25ngckq(t179, t180, &(*(gm_ttu09lij36f8c *)((uint8_t *)&F.l12)));
        gm_tftb1odc739t5 t182 = t181;
        if (((((uint8_t *)&t182))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t182) + 1, gm_srcs9q7ghsrcgv5, 131, 5);
        gm_b0_1 t183 = GM_LD(gm_b0_1, (((uint8_t *)&t182) + 1));
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l12), 40);
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
inline __attribute__((aligned(4))) void gm_f_views_bc1b3r4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_views_bc1b3r4 F;
    GM_STACK_CHECK(&F, 248u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_tbpdf5ej15iof *)gm_out) + 97, 0, 7);
    gm_t4c7ipjj9s3fp t1;
    gm_f_hash_map_9mnq5mu((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t2;
    gm_f_list_nof7vfj((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t3;
    gm_f_list_uvfu1k1((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t4;
    gm_f_hash_map_5kr0r9p((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    gm_f_hash_map_cuh4fs9((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_tbpdf5ej15iof *)gm_out) + 0, t1);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbpdf5ej15iof *)gm_out) + 56, t2);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tbpdf5ej15iof *)gm_out) + 96, ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbpdf5ej15iof *)gm_out) + 104, t3);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_tbpdf5ej15iof *)gm_out) + 144, t4);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_tbpdf5ej15iof *)gm_out) + 200, t5);
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
