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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t a[800]; } gm_t5jveu1vhsgsj;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { int64_t f0; int32_t f1; uint8_t pe[4]; } gm_tnur0m5k1ttis;
typedef struct { uint16_t f0; } gm_tu0pangbbaeec;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; } gm_t586m754bse2v;
_Static_assert(sizeof(gm_t5jveu1vhsgsj) == 800 && _Alignof(gm_t5jveu1vhsgsj) == 1 && offsetof(gm_t5jveu1vhsgsj, a) == 0, "layout of `[800]u8`");
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
typedef struct { gm_t5jveu1vhsgsj f0; uint64_t f1; int64_t f2; uint8_t f3; uint8_t pe[7]; } gm_ti08v370dohoc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint32_t f3; uint8_t pe[4]; } gm_tj4tbafenpsjm;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tnur0m5k1ttis) == 16 && _Alignof(gm_tnur0m5k1ttis) == 8 && offsetof(gm_tnur0m5k1ttis, f0) == 0 && offsetof(gm_tnur0m5k1ttis, f1) == 8, "layout of `(i64, i32)`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
_Static_assert(sizeof(gm_tu0pangbbaeec) == 2 && _Alignof(gm_tu0pangbbaeec) == 2 && offsetof(gm_tu0pangbbaeec, f0) == 0, "layout of `(u16,)`");
typedef struct { gm_b16_8 f0; uint16_t f1; uint8_t pe[6]; } gm_tvaas22dl64e0;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t586m754bse2v) == 120 && _Alignof(gm_t586m754bse2v) == 8 && offsetof(gm_t586m754bse2v, f0) == 0 && offsetof(gm_t586m754bse2v, f1) == 16 && offsetof(gm_t586m754bse2v, f2) == 32 && offsetof(gm_t586m754bse2v, f3) == 40 && offsetof(gm_t586m754bse2v, f4) == 56 && offsetof(gm_t586m754bse2v, f5) == 72 && offsetof(gm_t586m754bse2v, f6) == 88 && offsetof(gm_t586m754bse2v, f7) == 104, "layout of `([]u8, string, u32, []u8, []u8, []u8, []u8, []u8)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
_Static_assert(sizeof(gm_ti08v370dohoc) == 824 && _Alignof(gm_ti08v370dohoc) == 8 && offsetof(gm_ti08v370dohoc, f0) == 0 && offsetof(gm_ti08v370dohoc, f1) == 800 && offsetof(gm_ti08v370dohoc, f2) == 808 && offsetof(gm_ti08v370dohoc, f3) == 816, "layout of `Decimal`");
_Static_assert(sizeof(gm_tj4tbafenpsjm) == 56 && _Alignof(gm_tj4tbafenpsjm) == 8 && offsetof(gm_tj4tbafenpsjm, f0) == 0 && offsetof(gm_tj4tbafenpsjm, f1) == 16 && offsetof(gm_tj4tbafenpsjm, f2) == 32 && offsetof(gm_tj4tbafenpsjm, f3) == 48, "layout of `(string, string, string, u32)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; double v1; } f1; } gm_tspt3vn9ljcnd;
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_tvaas22dl64e0) == 24 && _Alignof(gm_tvaas22dl64e0) == 8 && offsetof(gm_tvaas22dl64e0, f0) == 0 && offsetof(gm_tvaas22dl64e0, f1) == 16, "layout of `(string, u16)`");
_Static_assert(sizeof(gm_tspt3vn9ljcnd) == 24 && _Alignof(gm_tspt3vn9ljcnd) == 8 && offsetof(gm_tspt3vn9ljcnd, f0) == 0 && offsetof(gm_tspt3vn9ljcnd, f1) == 8, "layout of `ParseError!f64`");
struct gm_fr_fn_0tkufqc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1bhi0el {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1ehf2in {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1iu0ril {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1t30amv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_230s5vf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_321vn6m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_366v7gn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4kt899m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4u04pfu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_5h85urn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6drm1q7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6oqcm5l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7020hka {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7jdg3j2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7u3s7d6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7vmjkcr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_81kj5f4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_84gj1mh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9aei4hh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9jroohf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_a7mjfct {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ape1dlm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_arkvnve {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_avfcmre {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_b5ncb44 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_b8umguv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_bcglb0d {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_dnoe8bq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_e9k6hvk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ead3e9o {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_efj1sku {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_egnv8ia {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_eqsgsm0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_g4ft86i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_gq0tg0n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_h794c0o {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_hokmfkj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_i03nooe {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_irskvq8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_jqo0p0s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_k0l9ohg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ka9aifq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_l54nl53 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_lodgai5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_lr3dcn9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_m16u54c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mgigmu6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mnug0dh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mqrb14b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_n6145sm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nr5lk0d {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_oejjcmg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_okgvaus {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_opm2rie {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_opu1gv4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_p1eft4f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_p942mlu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pdgdoo1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_prhsfor {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pu948qi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pv07cle {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_q78colo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_qevtudj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_r97t88j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_s53265p {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_sr84td4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_t14ftq0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tt8mp41 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_uap0af7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_udrroha {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_usqmpmt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_uv03noh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vd4c7ce {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vh5k572 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vn526ra {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vqs5lmc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vsaec5d {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_10ctmcq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_12q2s8v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_12rbmhd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_12uv53h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_13mdm96 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_13oum9l {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_14t1s38 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_15kpcel {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_15tuglm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_16dceus {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_16dq22e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_16f92bp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_173gk69 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_17eulka {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_18lv8vg {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_18tu4v6 {
    gm_frame h;
    gm_t586m754bse2v l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t586m754bse2v l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_19e6va7 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_19ftgop {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1amnh72 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1cgbgnn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1ck6tsm {
    gm_frame h;
    gm_tj4tbafenpsjm l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tj4tbafenpsjm l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1coo7na {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1d88j1c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1dfo4pq {
    gm_frame h;
    gm_tftksr4op114e l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1e409ou {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1e53aq3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1il5da4 {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tq3m5qgvkpje8 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1kivsu4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1kki7cc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1mojd22 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1ncs69t {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1o1mt78 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1oelod5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1p8frtu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1u4jnnu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1va9dpi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1vkmnih {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h02tha7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h0dnf9n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h13dbf2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h1n6lc9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h4jcodi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h6b2ofo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h6uc0qg {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h75o0bg {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h78t8d0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h8e7ps1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h95c7u1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h9otq43 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hb3htfv {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hb6cr3p {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hc3p1hh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hcca65h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_he684oe {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_he769rg {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hedon8i {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hfu2i92 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hgc3vel {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hgrco1l {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hi89jlk {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hj7ahc4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hjgrmth {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hm2bsgr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hn5gv7p {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hp1rthv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hpnf26s {
    gm_frame h;
    gm_thddbqb6snabm l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_thddbqb6snabm l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hqail32 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hqkpc5s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hr0sl7f {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hs0gaus {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hs8tdgq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ht23jk8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ht2r43m {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_htqcdvo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_htqqv8t {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hum25d9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hvaqp8b {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hvnghft {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_parse_float_17hgksa {
    gm_frame h;
    gm_ti08v370dohoc l9; /* dec */
    gm_tspt3vn9ljcnd ret;
};
__attribute__((aligned(4))) uint64_t gm_f_decimal_bits_c63p8nv(void * a0, uint32_t a1, uint32_t a2, int64_t a3);
__attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_eq_ignore_case_qqsqlkn(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_0tkufqc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_1bhi0el(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_1ehf2in(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_1iu0ril(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_1t30amv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_230s5vf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_321vn6m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_366v7gn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_4kt899m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_4u04pfu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_5h85urn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_6drm1q7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_6oqcm5l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_7020hka(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_7jdg3j2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u3s7d6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_7vmjkcr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_81kj5f4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_84gj1mh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_9aei4hh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_9jroohf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_a7mjfct(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_ape1dlm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_arkvnve(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_avfcmre(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_b5ncb44(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_b8umguv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_bcglb0d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_dnoe8bq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_e9k6hvk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_ead3e9o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_efj1sku(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_egnv8ia(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_eqsgsm0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_g4ft86i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_gq0tg0n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_h794c0o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_hokmfkj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_i03nooe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_irskvq8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_jqo0p0s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_k0l9ohg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_ka9aifq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_l54nl53(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_lodgai5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_lr3dcn9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_m16u54c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_mgigmu6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_mnug0dh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_mqrb14b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_n6145sm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_nr5lk0d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_oejjcmg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_okgvaus(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_opm2rie(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_opu1gv4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_p1eft4f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_p942mlu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_pdgdoo1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_prhsfor(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_pu948qi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_pv07cle(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_q78colo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_qevtudj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_r97t88j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_s53265p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_sr84td4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_t14ftq0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_tt8mp41(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_uap0af7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_udrroha(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_usqmpmt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_uv03noh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_vd4c7ce(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_vh5k572(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_vn526ra(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_vqs5lmc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_vsaec5d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_10ctmcq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_12q2s8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_12rbmhd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_12uv53h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_13mdm96(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_13oum9l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnur0m5k1ttis a0);
__attribute__((aligned(4))) void gm_f_format_14t1s38(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_15kpcel(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_15tuglm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_16dceus(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_16dq22e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
__attribute__((aligned(4))) void gm_f_format_16f92bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_173gk69(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_17eulka(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_18lv8vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_18tu4v6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_19e6va7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_19ftgop(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_1cgbgnn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_1ck6tsm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1coo7na(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_1d88j1c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1dfo4pq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1e409ou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1e53aq3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1il5da4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1kivsu4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_1kki7cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_1mojd22(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_1ncs69t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_1o1mt78(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1oelod5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1p8frtu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_1u4jnnu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_1va9dpi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_1vkmnih(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h02tha7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_h0dnf9n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h13dbf2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h1n6lc9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h6b2ofo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h6uc0qg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h75o0bg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h78t8d0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h95c7u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h9otq43(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tu0pangbbaeec a0);
__attribute__((aligned(4))) void gm_f_format_hb3htfv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hb6cr3p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hc3p1hh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hcca65h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_he684oe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_he769rg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_hedon8i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hfu2i92(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_hgc3vel(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hgrco1l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hi89jlk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hj7ahc4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hjgrmth(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hm2bsgr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hn5gv7p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hp1rthv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hpnf26s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hqail32(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hqkpc5s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hr0sl7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hs0gaus(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hs8tdgq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ht23jk8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ht2r43m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_htqcdvo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_htqqv8t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hum25d9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hvaqp8b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_hvnghft(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) double gm_f_from_bits_mv0phhs(uint64_t a0);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_parse_float_17hgksa(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tspt3vn9ljcnd gm_f_parse_float_17hgksa_c(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_d1qo8mf(void * a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_to_lower_0fu6efk(uint8_t a0);
__attribute__((aligned(4))) void gm_f_trim_decimal_56e5p3i(void * a0);
GM_NOINLINE bool gm_pf0aqhubr4up(void *code, void *env, gm_node *cx, const void *d, uintptr_t n);
GM_NOINLINE bool gm_pfk8csn7us23(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s);
extern const uint8_t gm_s02aodrl2fpra[12];
extern const uint8_t gm_s05qp4ie7rju8[84];
extern const uint8_t gm_s067qe7d1tphl[31];
extern const uint8_t gm_s09jc0n36v9gm[97];
extern const uint8_t gm_s0b795193rvs2[7];
extern const uint8_t gm_s0kf3vtbitlnb[58];
extern const uint8_t gm_s0nnk79up541r[32];
extern const uint8_t gm_s0pf2spartj4v[12];
extern const uint8_t gm_s0r71f5repum2[21];
extern const uint8_t gm_s0ug1ukauk3l9[74];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1mc2427h3p1h[9];
extern const uint8_t gm_s1p0thm43h6d6[21];
extern const uint8_t gm_s1utcdes4r8ku[7];
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s221m8qkvkf71[23];
extern const uint8_t gm_s268n7l3tugli[6];
extern const uint8_t gm_s26crtotu14pn[3];
extern const uint8_t gm_s2c267emk2nni[6];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2kg2oqqdqf2r[23];
extern const uint8_t gm_s2mb2caup8rnh[49];
extern const uint8_t gm_s30rt0lbjn52b[19];
extern const uint8_t gm_s329kcr9363eu[11];
extern const uint8_t gm_s3e1pn3bonjpd[9];
extern const uint8_t gm_s3jl3afv8nlvv[4];
extern const uint8_t gm_s3kb1co7e63ka[37];
extern const uint8_t gm_s3mmpurhmitlr[46];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3phpt25k93cc[39];
extern const uint8_t gm_s3thrieq7sddl[23];
extern const uint8_t gm_s46ki8g7j9f6v[6];
extern const uint8_t gm_s48hq6ubqo1rd[10];
extern const uint8_t gm_s4bv7heri08lj[24];
extern const uint8_t gm_s4ica9th0h3pb[54];
extern const uint8_t gm_s4kq1149c9fl2[55];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s4s2kvgh5crk6[63];
extern const uint8_t gm_s50pmptf3idds[2];
extern const uint8_t gm_s554r118mfcao[34];
extern const uint8_t gm_s592b618tctb1[15];
extern const uint8_t gm_s5k56vqvt8t7d[12];
extern const uint8_t gm_s5nt88lj0viqk[4];
extern const uint8_t gm_s619rbci7p06v[20];
extern const uint8_t gm_s65dghs61r7cp[13];
extern const uint8_t gm_s65fsr1v7i389[135];
extern const uint8_t gm_s66fbu3uhl900[20];
extern const uint8_t gm_s6pgabt9io52p[132];
extern const uint8_t gm_s72tasmt4pg49[26];
extern const uint8_t gm_s73vt0nuu7890[28];
extern const uint8_t gm_s7c9tkfjfc169[26];
extern const uint8_t gm_s7mfvroqmll1f[3];
extern const uint8_t gm_s7nnecs6i9eki[41];
extern const uint8_t gm_s823123ng02vo[44];
extern const uint8_t gm_s872048h9ttis[26];
extern const uint8_t gm_s8fhmkhb94g4l[8];
extern const uint8_t gm_s8s03uo3h8dsp[9];
extern const uint8_t gm_s8uplmiglsqkm[15];
extern const uint8_t gm_s91vt5u8rghg3[3];
extern const uint8_t gm_s92n4v7htp6pi[11];
extern const uint8_t gm_s965n8o245s16[10];
extern const uint8_t gm_s99ra887gkc8u[39];
extern const uint8_t gm_s9j0jokaa6cqi[113];
extern const uint8_t gm_s9ob1jv3b8md9[10];
extern const uint8_t gm_s9oo6ammv0sch[17];
extern const uint8_t gm_s9r71kmnlqpv8[17];
extern const uint8_t gm_sa29cuhpvu442[54];
extern const uint8_t gm_sa8k1i3b5phk8[24];
extern const uint8_t gm_saco8uvrm6fd3[4];
extern const uint8_t gm_sait90vqr6gam[33];
extern const uint8_t gm_sas0hkqll4io0[10];
extern const uint8_t gm_sauf6ibu7mnba[5];
extern const uint8_t gm_sb8us6li0fvm5[42];
extern const uint8_t gm_sbdqlrmtee4eo[66];
extern const uint8_t gm_sbe61ep7tock5[5];
extern const uint8_t gm_sc5aaou69jljp[10];
extern const uint8_t gm_sc6st5mdtp49u[16];
extern const uint8_t gm_scjbu6gm2hrhf[8];
extern const uint8_t gm_sckqm6be9dvme[13];
extern const uint8_t gm_sclhssivsh27a[37];
extern const uint8_t gm_scmp8kk3f4q6t[46];
extern const uint8_t gm_scs3asa1395s7[6];
extern const uint8_t gm_sdark9dtsgt1q[41];
extern const uint8_t gm_sddo97gu3reaa[72];
extern const uint8_t gm_sdnh9sm5pppq1[40];
extern const uint8_t gm_sdpkm2aceiv8l[47];
extern const uint8_t gm_sdtnu56jsuhl2[8];
extern const uint8_t gm_se1krm9tn2oaa[3];
extern const uint8_t gm_se2e37nl2hi6g[4];
extern const uint8_t gm_se8cpd8s09lkc[13];
extern const uint8_t gm_sej6t944fna3a[61];
extern const uint8_t gm_sfa86erpod86s[4];
extern const uint8_t gm_sfbr39r30psvt[4];
extern const uint8_t gm_sfc624hllrs4g[6];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfi2ha42r1nr2[2];
extern const uint8_t gm_sfm8ps2e26dh1[3];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgc02r26g8j6o[57];
extern const uint8_t gm_sgsvgcmirhojs[28];
extern const uint8_t gm_sgtpi35uur9ic[7];
extern const uint8_t gm_sh40na9qbga3l[2];
extern const uint8_t gm_sh4b8t54h2jna[49];
extern const uint8_t gm_shvak64vhokft[26];
extern const uint8_t gm_si1kalsj77bue[41];
extern const uint8_t gm_sigac1ehmei1f[69];
extern const uint8_t gm_sihb3eljrjar6[16];
extern const uint8_t gm_sihclrdudbhkh[9];
extern const uint8_t gm_siqfg68f4ud6j[16];
extern const uint8_t gm_sive7ce5krqer[18];
extern const uint8_t gm_sj1gcl0qo4i9p[7];
extern const uint8_t gm_sjbtn8nt9amae[3];
extern const uint8_t gm_sjfrmur8rvp24[34];
extern const uint8_t gm_sjg4ie5b5ufsm[8];
extern const uint8_t gm_sjkj5g78lejrj[4];
extern const uint8_t gm_sjnkguo0atpm5[3];
extern const uint8_t gm_sjpr4fvui63qj[13];
extern const uint8_t gm_sk04qg20jmac0[4];
extern const uint8_t gm_sk9jo4s16ifdk[9];
extern const uint8_t gm_skme3gpq5egsp[40];
extern const uint8_t gm_skmfilbqoc8lo[87];
extern const uint8_t gm_skr5vsm3evt1r[36];
extern const uint8_t gm_sl0n65k3lfps6[18];
extern const uint8_t gm_sl3acq1nje8kk[39];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sla3unrbmgc0v[6];
extern const uint8_t gm_slf85brqp8fiu[125];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slq0l63qf1dvs[11];
extern const uint8_t gm_slq58vfha8k9h[2];
extern const uint8_t gm_slq8b0dsnkd8e[14];
extern const uint8_t gm_slqgeomhftr16[15];
extern const uint8_t gm_sm34bm8ke9n7b[22];
extern const uint8_t gm_sm55k23067j17[16];
extern const uint8_t gm_smbbaf3n1gml2[8];
extern const uint8_t gm_smdj5floh97je[10];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_sml38fgda46cm[56];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smr39cplkqb3n[51];
extern const uint8_t gm_smrahl8ppkcu9[34];
extern const uint8_t gm_smt0v9p72f507[42];
extern const uint8_t gm_sn4utba4jl66l[9];
extern const uint8_t gm_sn57nkh1rblgd[62];
extern const uint8_t gm_sni9iu6vnvsjk[44];
extern const uint8_t gm_snjonc4tmu56e[21];
extern const uint8_t gm_sns75reqq32d2[4];
extern const uint8_t gm_snti96ut68ga4[120];
extern const uint8_t gm_so31m6feg820p[4];
extern const uint8_t gm_so43rnba8j5pb[35];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so9fi6r28770a[3];
extern const uint8_t gm_sog9gpimub31i[7];
extern const uint8_t gm_soiotnern2p8m[19];
extern const uint8_t gm_soo0b43e4t7sn[12];
extern const uint8_t gm_souosds7adra9[36];
extern const uint8_t gm_sp02qqgucmpt7[3];
extern const uint8_t gm_sp1srk1sv0crk[52];
extern const uint8_t gm_sp2du31gjaojf[8];
extern const uint8_t gm_spbhhmdbp335u[4];
extern const uint8_t gm_spg4d1df6jnji[7];
extern const uint8_t gm_spgfhj218uj9k[8];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_sq9qefps1uqei[10];
extern const uint8_t gm_sqa5gdfklkjii[39];
extern const uint8_t gm_sqa5rf17c57e4[5];
extern const uint8_t gm_sqbq94vn3heqs[49];
extern const uint8_t gm_sqjh8kseu4lti[6];
extern const uint8_t gm_sqr1rjjq3dpuh[17];
extern const uint8_t gm_sr8dujibvqj2g[82];
extern const char gm_src3d0v42kavhv7[];
extern const uint8_t gm_srlt17ne6o897[46];
extern const uint8_t gm_srof610t1er1p[26];
extern const uint8_t gm_sroh2go4g3kcu[107];
extern const uint8_t gm_srp6tpaiaeunu[8];
extern const uint8_t gm_srv4dcfccuset[3];
extern const uint8_t gm_srv5rbhjb8ns5[15];
extern const uint8_t gm_ss8448efddk3m[32];
extern const uint8_t gm_ss8q42tvppqf5[79];
extern const uint8_t gm_ssdi9g220if8h[2];
extern const uint8_t gm_ssf1r8m922l8a[16];
extern const uint8_t gm_sso4tmqr8gin2[20];
extern const uint8_t gm_st7fm661apctp[17];
extern const uint8_t gm_stadskj9k22h7[30];
extern const uint8_t gm_stk9qd6rl9jdj[21];
extern const uint8_t gm_stlm46rulfhsg[39];
extern const uint8_t gm_stmsadvf54k73[3];
extern const uint8_t gm_stqlepc346jgk[85];
extern const uint8_t gm_sttkmg61kqc3k[65];
extern const uint8_t gm_stuj0mv5nl55t[4];
extern const uint8_t gm_su5mku12ctc3k[92];
extern const uint8_t gm_su8bu9ae1ds2f[19];
extern const uint8_t gm_suftpl3n8736g[5];
extern const uint8_t gm_suiq4ou0s86ho[14];
extern const uint8_t gm_sujpen8u5kmon[10];
extern const uint8_t gm_sunidvof1bsgr[33];
extern const uint8_t gm_suo9n1e1rnjb6[20];
extern const uint8_t gm_svhavo13qha5k[4];
extern const uint8_t gm_svjsvemd189fa[40];
extern const uint8_t gm_svlbiokurll04[12];
extern const uint8_t gm_svm0rfaqk3b2q[4];
extern const uint8_t gm_svr1ohtrtj17p[64];
extern const uint8_t gm_svutc5hf6cgiv[2];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_ty6groq37ram;
extern const gm_type gm_ty9oqmmiu8qc;
extern const gm_type gm_tyb2nno7n67r;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyqa6gfa8omk;
extern const gm_type gm_tysqt22rph28;
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0tkufqc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_0tkufqc F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_h8e7ps1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_h8e7ps1 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_h8e7ps1 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1bhi0el(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1bhi0el F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_he769rg *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_he769rg *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_he769rg *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1ehf2in(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1ehf2in F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1d88j1c *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1d88j1c *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1d88j1c *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1iu0ril(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1iu0ril F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hgc3vel *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hgc3vel *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hgc3vel *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1t30amv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1t30amv F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hr0sl7f *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hr0sl7f *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hr0sl7f *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_230s5vf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_230s5vf F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1e53aq3 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1e53aq3 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1e53aq3 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_321vn6m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_321vn6m F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ht2r43m *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ht2r43m *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ht2r43m *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_366v7gn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_366v7gn F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_h6uc0qg *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_h6uc0qg *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_h6uc0qg *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4kt899m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4kt899m F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_18tu4v6 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_18tu4v6 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_18tu4v6 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4u04pfu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4u04pfu F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1e409ou *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1e409ou *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1e409ou *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5h85urn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_5h85urn F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hi89jlk *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hi89jlk *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hi89jlk *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6drm1q7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_6drm1q7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hm2bsgr *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hm2bsgr *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hm2bsgr *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6oqcm5l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_6oqcm5l F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_h13dbf2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_h13dbf2 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_h13dbf2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7020hka(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_7020hka F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hvnghft *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hvnghft *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hvnghft *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7jdg3j2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_7jdg3j2 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1dfo4pq *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1dfo4pq *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1dfo4pq *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u3s7d6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_7u3s7d6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_htqqv8t *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_htqqv8t *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_htqqv8t *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7vmjkcr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_7vmjkcr F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_h02tha7 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_h02tha7 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_h02tha7 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_81kj5f4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_81kj5f4 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1p8frtu *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1p8frtu *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1p8frtu *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_84gj1mh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_84gj1mh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hqkpc5s *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hqkpc5s *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hqkpc5s *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9aei4hh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9aei4hh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hs0gaus *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hs0gaus *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hs0gaus *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9jroohf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9jroohf F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hedon8i *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hedon8i *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hedon8i *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a7mjfct(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_a7mjfct F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_16f92bp *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_16f92bp *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_16f92bp *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ape1dlm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ape1dlm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_19ftgop *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_19ftgop *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_19ftgop *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_arkvnve(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_arkvnve F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_10ctmcq *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_10ctmcq *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_10ctmcq *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_avfcmre(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_avfcmre F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_h4jcodi *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_h4jcodi *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_h4jcodi *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b5ncb44(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b5ncb44 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_12uv53h *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_12uv53h *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_12uv53h *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b8umguv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b8umguv F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hum25d9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hum25d9 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hum25d9 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bcglb0d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_bcglb0d F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1coo7na *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1coo7na *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1coo7na *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dnoe8bq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_dnoe8bq F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_18lv8vg *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_18lv8vg *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_18lv8vg *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e9k6hvk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_e9k6hvk F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_15kpcel *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_15kpcel *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_15kpcel *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ead3e9o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ead3e9o F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hqail32 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hqail32 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hqail32 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_efj1sku(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_efj1sku F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hn5gv7p *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hn5gv7p *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hn5gv7p *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_egnv8ia(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_egnv8ia F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hfu2i92 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hfu2i92 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hfu2i92 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eqsgsm0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_eqsgsm0 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hs8tdgq *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hs8tdgq *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hs8tdgq *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g4ft86i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_g4ft86i F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_15tuglm *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_15tuglm *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_15tuglm *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gq0tg0n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_gq0tg0n F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_h1n6lc9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_h1n6lc9 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_h1n6lc9 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h794c0o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_h794c0o F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hjgrmth *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hjgrmth *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hjgrmth *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hokmfkj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_hokmfkj F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_13oum9l *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_13oum9l *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_13oum9l *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i03nooe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_i03nooe F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1kivsu4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1kivsu4 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1kivsu4 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_irskvq8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_irskvq8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_h95c7u1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_h95c7u1 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_h95c7u1 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jqo0p0s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_jqo0p0s F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1o1mt78 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1o1mt78 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1o1mt78 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k0l9ohg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_k0l9ohg F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1va9dpi *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1va9dpi *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1va9dpi *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ka9aifq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ka9aifq F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_14t1s38 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_14t1s38 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_14t1s38 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l54nl53(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_l54nl53 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hgrco1l *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hgrco1l *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hgrco1l *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lodgai5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_lodgai5 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_12q2s8v *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_12q2s8v *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_12q2s8v *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lr3dcn9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_lr3dcn9 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ht23jk8 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ht23jk8 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ht23jk8 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m16u54c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_m16u54c F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1ck6tsm *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1ck6tsm *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1ck6tsm *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mgigmu6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_mgigmu6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1kki7cc *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1kki7cc *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1kki7cc *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mnug0dh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_mnug0dh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hj7ahc4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hj7ahc4 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hj7ahc4 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mqrb14b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_mqrb14b F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_h6b2ofo *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_h6b2ofo *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_h6b2ofo *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n6145sm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_n6145sm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_13mdm96 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_13mdm96 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_13mdm96 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nr5lk0d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_nr5lk0d F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_h0dnf9n *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_h0dnf9n *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_h0dnf9n *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oejjcmg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_oejjcmg F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hp1rthv *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hp1rthv *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hp1rthv *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_okgvaus(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_okgvaus F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_16dceus *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_16dceus *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_16dceus *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_opm2rie(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_opm2rie F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_he684oe *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_he684oe *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_he684oe *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_opu1gv4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_opu1gv4 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hvaqp8b *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hvaqp8b *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hvaqp8b *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p1eft4f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_p1eft4f F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_173gk69 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_173gk69 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_173gk69 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p942mlu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_p942mlu F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1il5da4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1il5da4 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1il5da4 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pdgdoo1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_pdgdoo1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_19e6va7 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_19e6va7 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_19e6va7 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_prhsfor(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_prhsfor F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1ncs69t *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1ncs69t *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1ncs69t *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pu948qi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_pu948qi F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1vkmnih *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1vkmnih *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1vkmnih *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pv07cle(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_pv07cle F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hb3htfv *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hb3htfv *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hb3htfv *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q78colo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_q78colo F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1amnh72 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1amnh72 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1amnh72 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qevtudj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_qevtudj F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hb6cr3p *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hb6cr3p *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hb6cr3p *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r97t88j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_r97t88j F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_h75o0bg *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_h75o0bg *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_h75o0bg *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_s53265p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_s53265p F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_htqcdvo *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_htqcdvo *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_htqcdvo *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sr84td4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_sr84td4 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hc3p1hh *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hc3p1hh *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hc3p1hh *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t14ftq0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_t14ftq0 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_16dq22e *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_16dq22e *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_16dq22e *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tt8mp41(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_tt8mp41 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1oelod5 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1oelod5 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1oelod5 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uap0af7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_uap0af7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hcca65h *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hcca65h *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hcca65h *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_udrroha(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_udrroha F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1cgbgnn *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1cgbgnn *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1cgbgnn *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_usqmpmt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_usqmpmt F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1u4jnnu *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1u4jnnu *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1u4jnnu *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uv03noh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_uv03noh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_17eulka *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_17eulka *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_17eulka *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vd4c7ce(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vd4c7ce F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_12rbmhd *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_12rbmhd *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_12rbmhd *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vh5k572(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vh5k572 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_h78t8d0 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_h78t8d0 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_h78t8d0 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vn526ra(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vn526ra F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_1mojd22 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_1mojd22 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_1mojd22 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vqs5lmc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vqs5lmc F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_h9otq43 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_h9otq43 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_h9otq43 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vsaec5d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vsaec5d F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_hpnf26s *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_hpnf26s *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_hpnf26s *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) void gm_f_format_10ctmcq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_10ctmcq F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sej6t944fna3a));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(60));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_arkvnve, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sauf6ibu7mnba, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s7c9tkfjfc169, 25)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s73vt0nuu7890, 27)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_12q2s8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_12q2s8v F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s067qe7d1tphl));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_lodgai5, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_shvak64vhokft, 25)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_12rbmhd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_12rbmhd F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sml38fgda46cm));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_vd4c7ce, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_stmsadvf54k73, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s2mb2caup8rnh, 48)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_12uv53h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_12uv53h F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5nt88lj0viqk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_b5ncb44, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s50pmptf3idds, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_13mdm96(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_13mdm96 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5k56vqvt8t7d));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_n6145sm, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s965n8o245s16, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_13oum9l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnur0m5k1ttis a0) {
    struct gm_fr_format_13oum9l F;
    gm_tnur0m5k1ttis gm_l0; /* args */
    gm_tnur0m5k1ttis gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 297u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scs3asa1395s7));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnur0m5k1ttis t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_hokmfkj, (void *)&F.h, cx});
            int64_t t20 = GM_LD(int64_t, (((uint8_t *)&gm_l3) + 0));
            int64_t t21 = t20;
            int32_t t22 = GM_LD(int32_t, (((uint8_t *)&gm_l3) + 8));
            int32_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tysqt22rph28, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_svutc5hf6cgiv, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tyqa6gfa8omk, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_14t1s38(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_14t1s38 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa29cuhpvu442));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ka9aifq, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s02aodrl2fpra, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s7nnecs6i9eki, 40)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_15kpcel(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_15kpcel F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soiotnern2p8m));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_e9k6hvk, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sihb3eljrjar6, 15)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_15tuglm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_15tuglm F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srlt17ne6o897));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(45));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_g4ft86i, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_smt0v9p72f507, 41)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_16dceus(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_16dceus F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4s2kvgh5crk6));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_okgvaus, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s823123ng02vo, 43)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sl0n65k3lfps6, 17)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_16dq22e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0) {
    struct gm_fr_format_16dq22e F;
    gm_tcc1rq4n1avb2 gm_l0; /* args */
    gm_tcc1rq4n1avb2 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0pf2spartj4v));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tcc1rq4n1avb2 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_t14ftq0, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            uint32_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sgtpi35uur9ic, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty467d5cm1d5, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_16f92bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_16f92bp F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skme3gpq5egsp));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_a7mjfct, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_st7fm661apctp, 16)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sm34bm8ke9n7b, 21)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_173gk69(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_173gk69 F;
    gm_t66j1im9ip128 gm_l0; /* args */
    gm_t66j1im9ip128 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sroh2go4g3kcu));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(106));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t66j1im9ip128 t18 = GM_LD(gm_t66j1im9ip128, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_p1eft4f, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t23 = t22;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t25 = t24;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 24));
            uint64_t t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sa8k1i3b5phk8, 23)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_stk9qd6rl9jdj, 20)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_tyb2nno7n67r, ((uint8_t *)&t23), &(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1})) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_si1kalsj77bue, 40)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_tyb2nno7n67r, ((uint8_t *)&t25), &(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1})) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sp02qqgucmpt7, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_tyb2nno7n67r, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sdtnu56jsuhl2, 7)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_17eulka(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_17eulka F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq9qefps1uqei));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_uv03noh, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sl3q52ep6aa53, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_slgmqgkioa4u6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_18lv8vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_18lv8vg F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snti96ut68ga4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(119));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_dnoe8bq, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s9j0jokaa6cqi, 112)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sjnkguo0atpm5, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_18tu4v6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_18tu4v6 F;
    GM_STACK_CHECK(&F, 577u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqbq94vn3heqs));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(48));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)120ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t586m754bse2v t18 = GM_LD(gm_t586m754bse2v, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_4kt899m, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 32));
            uint32_t t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
            gm_b16_8 t27 = t26;
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 56));
            gm_b16_8 t29 = t28;
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 72));
            gm_b16_8 t31 = t30;
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 88));
            gm_b16_8 t33 = t32;
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 104));
            gm_b16_8 t35 = t34;
            gm_closure t36 = t19;
            gm_node *ccx37 = t36.cx;
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_so9fi6r28770a, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_tybihgc69jv3, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_s92n4v7htp6pi, 10)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_ty467d5cm1d5, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_se2e37nl2hi6g, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_tybihgc69jv3, ((uint8_t *)&t29), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_srp6tpaiaeunu, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_tybihgc69jv3, ((uint8_t *)&t31), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_sk9jo4s16ifdk, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_tybihgc69jv3, ((uint8_t *)&t33), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_ty0iqiubfm7e, ((uint8_t *)&t35), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t39 = F.l2;
        if (!t39) goto ie38;
        {
            void * t41 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t41);
        }
        {
            gm_t9qkh0rsooc8d t42;
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t43;
            GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t43 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t43 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t43 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t43 + 16, 0);
            ((uint8_t *)&t43)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t43 + 8), t42);
            gm_tabb710d9mq2e t44 = t43;
            uint8_t t45 = ((((uint8_t *)&t44))[0] != 0);
            F.ret = t43;
            goto gm_fret;
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        void * t46 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t46);
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
inline __attribute__((aligned(4))) void gm_f_format_19e6va7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_19e6va7 F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp2du31gjaojf));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_pdgdoo1, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tybihgc69jv3, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_19ftgop(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_19ftgop F;
    gm_t3877pk7lv748 gm_l0; /* args */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s48hq6ubqo1rd));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t3877pk7lv748 t18 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ape1dlm, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_saco8uvrm6fd3, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tyb2nno7n67r, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm8ps2e26dh1, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_1amnh72 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_q78colo, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_1cgbgnn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_1cgbgnn F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s592b618tctb1));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_udrroha, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_soo0b43e4t7sn, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_1ck6tsm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1ck6tsm F;
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so43rnba8j5pb));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)56ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tj4tbafenpsjm t18 = GM_LD(gm_tj4tbafenpsjm, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_m16u54c, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 48));
            uint32_t t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_so9fi6r28770a, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_svhavo13qha5k, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_spgfhj218uj9k, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s1utcdes4r8ku, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty467d5cm1d5, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s1mc2427h3p1h, 8)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_1coo7na(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_1coo7na F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss8448efddk3m));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_bcglb0d, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_srof610t1er1p, 25)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s91vt5u8rghg3, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_1d88j1c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1d88j1c F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s268n7l3tugli));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_1ehf2in, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfi2ha42r1nr2, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_1dfo4pq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1dfo4pq F;
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9ob1jv3b8md9));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)40ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tftksr4op114e t18 = GM_LD(gm_tftksr4op114e, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_7jdg3j2, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 32));
            uint64_t t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sjbtn8nt9amae, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty6groq37ram, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_spod3tmkh25up, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_1e409ou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1e409ou F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbdqlrmtee4eo));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(65));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_4u04pfu, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_scmp8kk3f4q6t, 45)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_ssf1r8m922l8a, 15)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_1e53aq3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1e53aq3 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stlm46rulfhsg));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_230s5vf, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_slq8b0dsnkd8e, 13)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_snjonc4tmu56e, 20)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_1il5da4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1il5da4 F;
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srv5rbhjb8ns5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)40ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tq3m5qgvkpje8 t18 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_p942mlu, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 16));
            uint64_t t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sh40na9qbga3l, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tyb2nno7n67r, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s2cuflhqkkuaq, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_1kivsu4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_1kivsu4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss8q42tvppqf5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(78));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_i03nooe, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_smrahl8ppkcu9, 33)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sni9iu6vnvsjk, 43)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_1kki7cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_1kki7cc F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0kf3vtbitlnb));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(57));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_mgigmu6, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s4kq1149c9fl2, 54)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_1mojd22(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_1mojd22 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfbr39r30psvt));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_vn526ra, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_ssdi9g220if8h, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_1ncs69t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_1ncs69t F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sckqm6be9dvme));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_prhsfor, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sas0hkqll4io0, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s2053hhvf16fm, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_1o1mt78(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1o1mt78 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svjsvemd189fa));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_jqo0p0s, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_se8cpd8s09lkc, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s221m8qkvkf71, 22)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_1oelod5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1oelod5 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgc02r26g8j6o));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_tt8mp41, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s3thrieq7sddl, 22)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_stadskj9k22h7, 29)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_1p8frtu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_1p8frtu F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdnh9sm5pppq1));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_81kj5f4, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s3kb1co7e63ka, 36)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_1u4jnnu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_1u4jnnu F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk04qg20jmac0));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_usqmpmt, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tybihgc69jv3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_1va9dpi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_1va9dpi F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb8us6li0fvm5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_k0l9ohg, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sl3acq1nje8kk, 38)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_1vkmnih(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_1vkmnih F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suftpl3n8736g));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_pu948qi, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_h02tha7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_h02tha7 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suo9n1e1rnjb6));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_7vmjkcr, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty6groq37ram, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sive7ce5krqer, 17)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_h0dnf9n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h0dnf9n F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn4utba4jl66l));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_nr5lk0d, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sqjh8kseu4lti, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_h13dbf2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h13dbf2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2kg2oqqdqf2r));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_6oqcm5l, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s66fbu3uhl900, 19)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tybihgc69jv3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_h1n6lc9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h1n6lc9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp1srk1sv0crk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_gq0tg0n, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sh4b8t54h2jna, 48)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h4jcodi F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqa5rf17c57e4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_avfcmre, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_h6b2ofo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h6b2ofo F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stuj0mv5nl55t));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_mqrb14b, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_smerqm8g793l6, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_h6uc0qg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_h6uc0qg F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s329kcr9363eu));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_366v7gn, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tybihgc69jv3, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_h75o0bg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_h75o0bg F;
    gm_t3877pk7lv748 gm_l0; /* args */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdpkm2aceiv8l));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(46));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t3877pk7lv748 t18 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_r97t88j, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s9r71kmnlqpv8, 16)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tyb2nno7n67r, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s72tasmt4pg49, 25)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_h78t8d0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h78t8d0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smr39cplkqb3n));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(50));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_vh5k572, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sm55k23067j17, 15)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s554r118mfcao, 33)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h8e7ps1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc6st5mdtp49u));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_0tkufqc, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_suiq4ou0s86ho, 13)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_h95c7u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h95c7u1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjpr4fvui63qj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_irskvq8, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_slq0l63qf1dvs, 10)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_h9otq43(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tu0pangbbaeec a0) {
    struct gm_fr_format_h9otq43 F;
    gm_tu0pangbbaeec gm_l0; /* args */
    gm_tu0pangbbaeec gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 235u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so31m6feg820p));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)2ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tu0pangbbaeec t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_vqs5lmc, (void *)&F.h, cx});
            uint16_t t20 = GM_LD(uint16_t, (((uint8_t *)&gm_l3) + 0));
            uint16_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_slq58vfha8k9h, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty9oqmmiu8qc, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_hb3htfv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hb3htfv F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s619rbci7p06v));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_pv07cle, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_srv4dcfccuset, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sujpen8u5kmon, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tybihgc69jv3, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_hb6cr3p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hb6cr3p F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slqgeomhftr16));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_qevtudj, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sihclrdudbhkh, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_slgmqgkioa4u6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_hc3p1hh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hc3p1hh F;
    gm_tommf4j9upomc gm_l0; /* args */
    gm_tommf4j9upomc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc5aaou69jljp));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tommf4j9upomc t18 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_sr84td4, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 16));
            uint32_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sbe61ep7tock5, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfdbfrv46lihd, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty467d5cm1d5, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_hcca65h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hcca65h F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfbr39r30psvt));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_uap0af7, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_ssdi9g220if8h, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_he684oe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_he684oe F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scjbu6gm2hrhf));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_opm2rie, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s2c267emk2nni, 5)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_he769rg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_he769rg F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sla3unrbmgc0v));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_1bhi0el, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s3jl3afv8nlvv, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_hedon8i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hedon8i F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6pgabt9io52p));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(131));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_9jroohf, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sdark9dtsgt1q, 40)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sog9gpimub31i, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sr8dujibvqj2g, 81)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_hfu2i92(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_hfu2i92 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slf85brqp8fiu));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(124));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_egnv8ia, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s3phpt25k93cc, 38)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_stqlepc346jgk, 84)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_hgc3vel(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hgc3vel F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s65fsr1v7i389));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(134));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_1iu0ril, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sclhssivsh27a, 36)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s09jc0n36v9gm, 96)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_hgrco1l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hgrco1l F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skmfilbqoc8lo));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(86));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_l54nl53, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s05qp4ie7rju8, 83)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_hi89jlk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hi89jlk F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_souosds7adra9));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_5h85urn, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s7mfvroqmll1f, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_siqfg68f4ud6j, 15)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_smbbaf3n1gml2, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_spbhhmdbp335u, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_hj7ahc4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hj7ahc4 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjg4ie5b5ufsm));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_mnug0dh, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_svm0rfaqk3b2q, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_hjgrmth(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hjgrmth F;
    gm_tvaas22dl64e0 gm_l0; /* args */
    gm_tvaas22dl64e0 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 333u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smdj5floh97je));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tvaas22dl64e0 t18 = GM_LD(gm_tvaas22dl64e0, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_h794c0o, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint16_t t22 = GM_LD(uint16_t, (((uint8_t *)&gm_l3) + 16));
            uint16_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sjkj5g78lejrj, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty9oqmmiu8qc, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s26crtotu14pn, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_hm2bsgr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hm2bsgr F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sddo97gu3reaa));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(71));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_6drm1q7, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sigac1ehmei1f, 68)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_hn5gv7p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hn5gv7p F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8s03uo3h8dsp));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_efj1sku, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_spod3tmkh25up, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_hp1rthv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hp1rthv F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s30rt0lbjn52b));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_oejjcmg, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s9oo6ammv0sch, 16)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tybihgc69jv3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_hpnf26s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hpnf26s F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sttkmg61kqc3k));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(64));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_thddbqb6snabm t18 = GM_LD(gm_thddbqb6snabm, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_vsaec5d, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 16));
            uint64_t t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
            gm_b16_8 t25 = t24;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 40));
            uint64_t t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sait90vqr6gam, 32)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s0b795193rvs2, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_tyb2nno7n67r, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s65dghs61r7cp, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s0b795193rvs2, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_tyb2nno7n67r, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_hqail32(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hqail32 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skr5vsm3evt1r));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ead3e9o, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sunidvof1bsgr, 32)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_hqkpc5s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hqkpc5s F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s99ra887gkc8u));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_84gj1mh, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_svlbiokurll04, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s872048h9ttis, 25)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_hr0sl7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hr0sl7f F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1p0thm43h6d6));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_1t30amv, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_su8bu9ae1ds2f, 18)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_hs0gaus(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hs0gaus F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su5mku12ctc3k));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(91));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_9aei4hh, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sclhssivsh27a, 36)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s4ica9th0h3pb, 53)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_hs8tdgq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_hs8tdgq F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8fhmkhb94g4l));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_eqsgsm0, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfc624hllrs4g, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_ht23jk8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ht23jk8 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0ug1ukauk3l9));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(73));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_lr3dcn9, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sqa5gdfklkjii, 38)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sjfrmur8rvp24, 33)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_ht2r43m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ht2r43m F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s329kcr9363eu));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_321vn6m, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_htqcdvo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_htqcdvo F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s46ki8g7j9f6v));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_s53265p, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tybihgc69jv3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_se1krm9tn2oaa, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_htqqv8t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_htqqv8t F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgsvgcmirhojs));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(27));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_7u3s7d6, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_spg4d1df6jnji, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sqr1rjjq3dpuh, 16)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_hum25d9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hum25d9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4bv7heri08lj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(23));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_b8umguv, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s0r71f5repum2, 20)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_hvaqp8b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_hvaqp8b F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    gm_t2lqu8jsj511d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svr1ohtrtj17p));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(63));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t2lqu8jsj511d t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_opu1gv4, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s0nnk79up541r, 31)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tyb2nno7n67r, ((uint8_t *)&t21), &(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1})) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sso4tmqr8gin2, 19)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tyb2nno7n67r, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sj1gcl0qo4i9p, 6)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_hvnghft(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hvnghft F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn57nkh1rblgd));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(61));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_7020hka, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s8uplmiglsqkm, 14)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s3mmpurhmitlr, 45)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
