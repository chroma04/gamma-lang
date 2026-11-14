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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { double f0; } gm_tidu2er0d7h95;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint64_t f0; gm_tcc1rq4n1avb2 f1; } gm_t2aqa0c3djlco;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { uint64_t f0; gm_t2lqu8jsj511d f1; } gm_t5o7duvovobu1;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_tidu2er0d7h95) == 8 && _Alignof(gm_tidu2er0d7h95) == 8 && offsetof(gm_tidu2er0d7h95, f0) == 0, "layout of `(f64,)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t2aqa0c3djlco) == 16 && _Alignof(gm_t2aqa0c3djlco) == 8 && offsetof(gm_t2aqa0c3djlco, f0) == 0 && offsetof(gm_t2aqa0c3djlco, f1) == 8, "layout of `(i: usize, x: (file: u32, line: u32))`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint64_t f0; gm_t5hdnnftkup6m f1; } gm_t4c7ikvc472re;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t5o7duvovobu1) == 24 && _Alignof(gm_t5o7duvovobu1) == 8 && offsetof(gm_t5o7duvovobu1, f0) == 0 && offsetof(gm_t5o7duvovobu1, f1) == 8, "layout of `(i: usize, x: (lo: usize, hi: usize))`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
typedef struct { gm_tdgfduqjheqdm f0; uint8_t f1; uint8_t pe[7]; } gm_tdn4erebgflsv;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; uint32_t v1; } f1; } gm_tssfa0col9q01;
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4c7ikvc472re) == 32 && _Alignof(gm_t4c7ikvc472re) == 8 && offsetof(gm_t4c7ikvc472re, f0) == 0 && offsetof(gm_t4c7ikvc472re, f1) == 8, "layout of `(i: usize, x: Hit)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdn4erebgflsv) == 72 && _Alignof(gm_tdn4erebgflsv) == 8 && offsetof(gm_tdn4erebgflsv, f0) == 0 && offsetof(gm_tdn4erebgflsv, f1) == 64, "layout of `?FnDef`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { void * f0; void * f1; gm_t4c7ipjj9s3fp f2; gm_t4c7ipjj9s3fp f3; gm_tin97tt4ud0s1 f4; } gm_tqh23fthuf005;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_tssfa0col9q01) == 24 && _Alignof(gm_tssfa0col9q01) == 8 && offsetof(gm_tssfa0col9q01, f0) == 0 && offsetof(gm_tssfa0col9q01, f1) == 8, "layout of `ParseError!u32`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tqh23fthuf005) == 168 && _Alignof(gm_tqh23fthuf005) == 8 && offsetof(gm_tqh23fthuf005, f0) == 0 && offsetof(gm_tqh23fthuf005, f1) == 8 && offsetof(gm_tqh23fthuf005, f2) == 16 && offsetof(gm_tqh23fthuf005, f3) == 72 && offsetof(gm_tqh23fthuf005, f4) == 128, "layout of `Instrument`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
struct gm_fr_add_counts_hprtdd9 {
    gm_frame h;
    void * l0; /* counts */
    uint64_t l2; /* i */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_block_u0oqu8p {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* stmts */
    gm_b0_1 ret;
};
struct gm_fr_bytes_less_r33ee4s {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_canonicalize_epo7cln {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_chars_232hkk6 {
    gm_frame h;
    uint64_t l1; /* n */
    uint64_t ret;
    gm_b0_1 w0;
};
struct gm_fr_counted_fns_j3p4pgn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* tests */
    gm_trqfvmjtt4sij l6; /* out */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_cover_stmt_bp29hrl {
    gm_frame h;
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_coverage_report_hrhbf22 {
    gm_frame h;
    gm_t4c7ipjj9s3fp l5; /* canon */
    gm_trqfvmjtt4sij l6; /* all */
    gm_trqfvmjtt4sij l14; /* lines */
    gm_trqfvmjtt4sij l19; /* files */
    gm_trqfvmjtt4sij l24; /* names */
    gm_trqfvmjtt4sij l43; /* text */
    uint8_t ret;
};
struct gm_fr_def_of_m512sbu {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_empty_slice_0lkule6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_es387ts {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n0agqu3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n44tb4r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qsq36pn {
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
struct gm_fr_empty_slice_v7c9k0s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_find_root_kjnm6hf {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_fn_g9dfnu7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nqs6or3 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_1vrv15m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_d80h595 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_tbgr2dj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_vetgq3s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_get_dk5gh28 {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_hash_map_70470jk {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_tl7apid {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_vqsve4u {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_instrument_mk8clvl {
    gm_frame h;
    gm_tqh23fthuf005 l2; /* cx */
    gm_trqfvmjtt4sij l3; /* counted */
    gm_trqfvmjtt4sij l11; /* stmts */
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_is_dir_2kkkskr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_dir_6sd3mnh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_test_1bbodje {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_14sicke {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1qnmubu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rdfjble {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_pad_o3a3nuu {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_path_less_il538gm {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* x */
    gm_trqfvmjtt4sij l4; /* y */
    uint8_t ret;
    gm_b0_1 w0;
    gm_b0_1 w1;
};
struct gm_fr_percent_ie1duk3 {
    gm_frame h;
    double ret;
};
struct gm_fr_point_8frh2ab {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_scope_4n0q0tq {
    gm_frame h;
    gm_t6j6smj7qoctu ret;
};
struct gm_fr_sort_by_lh24ei9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_stmt_span_slh12dn {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_under_rb6c23o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_visit_mnqi05r {
    gm_frame h;
    void * l0; /* cx */
    gm_t710lsrq1m8gk l3; /* l */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_counts_hprtdd9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_block_u0oqu8p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_bytes_less_r33ee4s(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_canonicalize_n3e3qri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_chars_232hkk6(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_counted_fns_j3p4pgn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_cover_stmt_bp29hrl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint8_t gm_f_coverage_report_hrhbf22(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, const void * a4);
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_0lkule6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_es387ts(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n0agqu3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n44tb4r(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_v7c9k0s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_hb6lt4h(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_find_package_root_fs4fuqj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_find_root_kjnm6hf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g9dfnu7(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_fn_nqs6or3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1vrv15m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_d80h595(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tbgr2dj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vetgq3s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_01jvudh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_78dnvqp(void * a0, gm_tcc1rq4n1avb2 a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_dk5gh28(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_gnmase9(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_mk00hfd(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_70470jk(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_vqsve4u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_instrument_mk8clvl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_introsort_se56bco(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_test_1bbodje(gm_b16_8 a0);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sqdm19s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_14sicke(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1qnmubu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rdfjble(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_pad_o3a3nuu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_int_i2lnj6n(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_path_less_il538gm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) double gm_f_percent_ie1duk3(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_point_8frh2ab(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8cklqk5(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_lstf2b8(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vgae2dr(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_a7l7vun(void * a0, gm_tcc1rq4n1avb2 a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_po09vud(void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_v6g5pup(void * a0, uint32_t a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_runes_5kk1v3e_ssomsr2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_scope_4n0q0tq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_sort_by_lh24ei9(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sq3203i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_st80pr1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_std_root_e0juohb(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_stmt_span_slh12dn(const void * a0);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_stmt_span_slh12dn_c(gm_trcmmd12cefb1 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_under_rb6c23o(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_visit_mnqi05r(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_all_o8ucaji(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_erog63d(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_l7mh821(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lmets0c(void * a0, gm_tnpifv20irnph a1);
GM_NOINLINE bool gm_pf1m4b8q4e78(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE void gm_pfmaojsuds6a(gm_node *cx, gm_b16_8 a, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern void *const gm_hl9hta9cqrqu[];
extern void *const gm_hlnaa5ptob1s[];
extern void *const gm_hlqu6cf845ie[];
extern void *const gm_hlvedue473cr[];
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_kp3nl0er6il[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s7r1917rmmif7[21];
extern const uint8_t gm_s93p2i934ld6p[5];
extern const uint8_t gm_sgankvlme129s[3];
extern const uint8_t gm_sh90fjlgqrlfc[7];
extern const uint8_t gm_sl1f0va0l61bh[23];
extern const uint8_t gm_sl6eotv49v54f[38];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_snge4b9tsq8qc[8];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sofs2uc27fjvk[3];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgr7vbgb5cqmg[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcoj9bno4fi1q4[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_ss7qhfi2s00na[11];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_suc2pvoh9ktdi[6];
extern const gm_type gm_ty6groq37ram;
extern const gm_type gm_tybihgc69jv3;
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
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_counts_hprtdd9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_add_counts_hprtdd9 F;
    gm_b16_8 gm_l1; /* text */
    gm_b16_8 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_l3 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl9hta9cqrqu, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l3;
        gm_b0_1 t9 = gm_f_lines_9uns0k2_sqdm19s((void *)NULL, (&wn5), NULL, t8);
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
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonicalize_epo7cln F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_f_canonicalize_n3e3qri((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) uint64_t gm_f_chars_232hkk6(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_chars_232hkk6 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        F.l1 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 78927826, .kind = GM_LIT, .clauses = gm_hlnaa5ptob1s, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l2;
        gm_b0_1 t9 = gm_f_runes_5kk1v3e_ssomsr2((void *)NULL, (&wn5), NULL, t8);
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
        uint64_t t13 = F.l1;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_0lkule6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_0lkule6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_es387ts(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_es387ts F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n0agqu3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n0agqu3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n44tb4r(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n44tb4r F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qsq36pn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_v7c9k0s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_v7c9k0s F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcv1nri2247d2r, 78, 75);
    void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 80);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_find_root_kjnm6hf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_find_root_kjnm6hf F;
    gm_b16_8 gm_l0; /* start */
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_th2lqbo77pvsc t4;
        gm_f_canonicalize_epo7cln((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t5;
        if (((((uint8_t *)&t4))[16] != 0)) goto mn7;
        t5 = ((uint8_t)1);
        goto me6;
mn7:;
        t5 = ((uint8_t)0);
        goto me6;
mn8:;
        gm_trap("no match arm matched the scrutinee", gm_srcoj9bno4fi1q4, 11, 8);
me6:;
        if (!t5) goto ie2;
        {
            gm_th2lqbo77pvsc t10;
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
        gm_b16_8 t12 = gm_l0;
        gm_f_find_package_root_fs4fuqj((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t12);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g9dfnu7(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_g9dfnu7 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_visit_mnqi05r *)((&F.h)->up))->l0));
        void * t3 = gm_l0;
        gm_b0_1 t4 = gm_f_visit_mnqi05r((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1vrv15m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_1vrv15m F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_path_less_il538gm *)((&F.h)->up))->l2);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcgr7vbgb5cqmg, 185, 34);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 185, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_d80h595(void *env, gm_node *cx, gm_node *inst, uint32_t a0) {
    struct gm_fr_for_body_d80h595 F;
    uint32_t gm_l0; /* <item> */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t1 = gm_l0;
    {
        uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_chars_232hkk6 *)((&F.h)->up))->l1));
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)1ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 206, 25);
        uint64_t t8 = r6;
        GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_chars_232hkk6 *)((&F.h)->up))->l1), t8);
    }
    goto me2;
mn3:;
    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 206, 5);
me2:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tbgr2dj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_tbgr2dj F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_path_less_il538gm *)((&F.h)->up))->l4);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcgr7vbgb5cqmg, 187, 34);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 187, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_dk5gh28(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_dk5gh28 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 111u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_hb6lt4h(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tqs5p4n2tq19v t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 119, 12);
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 12) + 8));
        gm_tqs5p4n2tq19v t14;
        GM_ST(uint8_t, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[1] = 1;
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_70470jk(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_70470jk F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_n44tb4r();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_tl7apid F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_qsq36pn();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_vqsve4u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_vqsve4u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_es387ts();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_dir_2kkkskr F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    uint8_t t2 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_dir_6sd3mnh F;
    gm_b16_8 gm_l0; /* p */
    gm_t5j14m8ru132p gm_l1; /* info */
    GM_STACK_CHECK(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 229, 13);
        gm_tih6h4avv5nrb t4;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[5])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5j14m8ru132p t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            t5 = (*(gm_t5j14m8ru132p *)(((uint8_t *)&t4) + 8));
        } else {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        uint8_t t7 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8) + 0) == ((uint8_t)1ULL)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_test_1bbodje(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_test_1bbodje F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sh90fjlgqrlfc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    uint8_t t4 = gm_f_starts_with_6vvt01e(t2, t3);
    if (!t4) {
        gm_b16_8 t5 = gm_l0;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_snge4b9tsq8qc));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(7));
        uint8_t t7 = gm_f_starts_with_6vvt01e(t5, t6);
        t1 = t7;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_14sicke(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_14sicke F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_0lkule6();
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
inline __attribute__((aligned(4))) void gm_f_list_1qnmubu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1qnmubu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_n0agqu3();
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void gm_f_list_rdfjble(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rdfjble F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_v7c9k0s();
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_t69m56meqefs8 t1;
    memset((uint8_t *)&t1 + 4, 0, 4);
    memset((uint8_t *)&t1 + 84, 0, 4);
    gm_t88rcucg1b7uk t2 = gm_l0;
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t1 + 0, t2);
    memcpy((uint8_t *)&t1 + 8, ((uint8_t *)&F.l1), 64);
    gm_tl874t2atnlh3 t3 = gm_l2;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 72, t3);
    gm_tq7s98ihonkju t4 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3tttsr3hp1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
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
inline __attribute__((aligned(4))) double gm_f_percent_ie1duk3(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_percent_ie1duk3 F;
    uint64_t gm_l0; /* hit */
    uint64_t gm_l1; /* all */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    double t1;
    uint64_t t3 = gm_l1;
    uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
    if (!t4) goto ie2;
    double t6 = ((double)1e2);
    t1 = t6;
    goto ix2;
ie2:;
    uint64_t t8 = gm_l0;
    double t9 = (double)(t8);
    double t10 = (t9 * ((double)1e2));
    uint64_t t11 = gm_l1;
    double t12 = (double)(t11);
    double t13 = (t10 / t12);
    double t14 = t13;
    t1 = t14;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sort_by_lh24ei9(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_lh24ei9 F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = gm_l1;
        gm_f_introsort_se56bco(t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_6vvt01e F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t3 > t4);
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t12 = (uintptr_t)t11;
        if (t10 > t12 || t12 > (uintptr_t)t8) gm_trap_slice(t10, t12, t8, gm_srcc24pnihhgkug, 416, 12);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t9 + t10 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12 - t10));
        gm_b16_8 t14 = gm_l1;
        uint8_t t15 = (gm_eq0iqiubfm7e(((uint8_t *)&t13), ((uint8_t *)&t14)));
        uint8_t t16 = 0;
        F.ret = t15;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t4);
    gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t6);
    gm_t53at80pfdus3 t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t7 + 8), t2);
    gm_t88rcucg1b7uk t8 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t8;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_add_counts_hprtdd9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_block_u0oqu8p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_block_u0oqu8p F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_trcmmd12cefb1 gm_l5; /* st */
    gm_tl874t2atnlh3 gm_l6; /* span */
    uint32_t gm_l7; /* id */
    void * gm_l8; /* t */
    uint32_t gm_l9; /* id */
    GM_STACK_CHECK(&F, 527u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_ir0h3tc_2((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
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
                    gm_trcmmd12cefb1 t16 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 32));
                    gm_l5 = t16;
                    {
                        gm_toceffb697p0u t19 = gm_f_stmt_span_slh12dn(&(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l5)));
                        if (!((((uint8_t *)&t19))[12] != 0)) goto ie18;
                        gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&t19));
                        gm_l6 = t20;
                        {
                            void * t23 = gm_l0;
                            gm_tl874t2atnlh3 t24 = gm_l6;
                            gm_t9805qtonbsag t25 = gm_f_point_8frh2ab((void *)NULL, cx, NULL, t23, t24);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            if (!((((uint8_t *)&t25))[4] != 0)) goto ie22;
                            uint32_t t26 = GM_LD(uint32_t, ((uint8_t *)&t25));
                            gm_l7 = t26;
                            {
                                void * t28 = (void *)((uint8_t *)&F.l2);
                                void * t29 = gm_l0;
                                void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
                                uint32_t t31 = gm_l7;
                                gm_tl874t2atnlh3 t32 = gm_l6;
                                gm_trcmmd12cefb1 t33;
                                gm_f_cover_stmt_bp29hrl((void *)&t33, (void *)NULL, cx, NULL, t30, t31, t32);
                                if (gm_unw.kind) {
                                    goto gm_ll9;
                                }
                                gm_tftb1odc739t5 t34 = gm_f_push_d6c76ud_2(t28, &t33);
                                if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srcgr7vbgb5cqmg, 137, 39);
                                gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
                            }
                            goto ix22;
ie22:;
ix22:;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    {
                        void * t36 = (void *)((uint8_t *)&F.l2);
                        gm_tftb1odc739t5 t37 = gm_f_push_d6c76ud_2(t36, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l5)));
                        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_srcgr7vbgb5cqmg, 139, 9);
                        gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 135, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t40 = gm_l4;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l4 = t43;
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
        void * t45 = gm_l1;
        void * t46 = GM_LD(void *, (((uint8_t *)t45) + 16));
        if (!(GM_LD(void *, ((uint8_t *)&t46)) != 0)) goto ie44;
        void * t47 = GM_LD(void *, ((uint8_t *)&t46));
        gm_l8 = t47;
        {
            void * t50 = gm_l0;
            void * t51 = gm_l8;
            gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t51) + 72));
            gm_t9805qtonbsag t53 = gm_f_point_8frh2ab((void *)NULL, cx, NULL, t50, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t53))[4] != 0)) goto ie49;
            uint32_t t54 = GM_LD(uint32_t, ((uint8_t *)&t53));
            gm_l9 = t54;
            {
                void * t56 = (void *)((uint8_t *)&F.l2);
                void * t57 = gm_l0;
                void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
                uint32_t t59 = gm_l9;
                void * t60 = gm_l8;
                gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t60) + 72));
                gm_trcmmd12cefb1 t62;
                gm_f_cover_stmt_bp29hrl((void *)&t62, (void *)NULL, cx, NULL, t58, t59, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t63 = gm_f_push_d6c76ud_2(t56, &t62);
                if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srcgr7vbgb5cqmg, 142, 37);
                gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
            }
            goto ix49;
ie49:;
ix49:;
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        void * t65 = gm_l1;
        gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)t65) + 0), t66);
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
inline __attribute__((aligned(4))) uint8_t gm_f_bytes_less_r33ee4s(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bytes_less_r33ee4s F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 252u);
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
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t t5 = (t3 < t4 ? t3 : t4);
            gm_l2 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l3;
                uint64_t t8 = gm_l2;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l3;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l4 = t15;
                    {
                        uint64_t t18 = gm_l4;
                        uint64_t t19 = (uintptr_t)t18;
                        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_srcgr7vbgb5cqmg, 198, 12);
                        uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t19 * 1));
                        uint64_t t22 = gm_l4;
                        uint64_t t23 = (uintptr_t)t22;
                        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcgr7vbgb5cqmg, 198, 24);
                        uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t23 * 1));
                        uint8_t t26 = (!(t21 == t25));
                        if (!t26) goto ie17;
                        {
                            uint64_t t28 = gm_l4;
                            uint64_t t29 = (uintptr_t)t28;
                            uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t29 >= (uintptr_t)(t30)) gm_trap_index(t29, t30, gm_srcgr7vbgb5cqmg, 198, 38);
                            uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t29 * 1));
                            uint64_t t32 = gm_l4;
                            uint64_t t33 = (uintptr_t)t32;
                            uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_srcgr7vbgb5cqmg, 198, 45);
                            uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t33 * 1));
                            uint8_t t36 = (t31 < t35);
                            uint8_t t37 = 0;
                            F.ret = t36;
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 197, 5);
me13:;
                }
lc6:;
                {
                    uint64_t t39 = gm_l3;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    uint64_t t42 = r40;
                    gm_l3 = t42;
                }
            }
lb6:;
        }
    }
    {
        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t45 = (t43 < t44);
        uint8_t t46 = 0;
        F.ret = t45;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_chars_232hkk6(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_counted_fns_j3p4pgn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_counted_fns_j3p4pgn F;
    void * gm_l0; /* s */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l5; /* b */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    void * gm_l10; /* sig */
    gm_b16_8 gm_l11; /* name */
    uint8_t gm_l12; /* generated */
    uint8_t gm_l13; /* in_test */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_tl874t2atnlh3 gm_l16; /* t */
    uint8_t gm_l17; /* has_body */
    GM_STACK_CHECK(&F, 811u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b0_1 t2;
    }
    {
        gm_f_list_7s4om10((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t4 = gm_l0;
            uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 344) + 0) + 8);
            gm_l2 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l3;
                uint64_t t8 = gm_l2;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l3;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l4 = t15;
                    {
                        uint64_t t18 = gm_l4;
                        uint64_t t19 = (uintptr_t)t18;
                        void * t20 = gm_l0;
                        uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 344) + 0) + 8);
                        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srcgr7vbgb5cqmg, 84, 24);
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 344) + 0)) + t19 * 136) + 0));
                        uint8_t t23 = gm_f_is_test_1bbodje(t22);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        uint8_t t24 = (uint8_t)!t23;
                        if (!t24) goto ie17;
                        {
                            goto lc6;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    {
                        void * t26 = gm_l0;
                        uint64_t t27 = gm_l4;
                        uint32_t t28 = (uint32_t)t27;
                        gm_tdgfduqjheqdm t29;
                        gm_f_def_of_m512sbu((void *)&t29, t26, t28);
                        void * t30 = GM_LD(void *, (((uint8_t *)&t29) + 40));
                        void * t31;
                        if ((GM_LD(void *, ((uint8_t *)&t30)) != 0)) {
                            void * t32 = GM_LD(void *, (((uint8_t *)&t30) + 0));
                            t31 = t32;
                        } else {
                            goto lc6;
                        }
                        gm_l5 = t31;
                    }
                    {
                        void * t33 = (void *)((uint8_t *)&F.l1);
                        void * t34 = gm_l5;
                        gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t34) + 72));
                        gm_tftb1odc739t5 t36 = gm_f_push_0mk7udh(t33, GM_CONV(gm_cseahvg8v7p0vs, t35));
                        if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srcgr7vbgb5cqmg, 86, 9);
                        gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 83, 5);
me13:;
                }
lc6:;
                {
                    uint64_t t39 = gm_l3;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    uint64_t t42 = r40;
                    gm_l3 = t42;
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
        gm_f_list_s83jn2q((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            void * t44 = gm_l0;
            uint64_t t45 = GM_LD(uintptr_t, ((((uint8_t *)t44) + 344) + 0) + 8);
            gm_l7 = t45;
        }
        {
            for (;;) {
                uint64_t t47 = gm_l8;
                uint64_t t48 = gm_l7;
                uint8_t t49 = (t47 < t48);
                if (!t49) goto lb46;
                {
                    uint64_t t52 = gm_l8;
                    uint64_t t55 = GM_LD(uint64_t, ((uint8_t *)&t52));
                    gm_l9 = t55;
                    {
                        uint64_t t57 = gm_l9;
                        uint64_t t58 = (uintptr_t)t57;
                        void * t59 = gm_l0;
                        uint64_t t60 = GM_LD(uintptr_t, ((((uint8_t *)t59) + 344) + 0) + 8);
                        if (t58 >= (uintptr_t)(t60)) gm_trap_index(t58, t60, gm_srcgr7vbgb5cqmg, 90, 17);
                        void * t61 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t59) + 344) + 0)) + t58 * 136);
                        gm_l10 = t61;
                    }
                    {
                        void * t62 = gm_l10;
                        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)t62) + 0));
                        gm_l11 = t63;
                    }
                    {
                        uint8_t t64 = 0;
                        uint8_t t65 = 0;
                        gm_b16_8 t66 = gm_l11;
                        gm_b16_8 t67;
                        GM_ST(void *, (uint8_t *)&t67, (void *)(gm_s1f440st4mnkp));
                        GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(1));
                        uint8_t t68 = gm_f_starts_with_6vvt01e(t66, t67);
                        if (t68) {
                            uint8_t t69 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l11)), gm_s93p2i934ld6p, 4) == 0));
                            t65 = t69;
                        } else t65 = 0;
                        if (t65) {
                            uint8_t t70 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l11)), gm_ss7qhfi2s00na, 10) == 0));
                            t64 = t70;
                        } else t64 = 0;
                        gm_l12 = t64;
                    }
                    {
                        gm_l13 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
                            gm_l14 = t72;
                        }
                        {
                            gm_l15 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t74 = gm_l15;
                                uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                uint8_t t76 = (t74 < t75);
                                if (!t76) goto lb73;
                                {
                                    uint64_t t79 = gm_l15;
                                    uint64_t t80 = (uintptr_t)t79;
                                    uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                    gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t80 * 12));
                                    gm_l16 = t84;
                                    {
                                        uint32_t t87 = GM_LD(uint32_t, (((uint8_t *)&gm_l16) + 0));
                                        void * t88 = gm_l10;
                                        uint32_t t89 = GM_LD(uint32_t, ((((uint8_t *)t88) + 16) + 0));
                                        uint8_t t90 = ((t87 == t89));
                                        if (!t90) goto ie86;
                                        uint32_t t91 = GM_LD(uint32_t, (((uint8_t *)&gm_l16) + 4));
                                        void * t92 = gm_l10;
                                        uint32_t t93 = GM_LD(uint32_t, ((((uint8_t *)t92) + 16) + 4));
                                        uint8_t t94 = (t91 <= t93);
                                        if (!t94) goto ie86;
                                        void * t95 = gm_l10;
                                        uint32_t t96 = GM_LD(uint32_t, ((((uint8_t *)t95) + 16) + 8));
                                        uint32_t t97 = GM_LD(uint32_t, (((uint8_t *)&gm_l16) + 8));
                                        uint8_t t98 = (t96 <= t97);
                                        if (!t98) goto ie86;
                                        {
                                            gm_l13 = ((uint8_t)1);
                                        }
                                        goto ix86;
ie86:;
ix86:;
                                    }
                                    goto me82;