__attribute__((aligned(4))) void gm_f_parse_float_17hgksa(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_parse_float_17hgksa F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* size */
    uint32_t gm_l2; /* mant_bits */
    uint32_t gm_l3; /* exp_bits */
    int64_t gm_l4; /* bias */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* sign_bit */
    uint64_t gm_l7; /* inf_bits */
    gm_b16_8 gm_l8; /* rest */
    int64_t gm_l10; /* seen */
    int64_t gm_l11; /* dp */
    uint8_t gm_l12; /* saw_dot */
    uint8_t gm_l13; /* saw_digit */
    uint8_t gm_l14; /* after_digit */
    uint8_t gm_l15; /* after_us */
    uint8_t gm_l16; /* c */
    uint8_t gm_l17; /* v */
    int64_t gm_l18; /* e */
    uint8_t gm_l19; /* eneg */
    uint8_t gm_l20; /* c */
    GM_STACK_CHECK(&F, 2006u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_ttiiv5vhiuvvr t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t6 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
            gm_tspt3vn9ljcnd t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_ttiiv5vhiuvvr, ((uint8_t *)&t7 + 8), t6);
            gm_tspt3vn9ljcnd t8 = t7;
            uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_l1 = ((uint64_t)8ULL);
    }
    {
        uint32_t t10;
        uint64_t t12 = gm_l1;
        uint8_t t13 = ((t12 == ((uint64_t)8ULL)));
        if (!t13) goto ie11;
        uint32_t t15 = ((uint32_t)52ULL);
        t10 = t15;
        goto ix11;
ie11:;
        uint32_t t16;
        uint64_t t18 = gm_l1;
        uint8_t t19 = ((t18 == ((uint64_t)4ULL)));
        if (!t19) goto ie17;
        uint32_t t21 = ((uint32_t)23ULL);
        t16 = t21;
        goto ix17;
ie17:;
        uint32_t t23 = ((uint32_t)10ULL);
        t16 = t23;
ix17:;
        t10 = t16;
ix11:;
        gm_l2 = t10;
    }
    {
        uint32_t t24;
        uint64_t t26 = gm_l1;
        uint8_t t27 = ((t26 == ((uint64_t)8ULL)));
        if (!t27) goto ie25;
        uint32_t t29 = ((uint32_t)11ULL);
        t24 = t29;
        goto ix25;
ie25:;
        uint32_t t30;
        uint64_t t32 = gm_l1;
        uint8_t t33 = ((t32 == ((uint64_t)4ULL)));
        if (!t33) goto ie31;
        uint32_t t35 = ((uint32_t)8ULL);
        t30 = t35;
        goto ix31;
ie31:;
        uint32_t t37 = ((uint32_t)5ULL);
        t30 = t37;
ix31:;
        t24 = t30;
ix25:;
        gm_l3 = t24;
    }
    {
        uint32_t t38 = gm_l3;
        uint32_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint32_t)1ULL), &r39);
        if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 626, 33);
        uint32_t t41 = r39;
        unsigned __int128 n42 = (unsigned __int128)t41;
        if (n42 >= 64) gm_trap_shift((uint64_t)n42, "i64", gm_src3d0v42kavhv7, 626, 22);
        int64_t r43 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n42);
        bool o44 = (int64_t)(r43 >> (int)n42) != ((int64_t)1LL);
        if (o44) gm_trap("integer overflow in `<<`", gm_src3d0v42kavhv7, 626, 22);
        int64_t t45 = r43;
        int64_t r46; bool o47 = __builtin_sub_overflow(((int64_t)1LL), t45, &r46);
        if (o47) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 626, 17);
        int64_t t48 = r46;
        gm_l4 = t48;
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        gm_l6 = ((uint64_t)0ULL);
    }
    {
        uint64_t t50 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_src3d0v42kavhv7, 629, 8);
        uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t50 * 1));
        uint8_t t53 = ((t52 == ((uint8_t)45ULL)));
        if (!t53) goto ie49;
        {
            uint32_t t55 = gm_l2;
            uint32_t t56 = gm_l3;
            uint32_t r57; bool o58 = __builtin_add_overflow(t55, t56, &r57);
            if (o58) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 630, 31);
            uint32_t t59 = r57;
            unsigned __int128 n60 = (unsigned __int128)t59;
            if (n60 >= 64) gm_trap_shift((uint64_t)n60, "u64", gm_src3d0v42kavhv7, 630, 20);
            uint64_t r61 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n60);
            bool o62 = (uint64_t)(r61 >> (int)n60) != ((uint64_t)1ULL);
            if (o62) gm_trap("integer overflow in `<<`", gm_src3d0v42kavhv7, 630, 20);
            uint64_t t63 = r61;
            gm_l6 = t63;
        }
        {
            gm_l5 = ((uint64_t)1ULL);
        }
        goto ix49;