mn83:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 94, 9);
me82:;
                                }
lc73:;
                                {
                                    uint64_t t101 = gm_l15;
                                    uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                                    uint64_t t104 = r102;
                                    gm_l15 = t104;
                                }
                            }
lb73:;
                        }
                    }
                    {
                        void * t105 = gm_l0;
                        uint64_t t106 = gm_l9;
                        uint32_t t107 = (uint32_t)t106;
                        gm_tdgfduqjheqdm t108;
                        gm_f_def_of_m512sbu((void *)&t108, t105, t107);
                        void * t109 = GM_LD(void *, (((uint8_t *)&t108) + 40));
                        uint8_t t110;
                        if ((GM_LD(void *, ((uint8_t *)&t109)) != 0)) goto mn112;
                        t110 = ((uint8_t)0);
                        goto me111;
mn112:;
                        t110 = ((uint8_t)1);
                        goto me111;
mn113:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 97, 21);
me111:;
                        gm_l17 = t110;
                    }
                    {
                        void * t114 = (void *)((uint8_t *)&F.l6);
                        uint8_t t115 = 0;
                        uint8_t t116 = 0;
                        uint8_t t117 = 0;
                        uint8_t t118 = gm_l17;
                        if (t118) {
                            uint8_t t119 = gm_l12;
                            uint8_t t120 = (uint8_t)!t119;
                            t117 = t120;
                        } else t117 = 0;
                        if (t117) {
                            gm_b16_8 t121 = gm_l11;
                            uint8_t t122 = gm_f_is_test_1bbodje(t121);
                            if (gm_unw.kind) {
                                goto gm_ll50;
                            }
                            uint8_t t123 = (uint8_t)!t122;
                            t116 = t123;
                        } else t116 = 0;
                        if (t116) {
                            uint8_t t124 = gm_l13;
                            uint8_t t125 = (uint8_t)!t124;
                            t115 = t125;
                        } else t115 = 0;
                        gm_tftb1odc739t5 t126 = gm_f_push_vd1290p(t114, t115);
                        if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 1, gm_srcgr7vbgb5cqmg, 98, 9);
                        gm_b0_1 t127 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 1));
                    }
                    goto me53;
mn54:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 89, 5);
me53:;
                }
lc46:;
                {
                    uint64_t t129 = gm_l8;
                    uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                    uint64_t t132 = r130;
                    gm_l8 = t132;
                }
            }
gm_ll50:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb46; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc46; }
            }
            goto gm_lad;
lb46:;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l6), 40);
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
inline __attribute__((aligned(4))) void gm_f_cover_stmt_bp29hrl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_cover_stmt_bp29hrl F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tt8o5kebou62r t5;
        memset((uint8_t *)&t5 + 12, 0, 4);
        uint32_t t6 = gm_l1;
        gm_tvdg94bk7aa22 t7;
        GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t7 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)81ULL));
        GM_ST(uint32_t, ((uint8_t *)&t7 + 4), t6);
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t5 + 0, t7);
        gm_b16_8 t8 = gm_f_empty_slice_g3tddrb_2();
        GM_ST(gm_b16_8, (uint8_t *)&t5 + 16, t8);
        gm_teti411eujvf4 t9;
        GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t9 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t9 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)33ULL));
        GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t9 + 8), t5);
        gm_tl874t2atnlh3 t10 = gm_l2;
        void * t11 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t4, &t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 24, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 0, ((uint8_t)4ULL));
        GM_ST(void *, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 8, t11);
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
__attribute__((aligned(4))) uint8_t gm_f_coverage_report_hrhbf22(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, const void * a4) {
    struct gm_fr_coverage_report_hrhbf22 F;
    void * gm_l0; /* s */
    void * gm_l1; /* scope */
    void * gm_l2; /* map */
    gm_b16_8 gm_l3; /* counts */
    gm_th2lqbo77pvsc gm_l4; /* lcov */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    gm_tcc1rq4n1avb2 gm_l10; /* p */
    gm_b16_8 gm_l11; /* given */
    gm_b16_8 gm_l12; /* c */
    gm_b16_8 gm_l13; /* path */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_t5hdnnftkup6m gm_l17; /* h */
    uint64_t gm_l18; /* n */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* i */
    gm_t5hdnnftkup6m gm_l23; /* h */
    uint64_t gm_l25; /* width */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    gm_t2lqu8jsj511d gm_l28; /* f */
    gm_b16_8 gm_l29; /* path */
    gm_b16_8 gm_l30; /* rel */
    uint64_t gm_l31; /* total_hit */
    uint64_t gm_l32; /* total_all */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* i */
    gm_t2lqu8jsj511d gm_l36; /* f */
    uint64_t gm_l37; /* hit */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_t5hdnnftkup6m gm_l40; /* h */
    uint64_t gm_l41; /* n */
    gm_b16_8 gm_l42; /* out */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_t2lqu8jsj511d gm_l46; /* f */
    uint64_t gm_l47; /* hit */
    gm_b16_8 gm_l48; /* <tmp> */
    uint64_t gm_l49; /* <tmp> */
    gm_t5hdnnftkup6m gm_l50; /* h */
    gm_toe5fmr3vvf4d gm_l51; /* e */
    GM_STACK_CHECK(&F, 3562u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_hash_map_tl7apid((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_14sicke((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l2;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 0));
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
            gm_l7 = t5;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l8;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l8;
                    uint64_t t13 = gm_l8;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_tcc1rq4n1avb2 t16 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t14 * 8));
                    gm_t2aqa0c3djlco t17;
                    GM_ST(uint64_t, ((uint8_t *)&t17 + 0), t12);
                    GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t17 + 8), t16);
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&t17) + 0));
                    gm_l9 = t20;
                    gm_tcc1rq4n1avb2 t21 = GM_LD(gm_tcc1rq4n1avb2, (((uint8_t *)&t17) + 8));
                    gm_l10 = t21;
                    {
                        void * t23 = (void *)((uint8_t *)&F.l5);
                        uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 0));
                        gm_th2lqbo77pvsc t25;
                        gm_f_get_gnmase9((void *)&t25, t23, t24);
                        gm_b16_8 t26;
                        if (((((uint8_t *)&t25))[16] != 0)) {
                            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 0));
                            t26 = t27;
                        } else {
                            {
                                void * t29 = gm_l0;
                                void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
                                void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
                                uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 0));
                                void * t33 = gm_f_file_o15fqku(t31, t32);
                                gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t33) + 0));
                                gm_l11 = t34;
                            }
                            {
                                gm_b16_8 t35 = gm_l11;
                                gm_th2lqbo77pvsc t36;
                                gm_f_canonicalize_epo7cln((void *)&t36, (void *)NULL, cx, NULL, t35);
                                if (gm_unw.kind) {
                                    goto gm_ll10;
                                }
                                gm_b16_8 t37;
                                if (((((uint8_t *)&t36))[16] != 0)) {
                                    gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 0));
                                    t37 = t38;
                                } else {
                                    gm_b16_8 t39 = gm_l11;
                                    t37 = t39;
                                }
                                gm_l12 = t37;
                            }
                            {
                                void * t40 = (void *)((uint8_t *)&F.l5);
                                uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 0));
                                gm_b16_8 t42 = gm_l12;
                                gm_tftb1odc739t5 t43 = gm_f_put_po09vud(t40, t41, t42);
                                if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcgr7vbgb5cqmg, 229, 13);
                                gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
                            }
                            gm_b16_8 t45 = gm_l12;
                            gm_b16_8 t46 = t45;
                            t26 = t46;
                        }
                        gm_l13 = t26;
                    }
                    {
                        void * t47 = (void *)((uint8_t *)&F.l6);
                        gm_t5hdnnftkup6m t48;
                        gm_b16_8 t49 = gm_l13;
                        GM_ST(gm_b16_8, (uint8_t *)&t48 + 0, t49);
                        uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 4));
                        GM_ST(uint32_t, (uint8_t *)&t48 + 16, t50);
                        uint32_t t51;
                        uint64_t t53 = gm_l9;
                        uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        uint8_t t55 = (t53 < t54);
                        if (!t55) goto ie52;
                        uint64_t t57 = gm_l9;
                        uint64_t t58 = (uintptr_t)t57;
                        uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t58 >= (uintptr_t)(t59)) gm_trap_index(t58, t59, gm_srcgr7vbgb5cqmg, 232, 74);
                        uint32_t t60 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t58 * 4));
                        uint32_t t61 = t60;
                        t51 = t61;
                        goto ix52;
ie52:;
                        uint32_t t63 = ((uint32_t)0ULL);
                        t51 = t63;
ix52:;
                        GM_ST(uint32_t, (uint8_t *)&t48 + 20, t51);
                        gm_tftb1odc739t5 t64 = gm_f_push_8cklqk5(t47, &t48);
                        if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_srcgr7vbgb5cqmg, 232, 9);
                        gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 225, 5);
me18:;
                }
lc6:;
                {
                    uint64_t t67 = gm_l8;
                    uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                    uint64_t t70 = r68;
                    gm_l8 = t70;
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
        gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_closure t72 = ((gm_closure){((void *)gm_f_fn_nqs6or3), NULL, cx});
        gm_f_sort_by_lh24ei9(t71, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_14sicke((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_l15 = t74;
        }
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t76 = gm_l16;
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint8_t t78 = (t76 < t77);
                if (!t78) goto lb75;
                {
                    uint64_t t81 = gm_l16;
                    uint64_t t82 = (uintptr_t)t81;
                    uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_t5hdnnftkup6m t86 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t82 * 24));
                    gm_l17 = t86;
                    {
                        uint64_t t88 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                        gm_l18 = t88;
                    }
                    {
                        uint64_t t90 = gm_l18;
                        uint8_t t91 = (t90 > ((uint64_t)0ULL));
                        if (!t91) goto ie89;
                        uint64_t t92 = gm_l18;
                        uint64_t r93; bool o94 = __builtin_sub_overflow(t92, ((uint64_t)1ULL), &r93);
                        if (o94) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 242, 34);
                        uint64_t t95 = r93;
                        uint64_t t96 = (uintptr_t)t95;
                        uint64_t t97 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                        if (t96 >= (uintptr_t)(t97)) gm_trap_index(t96, t97, gm_srcgr7vbgb5cqmg, 242, 22);
                        gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)) + t96 * 24) + 0));
                        gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 0));
                        uint8_t t100 = (gm_eq0iqiubfm7e(((uint8_t *)&t98), ((uint8_t *)&t99)));
                        if (!t100) goto ie89;
                        uint64_t t101 = gm_l18;
                        uint64_t r102; bool o103 = __builtin_sub_overflow(t101, ((uint64_t)1ULL), &r102);
                        if (o103) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 242, 72);
                        uint64_t t104 = r102;
                        uint64_t t105 = (uintptr_t)t104;
                        uint64_t t106 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                        if (t105 >= (uintptr_t)(t106)) gm_trap_index(t105, t106, gm_srcgr7vbgb5cqmg, 242, 60);
                        uint32_t t107 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)) + t105 * 24) + 16));
                        uint32_t t108 = GM_LD(uint32_t, (((uint8_t *)&gm_l17) + 16));
                        uint8_t t109 = ((t107 == t108));
                        if (!t109) goto ie89;
                        {
                            uint64_t t111 = gm_l18;
                            uint64_t r112; bool o113 = __builtin_sub_overflow(t111, ((uint64_t)1ULL), &r112);
                            if (o113) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 243, 25);
                            uint64_t t114 = r112;
                            uint64_t t115 = (uintptr_t)t114;
                            uint64_t t116 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                            if (t115 >= (uintptr_t)(t116)) gm_trap_index(t115, t116, gm_srcgr7vbgb5cqmg, 243, 13);
                            uint64_t t117 = gm_l18;
                            uint64_t r118; bool o119 = __builtin_sub_overflow(t117, ((uint64_t)1ULL), &r118);
                            if (o119) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 243, 51);
                            uint64_t t120 = r118;
                            uint64_t t121 = (uintptr_t)t120;
                            uint64_t t122 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                            if (t121 >= (uintptr_t)(t122)) gm_trap_index(t121, t122, gm_srcgr7vbgb5cqmg, 243, 39);
                            uint32_t t123 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)) + t121 * 24) + 20));
                            uint32_t t124 = GM_LD(uint32_t, (((uint8_t *)&gm_l17) + 20));
                            uint32_t t125 = (t123 > t124 ? t123 : t124);
                            GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)) + t115 * 24) + 20), t125);
                        }
                        goto ix89;
ie89:;
                        {
                            void * t127 = (void *)((uint8_t *)&F.l14);
                            gm_tftb1odc739t5 t128 = gm_f_push_8cklqk5(t127, &(*(gm_t5hdnnftkup6m *)((uint8_t *)&gm_l17)));
                            if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 1, gm_srcgr7vbgb5cqmg, 245, 13);
                            gm_b0_1 t129 = GM_LD(gm_b0_1, (((uint8_t *)&t128) + 1));
                        }
ix89:;
                    }
                    goto me84;
mn85:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 240, 5);
me84:;
                }
lc75:;
                {
                    uint64_t t131 = gm_l16;
                    uint64_t r132; bool o133 = __builtin_add_overflow(t131, ((uint64_t)1ULL), &r132);
                    uint64_t t134 = r132;
                    gm_l16 = t134;
                }
            }
lb75:;
        }
    }
    {
        gm_f_list_rdfjble((void *)&F.l19, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
            gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
            gm_l20 = t137;
        }
        {
            gm_l21 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t139 = gm_l21;
                uint64_t t140 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                uint8_t t141 = (t139 < t140);
                if (!t141) goto lb138;
                {
                    gm_t4c7ikvc472re t144;
                    uint64_t t145 = gm_l21;
                    GM_ST(uint64_t, (uint8_t *)&t144 + 0, t145);
                    uint64_t t146 = gm_l21;
                    uint64_t t147 = (uintptr_t)t146;
                    uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    memcpy((uint8_t *)&t144 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t147 * 24), 24);
                    uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)&t144) + 0));
                    gm_l22 = t151;
                    gm_l23 = (*(gm_t5hdnnftkup6m *)(((uint8_t *)&t144) + 8));
                    {
                        uint8_t t154 = 0;
                        uint64_t t155 = gm_l22;
                        uint8_t t156 = ((t155 == ((uint64_t)0ULL)));
                        if (!t156) {
                            uint64_t t157 = gm_l22;
                            uint64_t r158; bool o159 = __builtin_sub_overflow(t157, ((uint64_t)1ULL), &r158);
                            if (o159) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 251, 34);
                            uint64_t t160 = r158;
                            uint64_t t161 = (uintptr_t)t160;
                            uint64_t t162 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                            if (t161 >= (uintptr_t)(t162)) gm_trap_index(t161, t162, gm_srcgr7vbgb5cqmg, 251, 22);
                            gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)) + t161 * 24) + 0));
                            gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 0));
                            uint8_t t165 = (!gm_eq0iqiubfm7e(((uint8_t *)&t163), ((uint8_t *)&t164)));
                            t154 = t165;
                        } else t154 = 1;
                        if (!t154) goto ie153;
                        {
                            void * t167 = (void *)((uint8_t *)&F.l19);
                            uint64_t t168 = gm_l22;
                            uint64_t t169 = gm_l22;
                            uint64_t r170; bool o171 = __builtin_add_overflow(t169, ((uint64_t)1ULL), &r170);
                            if (o171) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 251, 85);
                            uint64_t t172 = r170;
                            gm_t2lqu8jsj511d t173;
                            GM_ST(uint64_t, ((uint8_t *)&t173 + 0), t168);
                            GM_ST(uint64_t, ((uint8_t *)&t173 + 8), t172);
                            gm_tftb1odc739t5 t174 = gm_f_push_vgae2dr(t167, t173);
                            if (((((uint8_t *)&t174))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t174) + 1, gm_srcgr7vbgb5cqmg, 251, 62);
                            gm_b0_1 t175 = GM_LD(gm_b0_1, (((uint8_t *)&t174) + 1));
                        }
                        goto ix153;
ie153:;
                        {
                            uint64_t t177 = GM_LD(uintptr_t, (((uint8_t *)&F.l19) + 0) + 8);
                            uint64_t r178; bool o179 = __builtin_sub_overflow(t177, ((uint64_t)1ULL), &r178);
                            if (o179) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 251, 115);
                            uint64_t t180 = r178;
                            uint64_t t181 = (uintptr_t)t180;
                            uint64_t t182 = GM_LD(uintptr_t, (((uint8_t *)&F.l19) + 0) + 8);
                            if (t181 >= (uintptr_t)(t182)) gm_trap_index(t181, t182, gm_srcgr7vbgb5cqmg, 251, 103);
                            uint64_t t183 = gm_l22;
                            uint64_t r184; bool o185 = __builtin_add_overflow(t183, ((uint64_t)1ULL), &r184);
                            if (o185) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 251, 141);
                            uint64_t t186 = r184;
                            GM_ST(uint64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l19) + 0)) + t181 * 16) + 8), t186);
                        }
ix153:;
                    }
                    goto me149;
mn150:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 250, 5);
me149:;
                }
lc138:;
                {
                    uint64_t t188 = gm_l21;
                    uint64_t r189; bool o190 = __builtin_add_overflow(t188, ((uint64_t)1ULL), &r189);
                    uint64_t t191 = r189;
                    gm_l21 = t191;
                }
            }
lb138:;
        }
    }
    {
        if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srcgr7vbgb5cqmg, 253, 5)) {
            goto gm_lad;
        }
    }
    {
        uint64_t t193 = GM_LD(uintptr_t, (((uint8_t *)&F.l19) + 0) + 8);
        uint8_t t194 = ((t193 == ((uint64_t)0ULL)));
        if (!t194) goto ie192;
        {
            if (gm_pf1m4b8q4e78(cx, gm_sl6eotv49v54f, 37, gm_srcgr7vbgb5cqmg, 255, 9)) {
                goto gm_lad;
            }
        }
        goto ix192;
ie192:;
        {
            gm_f_list_epvducg((void *)&F.l24, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t197;
            GM_ST(void *, (uint8_t *)&t197, (void *)(gm_suc2pvoh9ktdi));
            GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)(5));
            uint64_t t198 = GM_LD(uintptr_t, ((uint8_t *)&t197) + 8);
            gm_l25 = t198;
        }
        {
            {
                gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                gm_l26 = t200;
            }
            {
                gm_l27 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t202 = gm_l27;
                    uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                    uint8_t t204 = (t202 < t203);
                    if (!t204) goto lb201;
                    {
                        uint64_t t207 = gm_l27;
                        uint64_t t208 = (uintptr_t)t207;
                        uint64_t t209 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                        gm_t2lqu8jsj511d t212 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t208 * 16));
                        gm_l28 = t212;
                        {
                            uint64_t t214 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 0));
                            uint64_t t215 = (uintptr_t)t214;
                            uint64_t t216 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                            if (t215 >= (uintptr_t)(t216)) gm_trap_index(t215, t216, gm_srcgr7vbgb5cqmg, 260, 21);
                            gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)) + t215 * 24) + 0));
                            gm_l29 = t217;
                        }
                        {
                            gm_b16_8 t218 = gm_l29;
                            gm_l30 = t218;
                        }
                        {
                            gm_b16_8 t220 = gm_l29;
                            void * t221 = gm_l1;
                            gm_b16_8 t222 = GM_LD(gm_b16_8, (((uint8_t *)t221) + 0));
                            uint8_t t223 = gm_f_under_rb6c23o(t220, t222);
                            if (!t223) goto ie219;
                            uint64_t t224 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                            void * t225 = gm_l1;
                            uint64_t t226 = GM_LD(uintptr_t, (((uint8_t *)t225) + 0) + 8);
                            uint8_t t227 = (t224 > t226);
                            if (!t227) goto ie219;
                            {
                                uint64_t t229 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                                void * t230 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)));
                                void * t231 = gm_l1;
                                uint64_t t232 = GM_LD(uintptr_t, (((uint8_t *)t231) + 0) + 8);
                                uint64_t t233 = (uintptr_t)t232;
                                if (t233 > (uintptr_t)t229 || (uintptr_t)t229 > (uintptr_t)t229) gm_trap_slice(t233, (uintptr_t)t229, t229, gm_srcgr7vbgb5cqmg, 263, 23);
                                gm_b16_8 t234;
                                GM_ST(void *, (uint8_t *)&t234, (void *)(((uint8_t *)t230 + t233 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)((uintptr_t)t229 - t233));
                                gm_l30 = t234;
                            }
                            {
                                gm_b16_8 t236 = gm_l30;
                                gm_b16_8 t237;
                                GM_ST(void *, (uint8_t *)&t237, (void *)(gm_st3ja0nqi37bv));
                                GM_ST(uintptr_t, (uint8_t *)&t237 + 8, (uintptr_t)(1));
                                uint8_t t238 = gm_f_starts_with_6vvt01e(t236, t237);
                                if (!t238) goto ie235;
                                {
                                    uint64_t t240 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                                    void * t241 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)));
                                    uint64_t t242 = (uintptr_t)((uint64_t)1ULL);
                                    if (t242 > (uintptr_t)t240 || (uintptr_t)t240 > (uintptr_t)t240) gm_trap_slice(t242, (uintptr_t)t240, t240, gm_srcgr7vbgb5cqmg, 264, 49);
                                    gm_b16_8 t243;
                                    GM_ST(void *, (uint8_t *)&t243, (void *)(((uint8_t *)t241 + t242 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t243 + 8, (uintptr_t)((uintptr_t)t240 - t242));
                                    gm_l30 = t243;
                                }
                                goto ix235;
ie235:;
ix235:;
                            }
                            goto ix219;
ie219:;
ix219:;
                        }
                        {
                            void * t244 = (void *)((uint8_t *)&F.l24);
                            gm_b16_8 t245 = gm_l30;
                            gm_tftb1odc739t5 t246 = gm_f_push_3ef746b(t244, t245);
                            if (((((uint8_t *)&t246))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t246) + 1, gm_srcgr7vbgb5cqmg, 266, 13);
                            gm_b0_1 t247 = GM_LD(gm_b0_1, (((uint8_t *)&t246) + 1));
                        }
                        {
                            uint64_t t248 = gm_l25;
                            gm_b16_8 t249 = gm_l30;
                            uint64_t t250 = gm_f_chars_232hkk6(t249);
                            uint64_t t251 = (t248 > t250 ? t248 : t250);
                            gm_l25 = t251;
                        }
                        goto me210;
mn211:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 259, 9);
me210:;
                    }
lc201:;
                    {
                        uint64_t t253 = gm_l27;
                        uint64_t r254; bool o255 = __builtin_add_overflow(t253, ((uint64_t)1ULL), &r254);
                        uint64_t t256 = r254;
                        gm_l27 = t256;
                    }
                }