ie49:;
        uint64_t t65 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t65 >= (uintptr_t)(t66)) gm_trap_index(t65, t66, gm_src3d0v42kavhv7, 632, 15);
        uint8_t t67 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t65 * 1));
        uint8_t t68 = ((t67 == ((uint8_t)43ULL)));
        if (!t68) goto ie64;
        {
            gm_l5 = ((uint64_t)1ULL);
        }
        goto ix64;
ie64:;
ix64:;
ix49:;
    }
    {
        uint32_t t70 = gm_l3;
        unsigned __int128 n71 = (unsigned __int128)t70;
        if (n71 >= 64) gm_trap_shift((uint64_t)n71, "u64", gm_src3d0v42kavhv7, 635, 23);
        uint64_t r72 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n71);
        bool o73 = (uint64_t)(r72 >> (int)n71) != ((uint64_t)1ULL);
        if (o73) gm_trap("integer overflow in `<<`", gm_src3d0v42kavhv7, 635, 23);
        uint64_t t74 = r72;
        uint64_t r75; bool o76 = __builtin_sub_overflow(t74, ((uint64_t)1ULL), &r75);
        if (o76) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 635, 22);
        uint64_t t77 = r75;
        uint32_t t78 = gm_l2;
        unsigned __int128 n79 = (unsigned __int128)t78;
        if (n79 >= 64) gm_trap_shift((uint64_t)n79, "u64", gm_src3d0v42kavhv7, 635, 21);
        uint64_t r80 = (uint64_t)((uint64_t)t77 << (int)n79);
        bool o81 = (uint64_t)(r80 >> (int)n79) != t77;
        if (o81) gm_trap("integer overflow in `<<`", gm_src3d0v42kavhv7, 635, 21);
        uint64_t t82 = r80;
        gm_l7 = t82;
    }
    {
        uint64_t t84 = gm_l5;
        uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t86 = (t84 < t85);
        if (!t86) goto ie83;
        uint8_t t87 = 0;
        uint64_t t88 = gm_l5;
        uint64_t t89 = (uintptr_t)t88;
        uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t89 >= (uintptr_t)(t90)) gm_trap_index(t89, t90, gm_src3d0v42kavhv7, 636, 38);
        uint8_t t91 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t89 * 1));
        uint8_t t92 = gm_f_to_lower_0fu6efk(t91);
        uint8_t t93 = ((t92 == ((uint8_t)105ULL)));
        if (!t93) {
            uint64_t t94 = gm_l5;
            uint64_t t95 = (uintptr_t)t94;
            uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t95 >= (uintptr_t)(t96)) gm_trap_index(t95, t96, gm_src3d0v42kavhv7, 636, 69);
            uint8_t t97 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t95 * 1));
            uint8_t t98 = gm_f_to_lower_0fu6efk(t97);
            uint8_t t99 = ((t98 == ((uint8_t)110ULL)));
            t87 = t99;
        } else t87 = 1;
        if (!t87) goto ie83;
        {
            uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t102 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t103 = gm_l5;
            uint64_t t104 = (uintptr_t)t103;
            uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint64_t t106 = (uintptr_t)t105;
            if (t104 > t106 || t106 > (uintptr_t)t101) gm_trap_slice(t104, t106, t101, gm_src3d0v42kavhv7, 637, 17);
            gm_b16_8 t107;
            GM_ST(void *, (uint8_t *)&t107, (void *)(((uint8_t *)t102 + t104 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(t106 - t104));
            gm_l8 = t107;
        }
        {
            uint8_t t109 = 0;
            gm_b16_8 t110 = gm_l8;
            gm_b16_8 t111;
            GM_ST(void *, (uint8_t *)&t111, (void *)(gm_sns75reqq32d2));
            GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(3));
            uint8_t t112 = gm_f_eq_ignore_case_qqsqlkn(t110, t111);
            if (!t112) {
                gm_b16_8 t113 = gm_l8;
                gm_b16_8 t114;
                GM_ST(void *, (uint8_t *)&t114, (void *)(gm_s3e1pn3bonjpd));
                GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(8));
                uint8_t t115 = gm_f_eq_ignore_case_qqsqlkn(t113, t114);
                t109 = t115;
            } else t109 = 1;
            if (!t109) goto ie108;
            {
                uint64_t t117 = gm_l6;
                uint64_t t118 = gm_l7;
                uint64_t t119 = (uint64_t)(t117 | t118);
                double t120 = gm_f_from_bits_mv0phhs(t119);
                gm_tspt3vn9ljcnd t121;
                GM_ST(uint8_t, (uint8_t *)&t121 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t121 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t121 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t121 + 16, 0);
                ((uint8_t *)&t121)[0] = 0;
                GM_ST(double, ((uint8_t *)&t121 + 8), t120);
                gm_tspt3vn9ljcnd t122 = t121;
                uint8_t t123 = ((((uint8_t *)&t122))[0] != 0);
                F.ret = t121;
                goto gm_fret;
            }
            goto ix108;
ie108:;
ix108:;
        }
        {
            gm_b16_8 t125 = gm_l8;
            gm_b16_8 t126;
            GM_ST(void *, (uint8_t *)&t126, (void *)(gm_sfa86erpod86s));
            GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(3));
            uint8_t t127 = gm_f_eq_ignore_case_qqsqlkn(t125, t126);
            if (!t127) goto ie124;
            {
                uint64_t t129 = gm_l6;
                uint64_t t130 = gm_l7;
                uint64_t t131 = (uint64_t)(t129 | t130);
                uint32_t t132 = gm_l2;
                uint32_t r133; bool o134 = __builtin_sub_overflow(t132, ((uint32_t)1ULL), &r133);
                if (o134) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 642, 67);
                uint32_t t135 = r133;
                unsigned __int128 n136 = (unsigned __int128)t135;
                if (n136 >= 64) gm_trap_shift((uint64_t)n136, "u64", gm_src3d0v42kavhv7, 642, 56);
                uint64_t r137 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n136);
                bool o138 = (uint64_t)(r137 >> (int)n136) != ((uint64_t)1ULL);
                if (o138) gm_trap("integer overflow in `<<`", gm_src3d0v42kavhv7, 642, 56);
                uint64_t t139 = r137;
                uint64_t t140 = (uint64_t)(t131 | t139);
                double t141 = gm_f_from_bits_mv0phhs(t140);
                gm_tspt3vn9ljcnd t142;
                GM_ST(uint8_t, (uint8_t *)&t142 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t142 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t142 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t142 + 16, 0);
                ((uint8_t *)&t142)[0] = 0;
                GM_ST(double, ((uint8_t *)&t142 + 8), t141);
                gm_tspt3vn9ljcnd t143 = t142;
                uint8_t t144 = ((((uint8_t *)&t143))[0] != 0);
                F.ret = t142;
                goto gm_fret;
            }
            goto ix124;