lb201:;
            }
        }
        {
            if (gm_pf1m4b8q4e78(cx, gm_sl1f0va0l61bh, 22, gm_srcgr7vbgb5cqmg, 269, 9)) {
                goto gm_lad;
            }
        }
        {
            gm_l31 = ((uint64_t)0ULL);
        }
        {
            gm_l32 = ((uint64_t)0ULL);
        }
        {
            {
                gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                gm_b16_8 t259 = GM_LD(gm_b16_8, ((uint8_t *)&t258));
                gm_l33 = t259;
            }
            {
                gm_l34 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t261 = gm_l34;
                    uint64_t t262 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                    uint8_t t263 = (t261 < t262);
                    if (!t263) goto lb260;
                    {
                        gm_t5o7duvovobu1 t266;
                        uint64_t t267 = gm_l34;
                        GM_ST(uint64_t, (uint8_t *)&t266 + 0, t267);
                        uint64_t t268 = gm_l34;
                        uint64_t t269 = (uintptr_t)t268;
                        uint64_t t270 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                        gm_t2lqu8jsj511d t271 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t269 * 16));
                        GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&t266 + 8, t271);
                        uint64_t t274 = GM_LD(uint64_t, (((uint8_t *)&t266) + 0));
                        gm_l35 = t274;
                        gm_t2lqu8jsj511d t275 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)&t266) + 8));
                        gm_l36 = t275;
                        {
                            gm_l37 = ((uint64_t)0ULL);
                        }
                        {
                            {
                                uint64_t t278 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                                void * t279 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)));
                                uint64_t t280 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 0));
                                uint64_t t281 = (uintptr_t)t280;
                                uint64_t t282 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                                uint64_t t283 = (uintptr_t)t282;
                                if (t281 > t283 || t283 > (uintptr_t)t278) gm_trap_slice(t281, t283, t278, gm_srcgr7vbgb5cqmg, 274, 22);
                                gm_b16_8 t284;
                                GM_ST(void *, (uint8_t *)&t284, (void *)(((uint8_t *)t279 + t281 * 24)));
                                GM_ST(uintptr_t, (uint8_t *)&t284 + 8, (uintptr_t)(t283 - t281));
                                gm_l38 = t284;
                            }
                            {
                                gm_l39 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t286 = gm_l39;
                                    uint64_t t287 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                                    uint8_t t288 = (t286 < t287);
                                    if (!t288) goto lb285;
                                    {
                                        uint64_t t291 = gm_l39;
                                        uint64_t t292 = (uintptr_t)t291;
                                        uint64_t t293 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                                        gm_t5hdnnftkup6m t296 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t292 * 24));
                                        gm_l40 = t296;
                                        {
                                            uint32_t t299 = GM_LD(uint32_t, (((uint8_t *)&gm_l40) + 20));
                                            uint8_t t300 = (t299 > ((uint32_t)0ULL));
                                            if (!t300) goto ie298;
                                            {
                                                uint64_t t302 = gm_l37;
                                                uint64_t r303; bool o304 = __builtin_add_overflow(t302, ((uint64_t)1ULL), &r303);
                                                if (o304) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 275, 33);
                                                uint64_t t305 = r303;
                                                gm_l37 = t305;
                                            }
                                            goto ix298;
ie298:;
ix298:;
                                        }
                                        goto me294;
mn295:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 274, 13);
me294:;
                                    }
lc285:;
                                    {
                                        uint64_t t307 = gm_l39;
                                        uint64_t r308; bool o309 = __builtin_add_overflow(t307, ((uint64_t)1ULL), &r308);
                                        uint64_t t310 = r308;
                                        gm_l39 = t310;
                                    }
                                }
lb285:;
                            }
                        }
                        {
                            uint64_t t311 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                            uint64_t t312 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 0));
                            uint64_t r313; bool o314 = __builtin_sub_overflow(t311, t312, &r313);
                            if (o314) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 277, 18);
                            uint64_t t315 = r313;
                            gm_l41 = t315;
                        }
                        {
                            uint64_t t316 = gm_l31;
                            uint64_t t317 = gm_l37;
                            uint64_t r318; bool o319 = __builtin_add_overflow(t316, t317, &r318);
                            if (o319) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 278, 13);
                            uint64_t t320 = r318;
                            gm_l31 = t320;
                        }
                        {
                            uint64_t t321 = gm_l32;
                            uint64_t t322 = gm_l41;
                            uint64_t r323; bool o324 = __builtin_add_overflow(t321, t322, &r323);
                            if (o324) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 279, 13);
                            uint64_t t325 = r323;
                            gm_l32 = t325;
                        }
                        {
                            uint64_t t326 = gm_l35;
                            uint64_t t327 = (uintptr_t)t326;
                            uint64_t t328 = GM_LD(uintptr_t, (((uint8_t *)&F.l24) + 0) + 8);
                            if (t327 >= (uintptr_t)(t328)) gm_trap_index(t327, t328, gm_srcgr7vbgb5cqmg, 280, 52);
                            gm_b16_8 t329 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l24) + 0)) + t327 * 16));
                            uint64_t t330 = gm_l25;
                            gm_b16_8 t331 = gm_f_pad_o3a3nuu((void *)NULL, cx, NULL, t329, t330);
                            if (gm_unw.kind) {
                                goto gm_ll264;
                            }
                            uint64_t t332 = gm_l37;
                            uint64_t t333 = gm_l41;
                            uint64_t t334 = gm_l37;
                            uint64_t t335 = gm_l41;
                            double t336 = gm_f_percent_ie1duk3(t334, t335);
                            gm_tidu2er0d7h95 t337;
                            GM_ST(double, ((uint8_t *)&t337 + 0), t336);
                            gm_tabb710d9mq2e t338;
                            gm_f_format_01jvudh((void *)&t338, (void *)NULL, cx, NULL, t337);
                            if (gm_unw.kind) {
                                goto gm_ll264;
                            }
                            if (((((uint8_t *)&t338))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t338) + 8, gm_srcgr7vbgb5cqmg, 280, 84);
                            gm_b16_8 t339 = GM_LD(gm_b16_8, (((uint8_t *)&t338) + 8));
                            if (gm_pf1m4b8q4e78(cx, gm_sgankvlme129s, 2, gm_srcgr7vbgb5cqmg, 280, 13)) {
                                goto gm_ll264;
                            }
                            if ((GM_LD(uintptr_t, ((uint8_t *)&t331) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t331)), GM_LD(uintptr_t, ((uint8_t *)&t331) + 8), gm_srcgr7vbgb5cqmg, 280, 13))) {
                                goto gm_ll264;
                            }
                            if (gm_pf1m4b8q4e78(cx, gm_s3p0rbs9hc3lg, 1, gm_srcgr7vbgb5cqmg, 280, 13)) {
                                goto gm_ll264;
                            }
                            gm_buf b340 = {0};
                            gm_format_spec(&b340, &gm_ty6groq37ram, ((uint8_t *)&t332), &(gm_spec){32, 2, 0, 0, 0, 0, 5u, -1});
                            if (b340.len != 0) {
                                gm_b16_8 t341;
                                GM_ST(void *, (uint8_t *)&t341, (void *)(b340.data));
                                GM_ST(uintptr_t, (uint8_t *)&t341 + 8, (uintptr_t)(b340.len));
                                gm_pfmaojsuds6a(cx, t341, gm_srcgr7vbgb5cqmg, 280, 13);
                                if (gm_unw.kind) {
                                    goto gm_ll264;
                                }
                            }
                            gm_buf_free(&b340);
                            if (gm_pf1m4b8q4e78(cx, gm_st3ja0nqi37bv, 1, gm_srcgr7vbgb5cqmg, 280, 13)) {
                                goto gm_ll264;
                            }
                            gm_buf b342 = {0};
                            gm_format_spec(&b342, &gm_ty6groq37ram, ((uint8_t *)&t333), &(gm_spec){32, 1, 0, 0, 0, 0, 5u, -1});
                            if (b342.len != 0) {
                                gm_b16_8 t343;
                                GM_ST(void *, (uint8_t *)&t343, (void *)(b342.data));
                                GM_ST(uintptr_t, (uint8_t *)&t343 + 8, (uintptr_t)(b342.len));
                                gm_pfmaojsuds6a(cx, t343, gm_srcgr7vbgb5cqmg, 280, 13);
                                if (gm_unw.kind) {
                                    goto gm_ll264;
                                }
                            }
                            gm_buf_free(&b342);
                            if (gm_pf1m4b8q4e78(cx, gm_s3p0rbs9hc3lg, 1, gm_srcgr7vbgb5cqmg, 280, 13)) {
                                goto gm_ll264;
                            }
                            gm_buf b344 = {0};
                            gm_format_spec(&b344, &gm_tybihgc69jv3, ((uint8_t *)&t339), &(gm_spec){32, 2, 0, 0, 0, 0, 6u, -1});
                            if (b344.len != 0) {
                                gm_b16_8 t345;
                                GM_ST(void *, (uint8_t *)&t345, (void *)(b344.data));
                                GM_ST(uintptr_t, (uint8_t *)&t345 + 8, (uintptr_t)(b344.len));
                                gm_pfmaojsuds6a(cx, t345, gm_srcgr7vbgb5cqmg, 280, 13);
                                if (gm_unw.kind) {
                                    goto gm_ll264;
                                }
                            }
                            gm_buf_free(&b344);
                            if (gm_pf1m4b8q4e78(cx, gm_sofs2uc27fjvk, 2, gm_srcgr7vbgb5cqmg, 280, 13)) {
                                goto gm_ll264;
                            }
                        }
                        goto me272;
mn273:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 272, 9);
me272:;
                    }
lc260:;
                    {
                        uint64_t t347 = gm_l34;
                        uint64_t r348; bool o349 = __builtin_add_overflow(t347, ((uint64_t)1ULL), &r348);
                        uint64_t t350 = r348;
                        gm_l34 = t350;
                    }
                }
gm_ll264:;
                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb260; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc260; }
                }
                goto gm_lad;