ie124:;
ix124:;
        }
        {
            uint64_t t145 = gm_l5;
            gm_t4ce0jd3tdro4 t146;
            GM_ST(uint64_t, ((uint8_t *)&t146 + 0), t145);
            gm_ttiiv5vhiuvvr t147;
            GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t147 + 8), t146);
            gm_tspt3vn9ljcnd t148;
            GM_ST(uint8_t, (uint8_t *)&t148 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t148 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t148 + 4, 0);
            ((uint8_t *)&t148)[0] = 1;
            GM_ST(gm_ttiiv5vhiuvvr, ((uint8_t *)&t148 + 8), t147);
            gm_tspt3vn9ljcnd t149 = t148;
            uint8_t t150 = ((((uint8_t *)&t149))[0] != 0);
            F.ret = t148;
            goto gm_fret;
        }
        goto ix83;
ie83:;
ix83:;
    }
    {
    }
    {
        GM_ST(uint64_t, (((uint8_t *)&F.l9) + 800), ((uint64_t)0ULL));
    }
    {
        GM_ST(int64_t, (((uint8_t *)&F.l9) + 808), ((int64_t)0LL));
    }
    {
        GM_ST(uint8_t, (((uint8_t *)&F.l9) + 816), ((uint8_t)0ULL));
    }
    {
        gm_l10 = ((int64_t)0LL);
    }
    {
        gm_l11 = ((int64_t)0LL);
    }
    {
        gm_l12 = ((uint8_t)0);
    }
    {
        gm_l13 = ((uint8_t)0);
    }
    {
        gm_l14 = ((uint8_t)0);
    }
    {
        gm_l15 = ((uint8_t)0);
    }
    {
        for (;;) {
            uint64_t t152 = gm_l5;
            uint64_t t153 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t154 = (t152 < t153);
            if (!t154) goto lb151;
            {
                uint64_t t157 = gm_l5;
                uint64_t t158 = (uintptr_t)t157;
                uint64_t t159 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t158 >= (uintptr_t)(t159)) gm_trap_index(t158, t159, gm_src3d0v42kavhv7, 658, 14);
                uint8_t t160 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t158 * 1));
                gm_l16 = t160;
            }
            {
                uint8_t t162 = gm_l16;
                uint8_t t163 = ((t162 == ((uint8_t)95ULL)));
                if (!t163) goto ie161;
                {
                    uint8_t t166 = gm_l14;
                    uint8_t t167 = (uint8_t)!t166;
                    if (!t167) goto ie165;
                    {
                        uint64_t t169 = gm_l5;
                        gm_t4ce0jd3tdro4 t170;
                        GM_ST(uint64_t, ((uint8_t *)&t170 + 0), t169);
                        gm_ttiiv5vhiuvvr t171;
                        GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t171 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t171 + 8), t170);
                        gm_tspt3vn9ljcnd t172;
                        GM_ST(uint8_t, (uint8_t *)&t172 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t172 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t172 + 4, 0);
                        ((uint8_t *)&t172)[0] = 1;
                        GM_ST(gm_ttiiv5vhiuvvr, ((uint8_t *)&t172 + 8), t171);
                        gm_tspt3vn9ljcnd t173 = t172;
                        uint8_t t174 = ((((uint8_t *)&t173))[0] != 0);
                        F.ret = t172;
                        goto gm_fret;
                    }
                    goto ix165;
ie165:;
ix165:;
                }
                {
                    gm_l14 = ((uint8_t)0);
                }
                {
                    gm_l15 = ((uint8_t)1);
                }
                goto ix161;
ie161:;
                uint8_t t176 = gm_l16;
                uint8_t t177 = ((t176 == ((uint8_t)46ULL)));
                if (!t177) goto ie175;
                {
                    uint8_t t180 = 0;
                    uint8_t t181 = gm_l12;
                    if (!t181) {
                        uint8_t t182 = gm_l15;
                        t180 = t182;
                    } else t180 = 1;
                    if (!t180) goto ie179;
                    {
                        uint64_t t184 = gm_l5;
                        gm_t4ce0jd3tdro4 t185;
                        GM_ST(uint64_t, ((uint8_t *)&t185 + 0), t184);
                        gm_ttiiv5vhiuvvr t186;
                        GM_ST(uint8_t, (uint8_t *)&t186 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t186 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t186 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t186 + 8), t185);
                        gm_tspt3vn9ljcnd t187;
                        GM_ST(uint8_t, (uint8_t *)&t187 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t187 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t187 + 4, 0);
                        ((uint8_t *)&t187)[0] = 1;
                        GM_ST(gm_ttiiv5vhiuvvr, ((uint8_t *)&t187 + 8), t186);
                        gm_tspt3vn9ljcnd t188 = t187;
                        uint8_t t189 = ((((uint8_t *)&t188))[0] != 0);
                        F.ret = t187;
                        goto gm_fret;
                    }
                    goto ix179;