lb260:;
            }
        }
        {
            gm_b16_8 t351;
            GM_ST(void *, (uint8_t *)&t351, (void *)(gm_suc2pvoh9ktdi));
            GM_ST(uintptr_t, (uint8_t *)&t351 + 8, (uintptr_t)(5));
            uint64_t t352 = gm_l25;
            gm_b16_8 t353 = gm_f_pad_o3a3nuu((void *)NULL, cx, NULL, t351, t352);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t354 = gm_l31;
            uint64_t t355 = gm_l32;
            uint64_t t356 = gm_l31;
            uint64_t t357 = gm_l32;
            double t358 = gm_f_percent_ie1duk3(t356, t357);
            gm_tidu2er0d7h95 t359;
            GM_ST(double, ((uint8_t *)&t359 + 0), t358);
            gm_tabb710d9mq2e t360;
            gm_f_format_01jvudh((void *)&t360, (void *)NULL, cx, NULL, t359);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t360))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t360) + 8, gm_srcgr7vbgb5cqmg, 282, 87);
            gm_b16_8 t361 = GM_LD(gm_b16_8, (((uint8_t *)&t360) + 8));
            if (gm_pf1m4b8q4e78(cx, gm_sgankvlme129s, 2, gm_srcgr7vbgb5cqmg, 282, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t353) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t353)), GM_LD(uintptr_t, ((uint8_t *)&t353) + 8), gm_srcgr7vbgb5cqmg, 282, 9))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_s3p0rbs9hc3lg, 1, gm_srcgr7vbgb5cqmg, 282, 9)) {
                goto gm_lad;
            }
            gm_buf b362 = {0};
            gm_format_spec(&b362, &gm_ty6groq37ram, ((uint8_t *)&t354), &(gm_spec){32, 2, 0, 0, 0, 0, 5u, -1});
            if (b362.len != 0) {
                gm_b16_8 t363;
                GM_ST(void *, (uint8_t *)&t363, (void *)(b362.data));
                GM_ST(uintptr_t, (uint8_t *)&t363 + 8, (uintptr_t)(b362.len));
                gm_pfmaojsuds6a(cx, t363, gm_srcgr7vbgb5cqmg, 282, 9);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_buf_free(&b362);
            if (gm_pf1m4b8q4e78(cx, gm_st3ja0nqi37bv, 1, gm_srcgr7vbgb5cqmg, 282, 9)) {
                goto gm_lad;
            }
            gm_buf b364 = {0};
            gm_format_spec(&b364, &gm_ty6groq37ram, ((uint8_t *)&t355), &(gm_spec){32, 1, 0, 0, 0, 0, 5u, -1});
            if (b364.len != 0) {
                gm_b16_8 t365;
                GM_ST(void *, (uint8_t *)&t365, (void *)(b364.data));
                GM_ST(uintptr_t, (uint8_t *)&t365 + 8, (uintptr_t)(b364.len));
                gm_pfmaojsuds6a(cx, t365, gm_srcgr7vbgb5cqmg, 282, 9);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_buf_free(&b364);
            if (gm_pf1m4b8q4e78(cx, gm_s3p0rbs9hc3lg, 1, gm_srcgr7vbgb5cqmg, 282, 9)) {
                goto gm_lad;
            }
            gm_buf b366 = {0};
            gm_format_spec(&b366, &gm_tybihgc69jv3, ((uint8_t *)&t361), &(gm_spec){32, 2, 0, 0, 0, 0, 6u, -1});
            if (b366.len != 0) {
                gm_b16_8 t367;
                GM_ST(void *, (uint8_t *)&t367, (void *)(b366.data));
                GM_ST(uintptr_t, (uint8_t *)&t367 + 8, (uintptr_t)(b366.len));
                gm_pfmaojsuds6a(cx, t367, gm_srcgr7vbgb5cqmg, 282, 9);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_buf_free(&b366);
            if (gm_pf1m4b8q4e78(cx, gm_sofs2uc27fjvk, 2, gm_srcgr7vbgb5cqmg, 282, 9)) {
                goto gm_lad;
            }
        }
ix192:;
    }
    {
        gm_th2lqbo77pvsc t368 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)a4));
        gm_b16_8 t369;
        if (((((uint8_t *)&t368))[16] != 0)) {
            gm_b16_8 t370 = GM_LD(gm_b16_8, (((uint8_t *)&t368) + 0));
            t369 = t370;
        } else {
            uint8_t t371 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        gm_l42 = t369;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l43, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t373 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
            gm_l44 = t373;
        }
        {
            gm_l45 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t375 = gm_l45;
                uint64_t t376 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                uint8_t t377 = (t375 < t376);
                if (!t377) goto lb374;
                {
                    uint64_t t380 = gm_l45;
                    uint64_t t381 = (uintptr_t)t380;
                    uint64_t t382 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                    gm_t2lqu8jsj511d t385 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l44)) + t381 * 16));
                    gm_l46 = t385;
                    {
                        void * t387 = (void *)((uint8_t *)&F.l43);
                        uint64_t t388 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 0));
                        uint64_t t389 = (uintptr_t)t388;
                        uint64_t t390 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                        if (t389 >= (uintptr_t)(t390)) gm_trap_index(t389, t390, gm_srcgr7vbgb5cqmg, 287, 36);
                        gm_b16_8 t391 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)) + t389 * 24) + 0));
                        gm_tnpifv20irnph t392;
                        GM_ST(gm_b16_8, ((uint8_t *)&t392 + 0), t391);
                        gm_tftb1odc739t5 t393 = gm_f_write_lmets0c(t387, t392);
                        if (((((uint8_t *)&t393))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t393) + 1, gm_srcgr7vbgb5cqmg, 287, 9);
                        gm_b0_1 t394 = GM_LD(gm_b0_1, (((uint8_t *)&t393) + 1));
                    }
                    {
                        gm_l47 = ((uint64_t)0ULL);
                    }
                    {
                        {
                            uint64_t t396 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                            void * t397 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)));
                            uint64_t t398 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 0));
                            uint64_t t399 = (uintptr_t)t398;
                            uint64_t t400 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 8));
                            uint64_t t401 = (uintptr_t)t400;
                            if (t399 > t401 || t401 > (uintptr_t)t396) gm_trap_slice(t399, t401, t396, gm_srcgr7vbgb5cqmg, 289, 18);
                            gm_b16_8 t402;
                            GM_ST(void *, (uint8_t *)&t402, (void *)(((uint8_t *)t397 + t399 * 24)));
                            GM_ST(uintptr_t, (uint8_t *)&t402 + 8, (uintptr_t)(t401 - t399));
                            gm_l48 = t402;
                        }
                        {
                            gm_l49 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t404 = gm_l49;
                                uint64_t t405 = GM_LD(uintptr_t, ((uint8_t *)&gm_l48) + 8);
                                uint8_t t406 = (t404 < t405);
                                if (!t406) goto lb403;
                                {
                                    uint64_t t409 = gm_l49;
                                    uint64_t t410 = (uintptr_t)t409;
                                    uint64_t t411 = GM_LD(uintptr_t, ((uint8_t *)&gm_l48) + 8);
                                    gm_t5hdnnftkup6m t414 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l48)) + t410 * 24));
                                    gm_l50 = t414;
                                    {
                                        void * t416 = (void *)((uint8_t *)&F.l43);
                                        uint32_t t417 = GM_LD(uint32_t, (((uint8_t *)&gm_l50) + 16));
                                        uint32_t t418 = GM_LD(uint32_t, (((uint8_t *)&gm_l50) + 20));
                                        gm_tcc1rq4n1avb2 t419;
                                        GM_ST(uint32_t, ((uint8_t *)&t419 + 0), t417);
                                        GM_ST(uint32_t, ((uint8_t *)&t419 + 4), t418);
                                        gm_tftb1odc739t5 t420 = gm_f_write_erog63d(t416, t419);
                                        if (((((uint8_t *)&t420))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t420) + 1, gm_srcgr7vbgb5cqmg, 290, 13);
                                        gm_b0_1 t421 = GM_LD(gm_b0_1, (((uint8_t *)&t420) + 1));
                                    }
                                    {
                                        uint32_t t423 = GM_LD(uint32_t, (((uint8_t *)&gm_l50) + 20));
                                        uint8_t t424 = (t423 > ((uint32_t)0ULL));
                                        if (!t424) goto ie422;
                                        {
                                            uint64_t t426 = gm_l47;
                                            uint64_t r427; bool o428 = __builtin_add_overflow(t426, ((uint64_t)1ULL), &r427);
                                            if (o428) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 291, 29);
                                            uint64_t t429 = r427;
                                            gm_l47 = t429;
                                        }
                                        goto ix422;
ie422:;
ix422:;
                                    }
                                    goto me412;
mn413:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 289, 9);
me412:;
                                }
lc403:;
                                {
                                    uint64_t t431 = gm_l49;
                                    uint64_t r432; bool o433 = __builtin_add_overflow(t431, ((uint64_t)1ULL), &r432);
                                    uint64_t t434 = r432;
                                    gm_l49 = t434;
                                }
                            }
lb403:;
                        }
                    }
                    {
                        void * t435 = (void *)((uint8_t *)&F.l43);
                        uint64_t t436 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 8));
                        uint64_t t437 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 0));
                        uint64_t r438; bool o439 = __builtin_sub_overflow(t436, t437, &r438);
                        if (o439) gm_trap("integer overflow in `-`", gm_srcgr7vbgb5cqmg, 293, 53);
                        uint64_t t440 = r438;
                        uint64_t t441 = gm_l47;
                        gm_t2lqu8jsj511d t442;
                        GM_ST(uint64_t, ((uint8_t *)&t442 + 0), t440);
                        GM_ST(uint64_t, ((uint8_t *)&t442 + 8), t441);
                        gm_tftb1odc739t5 t443 = gm_f_write_l7mh821(t435, t442);
                        if (((((uint8_t *)&t443))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t443) + 1, gm_srcgr7vbgb5cqmg, 293, 9);
                        gm_b0_1 t444 = GM_LD(gm_b0_1, (((uint8_t *)&t443) + 1));
                    }
                    goto me383;
mn384:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 286, 5);
me383:;
                }
lc374:;
                {
                    uint64_t t446 = gm_l45;
                    uint64_t r447; bool o448 = __builtin_add_overflow(t446, ((uint64_t)1ULL), &r447);
                    uint64_t t449 = r447;
                    gm_l45 = t449;
                }
            }
lb374:;
        }
    }
    {
        gm_b16_8 t450 = gm_l42;
        gm_b16_8 t451 = GM_LD(gm_b16_8, (((uint8_t *)&F.l43) + 0));
        gm_b16_8 t452 = GM_LD(gm_b16_8, ((uint8_t *)&t451));
        gm_ttln0d0hd70en t453 = gm_f_write_all_o8ucaji((void *)NULL, cx, NULL, t450, t452);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t454;
        if (!((((uint8_t *)&t453))[0] != 0)) {
            gm_b0_1 t455 = GM_LD(gm_b0_1, (((uint8_t *)&t453) + 4));
            t454 = t455;
        } else {
            gm_toe5fmr3vvf4d t456 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t453) + 4));
            gm_l51 = t456;
            {
                gm_b16_8 t458 = gm_l42;
                gm_toe5fmr3vvf4d t459 = gm_l51;
                gm_b16_8 t460 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t459);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_s7r1917rmmif7, 20, gm_srcgr7vbgb5cqmg, 296, 9)) {
                    goto gm_lad;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t458) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t458)), GM_LD(uintptr_t, ((uint8_t *)&t458) + 8), gm_srcgr7vbgb5cqmg, 296, 9))) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_slgmqgkioa4u6, 2, gm_srcgr7vbgb5cqmg, 296, 9)) {
                    goto gm_lad;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t460) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t460)), GM_LD(uintptr_t, ((uint8_t *)&t460) + 8), gm_srcgr7vbgb5cqmg, 296, 9))) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcgr7vbgb5cqmg, 296, 9)) {
                    goto gm_lad;
                }
            }
            uint8_t t461 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
    }
    {
        uint8_t t462 = 0;
        F.ret = ((uint8_t)1);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g9dfnu7(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_nqs6or3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_nqs6or3 F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK(&F, 143u);
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
        uint8_t t7 = (!gm_eq0iqiubfm7e(((uint8_t *)&t4), ((uint8_t *)&t6)));
        if (!t7) goto ie2;
        {
            void * t9 = gm_l0;
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)t9) + 0));
            void * t11 = gm_l1;
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)t11) + 0));
            uint8_t t13 = gm_f_path_less_il538gm((void *)NULL, cx, NULL, t10, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t15 = gm_l0;
        uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)t15) + 16));
        void * t17 = gm_l1;
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)t17) + 16));
        uint8_t t19 = (t16 < t18);
        uint8_t t20 = 0;
        F.ret = t19;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1vrv15m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_d80h595(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tbgr2dj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vetgq3s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_vetgq3s F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* l */
    uint32_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 243u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t5 = gm_l1;
        gm_b16_8 t6 = gm_f_trim_fdrfe6i(t5);
        gm_tssfa0col9q01 t7;
        gm_f_parse_int_i2lnj6n((void *)&t7, t6);
        uint32_t t8;
        if (!((((uint8_t *)&t7))[0] != 0)) {
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&t7) + 8));
            t8 = t9;
        } else {
            t8 = ((uint32_t)0ULL);
        }
        gm_l2 = t8;
    }
    {
        for (;;) {
            void * t11 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l0));
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
            uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l2));
            uint8_t t14 = (t12 <= t13);
            if (!t14) goto lb10;
            {
                void * t17 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l0));
                gm_tftb1odc739t5 t18 = gm_f_push_im9d4ro(t17, ((uint32_t)0ULL));
                if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_srcgr7vbgb5cqmg, 169, 39);
                gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
            }
lc10:;
        }
lb10:;
    }
    {
        uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l2));
        uint64_t t21 = (uintptr_t)t20;
        void * t22 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l0));
        uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)t22) + 0) + 8);
        if (t21 >= (uintptr_t)(t23)) gm_trap_index(t21, t23, gm_srcgr7vbgb5cqmg, 170, 9);
        uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l2));
        uint64_t t25 = (uintptr_t)t24;
        void * t26 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l0));
        uint64_t t27 = GM_LD(uintptr_t, (((uint8_t *)t26) + 0) + 8);
        if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srcgr7vbgb5cqmg, 170, 27);
        uint32_t t28 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t26) + 0)) + t25 * 4));
        uint32_t t29 = gm_l2;
        uint32_t r30; bool o31 = __builtin_add_overflow(t28, t29, &r30);
        uint32_t t32 = r30;
        GM_ST(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t22) + 0)) + t21 * 4), t32);
    }
    {
        uint64_t t33 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l2));
        uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
        if (o35) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 171, 9);
        uint64_t t36 = r34;
        GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_add_counts_hprtdd9 *)((&F.h)->up))->l2), t36);
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 167, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_instrument_mk8clvl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_instrument_mk8clvl F;
    void * gm_l0; /* s */
    void * gm_l1; /* scope */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* f */
    void * gm_l7; /* body */
    gm_tl874t2atnlh3 gm_l8; /* span */
    uint32_t gm_l9; /* id */
    void * gm_l10; /* old */
    void * gm_l12; /* block */
    GM_STACK_CHECK(&F, 711u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        GM_ST(void *, (uint8_t *)&F.l2 + 0, t2);
        void * t3 = gm_l1;
        GM_ST(void *, (uint8_t *)&F.l2 + 8, t3);
        gm_f_hash_map_vqsve4u((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l2 + 16)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_70470jk((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l2 + 72)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_1qnmubu((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&(*(gm_tin97tt4ud0s1 *)((uint8_t *)&F.l2 + 128)) + 0)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = gm_l0;
        gm_f_counted_fns_j3p4pgn((void *)&F.l3, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            void * t6 = gm_l0;
            uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 344) + 0) + 8);
            gm_l4 = t7;
        }
        {
            for (;;) {
                uint64_t t9 = gm_l5;
                uint64_t t10 = gm_l4;
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l5;
                    uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&t14));
                    gm_l6 = t17;
                    {
                        uint64_t t20 = gm_l6;
                        uint64_t t21 = (uintptr_t)t20;
                        uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                        if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_srcgr7vbgb5cqmg, 58, 16);
                        uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t21 * 1));
                        uint8_t t24 = (uint8_t)!t23;
                        if (!t24) goto ie19;
                        {
                            goto lc8;
                        }
                        goto ix19;