ie179:;
ix179:;
                }
                {
                    gm_l12 = ((uint8_t)1);
                }
                {
                    int64_t t190 = gm_l10;
                    gm_l11 = t190;
                }
                {
                    gm_l14 = ((uint8_t)0);
                }
                goto ix175;
ie175:;
                uint8_t t192 = gm_l16;
                uint8_t t193 = (t192 >= ((uint8_t)48ULL));
                if (!t193) goto ie191;
                uint8_t t194 = gm_l16;
                uint8_t t195 = (t194 <= ((uint8_t)57ULL));
                if (!t195) goto ie191;
                {
                    gm_l13 = ((uint8_t)1);
                }
                {
                    gm_l14 = ((uint8_t)1);
                }
                {
                    gm_l15 = ((uint8_t)0);
                }
                {
                    uint8_t t197 = gm_l16;
                    uint8_t r198; bool o199 = __builtin_sub_overflow(t197, ((uint8_t)48ULL), &r198);
                    if (o199) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 672, 18);
                    uint8_t t200 = r198;
                    gm_l17 = t200;
                }
                {
                    uint8_t t202 = gm_l17;
                    uint8_t t203 = ((t202 == ((uint8_t)0ULL)));
                    if (!t203) goto ie201;
                    int64_t t204 = gm_l10;
                    uint8_t t205 = ((t204 == ((int64_t)0LL)));
                    if (!t205) goto ie201;
                    {
                        uint8_t t208 = gm_l12;
                        if (!t208) goto ie207;
                        {
                            int64_t t210 = gm_l11;
                            int64_t r211; bool o212 = __builtin_sub_overflow(t210, ((int64_t)1LL), &r211);
                            if (o212) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 674, 30);
                            int64_t t213 = r211;
                            gm_l11 = t213;
                        }
                        goto ix207;
ie207:;
ix207:;
                    }
                    goto ix201;