ie19:;
ix19:;
                    }
                    {
                        void * t26 = gm_l0;
                        uint64_t t27 = gm_l6;
                        uint32_t t28 = (uint32_t)t27;
                        gm_tdgfduqjheqdm t29;
                        gm_f_def_of_m512sbu((void *)&t29, t26, t28);
                        void * t30 = GM_LD(void *, (((uint8_t *)&t29) + 40));
                        void * t31;
                        if ((GM_LD(void *, ((uint8_t *)&t30)) != 0)) {
                            void * t32 = GM_LD(void *, (((uint8_t *)&t30) + 0));
                            t31 = t32;
                        } else {
                            goto lc8;
                        }
                        gm_l7 = t31;
                    }
                    {
                        void * t34 = gm_l7;
                        uint8_t t35;
                        uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t34) + 8) + 0));
                        if (t38 != ((uint8_t)46ULL)) goto mn37;
                        t35 = ((uint8_t)0);
                        goto me36;
mn37:;
                        t35 = ((uint8_t)1);
                        goto me36;
mn39:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 60, 12);
me36:;
                        if (!t35) goto ie33;
                        {
                            void * t41 = gm_l7;
                            gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t41) + 72));
                            gm_l8 = t42;
                        }
                        {
                            void * t44 = (void *)((uint8_t *)&F.l2);
                            gm_tl874t2atnlh3 t45 = gm_l8;
                            gm_t9805qtonbsag t46 = gm_f_point_8frh2ab((void *)NULL, cx, NULL, t44, t45);
                            if (gm_unw.kind) {
                                goto gm_ll12;
                            }
                            if (!((((uint8_t *)&t46))[4] != 0)) goto ie43;
                            uint32_t t47 = GM_LD(uint32_t, ((uint8_t *)&t46));
                            gm_l9 = t47;
                            {
                                void * t49 = gm_l7;
                                gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t49) + 0));
                                void * t51 = gm_l7;
                                gm_tl874t2atnlh3 t52 = gm_l8;
                                void * t53 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t50, &(*(gm_teti411eujvf4 *)(((uint8_t *)t51) + 8)), t52);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                                gm_l10 = t53;
                            }
                            {
                                gm_f_list_ir0h3tc_2((void *)&F.l11, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                            }
                            {
                                void * t54 = (void *)((uint8_t *)&F.l11);
                                void * t55 = gm_l0;
                                uint32_t t56 = gm_l9;
                                gm_tl874t2atnlh3 t57 = gm_l8;
                                gm_trcmmd12cefb1 t58;
                                gm_f_cover_stmt_bp29hrl((void *)&t58, (void *)NULL, cx, NULL, t55, t56, t57);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                                gm_tftb1odc739t5 t59 = gm_f_push_d6c76ud_2(t54, &t58);
                                if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srcgr7vbgb5cqmg, 66, 17);
                                gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
                            }
                            {
                                gm_tgabaikr08mjg t61;
                                memset((uint8_t *)&t61 + 36, 0, 4);
                                gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t61 + 0, t62);
                                void * t63 = gm_l10;
                                GM_ST(void *, (uint8_t *)&t61 + 16, t63);
                                gm_tl874t2atnlh3 t64 = gm_l8;
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t61 + 24, t64);
                                gm_tq7s98ihonkju t65 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t61, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kp3nl0er6il)));
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                                if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 8, gm_srcgr7vbgb5cqmg, 67, 26);
                                void * t66 = GM_LD(void *, (((uint8_t *)&t65) + 8));
                                gm_l12 = t66;
                            }
                            {
                                void * t67 = gm_l7;
                                memset((uint8_t *)&(*(gm_t69m56meqefs8 *)((uint8_t *)t67)) + 4, 0, 4);
                                memset((uint8_t *)&(*(gm_t69m56meqefs8 *)((uint8_t *)t67)) + 84, 0, 4);
                                void * t68 = gm_l7;
                                gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t68) + 0));
                                void * t70 = gm_l12;
                                gm_teti411eujvf4 t71;
                                memset(&t71, 0, sizeof t71);
                                GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)46ULL));
                                GM_ST(void *, ((uint8_t *)&t71 + 8), t70);
                                gm_tl874t2atnlh3 t72 = gm_l8;
                                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_t69m56meqefs8 *)((uint8_t *)t67)) + 0, t69);
                                GM_ST(gm_teti411eujvf4, (uint8_t *)&(*(gm_t69m56meqefs8 *)((uint8_t *)t67)) + 8, t71);
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t69m56meqefs8 *)((uint8_t *)t67)) + 72, t72);
                            }
                            goto ix43;
ie43:;
ix43:;
                        }
                        goto ix33;
ie33:;
ix33:;
                    }
                    {
                        void * t73 = (void *)((uint8_t *)&F.l2);
                        void * t74 = gm_l7;
                        gm_b0_1 t75 = gm_f_visit_mnqi05r((void *)NULL, cx, NULL, t73, t74);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 57, 5);
me15:;
                }
lc8:;
                {
                    uint64_t t77 = gm_l5;
                    uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                    uint64_t t80 = r78;
                    gm_l5 = t80;
                }
            }
gm_ll12:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
            }
            goto gm_lad;
lb8:;
        }
    }
    {
        void * t81 = gm_l0;
        uint64_t t82 = GM_LD(uintptr_t, (((((uint8_t *)&F.l2) + 128) + 0) + 0) + 8);
        uint32_t t83 = (uint32_t)t82;
        GM_ST(uint32_t, (((uint8_t *)t81) + 2160), t83);
    }
    {
        memmove(gm_out, (((uint8_t *)&F.l2) + 128), 40);
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_test_1bbodje(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_pad_o3a3nuu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_pad_o3a3nuu F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* width */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 214u);
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
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srcgr7vbgb5cqmg, 213, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        {
            gm_b16_8 t7 = gm_l0;
            uint64_t t8 = gm_f_chars_232hkk6(t7);
            gm_l4 = t8;
        }
        {
            uint64_t t9 = gm_l1;
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
                    {
                        void * t20 = (void *)((uint8_t *)&F.l2);
                        gm_tftb1odc739t5 t21 = gm_f_push_t83rg2m(t20, ((uint8_t)32ULL));
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srcgr7vbgb5cqmg, 214, 32);
                        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 214, 5);
me17:;
                }
lc10:;
                {
                    uint64_t t24 = gm_l4;
                    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                    uint64_t t27 = r25;
                    gm_l4 = t27;
                }
            }
lb10:;
        }
    }
    {
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        uint8_t t30 = 0;
        F.ret = t29;
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
__attribute__((aligned(4))) uint8_t gm_f_path_less_il538gm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_path_less_il538gm F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    gm_b16_8 gm_l3; /* <tmp> */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* n */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    GM_STACK_CHECK(&F, 488u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l3 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlvedue473cr, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l3;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(1));
        gm_b0_1 t10 = gm_f_split_obtinjv_st80pr1((void *)NULL, (&wn5), NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_ll7;
        }
        F.w0 = t10;
        goto gm_ls11;
gm_ll7:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we6; }
        goto gm_lad;
gm_ls11:;
we6:;
        gm_b0_1 t12 = F.w0;
        gm_b0_1 t13 = t12;
    }
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t15 = gm_l1;
            gm_l5 = t15;
        }
        gm_hval t16 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn17 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlqu6cf845ie, .env = t16.env, .hctx = t16.cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
        gm_b16_8 t20 = gm_l5;
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
        gm_b0_1 t22 = gm_f_split_obtinjv_sq3203i((void *)NULL, (&wn17), NULL, t20, t21);
        if (gm_unw.kind) {
            goto gm_ll19;
        }
        F.w1 = t22;
        goto gm_ls23;
gm_ll19:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we18; }
        goto gm_lad;
gm_ls23:;
we18:;
        gm_b0_1 t24 = F.w1;
        gm_b0_1 t25 = t24;
    }
    {
        uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        uint64_t t27 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        uint64_t t28 = (t26 < t27 ? t26 : t27);
        gm_l6 = t28;
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t30 = gm_l6;
            gm_l7 = t30;
        }
        {
            for (;;) {
                uint64_t t32 = gm_l8;
                uint64_t t33 = gm_l7;
                uint8_t t34 = (t32 < t33);
                if (!t34) goto lb31;
                {
                    uint64_t t37 = gm_l8;
                    uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&t37));
                    gm_l9 = t40;
                    {
                        uint64_t t43 = gm_l9;
                        uint64_t t44 = (uintptr_t)t43;
                        uint64_t t45 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                        if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srcgr7vbgb5cqmg, 190, 12);
                        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t44 * 16));
                        uint64_t t47 = gm_l9;
                        uint64_t t48 = (uintptr_t)t47;
                        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
                        if (t48 >= (uintptr_t)(t49)) gm_trap_index(t48, t49, gm_srcgr7vbgb5cqmg, 190, 30);
                        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t48 * 16));
                        uint8_t t51 = (!gm_eq0iqiubfm7e(((uint8_t *)&t46), ((uint8_t *)&t50)));
                        if (!t51) goto ie42;
                        {
                            uint64_t t53 = gm_l9;
                            uint64_t t54 = (uintptr_t)t53;
                            uint64_t t55 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                            if (t54 >= (uintptr_t)(t55)) gm_trap_index(t54, t55, gm_srcgr7vbgb5cqmg, 190, 61);
                            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t54 * 16));
                            uint64_t t57 = gm_l9;
                            uint64_t t58 = (uintptr_t)t57;
                            uint64_t t59 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
                            if (t58 >= (uintptr_t)(t59)) gm_trap_index(t58, t59, gm_srcgr7vbgb5cqmg, 190, 73);
                            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t58 * 16));
                            uint8_t t61 = gm_f_bytes_less_r33ee4s(t56, t60);
                            uint8_t t62 = 0;
                            F.ret = t61;
                            goto gm_fret;
                        }
                        goto ix42;
ie42:;
ix42:;
                    }
                    goto me38;
mn39:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 189, 5);
me38:;
                }
lc31:;
                {
                    uint64_t t64 = gm_l8;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                    uint64_t t67 = r65;
                    gm_l8 = t67;
                }
            }
lb31:;
        }
    }
    {
        uint64_t t68 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        uint64_t t69 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        uint8_t t70 = (t68 < t69);
        uint8_t t71 = 0;
        F.ret = t70;
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
extern __attribute__((aligned(4))) double gm_f_percent_ie1duk3(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_point_8frh2ab(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_point_8frh2ab F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* path */
    gm_b16_8 gm_l3; /* canon */
    uint8_t gm_l4; /* yes */
    uint8_t gm_l5; /* inside */
    uint32_t gm_l6; /* line */
    gm_tcc1rq4n1avb2 gm_l7; /* key */
    uint32_t gm_l8; /* id */
    uint32_t gm_l9; /* id */
    GM_STACK_CHECK(&F, 654u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t5 = (gm_eqff85ac10o3(((uint8_t *)&t3), ((uint8_t *)&t4)));
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
        void * t10 = (void *)(((uint8_t *)t9) + 72);
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_tqs5p4n2tq19v t12 = gm_f_get_dk5gh28(t10, t11);
        uint8_t t13;
        if (((((uint8_t *)&t12))[1] != 0)) {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
            t13 = t14;
        } else {
            {
                void * t16 = gm_l0;
                void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
                void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
                void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
                uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
                void * t21 = gm_f_file_o15fqku(t19, t20);
                gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 0));
                gm_l2 = t22;
            }
            {
                gm_b16_8 t23 = gm_l2;
                gm_th2lqbo77pvsc t24;
                gm_f_canonicalize_epo7cln((void *)&t24, (void *)NULL, cx, NULL, t23);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t25;
                if (((((uint8_t *)&t24))[16] != 0)) {
                    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 0));
                    t25 = t26;
                } else {
                    gm_b16_8 t27 = gm_l2;
                    t25 = t27;
                }
                gm_l3 = t25;
            }
            {
                uint8_t t28 = 0;
                gm_b16_8 t29 = gm_l3;
                void * t30 = gm_l0;
                void * t31 = GM_LD(void *, (((uint8_t *)t30) + 8));
                gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)t31) + 0));
                uint8_t t33 = gm_f_under_rb6c23o(t29, t32);
                if (t33) {
                    gm_b16_8 t34 = gm_l3;
                    void * t35 = gm_l0;
                    void * t36 = GM_LD(void *, (((uint8_t *)t35) + 8));
                    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)t36) + 16));
                    uint8_t t38 = gm_f_under_rb6c23o(t34, t37);
                    uint8_t t39 = (uint8_t)!t38;
                    t28 = t39;
                } else t28 = 0;
                gm_l4 = t28;
            }
            {
                void * t40 = gm_l0;
                void * t41 = (void *)(((uint8_t *)t40) + 72);
                uint32_t t42 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
                uint8_t t43 = gm_l4;
                gm_tftb1odc739t5 t44 = gm_f_put_v6g5pup(t41, t42, t43);
                if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 1, gm_srcgr7vbgb5cqmg, 110, 9);
                gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 1));
            }
            uint8_t t46 = gm_l4;
            uint8_t t47 = t46;
            t13 = t47;
        }
        gm_l5 = t13;
    }
    {
        uint8_t t49 = gm_l5;
        uint8_t t50 = (uint8_t)!t49;
        if (!t50) goto ie48;
        {
            gm_t9805qtonbsag t52;
            memset(&t52, 0, sizeof t52);
            uint8_t t53 = 0;
            F.ret = t52;
            goto gm_fret;
        }
        goto ix48;
ie48:;
ix48:;
    }
    {
        void * t54 = gm_l0;
        void * t55 = GM_LD(void *, (((uint8_t *)t54) + 0));
        void * t56 = GM_LD(void *, (((uint8_t *)t55) + 0));
        void * t57 = GM_LD(void *, (((uint8_t *)t56) + 0));
        uint32_t t58 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t59 = gm_f_file_o15fqku(t57, t58);
        uint32_t t60 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_tcc1rq4n1avb2 t61 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t59, t60));
        uint32_t t62 = GM_LD(uint32_t, (((uint8_t *)&t61) + 0));
        gm_l6 = t62;
    }
    {
        uint32_t t63 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        uint32_t t64 = gm_l6;
        gm_tcc1rq4n1avb2 t65;
        GM_ST(uint32_t, ((uint8_t *)&t65 + 0), t63);
        GM_ST(uint32_t, ((uint8_t *)&t65 + 4), t64);
        gm_l7 = t65;
    }
    {
        void * t67 = gm_l0;
        void * t68 = (void *)(((uint8_t *)t67) + 16);
        gm_tcc1rq4n1avb2 t69 = gm_l7;
        gm_t9805qtonbsag t70 = gm_f_get_78dnvqp(t68, t69);
        if (!((((uint8_t *)&t70))[4] != 0)) goto ie66;
        uint32_t t71 = GM_LD(uint32_t, ((uint8_t *)&t70));
        gm_l8 = t71;
        {
            uint32_t t73 = gm_l8;
            gm_t9805qtonbsag t74;
            GM_ST(uint8_t, (uint8_t *)&t74 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t74 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t74), t73);
            ((uint8_t *)&t74)[4] = 1;
            uint8_t t75 = 0;
            F.ret = t74;
            goto gm_fret;
        }
        goto ix66;