ie201:;
                    {
                        uint64_t t216 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 800));
                        uint8_t t217 = (t216 < ((uint64_t)800ULL));
                        if (!t217) goto ie215;
                        {
                            uint64_t t219 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 800));
                            uint64_t t220 = (uintptr_t)t219;
                            if (t220 >= (uintptr_t)(800)) gm_trap_index(t220, 800, gm_src3d0v42kavhv7, 677, 21);
                            uint8_t t221 = gm_l17;
                            GM_ST(uint8_t, ((((uint8_t *)&F.l9) + 0) + t220 * 1), t221);
                        }
                        {
                            uint64_t t222 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 800));
                            uint64_t r223; bool o224 = __builtin_add_overflow(t222, ((uint64_t)1ULL), &r223);
                            if (o224) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 678, 21);
                            uint64_t t225 = r223;
                            GM_ST(uint64_t, (((uint8_t *)&F.l9) + 800), t225);
                        }
                        goto ix215;
ie215:;
                        uint8_t t227 = gm_l17;
                        uint8_t t228 = (!(t227 == ((uint8_t)0ULL)));
                        if (!t228) goto ie226;
                        {
                            GM_ST(uint8_t, (((uint8_t *)&F.l9) + 816), ((uint8_t)1ULL));
                        }
                        goto ix226;
ie226:;
ix226:;
ix215:;
                    }
                    {
                        int64_t t230 = gm_l10;
                        int64_t r231; bool o232 = __builtin_add_overflow(t230, ((int64_t)1LL), &r231);
                        if (o232) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 682, 17);
                        int64_t t233 = r231;
                        gm_l10 = t233;
                    }
ix201:;
                }
                goto ix191;
ie191:;
                {
                    goto lb151;
                }
ix191:;
ix175:;
ix161:;
            }
            {
                uint64_t t235 = gm_l5;
                uint64_t r236; bool o237 = __builtin_add_overflow(t235, ((uint64_t)1ULL), &r236);
                if (o237) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 687, 9);
                uint64_t t238 = r236;
                gm_l5 = t238;
            }
lc151:;
        }
lb151:;
    }
    {
        uint8_t t240 = 0;
        uint8_t t241 = gm_l15;
        if (!t241) {
            uint8_t t242 = gm_l13;
            uint8_t t243 = (uint8_t)!t242;
            t240 = t243;
        } else t240 = 1;
        if (!t240) goto ie239;
        {
            uint64_t t245 = gm_l5;
            gm_t4ce0jd3tdro4 t246;
            GM_ST(uint64_t, ((uint8_t *)&t246 + 0), t245);
            gm_ttiiv5vhiuvvr t247;
            GM_ST(uint8_t, (uint8_t *)&t247 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t247 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t247 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t247 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t247 + 8), t246);
            gm_tspt3vn9ljcnd t248;
            GM_ST(uint8_t, (uint8_t *)&t248 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t248 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t248 + 4, 0);
            ((uint8_t *)&t248)[0] = 1;
            GM_ST(gm_ttiiv5vhiuvvr, ((uint8_t *)&t248 + 8), t247);
            gm_tspt3vn9ljcnd t249 = t248;
            uint8_t t250 = ((((uint8_t *)&t249))[0] != 0);
            F.ret = t248;
            goto gm_fret;
        }
        goto ix239;
ie239:;
ix239:;
    }
    {
        uint8_t t252 = gm_l12;
        uint8_t t253 = (uint8_t)!t252;
        if (!t253) goto ie251;
        {
            int64_t t255 = gm_l10;
            gm_l11 = t255;
        }
        goto ix251;
ie251:;
ix251:;
    }
    {
        gm_l18 = ((int64_t)0LL);
    }
    {
        uint64_t t257 = gm_l5;
        uint64_t t258 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t259 = (t257 < t258);
        if (!t259) goto ie256;
        uint64_t t260 = gm_l5;
        uint64_t t261 = (uintptr_t)t260;
        uint64_t t262 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t261 >= (uintptr_t)(t262)) gm_trap_index(t261, t262, gm_src3d0v42kavhv7, 693, 37);
        uint8_t t263 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t261 * 1));
        uint8_t t264 = gm_f_to_lower_0fu6efk(t263);
        uint8_t t265 = ((t264 == ((uint8_t)101ULL)));
        if (!t265) goto ie256;
        {
            uint64_t t267 = gm_l5;
            uint64_t r268; bool o269 = __builtin_add_overflow(t267, ((uint64_t)1ULL), &r268);
            if (o269) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 694, 9);
            uint64_t t270 = r268;
            gm_l5 = t270;
        }
        {
            gm_l19 = ((uint8_t)0);
        }
        {
            uint64_t t272 = gm_l5;
            uint64_t t273 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t274 = (t272 < t273);
            if (!t274) goto ie271;
            uint64_t t275 = gm_l5;
            uint64_t t276 = (uintptr_t)t275;
            uint64_t t277 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t276 >= (uintptr_t)(t277)) gm_trap_index(t276, t277, gm_src3d0v42kavhv7, 696, 26);
            uint8_t t278 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t276 * 1));
            uint8_t t279 = ((t278 == ((uint8_t)45ULL)));
            if (!t279) goto ie271;
            {
                gm_l19 = ((uint8_t)1);
            }
            {
                uint64_t t281 = gm_l5;
                uint64_t r282; bool o283 = __builtin_add_overflow(t281, ((uint64_t)1ULL), &r282);
                if (o283) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 698, 13);
                uint64_t t284 = r282;
                gm_l5 = t284;
            }
            goto ix271;
ie271:;
            uint64_t t286 = gm_l5;
            uint64_t t287 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t288 = (t286 < t287);
            if (!t288) goto ie285;
            uint64_t t289 = gm_l5;
            uint64_t t290 = (uintptr_t)t289;
            uint64_t t291 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t290 >= (uintptr_t)(t291)) gm_trap_index(t290, t291, gm_src3d0v42kavhv7, 699, 33);
            uint8_t t292 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t290 * 1));
            uint8_t t293 = ((t292 == ((uint8_t)43ULL)));
            if (!t293) goto ie285;
            {
                uint64_t t295 = gm_l5;
                uint64_t r296; bool o297 = __builtin_add_overflow(t295, ((uint64_t)1ULL), &r296);
                if (o297) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 700, 13);
                uint64_t t298 = r296;
                gm_l5 = t298;
            }
            goto ix285;
ie285:;
ix285:;
ix271:;
        }
        {
            gm_l14 = ((uint8_t)0);
        }
        {
            for (;;) {
                uint64_t t300 = gm_l5;
                uint64_t t301 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t302 = (t300 < t301);
                if (!t302) goto lb299;
                {
                    uint64_t t305 = gm_l5;
                    uint64_t t306 = (uintptr_t)t305;
                    uint64_t t307 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t306 >= (uintptr_t)(t307)) gm_trap_index(t306, t307, gm_src3d0v42kavhv7, 704, 18);
                    uint8_t t308 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t306 * 1));
                    gm_l20 = t308;
                }
                {
                    uint8_t t310 = gm_l20;
                    uint8_t t311 = ((t310 == ((uint8_t)95ULL)));
                    if (!t311) goto ie309;
                    {
                        uint8_t t314 = gm_l14;
                        uint8_t t315 = (uint8_t)!t314;
                        if (!t315) goto ie313;
                        {
                            uint64_t t317 = gm_l5;
                            gm_t4ce0jd3tdro4 t318;
                            GM_ST(uint64_t, ((uint8_t *)&t318 + 0), t317);
                            gm_ttiiv5vhiuvvr t319;
                            GM_ST(uint8_t, (uint8_t *)&t319 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t319 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t319 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t319 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t319 + 8), t318);
                            gm_tspt3vn9ljcnd t320;
                            GM_ST(uint8_t, (uint8_t *)&t320 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t320 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t320 + 4, 0);
                            ((uint8_t *)&t320)[0] = 1;
                            GM_ST(gm_ttiiv5vhiuvvr, ((uint8_t *)&t320 + 8), t319);
                            gm_tspt3vn9ljcnd t321 = t320;
                            uint8_t t322 = ((((uint8_t *)&t321))[0] != 0);
                            F.ret = t320;
                            goto gm_fret;
                        }
                        goto ix313;
ie313:;
ix313:;
                    }
                    {
                        gm_l14 = ((uint8_t)0);
                    }
                    goto ix309;
ie309:;
                    uint8_t t324 = gm_l20;
                    uint8_t t325 = (t324 >= ((uint8_t)48ULL));
                    if (!t325) goto ie323;
                    uint8_t t326 = gm_l20;
                    uint8_t t327 = (t326 <= ((uint8_t)57ULL));
                    if (!t327) goto ie323;
                    {
                        gm_l14 = ((uint8_t)1);
                    }
                    {
                        int64_t t330 = gm_l18;
                        uint8_t t331 = (t330 < ((int64_t)100000LL));
                        if (!t331) goto ie329;
                        {
                            int64_t t333 = gm_l18;
                            int64_t r334; bool o335 = __builtin_mul_overflow(t333, ((int64_t)10LL), &r334);
                            if (o335) gm_trap("integer overflow in `*`", gm_src3d0v42kavhv7, 711, 38);
                            int64_t t336 = r334;
                            uint8_t t337 = gm_l20;
                            uint8_t r338; bool o339 = __builtin_sub_overflow(t337, ((uint8_t)48ULL), &r338);
                            if (o339) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 711, 51);
                            uint8_t t340 = r338;
                            int64_t t341 = (int64_t)t340;
                            int64_t r342; bool o343 = __builtin_add_overflow(t336, t341, &r342);
                            if (o343) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 711, 38);
                            int64_t t344 = r342;
                            gm_l18 = t344;
                        }
                        goto ix329;
ie329:;
ix329:;
                    }
                    goto ix323;
ie323:;
                    {
                        goto lb299;
                    }
ix323:;
ix309:;
                }
                {
                    uint64_t t346 = gm_l5;
                    uint64_t r347; bool o348 = __builtin_add_overflow(t346, ((uint64_t)1ULL), &r347);
                    if (o348) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 715, 13);
                    uint64_t t349 = r347;
                    gm_l5 = t349;
                }
lc299:;
            }
lb299:;
        }
        {
            uint8_t t351 = gm_l14;
            uint8_t t352 = (uint8_t)!t351;
            if (!t352) goto ie350;
            {
                uint64_t t354 = gm_l5;
                gm_t4ce0jd3tdro4 t355;
                GM_ST(uint64_t, ((uint8_t *)&t355 + 0), t354);
                gm_ttiiv5vhiuvvr t356;
                GM_ST(uint8_t, (uint8_t *)&t356 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t356 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t356 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t356 + 0), ((uint8_t)1ULL));
                GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t356 + 8), t355);
                gm_tspt3vn9ljcnd t357;
                GM_ST(uint8_t, (uint8_t *)&t357 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t357 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t357 + 4, 0);
                ((uint8_t *)&t357)[0] = 1;
                GM_ST(gm_ttiiv5vhiuvvr, ((uint8_t *)&t357 + 8), t356);
                gm_tspt3vn9ljcnd t358 = t357;
                uint8_t t359 = ((((uint8_t *)&t358))[0] != 0);
                F.ret = t357;
                goto gm_fret;
            }
            goto ix350;
ie350:;
ix350:;
        }
        {
            uint8_t t361 = gm_l19;
            if (!t361) goto ie360;
            {
                int64_t t363 = gm_l18;
                if (t363 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 718, 23);
                int64_t t364 = (int64_t)((uint64_t)0 - (uint64_t)t363);
                gm_l18 = t364;
            }
            goto ix360;
ie360:;
ix360:;
        }
        goto ix256;
ie256:;
ix256:;
    }
    {
        uint64_t t366 = gm_l5;
        uint64_t t367 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t368 = (t366 < t367);
        if (!t368) goto ie365;
        {
            uint64_t t370 = gm_l5;
            gm_t4ce0jd3tdro4 t371;
            GM_ST(uint64_t, ((uint8_t *)&t371 + 0), t370);
            gm_ttiiv5vhiuvvr t372;
            GM_ST(uint8_t, (uint8_t *)&t372 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t372 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t372 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t372 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t372 + 8), t371);
            gm_tspt3vn9ljcnd t373;
            GM_ST(uint8_t, (uint8_t *)&t373 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t373 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t373 + 4, 0);
            ((uint8_t *)&t373)[0] = 1;
            GM_ST(gm_ttiiv5vhiuvvr, ((uint8_t *)&t373 + 8), t372);
            gm_tspt3vn9ljcnd t374 = t373;
            uint8_t t375 = ((((uint8_t *)&t374))[0] != 0);
            F.ret = t373;
            goto gm_fret;
        }
        goto ix365;
ie365:;
ix365:;
    }
    {
        int64_t t376 = gm_l11;
        int64_t t377 = gm_l18;
        int64_t r378; bool o379 = __builtin_add_overflow(t376, t377, &r378);
        if (o379) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 721, 14);
        int64_t t380 = r378;
        GM_ST(int64_t, (((uint8_t *)&F.l9) + 808), t380);
    }
    {
        void * t381 = (void *)((uint8_t *)&F.l9);
        gm_f_trim_decimal_56e5p3i(t381);
    }
    {
        uint64_t t382 = gm_l6;
        void * t383 = (void *)((uint8_t *)&F.l9);
        uint32_t t384 = gm_l2;
        uint32_t t385 = gm_l3;
        int64_t t386 = gm_l4;
        uint64_t t387 = gm_f_decimal_bits_c63p8nv(t383, t384, t385, t386);
        uint64_t t388 = (uint64_t)(t382 | t387);
        double t389 = gm_f_from_bits_mv0phhs(t388);
        gm_tspt3vn9ljcnd t390;
        GM_ST(uint8_t, (uint8_t *)&t390 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t390 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t390 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t390 + 16, 0);
        ((uint8_t *)&t390)[0] = 0;
        GM_ST(double, ((uint8_t *)&t390 + 8), t389);
        gm_tspt3vn9ljcnd t391 = t390;
        uint8_t t392 = ((((uint8_t *)&t391))[0] != 0);
        F.ret = t390;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tspt3vn9ljcnd gm_f_parse_float_17hgksa_c(gm_b16_8 a0) { gm_tspt3vn9ljcnd gm_r; gm_f_parse_float_17hgksa(&gm_r, a0); return gm_r; }