ie66:;
ix66:;
    }
    {
        void * t76 = gm_l0;
        uint64_t t77 = GM_LD(uintptr_t, (((((uint8_t *)t76) + 128) + 0) + 0) + 8);
        uint32_t t78 = (uint32_t)t77;
        gm_l9 = t78;
    }
    {
        void * t79 = gm_l0;
        void * t80 = (void *)((((uint8_t *)t79) + 128) + 0);
        uint32_t t81 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        uint32_t t82 = gm_l6;
        gm_tcc1rq4n1avb2 t83;
        GM_ST(uint32_t, ((uint8_t *)&t83 + 0), t81);
        GM_ST(uint32_t, ((uint8_t *)&t83 + 4), t82);
        gm_tftb1odc739t5 t84 = gm_f_push_lstf2b8(t80, t83);
        if (((((uint8_t *)&t84))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t84) + 1, gm_srcgr7vbgb5cqmg, 118, 5);
        gm_b0_1 t85 = GM_LD(gm_b0_1, (((uint8_t *)&t84) + 1));
    }
    {
        void * t86 = gm_l0;
        void * t87 = (void *)(((uint8_t *)t86) + 16);
        gm_tcc1rq4n1avb2 t88 = gm_l7;
        uint32_t t89 = gm_l9;
        gm_tftb1odc739t5 t90 = gm_f_put_a7l7vun(t87, t88, t89);
        if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 1, gm_srcgr7vbgb5cqmg, 119, 5);
        gm_b0_1 t91 = GM_LD(gm_b0_1, (((uint8_t *)&t90) + 1));
    }
    {
        uint32_t t92 = gm_l9;
        gm_t9805qtonbsag t93;
        GM_ST(uint8_t, (uint8_t *)&t93 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t93 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t93), t92);
        ((uint8_t *)&t93)[4] = 1;
        uint8_t t94 = 0;
        F.ret = t93;
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
inline __attribute__((aligned(4))) void gm_f_scope_4n0q0tq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_scope_4n0q0tq F;
    gm_b16_8 gm_l0; /* input */
    gm_b16_8 gm_l1; /* canon */
    gm_b16_8 gm_l2; /* dir */
    gm_b16_8 gm_l3; /* root */
    gm_b16_8 gm_l4; /* std */
    GM_STACK_CHECK(&F, 545u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_th2lqbo77pvsc t3;
        gm_f_canonicalize_epo7cln((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_b16_8 t6 = gm_l0;
            t4 = t6;
        }
        gm_l1 = t4;
    }
    {
        gm_b16_8 t7;
        gm_b16_8 t9 = gm_l1;
        uint8_t t10 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t10) goto ie8;
        gm_b16_8 t12 = gm_l1;
        gm_b16_8 t13 = t12;
        t7 = t13;
        goto ix8;
ie8:;
        gm_b16_8 t15 = gm_l1;
        gm_th2lqbo77pvsc t16;
        gm_f_parent_lk0iero((void *)&t16, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t17;
        if (((((uint8_t *)&t16))[16] != 0)) {
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 0));
            t17 = t18;
        } else {
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(0));
            t17 = t19;
        }
        gm_b16_8 t20 = t17;
        t7 = t20;
ix8:;
        gm_l2 = t7;
    }
    {
        gm_b16_8 t21 = gm_l2;
        gm_th2lqbo77pvsc t22;
        gm_f_find_root_kjnm6hf((void *)&t22, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t23;
        if (((((uint8_t *)&t22))[16] != 0)) {
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 0));
            t23 = t24;
        } else {
            gm_b16_8 t25 = gm_l2;
            t23 = t25;
        }
        gm_l3 = t23;
    }
    {
        gm_b16_8 t26 = gm_f_std_root_e0juohb((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t26;
    }
    {
        gm_b16_8 t27 = gm_l3;
        gm_b16_8 t28 = gm_l4;
        gm_th2lqbo77pvsc t29;
        gm_f_canonicalize_epo7cln((void *)&t29, (void *)NULL, cx, NULL, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t30;
        if (((((uint8_t *)&t29))[16] != 0)) {
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 0));
            t30 = t31;
        } else {
            gm_b16_8 t32 = gm_l4;
            t30 = t32;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6j6smj7qoctu *)gm_out) + 0, t27);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6j6smj7qoctu *)gm_out) + 16, t30);
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
inline __attribute__((aligned(4))) gm_toceffb697p0u gm_f_stmt_span_slh12dn(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stmt_span_slh12dn F;
    gm_trcmmd12cefb1 gm_l0; /* st */
    void * gm_l1; /* x */
    void * gm_l2; /* init */
    void * gm_l3; /* place */
    void * gm_l4; /* value */
    void * gm_l5; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 381u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_toceffb697p0u t2;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma10; default: goto ma11; }
ma4:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t14 != ((uint8_t)0ULL)) goto mn13;
        if ((GM_LD(void *, ((((uint8_t *)a0) + 8) + 8)) != 0)) goto mn13;
        gm_toceffb697p0u t15;
        memset(&t15, 0, sizeof t15);
        t2 = t15;
        goto me3;
mn13:;
ma5:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t17 != ((uint8_t)0ULL)) goto mn16;
        if (!(GM_LD(void *, ((((uint8_t *)a0) + 8) + 8)) != 0)) goto mn16;
        void * t18 = GM_LD(void *, ((((uint8_t *)a0) + 8) + 8));
        gm_l1 = t18;
        void * t19 = gm_l1;
        gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t19) + 72));
        gm_toceffb697p0u t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t21), t20);
        ((uint8_t *)&t21)[12] = 1;
        t2 = t21;
        goto me3;
mn16:;
ma6:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t23 != ((uint8_t)1ULL)) goto mn22;
        void * t24 = GM_LD(void *, ((((uint8_t *)a0) + 8) + 16));
        gm_l2 = t24;
        void * t25 = gm_l2;
        gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t25) + 72));
        gm_toceffb697p0u t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t27 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t27), t26);
        ((uint8_t *)&t27)[12] = 1;
        t2 = t27;
        goto me3;
mn22:;
ma7:;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t29 != ((uint8_t)2ULL)) goto mn28;
        void * t30 = GM_LD(void *, ((((uint8_t *)a0) + 8) + 0));
        gm_l3 = t30;
        void * t31 = gm_l3;
        gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t31) + 72));
        gm_toceffb697p0u t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t33 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t33), t32);
        ((uint8_t *)&t33)[12] = 1;
        t2 = t33;
        goto me3;
mn28:;
ma8:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t35 != ((uint8_t)3ULL)) goto mn34;
        void * t36 = GM_LD(void *, ((((uint8_t *)a0) + 8) + 16));
        gm_l4 = t36;
        void * t37 = gm_l4;
        gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t37) + 72));
        gm_toceffb697p0u t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t39), t38);
        ((uint8_t *)&t39)[12] = 1;
        t2 = t39;
        goto me3;
mn34:;
ma9:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t41 != ((uint8_t)4ULL)) goto mn40;
        void * t42 = GM_LD(void *, (((uint8_t *)a0) + 8));
        gm_l5 = t42;
        void * t43 = gm_l5;
        gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t43) + 72));
        gm_toceffb697p0u t45;
        GM_ST(uint8_t, (uint8_t *)&t45 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t45 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t45), t44);
        ((uint8_t *)&t45)[12] = 1;
        t2 = t45;
        goto me3;
mn40:;
ma10:;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t47 != ((uint8_t)5ULL)) goto mn46;
        gm_toceffb697p0u t48;
        memset(&t48, 0, sizeof t48);
        t2 = t48;
        goto me3;
mn46:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 149, 12);
me3:;
        uint8_t t49 = 0;
        F.ret = t2;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_stmt_span_slh12dn_c(gm_trcmmd12cefb1 a0) { return gm_f_stmt_span_slh12dn(&a0); }
inline __attribute__((aligned(4))) uint8_t gm_f_under_rb6c23o(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_under_rb6c23o F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* dir */
    GM_STACK_CHECK_SHALLOW(&F, 148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4 = gm_l1;
        uint8_t t5 = gm_f_starts_with_6vvt01e(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t9 = 0;
        uint8_t t10 = 0;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t13 = ((t11 == t12));
        if (!t13) {
            gm_b16_8 t14 = gm_l1;
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_st3ja0nqi37bv));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(1));
            uint8_t t16 = gm_f_ends_with_biriu5l(t14, t15);
            t10 = t16;
        } else t10 = 1;
        if (!t10) {
            uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t t18 = (uintptr_t)t17;
            uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t18 >= (uintptr_t)(t19)) gm_trap_index(t18, t19, gm_srcgr7vbgb5cqmg, 33, 54);
            uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t18 * 1));
            uint8_t t21 = ((t20 == ((uint8_t)47ULL)));
            t9 = t21;
        } else t9 = 1;
        uint8_t t22 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_visit_mnqi05r(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_visit_mnqi05r F;
    void * gm_l1; /* e */
    void * gm_l2; /* b */
    GM_STACK_CHECK(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma4; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma5; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; case ((uint8_t)51ULL): goto ma6; case ((uint8_t)52ULL): goto ma6; case ((uint8_t)53ULL): goto ma6; case ((uint8_t)54ULL): goto ma6; case ((uint8_t)55ULL): goto ma6; case ((uint8_t)56ULL): goto ma6; case ((uint8_t)57ULL): goto ma6; case ((uint8_t)58ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)46ULL)) goto mn9;
        void * t11 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t11;
        void * t12 = F.l0;
        void * t13 = gm_l2;
        gm_b0_1 t14 = gm_f_block_u0oqu8p((void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn9:;
ma5:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t16 != ((uint8_t)48ULL)) goto mn15;
        gm_t710lsrq1m8gk t17 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t2) + 8) + 8));
        F.l3 = t17;
        void * t18 = F.l0;
        void * t19 = GM_LD(void *, (((uint8_t *)&F.l3) + 24));
        gm_b0_1 t20 = gm_f_block_u0oqu8p((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn15:;
ma6:;
        goto me3;
mn21:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 124, 5);
me3:;
    }
    {
        void * t23 = gm_l1;
        gm_closure t24 = ((gm_closure){(void *)gm_f_fn_g9dfnu7, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t23, t24);
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
