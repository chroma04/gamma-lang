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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; union { uint8_t v0; } f1; } gm_tk5oh6c9vhlab;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { void * f0; void * f1; gm_b16_8 f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; void * f5; } gm_t9t3j6eapd3uf;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; uint64_t v1; uint64_t v2; uint64_t v3; uint64_t v4; uint64_t v5; uint64_t v6; uint64_t v7; uint64_t v8; gm_t9qkh0rsooc8d v9; } f1; } gm_tekvgkj2hms37;
typedef struct { void * f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; void * f2; } gm_tf361jk9mn3il;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { int64_t f0; gm_b16_8 f1; } gm_thgmovcqlsivv;
typedef struct { gm_t3mf58gv9kf3n f0; void * f1; uint64_t f2; uint64_t f3; uint64_t f4; } gm_tjt4ka19str13;
_Static_assert(sizeof(gm_tk5oh6c9vhlab) == 2 && _Alignof(gm_tk5oh6c9vhlab) == 1 && offsetof(gm_tk5oh6c9vhlab, f0) == 0 && offsetof(gm_tk5oh6c9vhlab, f1) == 1, "layout of `Step`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; int64_t f1; gm_b16_8 f2; } gm_tqj9esv5ukti3;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t2792tsg5eg72;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t3ujhu6qph7de v0; } f1; } gm_t715ajl6o1ipo;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; gm_tnpifv20irnph v1; gm_b16_8 v2; gm_b16_8 v3; gm_b16_8 v4; } f1; } gm_t8vuock3jnv59;
_Static_assert(sizeof(gm_t9t3j6eapd3uf) == 64 && _Alignof(gm_t9t3j6eapd3uf) == 8 && offsetof(gm_t9t3j6eapd3uf, f0) == 0 && offsetof(gm_t9t3j6eapd3uf, f1) == 8 && offsetof(gm_t9t3j6eapd3uf, f2) == 16 && offsetof(gm_t9t3j6eapd3uf, f3) == 32 && offsetof(gm_t9t3j6eapd3uf, f4) == 48 && offsetof(gm_t9t3j6eapd3uf, f5) == 56, "layout of `Request`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
_Static_assert(sizeof(gm_tekvgkj2hms37) == 16 && _Alignof(gm_tekvgkj2hms37) == 8 && offsetof(gm_tekvgkj2hms37, f0) == 0 && offsetof(gm_tekvgkj2hms37, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_tf361jk9mn3il) == 24 && _Alignof(gm_tf361jk9mn3il) == 8 && offsetof(gm_tf361jk9mn3il, f0) == 0 && offsetof(gm_tf361jk9mn3il, f1) == 8 && offsetof(gm_tf361jk9mn3il, f2) == 16, "layout of `Ctx`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
typedef struct { gm_b16_8 f0; gm_tqj9esv5ukti3 f1; } gm_tggvvh3lncvcg;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thgmovcqlsivv) == 24 && _Alignof(gm_thgmovcqlsivv) == 8 && offsetof(gm_thgmovcqlsivv, f0) == 0 && offsetof(gm_thgmovcqlsivv, f1) == 8, "layout of `CachedProgram`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnpifv20irnph v0; gm_b16_8 v1; } f1; } gm_ti9v8ce4a1rnh;
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tjt4ka19str13) == 48 && _Alignof(gm_tjt4ka19str13) == 8 && offsetof(gm_tjt4ka19str13, f0) == 0 && offsetof(gm_tjt4ka19str13, f1) == 16 && offsetof(gm_tjt4ka19str13, f2) == 24 && offsetof(gm_tjt4ka19str13, f3) == 32 && offsetof(gm_tjt4ka19str13, f4) == 40, "layout of `Arena`");
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_thgmovcqlsivv v0; gm_b16_8 v1; } f1; } gm_tm2ah4hc06bl5;
typedef struct { gm_tqj9esv5ukti3 f0; uint8_t f1; uint8_t pe[7]; } gm_tmle170q2vbs1;
typedef struct { gm_t3ujhu6qph7de f0; uint8_t f1; uint8_t pe[7]; } gm_tnnrsfd8ehhme;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
_Static_assert(sizeof(gm_tqj9esv5ukti3) == 40 && _Alignof(gm_tqj9esv5ukti3) == 8 && offsetof(gm_tqj9esv5ukti3, f0) == 0 && offsetof(gm_tqj9esv5ukti3, f1) == 16 && offsetof(gm_tqj9esv5ukti3, f2) == 24, "layout of `Doc`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint64_t f2; void * f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_tc0nbpdhofi37 f7; gm_trqfvmjtt4sij f8; gm_toceffb697p0u f9; gm_trqfvmjtt4sij f10; uint8_t f11; uint8_t pe[7]; } gm_trd6bg8is1v4n;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t4c7ipjj9s3fp f0; } gm_ttiitul0263ld;
_Static_assert(sizeof(gm_t2792tsg5eg72) == 64 && _Alignof(gm_t2792tsg5eg72) == 8 && offsetof(gm_t2792tsg5eg72, f0) == 0 && offsetof(gm_t2792tsg5eg72, f1) == 40 && offsetof(gm_t2792tsg5eg72, f2) == 48 && offsetof(gm_t2792tsg5eg72, f3) == 56, "layout of `Diagnostics`");
typedef struct { gm_t8vuock3jnv59 f0; uint8_t f1; uint8_t pe[7]; } gm_t3ph3kbiqt1ah;
_Static_assert(sizeof(gm_t715ajl6o1ipo) == 32 && _Alignof(gm_t715ajl6o1ipo) == 8 && offsetof(gm_t715ajl6o1ipo, f0) == 0 && offsetof(gm_t715ajl6o1ipo, f1) == 8, "layout of `Taken`");
_Static_assert(sizeof(gm_t8vuock3jnv59) == 24 && _Alignof(gm_t8vuock3jnv59) == 8 && offsetof(gm_t8vuock3jnv59, f0) == 0 && offsetof(gm_t8vuock3jnv59, f1) == 8, "layout of `Value`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t8vuock3jnv59 v1; } f1; } gm_t9l835tjp727s;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_thgmovcqlsivv v0; gm_t8vuock3jnv59 v1; } f1; } gm_tfaata5ba51n4;
typedef struct { gm_t8vuock3jnv59 a[2]; } gm_tg1c132scqcte;
_Static_assert(sizeof(gm_tggvvh3lncvcg) == 56 && _Alignof(gm_tggvvh3lncvcg) == 8 && offsetof(gm_tggvvh3lncvcg, f0) == 0 && offsetof(gm_tggvvh3lncvcg, f1) == 16, "layout of `(key: string, value: Doc)`");
typedef struct { gm_t8vuock3jnv59 f0; gm_tjt4ka19str13 f1; } gm_thshlh2q7ekfv;
_Static_assert(sizeof(gm_ti9v8ce4a1rnh) == 24 && _Alignof(gm_ti9v8ce4a1rnh) == 8 && offsetof(gm_ti9v8ce4a1rnh, f0) == 0 && offsetof(gm_ti9v8ce4a1rnh, f1) == 8, "layout of `FormatError!string`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
_Static_assert(sizeof(gm_tm2ah4hc06bl5) == 32 && _Alignof(gm_tm2ah4hc06bl5) == 8 && offsetof(gm_tm2ah4hc06bl5, f0) == 0 && offsetof(gm_tm2ah4hc06bl5, f1) == 8, "layout of `ReqError!string`");
_Static_assert(sizeof(gm_tmle170q2vbs1) == 48 && _Alignof(gm_tmle170q2vbs1) == 8 && offsetof(gm_tmle170q2vbs1, f0) == 0 && offsetof(gm_tmle170q2vbs1, f1) == 40, "layout of `?Doc`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_t4c7ipjj9s3fp f1; gm_t4c7ipjj9s3fp f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; gm_ttiitul0263ld f5; uint8_t f6; uint8_t f7; uint8_t f8; uint8_t p9[5]; uint64_t f9; } gm_tmnkfo49014kk;
_Static_assert(sizeof(gm_tnnrsfd8ehhme) == 32 && _Alignof(gm_tnnrsfd8ehhme) == 8 && offsetof(gm_tnnrsfd8ehhme, f0) == 0 && offsetof(gm_tnnrsfd8ehhme, f1) == 24, "layout of `?Incoming`");
typedef struct { gm_t8vuock3jnv59 a[1]; } gm_tq47bkpmd2b9a;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_trd6bg8is1v4n) == 288 && _Alignof(gm_trd6bg8is1v4n) == 8 && offsetof(gm_trd6bg8is1v4n, f0) == 0 && offsetof(gm_trd6bg8is1v4n, f1) == 8 && offsetof(gm_trd6bg8is1v4n, f2) == 24 && offsetof(gm_trd6bg8is1v4n, f3) == 32 && offsetof(gm_trd6bg8is1v4n, f4) == 40 && offsetof(gm_trd6bg8is1v4n, f5) == 80 && offsetof(gm_trd6bg8is1v4n, f6) == 120 && offsetof(gm_trd6bg8is1v4n, f7) == 160 && offsetof(gm_trd6bg8is1v4n, f8) == 184 && offsetof(gm_trd6bg8is1v4n, f9) == 224 && offsetof(gm_trd6bg8is1v4n, f10) == 240 && offsetof(gm_trd6bg8is1v4n, f11) == 280, "layout of `Lexer`");
_Static_assert(sizeof(gm_ttiitul0263ld) == 56 && _Alignof(gm_ttiitul0263ld) == 8 && offsetof(gm_ttiitul0263ld, f0) == 0, "layout of `Namer`");
_Static_assert(sizeof(gm_t3ph3kbiqt1ah) == 32 && _Alignof(gm_t3ph3kbiqt1ah) == 8 && offsetof(gm_t3ph3kbiqt1ah, f0) == 0 && offsetof(gm_t3ph3kbiqt1ah, f1) == 24, "layout of `?Value`");
_Static_assert(sizeof(gm_t9l835tjp727s) == 32 && _Alignof(gm_t9l835tjp727s) == 8 && offsetof(gm_t9l835tjp727s, f0) == 0 && offsetof(gm_t9l835tjp727s, f1) == 8, "layout of `string!Value`");
_Static_assert(sizeof(gm_tfaata5ba51n4) == 32 && _Alignof(gm_tfaata5ba51n4) == 8 && offsetof(gm_tfaata5ba51n4, f0) == 0 && offsetof(gm_tfaata5ba51n4, f1) == 8, "layout of `ReqError!Value`");
_Static_assert(sizeof(gm_tg1c132scqcte) == 48 && _Alignof(gm_tg1c132scqcte) == 8 && offsetof(gm_tg1c132scqcte, a) == 0, "layout of `[2]Value`");
_Static_assert(sizeof(gm_thshlh2q7ekfv) == 72 && _Alignof(gm_thshlh2q7ekfv) == 8 && offsetof(gm_thshlh2q7ekfv, f0) == 0 && offsetof(gm_thshlh2q7ekfv, f1) == 24, "layout of `Document`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; gm_thshlh2q7ekfv v1; } f1; } gm_tit4ajsn3pv91;
_Static_assert(sizeof(gm_tmnkfo49014kk) == 304 && _Alignof(gm_tmnkfo49014kk) == 8 && offsetof(gm_tmnkfo49014kk, f0) == 0 && offsetof(gm_tmnkfo49014kk, f1) == 8 && offsetof(gm_tmnkfo49014kk, f2) == 64 && offsetof(gm_tmnkfo49014kk, f3) == 120 && offsetof(gm_tmnkfo49014kk, f4) == 176 && offsetof(gm_tmnkfo49014kk, f5) == 232 && offsetof(gm_tmnkfo49014kk, f6) == 288 && offsetof(gm_tmnkfo49014kk, f7) == 289 && offsetof(gm_tmnkfo49014kk, f8) == 290 && offsetof(gm_tmnkfo49014kk, f9) == 296, "layout of `Server`");
_Static_assert(sizeof(gm_tq47bkpmd2b9a) == 24 && _Alignof(gm_tq47bkpmd2b9a) == 8 && offsetof(gm_tq47bkpmd2b9a, a) == 0, "layout of `[1]Value`");
_Static_assert(sizeof(gm_tit4ajsn3pv91) == 80 && _Alignof(gm_tit4ajsn3pv91) == 8 && offsetof(gm_tit4ajsn3pv91, f0) == 0 && offsetof(gm_tit4ajsn3pv91, f1) == 8, "layout of `ParseError!Document`");
struct gm_fr_after_value_1vi101i {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_array_k32ana3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_as_array_nfvj8pk {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_as_bool_g9idbdk {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_as_str_9rhg9v0 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_bool_value_j9bnrp8 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_code_actions_k897tcq {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* actions */
    gm_tin97tt4ud0s1 l17; /* o */
    gm_tin97tt4ud0s1 l19; /* e */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_completion_r10tmh9 {
    gm_frame h;
    gm_ttiitul0263ld l4; /* uris */
    gm_trqfvmjtt4sij l9; /* items */
    gm_trqfvmjtt4sij l12; /* found */
    gm_trqfvmjtt4sij l14; /* ovs */
    gm_trqfvmjtt4sij l22; /* found */
    gm_tin97tt4ud0s1 l24; /* o */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_component_k9b35ju {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_contains_57n7jqd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_c624pm6 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_eq02jnv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_deinit_l8j91qd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_destroy_602qeev {
    gm_frame h;
    gm_tb40qn7hk0tl9 l1; /* site */
    gm_b0_1 ret;
};
struct gm_fr_diagnostics_n3kkhf4 {
    gm_frame h;
    gm_t2792tsg5eg72 ret;
};
struct gm_fr_did_change_v6sghvq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_did_close_f9lpcgi {
    gm_frame h;
    gm_b16_8 l5; /* dir */
    gm_trqfvmjtt4sij l8; /* old */
    gm_trqfvmjtt4sij l9; /* others */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_did_open_9b7n0ug {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_dir_of_ho73hue {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_49nm4li {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5ns1sqk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8jt91p4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ab79bmt_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d0vhctu {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dheabhi_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lqcrtc5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oq9aajs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vu26gaj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_encoding_name_m6926rk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ensure_9tp264o {
    gm_frame h;
    gm_b16_8 l1; /* dir */
    gm_trqfvmjtt4sij l2; /* ovs */
    gm_th2lqbo77pvsc l3; /* primary */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_envelope_4dp2tms {
    gm_frame h;
    gm_tin97tt4ud0s1 l0; /* o */
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_fail_8tj0r64 {
    gm_frame h;
    gm_thgmovcqlsivv ret;
};
struct gm_fr_field_7904fsq {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_file_id_mif69v8 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_file_name_em7v8lv {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_6m9fu50 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_pt42u9t {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_r1rj4g9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_1qm1hvv {
    gm_frame h;
    gm_tggvvh3lncvcg l0; /* <item> */
    gm_tggvvh3lncvcg l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_for_body_7fmdm88 {
    gm_frame h;
    gm_tggvvh3lncvcg l0; /* <item> */
    gm_tggvvh3lncvcg l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_for_body_9nsvqlg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_i2u6ls4 {
    gm_frame h;
    gm_tggvvh3lncvcg l0; /* <item> */
    gm_tggvvh3lncvcg l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_for_body_n6686bi {
    gm_frame h;
    gm_tpe1hv3fekn05 l1; /* kv */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_for_body_qfqkllj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_s23qjlr {
    gm_frame h;
    gm_tggvvh3lncvcg l0; /* <item> */
    gm_tggvvh3lncvcg l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_for_body_sf9naq7 {
    gm_frame h;
    gm_tggvvh3lncvcg l0; /* <item> */
    gm_tggvvh3lncvcg l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_for_body_t3rkkf2 {
    gm_frame h;
    gm_tggvvh3lncvcg l0; /* <item> */
    gm_tggvvh3lncvcg l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_formatting_cu98209 {
    gm_frame h;
    gm_tqj9esv5ukti3 l2; /* d */
    gm_tin97tt4ud0s1 l3; /* sm */
    gm_t2792tsg5eg72 l4; /* diags */
    gm_tr8dvlu9ik4gj l6; /* lexed */
    gm_tfaata5ba51n4 ret;
};
struct gm_fr_get_lco3u6e {
    gm_frame h;
    void * ret;
};
struct gm_fr_get_ptr_egf3gtq {
    gm_frame h;
    void * ret;
};
struct gm_fr_handle_incoming_uj1ifur {
    gm_frame h;
    gm_thshlh2q7ekfv l5; /* doc */
    gm_tk5oh6c9vhlab ret;
};
struct gm_fr_handle_uop7uti {
    gm_frame h;
    gm_tk5oh6c9vhlab ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_8h9shb4 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_irta8m9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_mkadlk3 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_p3t5tj8 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_s1unuf2 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_initialize_q3q654t {
    gm_frame h;
    gm_tin97tt4ud0s1 l9; /* caps */
    gm_tin97tt4ud0s1 l10; /* sync */
    gm_tin97tt4ud0s1 l11; /* save */
    gm_tin97tt4ud0s1 l12; /* rename */
    gm_tin97tt4ud0s1 l13; /* ca */
    gm_tin97tt4ud0s1 l15; /* comp */
    gm_tin97tt4ud0s1 l17; /* sig */
    gm_tg1c132scqcte l18; /* <temporary array> */
    gm_tin97tt4ud0s1 l19; /* info */
    gm_tin97tt4ud0s1 l20; /* result */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_int_95caidg {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_invalidate_uhhh539 {
    gm_frame h;
    void * l0; /* srv */
    gm_b16_8 l2; /* dir */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_is_gm_file_0ofcrmu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_true_3osltpn {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_language_request_21k642b {
    gm_frame h;
    gm_ttiitul0263ld l5; /* uris */
    gm_trqfvmjtt4sij l6; /* ovs */
    gm_t9t3j6eapd3uf l12; /* rq */
    gm_tfaata5ba51n4 ret;
};
struct gm_fr_lex_dc8mbmf {
    gm_frame h;
    gm_trd6bg8is1v4n l3; /* lx */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_list_0202sdh_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_792g2go_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_bvkp9ir {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_j41vvuc {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_needs_analysis_l0op99b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_notification_6t090qd {
    gm_frame h;
    gm_tk5oh6c9vhlab ret;
};
struct gm_fr_notify_ls6n69d {
    gm_frame h;
    gm_tin97tt4ud0s1 l2; /* o */
    gm_b0_1 ret;
};
struct gm_fr_obj_aoa7fl7 {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_offset_at_73sli4j {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_open_dirs_9ud02ot {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* dirs */
    gm_trqfvmjtt4sij ret;
    gm_b0_1 w0;
};
struct gm_fr_overlays_6rdbiin {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij ret;
    gm_b0_1 w0;
};
struct gm_fr_parse_error_text_pasvuia {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_path2_enc7vaj {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_publish_empty_lh10nsv {
    gm_frame h;
    gm_tin97tt4ud0s1 l1; /* p */
    gm_b0_1 ret;
};
struct gm_fr_publish_put2rqb {
    gm_frame h;
    gm_b16_8 l1; /* dir */
    gm_ttiitul0263ld l3; /* uris */
    gm_trqfvmjtt4sij l4; /* diags */
    gm_trqfvmjtt4sij l5; /* targets */
    gm_tl7hpf5nlilqq l8; /* d */
    gm_trqfvmjtt4sij l10; /* old */
    gm_tl7hpf5nlilqq l20; /* d */
    gm_tin97tt4ud0s1 l21; /* params */
    gm_tqj9esv5ukti3 l22; /* d */
    gm_trqfvmjtt4sij l23; /* paths */
    gm_tl7hpf5nlilqq l26; /* d */
    gm_b0_1 ret;
    gm_b0_1 w1;
};
struct gm_fr_refresh_rsuaicn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_register_watchers_k2akg1c {
    gm_frame h;
    gm_tin97tt4ud0s1 l2; /* w1 */
    gm_tin97tt4ud0s1 l3; /* w2 */
    gm_trqfvmjtt4sij l4; /* ws */
    gm_tin97tt4ud0s1 l5; /* ro */
    gm_tin97tt4ud0s1 l6; /* reg */
    gm_tin97tt4ud0s1 l7; /* p */
    gm_tin97tt4ud0s1 l9; /* o */
    gm_b0_1 ret;
};
struct gm_fr_release_7gtbra1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_request_nncosh4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_respond_error_5vpslhl {
    gm_frame h;
    gm_tin97tt4ud0s1 l3; /* e */
    gm_tin97tt4ud0s1 l4; /* o */
    gm_b0_1 ret;
};
struct gm_fr_respond_vfmbdjk {
    gm_frame h;
    gm_tin97tt4ud0s1 l2; /* o */
    gm_b0_1 ret;
};
struct gm_fr_run_rkae3vt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_send_cg3jp71 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_server_6g9m6gg {
    gm_frame h;
    gm_tmnkfo49014kk ret;
};
struct gm_fr_sort_by_s9l01h6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_source_map_4v8dg4b {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_source_text_7ru912k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_syntax_error_4bqcc39 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* stack */
    gm_b16_8 ret;
};
struct gm_fr_target_j5nr6ju {
    gm_frame h;
    gm_tm2ah4hc06bl5 ret;
};
struct gm_fr_text_ls49dp6 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_to_text_2cum4i7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_truncate_ov4pgoq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_vir0vu7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_uri_map_mhud6ak {
    gm_frame h;
    gm_ttiitul0263ld l1; /* m */
    gm_ttiitul0263ld ret;
    gm_b0_1 w0;
};
struct gm_fr_uri_of_t6r79gp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_value_pkuqal3 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_watched_files_changed_qvnrc63 {
    gm_frame h;
    void * l0; /* srv */
    gm_b0_1 ret;
    gm_b0_1 w1;
};
struct gm_fr_workspace_ubjeikp {
    gm_frame h;
    gm_ttiitul0263ld ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint32_t gm_f_add_pva1tcq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_after_value_1vi101i(uint8_t a0);
inline __attribute__((aligned(4))) void gm_f_array_k32ana3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_as_array_nfvj8pk(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_as_bool_g9idbdk(const void * a0);
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_as_i64_iojmg3q(const void * a0);
inline __attribute__((aligned(4))) void gm_f_as_str_9rhg9v0(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_as_u64_0obhk18(const void * a0);
inline __attribute__((aligned(4))) void gm_f_bool_value_j9bnrp8(void *gm_out, uint8_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_canonical_path_q6lq4b6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void * gm_f_check_module_6elsf9l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_code_actions_k897tcq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_completion_r10tmh9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint32_t a2);
inline __attribute__((aligned(4))) uint32_t gm_f_component_k9b35ju(const void * a0);
__attribute__((aligned(4))) uint32_t gm_f_component_k9b35ju_c(gm_t8vuock3jnv59 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_c624pm6(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_eq02jnv(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_dedup_6a2r2j0(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_definition_kaqo3c6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) void gm_f_deinit_l8j91qd(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_destroy_602qeev(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_diagnostics_7ntqr3v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_did_change_v6sghvq(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_did_close_f9lpcgi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_did_open_9b7n0ug(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dir_of_ho73hue(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_document_symbols_0nlnk2i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_49nm4li(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5ns1sqk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d0vhctu(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lqcrtc5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oq9aajs(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_encoding_name_m6926rk(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ensure_9tp264o(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_5dln36o_s7sgbc0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_5dln36o_s7v20pl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_bdtp5sv_ses338s(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_s728q5p(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_s75phag(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_secjmhg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_smjejb0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_sogpj69(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_son0uk9(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_envelope_4dp2tms(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_qmfcuqk_2(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_fail_8tj0r64(void *gm_out, int64_t a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_thgmovcqlsivv gm_f_fail_8tj0r64_c(int64_t a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_field_7904fsq(void *gm_out, const void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_file_id_mif69v8(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_4uea0u1(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_c9mffp1(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6m9fu50(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_pt42u9t(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_r1rj4g9(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1qm1hvv(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_7fmdm88(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_9nsvqlg(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2u6ls4(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_n6686bi(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_qfqkllj(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_s23qjlr(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_sf9naq7(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_t3rkkf2(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_gej351p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0);
__attribute__((aligned(4))) void gm_f_format_hr0sl7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kb96ddt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_p7shig5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_shkdld2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_source_a5lvenl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_formatting_cu98209(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_full_edit_82h286k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) void gm_f_get_ahntg11(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_fjdb5ou(void *gm_out, const void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_get_lco3u6e(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_m1knqqk(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_get_ptr_egf3gtq(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_t10cn5s(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tk5oh6c9vhlab gm_f_handle_incoming_uj1ifur(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tk5oh6c9vhlab gm_f_handle_uop7uti(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_8h9shb4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_irta8m9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_mkadlk3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_s1unuf2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hover_ub02g3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_initialize_q3q654t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_int_95caidg(void *gm_out, void *env, gm_node *cx, gm_node *inst, int64_t a0);
__attribute__((aligned(4))) void gm_f_introsort_r6p6ckl(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_invalidate_uhhh539(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_is_gm_file_0ofcrmu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_true_3osltpn(const void * a0);
__attribute__((aligned(4))) void gm_f_language_request_21k642b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) void gm_f_lines_0mpphhg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_792g2go_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_j41vvuc(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_member_context_p58kq5l(void *gm_out, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_members_8m2g0sl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_names_ms1jmv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) uint8_t gm_f_needs_analysis_l0op99b(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_tk5oh6c9vhlab gm_f_notification_6t090qd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_notify_ls6n69d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_obj_aoa7fl7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint32_t gm_f_offset_at_73sli4j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint32_t a3);
__attribute__((aligned(4))) uint64_t gm_f_offset_hdhse2v(void * a0, uint32_t a1, uint32_t a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) void gm_f_open_dirs_9ud02ot(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_overlays_6rdbiin(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_parse_error_text_pasvuia(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tekvgkj2hms37 a1);
__attribute__((aligned(4))) void gm_f_parse_file_70g7vok(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, void * a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_parse_invslf8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_path2_enc7vaj(void *gm_out, const void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_path_less_702ltp5(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_path_to_uri_as8bikc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_position_f7cepnk(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_prepare_rename_i0odtgj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_publish_empty_lh10nsv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_publish_put2rqb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0pgacsj_3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_302k12h(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_625pk7t(void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_79ksd1u(void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_91stqpj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_eppc06m(void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_references_all_9v25v88(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint8_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_refresh_rsuaicn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_register_watchers_k2akg1c(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_release_7gtbra1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_remove_6s9ti8v(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_remove_c3h4fju(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_remove_jktghr7(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_remove_mabr80f(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_rename_4pp7bs8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_request_nncosh4(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, const void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_respond_error_5vpslhl(void *env, gm_node *cx, gm_node *inst, const void * a0, int64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_respond_vfmbdjk(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_run_rkae3vt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_send_cg3jp71(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_server_6g9m6gg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_signature_help_16c2a8f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) void gm_f_sort_by_s9l01h6(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_source_text_7ru912k(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_split_cbk4ks8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_syntax_error_4bqcc39(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_take_mg8jq2q(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_target_j5nr6ju(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_text_ls49dp6(void *gm_out, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_text_2cum4i7(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_truncate_ov4pgoq(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_vir0vu7(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_try_take_gd0vuic(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_uri_map_mhud6ak(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_uri_of_t6r79gp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_uri_to_path_kbof22r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_value_pkuqal3(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_watched_files_changed_qvnrc63(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_workspace_ubjeikp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_message_bp6uv22(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_value_5haur8t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
extern uint8_t gm_cro0h2panct[24] __attribute__((aligned(8)));
extern uint8_t gm_ctdm11uuaj6[16] __attribute__((aligned(8)));
extern void *const gm_hl0b9itmh1di[];
extern void *const gm_hl8iqbv5s08g[];
extern void *const gm_hleu8p83p90p[];
extern void *const gm_hlgmnqv27ug4[];
extern void *const gm_hlk5mm1461td[];
extern void *const gm_hlklsb3fd3g1[];
extern void *const gm_hlp1oga9ehto[];
extern void *const gm_hltn0orqptuk[];
extern void *const gm_hlvop6ag413a[];
extern uint8_t gm_k3h72k4hfps[40] __attribute__((aligned(8)));
extern uint8_t gm_khb6ap185f0[40] __attribute__((aligned(8)));
extern uint8_t gm_kismru0mlt2[40] __attribute__((aligned(8)));
extern uint8_t gm_ks1bcpoe2h2[40] __attribute__((aligned(8)));
extern const uint8_t gm_s041tbii048j3[8];
extern const uint8_t gm_s06kk1tae680i[27];
extern const uint8_t gm_s0h2dl2t3n8dh[25];
extern const uint8_t gm_s0jtk73jm5ft9[16];
extern const uint8_t gm_s0lh70hs4of8e[22];
extern const uint8_t gm_s0o9ngaj56qhu[12];
extern const uint8_t gm_s0odi4lbspe7f[25];
extern const uint8_t gm_s0ucactj6hj0a[4];
extern const uint8_t gm_s16vnj1m5rn6t[9];
extern const uint8_t gm_s19jg4ct3aq11[6];
extern const uint8_t gm_s227dnd7n32vg[12];
extern const uint8_t gm_s2431f6f423u2[21];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s274psem80gbe[10];
extern const uint8_t gm_s28fmt6o4roj7[10];
extern const uint8_t gm_s2e2o3u4kr3q0[5];
extern const uint8_t gm_s2ht50jletjjv[23];
extern const uint8_t gm_s37v6u2dqp80f[5];
extern const uint8_t gm_s3i94v2v4ifm3[17];
extern const uint8_t gm_s3o8dsbklmfsh[10];
extern const uint8_t gm_s4dqb8b35j0gp[24];
extern const uint8_t gm_s4j3ecogbjkvn[13];
extern const uint8_t gm_s4n5m9kt2s9ld[27];
extern const uint8_t gm_s5g1e6qrps9q6[19];
extern const uint8_t gm_s5herb5iq95im[5];
extern const uint8_t gm_s5o6ultk7ms9u[16];
extern const uint8_t gm_s62maodcvu607[20];
extern const uint8_t gm_s79s3h98matgr[6];
extern const uint8_t gm_s7j2u3ofu66j8[5];
extern const uint8_t gm_s7mc3uqhc5a41[14];
extern const uint8_t gm_s7s8viq00e4g2[15];
extern const uint8_t gm_s8ilcu32tpscb[21];
extern const uint8_t gm_s8uvhki57nqp6[30];
extern const uint8_t gm_s8v8krjfns655[20];
extern const uint8_t gm_s9cg870cl2t07[6];
extern const uint8_t gm_s9eiod3sq367m[7];
extern const uint8_t gm_s9sif80bjh9f7[12];
extern const uint8_t gm_sb3sdaa9odag6[13];
extern const uint8_t gm_sb745qqv6kdrg[11];
extern const uint8_t gm_sbo52miqad506[4];
extern const uint8_t gm_sc6qhprasnd4f[25];
extern const uint8_t gm_sc7ertn8kmtl3[4];
extern const uint8_t gm_sc8o4oaph9gbr[2];
extern const uint8_t gm_scbch1b3qo8hr[9];
extern const uint8_t gm_scbq2ng311vcu[18];
extern const uint8_t gm_schmrvl8mk83l[9];
extern const uint8_t gm_scifaj4dn1ok5[6];
extern const uint8_t gm_scj618tc62815[15];
extern const uint8_t gm_scmaavulfehmr[5];
extern const uint8_t gm_sd80su3vlvhjt[27];
extern const uint8_t gm_sdo5tfkgp02tp[26];
extern const uint8_t gm_sdopt7plujc7n[16];
extern const uint8_t gm_sdujhgrk0oln8[5];
extern const uint8_t gm_se23d1hoq85r1[5];
extern const uint8_t gm_se24c7hpp401n[11];
extern const uint8_t gm_se287i5v97ugv[8];
extern const uint8_t gm_se9jjcum4l3k9[32];
extern const uint8_t gm_segcvuj6bu7e4[17];
extern const uint8_t gm_segm7or15hs5h[20];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sfc9avb7sceba[21];
extern const uint8_t gm_sfkmkcagsnqnb[7];
extern const uint8_t gm_sg7of7eks8a5u[7];
extern const uint8_t gm_sgge76tb2bmvi[32];
extern const uint8_t gm_sgh9nkjgl73of[27];
extern const uint8_t gm_sgpokbp1avpcv[14];
extern const uint8_t gm_sgpr1l6rnrmel[20];
extern const uint8_t gm_sh0h8t5us2qkm[15];
extern const uint8_t gm_shf0g22rjm3o7[5];
extern const uint8_t gm_shqein5ip3gpn[19];
extern const uint8_t gm_si702r0b37nml[6];
extern const uint8_t gm_sidpdvffdra02[14];
extern const uint8_t gm_sius9pki5u4e3[5];
extern const uint8_t gm_siv8n5ojlp0dd[18];
extern const uint8_t gm_sj3m78jf2dftr[24];
extern const uint8_t gm_sjl83c3218sse[19];
extern const uint8_t gm_sk4i403fuak6l[19];
extern const uint8_t gm_ska23dbjvthl3[15];
extern const uint8_t gm_sktnfpvsdatf3[22];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl775fgtt04iu[15];
extern const uint8_t gm_sla3alt5340mf[8];
extern const uint8_t gm_slogp373o1rii[24];
extern const uint8_t gm_smga32pj96fal[11];
extern const uint8_t gm_smhebipq24jdo[28];
extern const uint8_t gm_smjr08rujacsf[9];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_sn5naisar83li[16];
extern const uint8_t gm_sn5qnjkjnmu95[23];
extern const uint8_t gm_so44j4cr69vhs[16];
extern const uint8_t gm_so4t3hmedbui5[7];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_soh6rol6f5r5k[27];
extern const uint8_t gm_solaeibjdukdj[24];
extern const uint8_t gm_spasf3abnfbjh[8];
extern const uint8_t gm_spgshrus7teti[24];
extern const uint8_t gm_spm7tm8a050b3[7];
extern const uint8_t gm_spnjhvas2rum4[15];
extern const uint8_t gm_spqqkr6igks2d[28];
extern const uint8_t gm_sq0p2ifj52tjf[8];
extern const uint8_t gm_sq3gi0chb3cs2[3];
extern const uint8_t gm_sq8aqd2uj6ppq[12];
extern const uint8_t gm_squedv51820an[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcfpg6jsb3cfp6[];
extern const char gm_srcgp1oqfehgsjb[];
extern const char gm_srcjeqrqq5omjoo[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srod2911mer3j[28];
extern const uint8_t gm_srssp1uhlkrll[20];
extern const uint8_t gm_srtlfdmpif4qk[9];
extern const uint8_t gm_srv7efdnr211j[12];
extern const uint8_t gm_ssvcneuafnfds[8];
extern const uint8_t gm_st0lgjd5uosa2[6];
extern const uint8_t gm_st3mnofi7dhrs[19];
extern const uint8_t gm_st9dp4tkd7f7l[13];
extern const uint8_t gm_stdehenbgfc4h[13];
extern const uint8_t gm_stpc2dcrflnth[63];
extern const uint8_t gm_stq6t1a4hvefn[14];
extern const uint8_t gm_sttasaa2bp05i[22];
extern const uint8_t gm_stv59gn45ivj9[37];
extern const uint8_t gm_su0qghe21d3de[26];
extern const uint8_t gm_sujli4dml3ipq[8];
extern const uint8_t gm_sunu1gosuevl4[15];
extern const uint8_t gm_suod5ocif7o5v[5];
extern const uint8_t gm_surog9s0b8r6p[20];
extern const uint8_t gm_svivu54u4uhtg[8];
extern const uint8_t gm_svrjvsqtthedc[19];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) uint8_t gm_f_after_value_1vi101i(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_after_value_1vi101i F;
    uint8_t gm_l0; /* st */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    uint8_t t2 = t1;
    uint8_t t3;
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)&t2));
    if (!(t6 == ((uint8_t)118ULL))) goto mn5;
    t3 = ((uint8_t)44ULL);
    goto me4;
mn5:;
    uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)&t2));
    if (!(t10 == ((uint8_t)91ULL))) goto pa9;
    goto po8;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)&t2));
    if (!(t12 == ((uint8_t)97ULL))) goto pa11;
    goto po8;
pa11:;
    goto mn7;
po8:;
    t3 = ((uint8_t)59ULL);
    goto me4;
mn7:;
    t3 = ((uint8_t)101ULL);
    goto me4;
mn13:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 238, 36);
me4:;
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_array_k32ana3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_array_k32ana3 F;
    gm_b16_8 gm_l0; /* items */
    GM_STACK_CHECK(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_0202sdh_3((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_qmfcuqk_2(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcgp1oqfehgsjb, 42, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_t8vuock3jnv59 t8;
        GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)4ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t8 + 8), t7);
        uint8_t t9 = 0;
        F.ret = t8;
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
inline __attribute__((aligned(4))) void gm_f_as_array_nfvj8pk(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_array_nfvj8pk F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_b16_8 gm_l1; /* xs */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)4ULL)) goto mn3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    gm_b16_8 t6 = gm_l1;
    gm_th2lqbo77pvsc t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t7), t6);
    ((uint8_t *)&t7)[16] = 1;
    t1 = t7;
    goto me2;
mn3:;
    gm_th2lqbo77pvsc t9;
    memset(&t9, 0, sizeof t9);
    t1 = t9;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 57, 41);
me2:;
    F.ret = t1;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_as_bool_g9idbdk(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_bool_g9idbdk F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    uint8_t gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tqs5p4n2tq19v t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)1ULL)) goto mn3;
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    uint8_t t6 = gm_l1;
    gm_tqs5p4n2tq19v t7;
    GM_ST(uint8_t, ((uint8_t *)&t7), t6);
    ((uint8_t *)&t7)[1] = 1;
    t1 = t7;
    goto me2;
mn3:;
    gm_tqs5p4n2tq19v t9;
    memset(&t9, 0, sizeof t9);
    t1 = t9;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 62, 37);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_as_str_9rhg9v0(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_str_9rhg9v0 F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)3ULL)) goto mn3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    gm_b16_8 t6 = gm_l1;
    gm_th2lqbo77pvsc t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t7), t6);
    ((uint8_t *)&t7)[16] = 1;
    t1 = t7;
    goto me2;
mn3:;
    gm_th2lqbo77pvsc t9;
    memset(&t9, 0, sizeof t9);
    t1 = t9;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 52, 38);
me2:;
    F.ret = t1;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_bool_value_j9bnrp8(void *gm_out, uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bool_value_j9bnrp8 F;
    uint8_t gm_l0; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 26u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    gm_t8vuock3jnv59 t2;
    GM_ST(uint8_t, (uint8_t *)&t2 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t2 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t2 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t2 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t2 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t2 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t2 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)1ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 8), t1);
    F.ret = t2;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_component_k9b35ju(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_component_k9b35ju F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tf5srh4mp4b3i t2 = gm_f_as_u64_0obhk18(&(*(gm_t8vuock3jnv59 *)((uint8_t *)a0)));
        gm_tf5srh4mp4b3i t3 = t2;
        uint64_t t4;
        if (((((uint8_t *)&t3))[8] != 0)) {
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            uint8_t t6 = 0;
            F.ret = ((uint32_t)0ULL);
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        uint64_t t8 = gm_l1;
        uint8_t t9 = (t8 > ((uint64_t)4294967295ULL));
        if (!t9) goto ie7;
        {
            uint8_t t11 = 0;
            F.ret = ((uint32_t)4294967295ULL);
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint64_t t12 = gm_l1;
        uint32_t t13 = (uint32_t)t12;
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_57n7jqd F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_722uua4(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint8_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) goto mn8;
        t6 = ((uint8_t)0);
        goto me7;
mn8:;
        t6 = ((uint8_t)1);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 153, 12);
me7:;
        uint8_t t10 = 0;
        F.ret = t6;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_c624pm6(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_c624pm6 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_c9mffp1(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint8_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) goto mn8;
        t6 = ((uint8_t)0);
        goto me7;
mn8:;
        t6 = ((uint8_t)1);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 153, 12);
me7:;
        uint8_t t10 = 0;
        F.ret = t6;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_eq02jnv(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_eq02jnv F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_4uea0u1(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint8_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) goto mn8;
        t6 = ((uint8_t)0);
        goto me7;
mn8:;
        t6 = ((uint8_t)1);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 153, 12);
me7:;
        uint8_t t10 = 0;
        F.ret = t6;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_deinit_l8j91qd(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_l8j91qd F;
    void * gm_l0; /* d */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 24);
        gm_f_deinit_sejdpqv(t3);
    }
    {
        void * t4 = gm_l0;
        gm_t8vuock3jnv59 t5;
        GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t5 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t5 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t8vuock3jnv59, (((uint8_t *)t4) + 0), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_destroy_602qeev(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_destroy_602qeev F;
    void * gm_l0; /* p */
    gm_t3mf58gv9kf3n gm_l2; /* a */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_node *pn2 = gm_find(cx, 587034433);
        if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_src2ah81i3aa8hm, 708, 10);
        gm_t3mf58gv9kf3n t3;
        t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
    }
    {
        void * t4 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 16));
        void * t6 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        void * t7 = gm_l0;
        void * t8 = t7;
        void * t9 = t5;
        gm_b0_1 t10;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t9))(t6, t8, ((uint64_t)48ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_diagnostics_n3kkhf4 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 60, 0, 4);
    gm_trqfvmjtt4sij t1;
    gm_f_list_prbqo9j((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 40, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 48, ((uint64_t)0ULL));
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 56, ((uint32_t)0ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dir_of_ho73hue(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_dir_of_ho73hue F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_th2lqbo77pvsc t2;
    gm_f_parent_lk0iero((void *)&t2, (void *)NULL, cx, NULL, t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_th2lqbo77pvsc t3 = t2;
    gm_b16_8 t4;
    if (((((uint8_t *)&t3))[16] != 0)) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
        t4 = t5;
    } else {
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(0));
        t4 = t6;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1th396a F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 184)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_49nm4li(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_49nm4li F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 64)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5ns1sqk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5ns1sqk F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 56)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8jt91p4 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 80)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ab79bmt_2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d0vhctu(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d0vhctu F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 64)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dheabhi_3 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)1ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lqcrtc5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lqcrtc5 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oq9aajs(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oq9aajs F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vu26gaj F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_encoding_name_m6926rk(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_encoding_name_m6926rk F;
    gm_t9qkh0rsooc8d gm_l0; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_st0lgjd5uosa2));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(5));
    t1 = t5;
    goto me2;
mn3:;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t7 != ((uint8_t)1ULL)) goto mn6;
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s9eiod3sq367m));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(6));
    t1 = t8;
    goto me2;
mn6:;
    gm_trap("no match arm matched the scrutinee", gm_srcjeqrqq5omjoo, 8, 47);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_envelope_4dp2tms(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_envelope_4dp2tms F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_obj_aoa7fl7((void *)&F.l0, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l0);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svivu54u4uhtg));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sc7ertn8kmtl3));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
        gm_t8vuock3jnv59 t5;
        gm_f_text_ls49dp6((void *)&t5, t4);
        gm_b0_1 t6 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t2, t3, &t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l0), 40);
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
inline __attribute__((aligned(4))) void gm_f_fail_8tj0r64(void *gm_out, int64_t a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fail_8tj0r64 F;
    int64_t gm_l0; /* code */
    gm_b16_8 gm_l1; /* msg */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    int64_t t1 = gm_l0;
    GM_ST(int64_t, (uint8_t *)&(*(gm_thgmovcqlsivv *)gm_out) + 0, t1);
    gm_b16_8 t2 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_thgmovcqlsivv *)gm_out) + 8, t2);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_field_7904fsq(void *gm_out, const void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_field_7904fsq F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_b16_8 gm_l1; /* key */
    GM_STACK_CHECK_SHALLOW(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    gm_t3ph3kbiqt1ah t2;
    gm_f_get_fjdb5ou((void *)&t2, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a0)), t1);
    gm_t3ph3kbiqt1ah t3 = t2;
    gm_t8vuock3jnv59 t4;
    if (((((uint8_t *)&t3))[24] != 0)) {
        gm_t8vuock3jnv59 t5 = GM_LD(gm_t8vuock3jnv59, (((uint8_t *)&t3) + 0));
        t4 = t5;
    } else {
        gm_t8vuock3jnv59 t6;
        GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t6 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t6 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
        t4 = t6;
    }
    F.ret = t4;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_file_id_mif69v8(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_file_id_mif69v8 F;
    void * gm_l0; /* a */
    gm_b16_8 gm_l1; /* path */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 480);
    gm_b16_8 t3 = gm_l1;
    gm_t9805qtonbsag t4 = gm_f_get_m1knqqk(t2, t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_file_name_em7v8lv F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 288u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_tl7hpf5nlilqq t2;
    gm_f_split_cbk4ks8((void *)&t2, (void *)NULL, cx, NULL, t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tl7hpf5nlilqq t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)&t3) + 16) + 0));
    gm_b16_8 t5 = t4;
    void * t6 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t5)));
    uint64_t t7 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t5) + 8);
    uint64_t t8 = (t7 - 1);
    gm_th2lqbo77pvsc t9;
    if (t8 < t7) {
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)t6 + t8 * 16));
    gm_th2lqbo77pvsc t11;
    GM_ST(uint8_t, (uint8_t *)&t11 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t11 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t11 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[16] = 1;
        t9 = t11;
    } else {
    gm_th2lqbo77pvsc t12;
    memset(&t12, 0, sizeof t12);
        t9 = t12;
    }
    F.ret = t9;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6m9fu50(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_6m9fu50 F;
    void * gm_l0; /* x */
    void * gm_l1; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)t1));
    void * t3 = gm_l1;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)t3));
    uint8_t t5 = gm_f_path_less_702ltp5(t2, t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_pt42u9t(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_pt42u9t F;
    void * gm_l0; /* x */
    void * gm_l1; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)t1));
    void * t3 = gm_l1;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)t3));
    uint8_t t5 = gm_f_path_less_702ltp5(t2, t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_r1rj4g9(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_r1rj4g9 F;
    void * gm_l0; /* x */
    void * gm_l1; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)t1));
    void * t3 = gm_l1;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)t3));
    uint8_t t5 = gm_f_path_less_702ltp5(t2, t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1qm1hvv(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_1qm1hvv F;
    GM_STACK_CHECK(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tggvvh3lncvcg t3 = GM_LD(gm_tggvvh3lncvcg, ((uint8_t *)&F.l0));
    F.l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_open_dirs_9ud02ot *)((&F.h)->up))->l1);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t7 = gm_f_dir_of_ho73hue((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t8 = gm_f_push_3ef746b(t5, t7);
        gm_tftb1odc739t5 t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_srcfpg6jsb3cfp6, 529, 36);
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 529, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_9nsvqlg(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_9nsvqlg F;
    gm_tpe1hv3fekn05 gm_l0; /* <item> */
    gm_tpe1hv3fekn05 gm_l1; /* kv */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tpe1hv3fekn05 t3 = GM_LD(gm_tpe1hv3fekn05, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_watched_files_changed_qvnrc63 *)((&F.h)->up))->l0));
        void * t6 = (void *)(((uint8_t *)t5) + 120);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 0));
        gm_tftb1odc739t5 t8 = gm_f_put_5c6qgui(t6, t7, ((uint8_t)1));
        gm_tftb1odc739t5 t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_srcfpg6jsb3cfp6, 520, 44);
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 520, 9);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2u6ls4(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_i2u6ls4 F;
    GM_STACK_CHECK(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tggvvh3lncvcg t3 = GM_LD(gm_tggvvh3lncvcg, ((uint8_t *)&F.l0));
    F.l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_overlays_6rdbiin *)((&F.h)->up))->l1);
        gm_t6j6smj7qoctu t6;
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 0, t7);
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l1) + 16) + 24));
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 16, t8);
        gm_tftb1odc739t5 t9 = gm_f_push_302k12h(t5, &t6);
        gm_tftb1odc739t5 t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srcfpg6jsb3cfp6, 549, 36);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 549, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_s23qjlr(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_s23qjlr F;
    GM_STACK_CHECK(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tggvvh3lncvcg t3 = GM_LD(gm_tggvvh3lncvcg, ((uint8_t *)&F.l0));
    F.l1 = t3;
    {
        void * t5 = (void *)(((uint8_t *)&((struct gm_fr_uri_map_mhud6ak *)((&F.h)->up))->l1) + 0);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l1) + 16) + 0));
        gm_tftb1odc739t5 t8 = gm_f_put_7shfu9u(t5, t6, t7);
        gm_tftb1odc739t5 t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_srcfpg6jsb3cfp6, 555, 36);
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 555, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_sf9naq7(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_sf9naq7 F;
    GM_STACK_CHECK(&F, 138u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tggvvh3lncvcg t3 = GM_LD(gm_tggvvh3lncvcg, ((uint8_t *)&F.l0));
    F.l1 = t3;
    {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t7 = gm_f_dir_of_ho73hue((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_did_close_f9lpcgi *)((&F.h)->up))->l5));
        gm_b16_8 t9 = t7;
        gm_b16_8 t10 = t8;
        uint8_t t11 = (gm_eq0iqiubfm7e(((uint8_t *)&t9), ((uint8_t *)&t10)));
        if (!t11) goto ie5;
        {
            gm_b0_1 t13;
            uint8_t t14 = 0;
            ((struct gm_fr_did_close_f9lpcgi *)((&F.h)->up))->ret = t13;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t14;
            goto gm_lad;
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 448, 5);
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
inline __attribute__((aligned(4))) void * gm_f_get_lco3u6e(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_lco3u6e F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_c9mffp1(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            uint8_t t8 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 119, 12);
        void * t13 = GM_LD(void *, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 32) + 24));
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_get_ptr_egf3gtq(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ptr_egf3gtq F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_4uea0u1(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            uint8_t t8 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 127, 13);
        void * t13 = (void *)(((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 64) + 24);
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_errors_if7ai0m F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 40));
    uint8_t t3 = (t2 > ((uint64_t)0ULL));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_8h9shb4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_8h9shb4 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_5ns1sqk();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_cuh4fs9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_k21v01c();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_irta8m9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_irta8m9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_49nm4li();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_mkadlk3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_mkadlk3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_d0vhctu();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_p3t5tj8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_vu26gaj();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_s1unuf2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_s1unuf2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_lqcrtc5();
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
inline __attribute__((aligned(4))) void gm_f_int_95caidg(void *gm_out, void *env, gm_node *cx, gm_node *inst, int64_t a0) {
    struct gm_fr_int_95caidg F;
    int64_t gm_l0; /* n */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1 = gm_l0;
    gm_tm98ap4112hee t2;
    GM_ST(int64_t, ((uint8_t *)&t2 + 0), t1);
    gm_tabb710d9mq2e t3;
    gm_f_format_gej351p((void *)&t3, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t4 = t3;
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcgp1oqfehgsjb, 33, 74);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
    gm_b16_8 t6 = t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_tnpifv20irnph t8;
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
    gm_t8vuock3jnv59 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)2ULL));
    GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t9 + 8), t8);
    F.ret = t9;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_true_3osltpn(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_true_3osltpn F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    uint8_t gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 31u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)1ULL)) goto mn3;
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    uint8_t t6 = gm_l1;
    t1 = t6;
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 89, 36);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_lex_dc8mbmf F;
    uint32_t gm_l0; /* file */
    gm_b16_8 gm_l1; /* src */
    void * gm_l2; /* diags */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        void * t4 = gm_l2;
        gm_f_new_lexer_dm12g12((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l3);
        gm_f_run_v26is74((void *)&(*(gm_tr8dvlu9ik4gj *)gm_out), (void *)NULL, cx, NULL, t5);
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
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0202sdh_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_dheabhi_3();
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
inline __attribute__((aligned(4))) void gm_f_list_792g2go_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_792g2go_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ab79bmt_2();
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
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_bvkp9ir F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8jt91p4();
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
inline __attribute__((aligned(4))) void gm_f_list_j41vvuc(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_j41vvuc F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_oq9aajs();
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
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_prbqo9j F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1th396a();
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
inline __attribute__((aligned(4))) void gm_f_obj_aoa7fl7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_obj_aoa7fl7 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_792g2go_2((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tin97tt4ud0s1 *)gm_out) + 0, t1);
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
inline __attribute__((aligned(4))) uint32_t gm_f_offset_at_73sli4j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    struct gm_fr_offset_at_73sli4j F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* line */
    uint32_t gm_l3; /* character */
    gm_t6j6smj7qoctu gm_l4; /* ix */
    GM_STACK_CHECK(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint32_t t4 = gm_l1;
        gm_f_lines_0mpphhg((void *)&gm_l4, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&gm_l4);
        uint32_t t6 = gm_l2;
        uint32_t t7 = gm_l3;
        void * t8 = gm_l0;
        gm_t9qkh0rsooc8d t9 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t8) + 8));
        uint64_t t10 = gm_f_offset_hdhse2v(t5, t6, t7, t9);
        uint32_t t11 = (uint32_t)t10;
        uint8_t t12 = 0;
        F.ret = t11;
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
inline __attribute__((aligned(4))) void gm_f_path2_enc7vaj(void *gm_out, const void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_path2_enc7vaj F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_b16_8 gm_l1; /* a */
    gm_b16_8 gm_l2; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l1;
    gm_t8vuock3jnv59 t2;
    gm_f_field_7904fsq((void *)&t2, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a0)), t1);
    gm_b16_8 t3 = gm_l2;
    gm_f_field_7904fsq((void *)&(*(gm_t8vuock3jnv59 *)gm_out), &t2, t3);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_release_7gtbra1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_release_7gtbra1 F;
    void * gm_l0; /* a */
    void * gm_l1; /* ar */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l1;
        gm_f_deinit_sejdpqv(t4);
    }
    {
        void * t5 = gm_l1;
        gm_b0_1 t6 = gm_f_destroy_602qeev((void *)NULL, cx, NULL, t5, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3h72k4hfps)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_send_cg3jp71(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_send_cg3jp71 F;
    gm_t8vuock3jnv59 gm_l0; /* msg */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_to_text_2cum4i7((void *)NULL, cx, NULL, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t3 = gm_f_write_message_bp6uv22((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) void gm_f_sort_by_s9l01h6(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_s9l01h6 F;
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
        gm_f_introsort_r6p6ckl(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_source_map_4v8dg4b F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_bvkp9ir((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tin97tt4ud0s1 *)gm_out) + 0, t1);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_source_text_7ru912k(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_source_text_7ru912k F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* file */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    uint32_t t3 = gm_l1;
    void * t4 = gm_f_file_o15fqku(t2, t3);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 16));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_text_ls49dp6(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_text_ls49dp6 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_t8vuock3jnv59 t2;
    GM_ST(uint8_t, (uint8_t *)&t2 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t2 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t2 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)3ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t2 + 8), t1);
    F.ret = t2;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_text_2cum4i7(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_text_2cum4i7 F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    GM_STACK_CHECK(&F, 113u);
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
        gm_b0_1 t3 = gm_f_write_value_5haur8t((void *)NULL, cx, NULL, t2, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t5 = t4;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
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
inline __attribute__((aligned(4))) void gm_f_truncate_ov4pgoq(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_ov4pgoq F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_vir0vu7(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_vir0vu7 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_uri_of_t6r79gp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_uri_of_t6r79gp F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* u */
    GM_STACK_CHECK(&F, 218u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 0);
        gm_b16_8 t5 = gm_l1;
        gm_th2lqbo77pvsc t6;
        gm_f_get_e3joqcc((void *)&t6, t4, t5);
        gm_th2lqbo77pvsc t7 = t6;
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l2 = t8;
        {
            gm_b16_8 t10 = gm_l2;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t12 = gm_l1;
        gm_b16_8 t13 = gm_f_path_to_uri_as8bikc((void *)NULL, cx, NULL, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) void gm_f_value_pkuqal3(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_value_pkuqal3 F;
    void * gm_l0; /* o */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)t1) + 0) + 0));
    gm_t8vuock3jnv59 t3;
    GM_ST(uint8_t, (uint8_t *)&t3 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t3 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t3 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)5ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 8), t2);
    F.ret = t3;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_workspace_ubjeikp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_workspace_ubjeikp F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t4c7ipjj9s3fp t1;
    gm_f_hash_map_8h9shb4((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_ttiitul0263ld *)gm_out) + 0, t1);
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
extern __attribute__((aligned(4))) uint8_t gm_f_after_value_1vi101i(uint8_t a0);
__attribute__((aligned(4))) void gm_f_code_actions_k897tcq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_code_actions_k897tcq F;
    gm_t8vuock3jnv59 gm_l0; /* params */
    gm_th2lqbo77pvsc gm_l1; /* only */
    gm_b16_8 gm_l2; /* kinds */
    uint8_t gm_l3; /* any */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t8vuock3jnv59 gm_l6; /* k */
    gm_b16_8 gm_l7; /* s */
    gm_b16_8 gm_l9; /* diags */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t8vuock3jnv59 gm_l12; /* d */
    gm_b16_8 gm_l13; /* fixes */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t8vuock3jnv59 gm_l16; /* fx */
    gm_tq47bkpmd2b9a gm_l18; /* <temporary array> */
    GM_STACK_CHECK(&F, 1588u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sq0p2ifj52tjf));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(7));
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdujhgrk0oln8));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        gm_t8vuock3jnv59 t4;
        gm_f_path2_enc7vaj((void *)&t4, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a0)), t2, t3);
        gm_f_as_array_nfvj8pk((void *)&gm_l1, &t4);
    }
    {
        if (!((((uint8_t *)&gm_l1))[16] != 0)) goto ie5;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
        gm_l2 = t6;
        {
            gm_l3 = ((uint8_t)0);
        }
        {
            {
                gm_b16_8 t9 = gm_l2;
                gm_l4 = t9;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t11 = gm_l5;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t13 = (t11 < t12);
                    if (!t13) goto lb10;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_t8vuock3jnv59 t21 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t17 * 24));
                        gm_l6 = t21;
                        {
                            gm_th2lqbo77pvsc t24;
                            gm_f_as_str_9rhg9v0((void *)&t24, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l6)));
                            gm_th2lqbo77pvsc t25 = t24;
                            if (!((((uint8_t *)&t25))[16] != 0)) goto ie23;
                            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
                            gm_l7 = t26;
                            {
                                gm_b16_8 t29;
                                GM_ST(void *, (uint8_t *)&t29, (void *)(gm_srtlfdmpif4qk));
                                GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(8));
                                gm_b16_8 t30 = gm_l7;
                                uint8_t t31 = gm_f_starts_with_6vvt01e(t29, t30);
                                if (!t31) goto ie28;
                                {
                                    gm_l3 = ((uint8_t)1);
                                }
                                goto ix28;
ie28:;
ix28:;
                            }
                            goto ix23;
ie23:;
ix23:;
                        }
                        goto me19;
mn20:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 74, 9);
me19:;
                    }
lc10:;
                    {
                        uint64_t t34 = gm_l5;
                        uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                        uint64_t t37 = r35;
                        gm_l5 = t37;
                    }
                }
lb10:;
            }
        }
        {
            uint8_t t39 = gm_l3;
            uint8_t t40 = (uint8_t)!t39;
            if (!t40) goto ie38;
            {
                gm_b16_8 t42 = gm_f_empty_slice_dheabhi_3();
                gm_b16_8 t43 = t42;
                gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
                gm_f_array_k32ana3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), (void *)NULL, cx, NULL, t44);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix38;
ie38:;
ix38:;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_f_list_0202sdh_3((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sq0p2ifj52tjf));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(7));
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sq8aqd2uj6ppq));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(11));
        gm_t8vuock3jnv59 t47;
        gm_f_path2_enc7vaj((void *)&t47, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a0)), t45, t46);
        gm_th2lqbo77pvsc t48;
        gm_f_as_array_nfvj8pk((void *)&t48, &t47);
        gm_th2lqbo77pvsc t49 = t48;
        gm_b16_8 t50;
        if (((((uint8_t *)&t49))[16] != 0)) {
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 0));
            t50 = t51;
        } else {
            gm_b16_8 t52 = gm_f_empty_slice_dheabhi_3();
            t50 = t52;
        }
        gm_l9 = t50;
    }
    {
        {
            gm_b16_8 t54 = gm_l9;
            gm_l10 = t54;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t56 = gm_l11;
                uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t58 = (t56 < t57);
                if (!t58) goto lb55;
                {
                    uint64_t t61 = gm_l11;
                    uint64_t t62 = (uintptr_t)t61;
                    uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_t8vuock3jnv59 t66 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t62 * 24));
                    gm_l12 = t66;
                    {
                        gm_b16_8 t68;
                        GM_ST(void *, (uint8_t *)&t68, (void *)(gm_se23d1hoq85r1));
                        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(4));
                        gm_b16_8 t69;
                        GM_ST(void *, (uint8_t *)&t69, (void *)(gm_si702r0b37nml));
                        GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(5));
                        gm_t8vuock3jnv59 t70;
                        gm_f_path2_enc7vaj((void *)&t70, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l12)), t68, t69);
                        gm_th2lqbo77pvsc t71;
                        gm_f_as_array_nfvj8pk((void *)&t71, &t70);
                        gm_th2lqbo77pvsc t72 = t71;
                        gm_b16_8 t73;
                        if (((((uint8_t *)&t72))[16] != 0)) {
                            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 0));
                            t73 = t74;
                        } else {
                            goto lc55;
                        }
                        gm_l13 = t73;
                    }
                    {
                        {
                            gm_b16_8 t76 = gm_l13;
                            gm_l14 = t76;
                        }
                        {
                            gm_l15 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t78 = gm_l15;
                                uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                uint8_t t80 = (t78 < t79);
                                if (!t80) goto lb77;
                                {
                                    uint64_t t83 = gm_l15;
                                    uint64_t t84 = (uintptr_t)t83;
                                    uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                    gm_t8vuock3jnv59 t88 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t84 * 24));
                                    gm_l16 = t88;
                                    {
                                        gm_f_obj_aoa7fl7((void *)&F.l17, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_ll81;
                                        }
                                    }
                                    {
                                        void * t90 = (void *)((uint8_t *)&F.l17);
                                        gm_b16_8 t91;
                                        GM_ST(void *, (uint8_t *)&t91, (void *)(gm_scifaj4dn1ok5));
                                        GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(5));
                                        gm_b16_8 t92;
                                        GM_ST(void *, (uint8_t *)&t92, (void *)(gm_scifaj4dn1ok5));
                                        GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(5));
                                        gm_t8vuock3jnv59 t93;
                                        gm_f_field_7904fsq((void *)&t93, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l16)), t92);
                                        gm_b0_1 t94 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t90, t91, &t93);
                                        if (gm_unw.kind) {
                                            goto gm_ll81;
                                        }
                                    }
                                    {
                                        void * t95 = (void *)((uint8_t *)&F.l17);
                                        gm_b16_8 t96;
                                        GM_ST(void *, (uint8_t *)&t96, (void *)(gm_s7j2u3ofu66j8));
                                        GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(4));
                                        gm_b16_8 t97;
                                        GM_ST(void *, (uint8_t *)&t97, (void *)(gm_srtlfdmpif4qk));
                                        GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(8));
                                        gm_t8vuock3jnv59 t98;
                                        gm_f_text_ls49dp6((void *)&t98, t97);
                                        gm_b0_1 t99 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t95, t96, &t98);
                                        if (gm_unw.kind) {
                                            goto gm_ll81;
                                        }
                                    }
                                    {
                                    }
                                    {
                                        void * t100 = (void *)((uint8_t *)&F.l17);
                                        gm_b16_8 t101;
                                        GM_ST(void *, (uint8_t *)&t101, (void *)(gm_sq8aqd2uj6ppq));
                                        GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(11));
                                        {
                                            gm_t8vuock3jnv59 t103 = gm_l12;
                                            GM_ST(gm_t8vuock3jnv59, (uint8_t *)&(*(gm_tq47bkpmd2b9a *)((uint8_t *)&gm_l18)) + 0, t103);
                                        }
                                        void * t104 = ((uint8_t *)&gm_l18);
                                        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcfpg6jsb3cfp6, 89, 40);
                                        gm_b16_8 t105;
                                        GM_ST(void *, (uint8_t *)&t105, (void *)(((uint8_t *)t104 + (uintptr_t)0 * 24)));
                                        GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                                        gm_b16_8 t106 = t105;
                                        gm_t8vuock3jnv59 t107;
                                        gm_f_array_k32ana3((void *)&t107, (void *)NULL, cx, NULL, t106);
                                        if (gm_unw.kind) {
                                            goto gm_ll81;
                                        }
                                        gm_b0_1 t108 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t100, t101, &t107);
                                        if (gm_unw.kind) {
                                            goto gm_ll81;
                                        }
                                    }
                                    {
                                        void * t109 = (void *)((uint8_t *)&F.l17);
                                        gm_b16_8 t110;
                                        GM_ST(void *, (uint8_t *)&t110, (void *)(gm_s9sif80bjh9f7));
                                        GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(11));
                                        uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                        uint8_t t112 = ((t111 == ((uint64_t)1ULL)));
                                        gm_t8vuock3jnv59 t113;
                                        gm_f_bool_value_j9bnrp8((void *)&t113, t112);
                                        gm_b0_1 t114 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t109, t110, &t113);
                                        if (gm_unw.kind) {
                                            goto gm_ll81;
                                        }
                                    }
                                    {
                                        gm_f_obj_aoa7fl7((void *)&F.l19, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_ll81;
                                        }
                                    }
                                    {
                                        void * t115 = (void *)((uint8_t *)&F.l19);
                                        gm_b16_8 t116;
                                        GM_ST(void *, (uint8_t *)&t116, (void *)(gm_ssvcneuafnfds));
                                        GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(7));
                                        gm_b16_8 t117;
                                        GM_ST(void *, (uint8_t *)&t117, (void *)(gm_ssvcneuafnfds));
                                        GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(7));
                                        gm_t8vuock3jnv59 t118;
                                        gm_f_field_7904fsq((void *)&t118, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l16)), t117);
                                        gm_b0_1 t119 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t115, t116, &t118);
                                        if (gm_unw.kind) {
                                            goto gm_ll81;
                                        }
                                    }
                                    {
                                        void * t120 = (void *)((uint8_t *)&F.l17);
                                        gm_b16_8 t121;
                                        GM_ST(void *, (uint8_t *)&t121, (void *)(gm_s5herb5iq95im));
                                        GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(4));
                                        void * t122 = (void *)((uint8_t *)&F.l19);
                                        gm_t8vuock3jnv59 t123;
                                        gm_f_value_pkuqal3((void *)&t123, t122);
                                        gm_b0_1 t124 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t120, t121, &t123);
                                        if (gm_unw.kind) {
                                            goto gm_ll81;
                                        }
                                    }
                                    {
                                        void * t125 = (void *)((uint8_t *)&F.l8);
                                        void * t126 = (void *)((uint8_t *)&F.l17);
                                        gm_t8vuock3jnv59 t127;
                                        gm_f_value_pkuqal3((void *)&t127, t126);
                                        gm_tftb1odc739t5 t128 = gm_f_push_0pgacsj_3(t125, &t127);
                                        gm_tftb1odc739t5 t129 = t128;
                                        if (((((uint8_t *)&t129))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t129) + 1, gm_srcfpg6jsb3cfp6, 94, 13);
                                        gm_b0_1 t130 = GM_LD(gm_b0_1, (((uint8_t *)&t129) + 1));
                                    }
                                    goto me86;
mn87:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 85, 9);
me86:;
                                }
lc77:;
                                {
                                    uint64_t t132 = gm_l15;
                                    uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                                    uint64_t t135 = r133;
                                    gm_l15 = t135;
                                }
                            }
gm_ll81:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb77; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc77; }
                            }
                            goto gm_ll59;
lb77:;
                        }
                    }
                    goto me64;
mn65:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 83, 5);
me64:;
                }
lc55:;
                {
                    uint64_t t137 = gm_l11;
                    uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                    uint64_t t140 = r138;
                    gm_l11 = t140;
                }
            }
gm_ll59:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb55; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc55; }
            }
            goto gm_lad;
lb55:;
        }
    }
    {
        gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t142 = t141;
        gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
        gm_f_array_k32ana3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), (void *)NULL, cx, NULL, t143);
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
__attribute__((aligned(4))) void gm_f_completion_r10tmh9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint32_t a2) {
    struct gm_fr_completion_r10tmh9 F;
    void * gm_l0; /* srv */
    gm_b16_8 gm_l1; /* path */
    uint32_t gm_l2; /* off_in */
    gm_b16_8 gm_l3; /* dir */
    void * gm_l5; /* a */
    uint32_t gm_l6; /* file */
    gm_b16_8 gm_l7; /* t */
    uint64_t gm_l8; /* off */
    gm_t2lqu8jsj511d gm_l10; /* mc */
    gm_tf361jk9mn3il gm_l11; /* cx */
    gm_b16_8 gm_l13; /* patched */
    uint8_t gm_l15; /* replaced */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    void * gm_l19; /* b */
    uint32_t gm_l20; /* f */
    gm_tf361jk9mn3il gm_l21; /* bcx */
    gm_tf361jk9mn3il gm_l23; /* cx */
    GM_STACK_CHECK(&F, 1902u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l1;
        gm_b16_8 t3 = gm_f_dir_of_ho73hue((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_f_uri_map_mhud6ak((void *)&F.l4, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 64);
        gm_b16_8 t7 = gm_l3;
        void * t8 = gm_f_get_lco3u6e(t6, t7);
        void * t9 = t8;
        void * t10;
        if ((GM_LD(void *, ((uint8_t *)&t9)) != 0)) {
            void * t11 = GM_LD(void *, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_t8vuock3jnv59 t12 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l5 = t10;
    }
    {
        void * t14 = gm_l5;
        gm_b16_8 t15 = gm_l1;
        gm_t9805qtonbsag t16 = gm_f_file_id_mif69v8(t14, t15);
        gm_t9805qtonbsag t17 = t16;
        uint32_t t18;
        if (((((uint8_t *)&t17))[4] != 0)) {
            uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&t17) + 0));
            t18 = t19;
        } else {
            gm_t8vuock3jnv59 t20 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        gm_l6 = t18;
    }
    {
        void * t22 = gm_l5;
        uint32_t t23 = gm_l6;
        gm_b16_8 t24 = gm_f_source_text_7ru912k(t22, t23);
        gm_l7 = t24;
    }
    {
        uint32_t t25 = gm_l2;
        uint64_t t26 = (uint64_t)t25;
        gm_l8 = t26;
    }
    {
        gm_f_list_0202sdh_3((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t28 = gm_l7;
        uint64_t t29 = gm_l8;
        gm_tc70a9dbbmgkg t30;
        gm_f_member_context_p58kq5l((void *)&t30, t28, t29);
        gm_tc70a9dbbmgkg t31 = t30;
        if (!((((uint8_t *)&t31))[16] != 0)) goto ie27;
        gm_t2lqu8jsj511d t32 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)&t31));
        gm_l10 = t32;
        {
            memset((uint8_t *)&gm_l11 + 9, 0, 7);
            void * t34 = gm_l5;
            GM_ST(void *, (uint8_t *)&gm_l11 + 0, t34);
            void * t35 = gm_l0;
            gm_t9qkh0rsooc8d t36 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t35) + 0));
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&gm_l11 + 8, t36);
            void * t37 = (void *)((uint8_t *)&F.l4);
            GM_ST(void *, (uint8_t *)&gm_l11 + 16, t37);
        }
        {
            void * t39 = (void *)((uint8_t *)&gm_l11);
            uint32_t t40 = gm_l6;
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 0));
            uint32_t t42 = (uint32_t)t41;
            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            uint32_t t44 = (uint32_t)t43;
            gm_tqetrgo0q24b5 t45;
            gm_f_members_8m2g0sl((void *)&t45, (void *)NULL, cx, NULL, t39, t40, t42, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqetrgo0q24b5 t46 = t45;
            if (!((((uint8_t *)&t46))[40] != 0)) goto ie38;
            gm_trqfvmjtt4sij t47 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t46));
            F.l12 = t47;
            {
                gm_trqfvmjtt4sij t49 = F.l12;
                F.l9 = t49;
            }
            goto ix38;
ie38:;
            {
                gm_t6j6smj7qoctu t51;
                uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                void * t53 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                uint64_t t54 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                uint64_t t56 = (uintptr_t)t55;
                if (t54 > t56 || t56 > (uintptr_t)t52) gm_trap_slice(t54, t56, t52, gm_srcfpg6jsb3cfp6, 775, 43);
                gm_b16_8 t57;
                GM_ST(void *, (uint8_t *)&t57, (void *)(((uint8_t *)t53 + t54 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(t56 - t54));
                GM_ST(gm_b16_8, (uint8_t *)&t51 + 0, t57);
                uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                void * t59 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                uint64_t t60 = gm_l8;
                uint64_t t61 = (uintptr_t)t60;
                if (t61 > (uintptr_t)t58 || (uintptr_t)t58 > (uintptr_t)t58) gm_trap_slice(t61, (uintptr_t)t58, t58, gm_srcfpg6jsb3cfp6, 775, 57);
                gm_b16_8 t62;
                GM_ST(void *, (uint8_t *)&t62, (void *)(((uint8_t *)t59 + t61 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)((uintptr_t)t58 - t61));
                GM_ST(gm_b16_8, (uint8_t *)&t51 + 16, t62);
                gm_tabb710d9mq2e t63;
                gm_f_format_8n626pi((void *)&t63, (void *)NULL, cx, NULL, &t51);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t64 = t63;
                if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 8, gm_srcfpg6jsb3cfp6, 775, 24);
                gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 8));
                gm_l13 = t65;
            }
            {
                void * t66 = gm_l0;
                gm_f_overlays_6rdbiin((void *)&F.l14, (void *)NULL, cx, NULL, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_l15 = ((uint8_t)0);
            }
            {
                {
                    gm_l17 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t68 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                    gm_l16 = t68;
                }
                {
                    for (;;) {
                        uint64_t t70 = gm_l17;
                        uint64_t t71 = gm_l16;
                        uint8_t t72 = (t70 < t71);
                        if (!t72) goto lb69;
                        {
                            uint64_t t75 = gm_l17;
                            uint64_t t76 = t75;
                            uint64_t t79 = GM_LD(uint64_t, ((uint8_t *)&t76));
                            gm_l18 = t79;
                            {
                                uint64_t t82 = gm_l18;
                                uint64_t t83 = (uintptr_t)t82;
                                uint64_t t84 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                                if (t83 >= (uintptr_t)(t84)) gm_trap_index(t83, t84, gm_srcfpg6jsb3cfp6, 779, 20);
                                gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)) + t83 * 32) + 0));
                                gm_b16_8 t86 = gm_l1;
                                gm_b16_8 t87 = t85;
                                gm_b16_8 t88 = t86;
                                uint8_t t89 = (gm_eq0iqiubfm7e(((uint8_t *)&t87), ((uint8_t *)&t88)));
                                if (!t89) goto ie81;
                                {
                                    uint64_t t91 = gm_l18;
                                    uint64_t t92 = (uintptr_t)t91;
                                    uint64_t t93 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                                    if (t92 >= (uintptr_t)(t93)) gm_trap_index(t92, t93, gm_srcfpg6jsb3cfp6, 780, 21);
                                    gm_b16_8 t94 = gm_l13;
                                    gm_b16_8 t95 = t94;
                                    gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
                                    GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)) + t92 * 32) + 16), t96);
                                }
                                {
                                    gm_l15 = ((uint8_t)1);
                                }
                                goto ix81;
ie81:;
ix81:;
                            }
                            goto me77;
mn78:;
                            gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 778, 13);
me77:;
                        }
lc69:;
                        {
                            uint64_t t98 = gm_l17;
                            uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                            uint64_t t101 = r99;
                            gm_l17 = t101;
                        }
                    }
lb69:;
                }
            }
            {
                uint8_t t103 = gm_l15;
                uint8_t t104 = (uint8_t)!t103;
                if (!t104) goto ie102;
                {
                    void * t106 = (void *)((uint8_t *)&F.l14);
                    gm_t6j6smj7qoctu t107;
                    gm_b16_8 t108 = gm_l1;
                    GM_ST(gm_b16_8, (uint8_t *)&t107 + 0, t108);
                    gm_b16_8 t109 = gm_l13;
                    gm_b16_8 t110 = t109;
                    gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                    GM_ST(gm_b16_8, (uint8_t *)&t107 + 16, t111);
                    gm_tftb1odc739t5 t112 = gm_f_push_302k12h(t106, &t107);
                    gm_tftb1odc739t5 t113 = t112;
                    if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 1, gm_srcfpg6jsb3cfp6, 784, 31);
                    gm_b0_1 t114 = GM_LD(gm_b0_1, (((uint8_t *)&t113) + 1));
                }
                goto ix102;
ie102:;
ix102:;
            }
            {
                gm_b16_8 t115 = gm_l3;
                gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                gm_b16_8 t117 = t116;
                gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                gm_b16_8 t119 = gm_l1;
                void * t120 = gm_f_check_module_6elsf9l((void *)NULL, cx, NULL, t115, t118, t119);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t120;
            }
            {
                void * t122 = gm_l19;
                gm_b16_8 t123 = gm_l1;
                gm_t9805qtonbsag t124 = gm_f_file_id_mif69v8(t122, t123);
                gm_t9805qtonbsag t125 = t124;
                if (!((((uint8_t *)&t125))[4] != 0)) goto ie121;
                uint32_t t126 = GM_LD(uint32_t, ((uint8_t *)&t125));
                gm_l20 = t126;
                {
                    memset((uint8_t *)&gm_l21 + 9, 0, 7);
                    void * t128 = gm_l19;
                    GM_ST(void *, (uint8_t *)&gm_l21 + 0, t128);
                    void * t129 = gm_l0;
                    gm_t9qkh0rsooc8d t130 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t129) + 0));
                    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&gm_l21 + 8, t130);
                    void * t131 = (void *)((uint8_t *)&F.l4);
                    GM_ST(void *, (uint8_t *)&gm_l21 + 16, t131);
                }
                {
                    void * t133 = (void *)((uint8_t *)&gm_l21);
                    uint32_t t134 = gm_l20;
                    uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 0));
                    uint32_t t136 = (uint32_t)t135;
                    uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint32_t t138 = (uint32_t)t137;
                    gm_tqetrgo0q24b5 t139;
                    gm_f_members_8m2g0sl((void *)&t139, (void *)NULL, cx, NULL, t133, t134, t136, t138);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tqetrgo0q24b5 t140 = t139;
                    if (!((((uint8_t *)&t140))[40] != 0)) goto ie132;
                    gm_trqfvmjtt4sij t141 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t140));
                    F.l22 = t141;
                    {
                        gm_trqfvmjtt4sij t143 = F.l22;
                        F.l9 = t143;
                    }
                    goto ix132;
ie132:;
ix132:;
                }
                goto ix121;
ie121:;
ix121:;
            }
            {
                void * t144 = gm_l19;
                gm_b0_1 t145 = gm_f_release_7gtbra1((void *)NULL, cx, NULL, t144);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix38:;
        }
        goto ix27;
ie27:;
        {
            memset((uint8_t *)&gm_l23 + 9, 0, 7);
            void * t147 = gm_l5;
            GM_ST(void *, (uint8_t *)&gm_l23 + 0, t147);
            void * t148 = gm_l0;
            gm_t9qkh0rsooc8d t149 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t148) + 0));
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&gm_l23 + 8, t149);
            void * t150 = (void *)((uint8_t *)&F.l4);
            GM_ST(void *, (uint8_t *)&gm_l23 + 16, t150);
        }
        {
            void * t151 = (void *)((uint8_t *)&gm_l23);
            uint32_t t152 = gm_l6;
            uint32_t t153 = gm_l2;
            gm_f_names_ms1jmv4((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l9)), (void *)NULL, cx, NULL, t151, t152, t153);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix27:;
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l24, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t154 = (void *)((uint8_t *)&F.l24);
        gm_b16_8 t155;
        GM_ST(void *, (uint8_t *)&t155, (void *)(gm_stdehenbgfc4h));
        GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(12));
        gm_t8vuock3jnv59 t156;
        gm_f_bool_value_j9bnrp8((void *)&t156, ((uint8_t)0));
        gm_b0_1 t157 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t154, t155, &t156);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t158 = (void *)((uint8_t *)&F.l24);
        gm_b16_8 t159;
        GM_ST(void *, (uint8_t *)&t159, (void *)(gm_s9cg870cl2t07));
        GM_ST(uintptr_t, (uint8_t *)&t159 + 8, (uintptr_t)(5));
        gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        gm_b16_8 t161 = t160;
        gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
        gm_t8vuock3jnv59 t163;
        gm_f_array_k32ana3((void *)&t163, (void *)NULL, cx, NULL, t162);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t164 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t158, t159, &t163);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t165 = (void *)((uint8_t *)&F.l24);
        gm_f_value_pkuqal3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), t165);
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
extern __attribute__((aligned(4))) uint32_t gm_f_component_k9b35ju(const void * a0);
__attribute__((aligned(4))) uint32_t gm_f_component_k9b35ju_c(gm_t8vuock3jnv59 a0) { return gm_f_component_k9b35ju(&a0); }
__attribute__((aligned(4))) gm_b0_1 gm_f_did_change_v6sghvq(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_did_change_v6sghvq F;
    void * gm_l0; /* srv */
    gm_t8vuock3jnv59 gm_l1; /* params */
    gm_t8vuock3jnv59 gm_l2; /* td */
    gm_b16_8 gm_l3; /* uri */
    gm_b16_8 gm_l4; /* raw */
    gm_b16_8 gm_l5; /* path */
    void * gm_l6; /* d */
    gm_b16_8 gm_l7; /* changes */
    gm_b16_8 gm_l8; /* body */
    GM_STACK_CHECK(&F, 996u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4j3ecogbjkvn));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
        gm_f_field_7904fsq((void *)&gm_l2, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a1)), t2);
    }
    {
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbo52miqad506));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_t8vuock3jnv59 t4;
        gm_f_field_7904fsq((void *)&t4, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l2)), t3);
        gm_th2lqbo77pvsc t5;
        gm_f_as_str_9rhg9v0((void *)&t5, &t4);
        gm_th2lqbo77pvsc t6 = t5;
        gm_b16_8 t7;
        if (((((uint8_t *)&t6))[16] != 0)) {
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l3 = t7;
    }
    {
        gm_b16_8 t11 = gm_l3;
        gm_th2lqbo77pvsc t12;
        gm_f_uri_to_path_kbof22r((void *)&t12, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t13 = t12;
        gm_b16_8 t14;
        if (((((uint8_t *)&t13))[16] != 0)) {
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            gm_b0_1 t16;
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        gm_l4 = t14;
    }
    {
        gm_b16_8 t18 = gm_l4;
        gm_b16_8 t19 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t19;
    }
    {
        void * t20 = gm_l0;
        void * t21 = (void *)(((uint8_t *)t20) + 8);
        gm_b16_8 t22 = gm_l5;
        void * t23 = gm_f_get_ptr_egf3gtq(t21, t22);
        void * t24 = t23;
        void * t25;
        if ((GM_LD(void *, ((uint8_t *)&t24)) != 0)) {
            void * t26 = GM_LD(void *, (((uint8_t *)&t24) + 0));
            t25 = t26;
        } else {
            gm_b0_1 t27;
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
        gm_l6 = t25;
    }
    {
        void * t29 = gm_l6;
        uint8_t *t30 = (((uint8_t *)t29) + 16);
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sla3alt5340mf));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(7));
        gm_t8vuock3jnv59 t32;
        gm_f_field_7904fsq((void *)&t32, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l2)), t31);
        gm_t913bb735pvt5 t33 = gm_f_as_i64_iojmg3q(&t32);
        gm_t913bb735pvt5 t34 = t33;
        int64_t t35;
        if (((((uint8_t *)&t34))[8] != 0)) {
            int64_t t36 = GM_LD(int64_t, (((uint8_t *)&t34) + 0));
            t35 = t36;
        } else {
            void * t37 = gm_l6;
            int64_t t38 = GM_LD(int64_t, (((uint8_t *)t37) + 16));
            t35 = t38;
        }
        GM_ST(int64_t, t30, t35);
    }
    {
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sunu1gosuevl4));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(14));
        gm_t8vuock3jnv59 t41;
        gm_f_field_7904fsq((void *)&t41, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a1)), t40);
        gm_th2lqbo77pvsc t42;
        gm_f_as_array_nfvj8pk((void *)&t42, &t41);
        gm_th2lqbo77pvsc t43 = t42;
        if (!((((uint8_t *)&t43))[16] != 0)) goto ie39;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
        gm_l7 = t44;
        {
            uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            uint8_t t48 = (t47 > ((uint64_t)0ULL));
            if (!t48) goto ie46;
            {
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint64_t r52; bool o53 = __builtin_sub_overflow(t51, ((uint64_t)1ULL), &r52);
                if (o53) gm_trap("integer overflow in `-`", gm_srcfpg6jsb3cfp6, 403, 45);
                uint64_t t54 = r52;
                uint64_t t55 = (uintptr_t)t54;
                uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                if (t55 >= (uintptr_t)(t56)) gm_trap_index(t55, t56, gm_srcfpg6jsb3cfp6, 403, 37);
                gm_b16_8 t57;
                GM_ST(void *, (uint8_t *)&t57, (void *)(gm_scmaavulfehmr));
                GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(4));
                gm_t8vuock3jnv59 t58;
                gm_f_field_7904fsq((void *)&t58, &(*(gm_t8vuock3jnv59 *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t55 * 24)), t57);
                gm_th2lqbo77pvsc t59;
                gm_f_as_str_9rhg9v0((void *)&t59, &t58);
                gm_th2lqbo77pvsc t60 = t59;
                if (!((((uint8_t *)&t60))[16] != 0)) goto ie50;
                gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                gm_l8 = t61;
                {
                    void * t63 = gm_l6;
                    uint8_t *t64 = (((uint8_t *)t63) + 24);
                    gm_b16_8 t65 = gm_l8;
                    gm_tabb710d9mq2e t66;
                    gm_f_dupe_g9iqpvn((void *)&t66, (void *)NULL, cx, NULL, t65, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kismru0mlt2)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t67 = t66;
                    if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 8, gm_srcfpg6jsb3cfp6, 403, 83);
                    gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
                    gm_b16_8 t69 = t68;
                    gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                    GM_ST(gm_b16_8, t64, t70);
                }
                goto ix50;
ie50:;
ix50:;
            }
            goto ix46;
ie46:;
ix46:;
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        void * t71 = gm_l0;
        gm_b16_8 t72 = gm_l5;
        gm_b0_1 t73 = gm_f_invalidate_uhhh539((void *)NULL, cx, NULL, t71, t72);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_did_close_f9lpcgi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_did_close_f9lpcgi F;
    void * gm_l0; /* srv */
    gm_t8vuock3jnv59 gm_l1; /* params */
    gm_b16_8 gm_l2; /* uri */
    gm_b16_8 gm_l3; /* raw */
    gm_b16_8 gm_l4; /* path */
    void * gm_l6; /* <tmp> */
    void * gm_l7; /* a */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l12; /* other */
    GM_STACK_CHECK(&F, 1281u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4j3ecogbjkvn));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbo52miqad506));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_t8vuock3jnv59 t4;
        gm_f_path2_enc7vaj((void *)&t4, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a1)), t2, t3);
        gm_th2lqbo77pvsc t5;
        gm_f_as_str_9rhg9v0((void *)&t5, &t4);
        gm_th2lqbo77pvsc t6 = t5;
        gm_b16_8 t7;
        if (((((uint8_t *)&t6))[16] != 0)) {
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l2 = t7;
    }
    {
        gm_b16_8 t11 = gm_l2;
        gm_th2lqbo77pvsc t12;
        gm_f_uri_to_path_kbof22r((void *)&t12, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t13 = t12;
        gm_b16_8 t14;
        if (((((uint8_t *)&t13))[16] != 0)) {
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            gm_b0_1 t16;
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        gm_l3 = t14;
    }
    {
        gm_b16_8 t18 = gm_l3;
        gm_b16_8 t19 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t19;
    }
    {
        void * t21 = gm_l0;
        void * t22 = (void *)(((uint8_t *)t21) + 8);
        gm_b16_8 t23 = gm_l4;
        uint8_t t24 = gm_f_contains_eq02jnv(t22, t23);
        uint8_t t25 = (uint8_t)!t24;
        if (!t25) goto ie20;
        {
            gm_b0_1 t27;
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        {
            void * t30 = gm_l0;
            void * t31 = (void *)(((uint8_t *)t30) + 8);
            gm_b16_8 t32 = gm_l4;
            gm_tmle170q2vbs1 t33;
            gm_f_remove_jktghr7((void *)&t33, t31, t32);
        }
    }
    {
        gm_b16_8 t34 = gm_l2;
        gm_b0_1 t35 = gm_f_publish_empty_lh10nsv((void *)NULL, cx, NULL, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t36 = gm_l0;
        gm_b16_8 t37 = gm_l4;
        gm_b0_1 t38 = gm_f_invalidate_uhhh539((void *)NULL, cx, NULL, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t39 = gm_l4;
        gm_b16_8 t40 = gm_f_dir_of_ho73hue((void *)NULL, cx, NULL, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t40;
    }
    {
        {
            void * t42 = gm_l0;
            void * t43 = (void *)(((uint8_t *)t42) + 8);
            gm_l6 = t43;
        }
        gm_hval t44 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn45 = {.eff = 1408113935, .kind = GM_LIT, .clauses = gm_hlp1oga9ehto, .env = t44.env, .hctx = t44.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        void * t48 = gm_l6;
        gm_b0_1 t49 = gm_f_entries_qfi2q5v_s728q5p((void *)NULL, (&wn45), NULL, t48);
        if (gm_unw.kind) {
            goto gm_ll47;
        }
        F.w0 = t49;
        goto gm_ls50;
gm_ll47:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we46; }
        goto gm_lad;
gm_ls50:;
we46:;
        gm_b0_1 t51 = F.w0;
        gm_b0_1 t52 = t51;
    }
    {
        void * t54 = gm_l0;
        void * t55 = (void *)(((uint8_t *)t54) + 64);
        gm_b16_8 t56 = F.l5;
        void * t57 = gm_f_get_lco3u6e(t55, t56);
        void * t58 = t57;
        if (!(GM_LD(void *, ((uint8_t *)&t58)) != 0)) goto ie53;
        void * t59 = GM_LD(void *, ((uint8_t *)&t58));
        gm_l7 = t59;
        {
            void * t61 = gm_l7;
            gm_b0_1 t62 = gm_f_release_7gtbra1((void *)NULL, cx, NULL, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix53;
ie53:;
ix53:;
    }
    {
        {
            void * t64 = gm_l0;
            void * t65 = (void *)(((uint8_t *)t64) + 64);
            gm_b16_8 t66 = F.l5;
            void * t67 = gm_f_remove_mabr80f(t65, t66);
        }
    }
    {
        {
            void * t69 = gm_l0;
            void * t70 = (void *)(((uint8_t *)t69) + 120);
            gm_b16_8 t71 = F.l5;
            gm_tqs5p4n2tq19v t72 = gm_f_remove_c3h4fju(t70, t71);
        }
    }
    {
        void * t74 = gm_l0;
        void * t75 = (void *)(((uint8_t *)t74) + 176);
        gm_b16_8 t76 = F.l5;
        gm_tqetrgo0q24b5 t77;
        gm_f_get_ahntg11((void *)&t77, t75, t76);
        gm_tqetrgo0q24b5 t78 = t77;
        if (!((((uint8_t *)&t78))[40] != 0)) goto ie73;
        gm_trqfvmjtt4sij t79 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t78));
        F.l8 = t79;
        {
            {
                void * t82 = gm_l0;
                void * t83 = (void *)(((uint8_t *)t82) + 176);
                gm_b16_8 t84 = F.l5;
                gm_tqetrgo0q24b5 t85;
                gm_f_remove_6s9ti8v((void *)&t85, t83, t84);
            }
        }
        {
            gm_trqfvmjtt4sij t86 = F.l8;
            F.l9 = t86;
        }
        {
            gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_closure t88 = ((gm_closure){GM_PTR_TAG(gm_f_fn_r1rj4g9), NULL, cx});
            gm_f_sort_by_s9l01h6(t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                gm_l10 = t90;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t92 = gm_l11;
                    uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t94 = (t92 < t93);
                    if (!t94) goto lb91;
                    {
                        uint64_t t97 = gm_l11;
                        uint64_t t98 = (uintptr_t)t97;
                        uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t98 * 16));
                        gm_l12 = t102;
                        {
                            gm_b16_8 t105 = gm_l12;
                            gm_b16_8 t106 = gm_l4;
                            gm_b16_8 t107 = t105;
                            gm_b16_8 t108 = t106;
                            uint8_t t109 = (!gm_eq0iqiubfm7e(((uint8_t *)&t107), ((uint8_t *)&t108)));
                            if (!t109) goto ie104;
                            {
                                gm_b16_8 t111 = gm_l12;
                                gm_b16_8 t112 = gm_f_path_to_uri_as8bikc((void *)NULL, cx, NULL, t111);
                                if (gm_unw.kind) {
                                    goto gm_ll95;
                                }
                                gm_b0_1 t113 = gm_f_publish_empty_lh10nsv((void *)NULL, cx, NULL, t112);
                                if (gm_unw.kind) {
                                    goto gm_ll95;
                                }
                            }
                            goto ix104;
ie104:;
ix104:;
                        }
                        goto me100;
mn101:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 459, 9);
me100:;
                    }
lc91:;
                    {
                        uint64_t t115 = gm_l11;
                        uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
                        uint64_t t118 = r116;
                        gm_l11 = t118;
                    }
                }
gm_ll95:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb91; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc91; }
                }
                goto gm_lad;
lb91:;
            }
        }
        goto ix73;
ie73:;
ix73:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_did_open_9b7n0ug(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_did_open_9b7n0ug F;
    void * gm_l0; /* srv */
    gm_t8vuock3jnv59 gm_l1; /* params */
    gm_t8vuock3jnv59 gm_l2; /* td */
    gm_b16_8 gm_l3; /* uri */
    gm_b16_8 gm_l4; /* body */
    gm_b16_8 gm_l5; /* raw */
    int64_t gm_l6; /* version */
    gm_b16_8 gm_l7; /* path */
    GM_STACK_CHECK(&F, 954u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4j3ecogbjkvn));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
        gm_f_field_7904fsq((void *)&gm_l2, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a1)), t2);
    }
    {
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbo52miqad506));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_t8vuock3jnv59 t4;
        gm_f_field_7904fsq((void *)&t4, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l2)), t3);
        gm_th2lqbo77pvsc t5;
        gm_f_as_str_9rhg9v0((void *)&t5, &t4);
        gm_th2lqbo77pvsc t6 = t5;
        gm_b16_8 t7;
        if (((((uint8_t *)&t6))[16] != 0)) {
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l3 = t7;
    }
    {
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_scmaavulfehmr));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(4));
        gm_t8vuock3jnv59 t12;
        gm_f_field_7904fsq((void *)&t12, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l2)), t11);
        gm_th2lqbo77pvsc t13;
        gm_f_as_str_9rhg9v0((void *)&t13, &t12);
        gm_th2lqbo77pvsc t14 = t13;
        gm_b16_8 t15;
        if (((((uint8_t *)&t14))[16] != 0)) {
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 0));
            t15 = t16;
        } else {
            gm_b0_1 t17;
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        gm_l4 = t15;
    }
    {
        gm_b16_8 t19 = gm_l3;
        gm_th2lqbo77pvsc t20;
        gm_f_uri_to_path_kbof22r((void *)&t20, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t21 = t20;
        gm_b16_8 t22;
        if (((((uint8_t *)&t21))[16] != 0)) {
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 0));
            t22 = t23;
        } else {
            gm_b0_1 t24;
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        gm_l5 = t22;
    }
    {
        gm_b16_8 t27 = gm_l5;
        uint8_t t28 = gm_f_is_gm_file_0ofcrmu((void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t29 = (uint8_t)!t28;
        if (!t29) goto ie26;
        {
            gm_b0_1 t31;
            uint8_t t32 = 0;
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        gm_b16_8 t33;
        GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sla3alt5340mf));
        GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(7));
        gm_t8vuock3jnv59 t34;
        gm_f_field_7904fsq((void *)&t34, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l2)), t33);
        gm_t913bb735pvt5 t35 = gm_f_as_i64_iojmg3q(&t34);
        gm_t913bb735pvt5 t36 = t35;
        int64_t t37;
        if (((((uint8_t *)&t36))[8] != 0)) {
            int64_t t38 = GM_LD(int64_t, (((uint8_t *)&t36) + 0));
            t37 = t38;
        } else {
            t37 = ((int64_t)0LL);
        }
        gm_l6 = t37;
    }
    {
        gm_b16_8 t39 = gm_l5;
        gm_b16_8 t40 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t40;
    }
    {
        void * t41 = gm_l0;
        void * t42 = (void *)(((uint8_t *)t41) + 8);
        gm_b16_8 t43 = gm_l7;
        gm_tqj9esv5ukti3 t44;
        gm_b16_8 t45 = gm_l3;
        gm_tabb710d9mq2e t46;
        gm_f_dupe_g9iqpvn((void *)&t46, (void *)NULL, cx, NULL, t45, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_khb6ap185f0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t47 = t46;
        if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 8, gm_srcfpg6jsb3cfp6, 388, 33);
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
        gm_b16_8 t49 = t48;
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
        GM_ST(gm_b16_8, (uint8_t *)&t44 + 0, t50);
        int64_t t51 = gm_l6;
        GM_ST(int64_t, (uint8_t *)&t44 + 16, t51);
        gm_b16_8 t52 = gm_l4;
        gm_tabb710d9mq2e t53;
        gm_f_dupe_g9iqpvn((void *)&t53, (void *)NULL, cx, NULL, t52, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ks1bcpoe2h2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t54 = t53;
        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 8, gm_srcfpg6jsb3cfp6, 388, 69);
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
        gm_b16_8 t56 = t55;
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
        GM_ST(gm_b16_8, (uint8_t *)&t44 + 24, t57);
        gm_tftb1odc739t5 t58 = gm_f_put_625pk7t(t42, t43, &t44);
        gm_tftb1odc739t5 t59 = t58;
        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srcfpg6jsb3cfp6, 388, 5);
        gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
    }
    {
        void * t61 = gm_l0;
        gm_b16_8 t62 = gm_l7;
        gm_b0_1 t63 = gm_f_invalidate_uhhh539((void *)NULL, cx, NULL, t61, t62);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_ensure_9tp264o(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_ensure_9tp264o F;
    void * gm_l0; /* srv */
    void * gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* p */
    void * gm_l6; /* a */
    void * gm_l7; /* old */
    GM_STACK_CHECK(&F, 531u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 64);
        gm_b16_8 t5 = F.l1;
        uint8_t t6 = gm_f_contains_c624pm6(t4, t5);
        if (!t6) goto ie2;
        void * t7 = gm_l0;
        void * t8 = (void *)(((uint8_t *)t7) + 120);
        gm_b16_8 t9 = F.l1;
        uint8_t t10 = gm_f_contains_57n7jqd(t8, t9);
        uint8_t t11 = (uint8_t)!t10;
        if (!t11) goto ie2;
        {
            gm_b0_1 t13;
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
        gm_f_overlays_6rdbiin((void *)&F.l2, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_th2lqbo77pvsc t16;
        memset(&t16, 0, sizeof t16);
        F.l3 = t16;
    }
    {
        {
            void * t18 = gm_l0;
            void * t19 = (void *)(((uint8_t *)t18) + 8);
            gm_l4 = t19;
        }
        gm_hval t20 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn21 = {.eff = 1408113935, .kind = GM_LIT, .clauses = gm_hleu8p83p90p, .env = t20.env, .hctx = t20.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        void * t24 = gm_l4;
        gm_b0_1 t25 = gm_f_entries_qfi2q5v_s75phag((void *)NULL, (&wn21), NULL, t24);
        if (gm_unw.kind) {
            goto gm_ll23;
        }
        F.w0 = t25;
        goto gm_ls26;
gm_ll23:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we22; }
        goto gm_lad;
gm_ls26:;
we22:;
        gm_b0_1 t27 = F.w0;
        gm_b0_1 t28 = t27;
    }
    {
        gm_th2lqbo77pvsc t29 = F.l3;
        gm_th2lqbo77pvsc t30 = t29;
        gm_b16_8 t31;
        if (((((uint8_t *)&t30))[16] != 0)) {
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 0));
            t31 = t32;
        } else {
            gm_b0_1 t33;
            uint8_t t34 = 0;
            F.ret = t33;
            goto gm_fret;
        }
        gm_l5 = t31;
    }
    {
        gm_b16_8 t35 = F.l1;
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t37 = t36;
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
        gm_b16_8 t39 = gm_l5;
        void * t40 = gm_f_check_module_6elsf9l((void *)NULL, cx, NULL, t35, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t40;
    }
    {
        {
            void * t42 = gm_l0;
            void * t43 = (void *)(((uint8_t *)t42) + 120);
            gm_b16_8 t44 = F.l1;
            gm_tqs5p4n2tq19v t45 = gm_f_remove_c3h4fju(t43, t44);
        }
    }
    {
        void * t46 = gm_l0;
        gm_b16_8 t47 = F.l1;
        void * t48 = gm_l6;
        gm_b0_1 t49 = gm_f_publish_put2rqb((void *)NULL, cx, NULL, t46, t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t51 = gm_l0;
        void * t52 = (void *)(((uint8_t *)t51) + 64);
        gm_b16_8 t53 = F.l1;
        void * t54 = gm_f_get_lco3u6e(t52, t53);
        void * t55 = t54;
        if (!(GM_LD(void *, ((uint8_t *)&t55)) != 0)) goto ie50;
        void * t56 = GM_LD(void *, ((uint8_t *)&t55));
        gm_l7 = t56;
        {
            void * t58 = gm_l7;
            gm_b0_1 t59 = gm_f_release_7gtbra1((void *)NULL, cx, NULL, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix50;
ie50:;
ix50:;
    }
    {
        void * t60 = gm_l0;
        void * t61 = (void *)(((uint8_t *)t60) + 64);
        gm_b16_8 t62 = F.l1;
        void * t63 = gm_l6;
        gm_tftb1odc739t5 t64 = gm_f_put_79ksd1u(t61, t62, t63);
        gm_tftb1odc739t5 t65 = t64;
        if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 1, gm_srcfpg6jsb3cfp6, 578, 5);
        gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 1));
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
extern __attribute__((aligned(4))) void gm_f_envelope_4dp2tms(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_fail_8tj0r64(void *gm_out, int64_t a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_thgmovcqlsivv gm_f_fail_8tj0r64_c(int64_t a0, gm_b16_8 a1) { gm_thgmovcqlsivv gm_r; gm_f_fail_8tj0r64(&gm_r, a0, a1); return gm_r; }
extern __attribute__((aligned(4))) uint8_t gm_f_fn_6m9fu50(void * a0, void * a1);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_pt42u9t(void * a0, void * a1);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_r1rj4g9(void * a0, void * a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1qm1hvv(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_7fmdm88(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_7fmdm88 F;
    gm_b16_8 gm_l2; /* p */
    GM_STACK_CHECK(&F, 282u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tggvvh3lncvcg t3 = GM_LD(gm_tggvvh3lncvcg, ((uint8_t *)&F.l0));
    F.l1 = t3;
    {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t7 = gm_f_dir_of_ho73hue((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_ensure_9tp264o *)((&F.h)->up))->l1));
        gm_b16_8 t9 = t7;
        gm_b16_8 t10 = t8;
        uint8_t t11 = (gm_eq0iqiubfm7e(((uint8_t *)&t9), ((uint8_t *)&t10)));
        if (!t11) goto ie5;
        {
            if (!((((uint8_t *)&((struct gm_fr_ensure_9tp264o *)((&F.h)->up))->l3))[16] != 0)) goto ie13;
            gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_ensure_9tp264o *)((&F.h)->up))->l3));
            gm_l2 = t14;
            {
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
                gm_b16_8 t18 = gm_l2;
                uint8_t t19 = gm_f_path_less_702ltp5(t17, t18);
                if (!t19) goto ie16;
                {
                    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
                    gm_th2lqbo77pvsc t22;
                    GM_ST(uint8_t, (uint8_t *)&t22 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t22 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t22 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t22), t21);
                    ((uint8_t *)&t22)[16] = 1;
                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&((struct gm_fr_ensure_9tp264o *)((&F.h)->up))->l3), t22);
                }
                goto ix16;
ie16:;
ix16:;
            }
            goto ix13;
ie13:;
            {
                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
                gm_th2lqbo77pvsc t25;
                GM_ST(uint8_t, (uint8_t *)&t25 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t25 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t25 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t25), t24);
                ((uint8_t *)&t25)[16] = 1;
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&((struct gm_fr_ensure_9tp264o *)((&F.h)->up))->l3), t25);
            }
ix13:;
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 564, 5);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_9nsvqlg(void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2u6ls4(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_n6686bi(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_n6686bi F;
    gm_tpe1hv3fekn05 gm_l0; /* <item> */
    void * gm_l2; /* a */
    void * gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tpe1hv3fekn05 t3 = GM_LD(gm_tpe1hv3fekn05, ((uint8_t *)&gm_l0));
    F.l1 = t3;
    {
        void * t5 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
        gm_l2 = t5;
    }
    {
        {
            void * t7 = gm_l2;
            void * t8 = (void *)(((uint8_t *)t7) + 480);
            gm_l3 = t8;
        }
        gm_hval t9 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn10 = {.eff = 1575796949, .kind = GM_LIT, .clauses = gm_hlklsb3fd3g1, .env = t9.env, .hctx = t9.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        void * t13 = gm_l3;
        gm_b0_1 t14 = gm_f_entries_bdtp5sv_ses338s((void *)NULL, (&wn10), NULL, t13);
        if (gm_unw.kind) {
            goto gm_ll12;
        }
        F.w0 = t14;
        goto gm_ls15;
gm_ll12:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we11; }
        goto gm_lad;
gm_ls15:;
we11:;
        gm_b0_1 t16 = F.w0;
        gm_b0_1 t17 = t16;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 487, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_qfqkllj(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_qfqkllj F;
    gm_tommf4j9upomc gm_l0; /* <item> */
    gm_tommf4j9upomc gm_l1; /* fe */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 0));
        gm_b16_8 t7 = gm_f_dir_of_ho73hue((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_invalidate_uhhh539 *)(((&F.h)->up)->up))->l2));
        gm_b16_8 t9 = t7;
        gm_b16_8 t10 = t8;
        uint8_t t11 = (gm_eq0iqiubfm7e(((uint8_t *)&t9), ((uint8_t *)&t10)));
        if (!t11) goto ie5;
        {
            void * t13 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_invalidate_uhhh539 *)(((&F.h)->up)->up))->l0));
            void * t14 = (void *)(((uint8_t *)t13) + 120);
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&((struct gm_fr_for_body_n6686bi *)((&F.h)->up))->l1) + 0));
            gm_tftb1odc739t5 t16 = gm_f_put_5c6qgui(t14, t15, ((uint8_t)1));
            gm_tftb1odc739t5 t17 = t16;
            if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srcfpg6jsb3cfp6, 491, 17);
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        {
            gm_unw.kind = GM_BREAK; gm_unw.frame = inst->with_frame; gm_unw.label = inst->with_label; gm_unw.is_err = 0;
            goto gm_lad;
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 489, 9);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_s23qjlr(void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_sf9naq7(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_t3rkkf2(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_t3rkkf2 F;
    GM_STACK_CHECK(&F, 231u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tggvvh3lncvcg t3 = GM_LD(gm_tggvvh3lncvcg, ((uint8_t *)&F.l0));
    F.l1 = t3;
    {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t7 = gm_f_dir_of_ho73hue((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_publish_put2rqb *)((&F.h)->up))->l1));
        gm_b16_8 t9 = t7;
        gm_b16_8 t10 = t8;
        uint8_t t11 = (gm_eq0iqiubfm7e(((uint8_t *)&t9), ((uint8_t *)&t10)));
        if (!t11) goto ie5;
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&((struct gm_fr_publish_put2rqb *)((&F.h)->up))->l5) + 0));
        gm_b16_8 t14 = t13;
        uint8_t t15 = 0;
        gm_b16_8 t16 = t12;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t14) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t14))) + i * 16, ((uint8_t *)&t16))) { t15 = 1; break; }
        uint8_t t17 = (uint8_t)(!t15);
        if (!t17) goto ie5;
        {
            void * t19 = (void *)((uint8_t *)&((struct gm_fr_publish_put2rqb *)((&F.h)->up))->l5);
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
            gm_tftb1odc739t5 t21 = gm_f_push_3ef746b(t19, t20);
            gm_tftb1odc739t5 t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srcfpg6jsb3cfp6, 587, 68);
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 586, 5);
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
inline __attribute__((aligned(4))) void gm_f_formatting_cu98209(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_formatting_cu98209 F;
    void * gm_l0; /* srv */
    gm_b16_8 gm_l1; /* path */
    uint32_t gm_l5; /* id */
    uint32_t gm_l7; /* next_id */
    gm_b16_8 gm_l8; /* new */
    GM_STACK_CHECK(&F, 1092u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_b16_8 t4 = gm_l1;
        gm_tmle170q2vbs1 t5;
        gm_f_get_t10cn5s((void *)&t5, t3, t4);
        gm_tmle170q2vbs1 t6 = t5;
        gm_tqj9esv5ukti3 t7;
        if (((((uint8_t *)&t6))[40] != 0)) {
            gm_tqj9esv5ukti3 t8 = GM_LD(gm_tqj9esv5ukti3, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_t8vuock3jnv59 t9 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            gm_tfaata5ba51n4 t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            ((uint8_t *)&t10)[0] = 0;
            GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t10 + 8), t9);
            gm_tfaata5ba51n4 t11 = t10;
            uint8_t t12 = ((((uint8_t *)&t11))[0] != 0);
            F.ret = t10;
            goto gm_fret;
        }
        F.l2 = t7;
    }
    {
        gm_f_source_map_4v8dg4b((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_diagnostics_n3kkhf4((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_smjr08rujacsf));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(8));
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
        uint32_t t16 = gm_f_add_pva1tcq((void *)NULL, cx, NULL, t13, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t16;
    }
    {
        uint32_t t17 = gm_l5;
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
        void * t19 = (void *)((uint8_t *)&F.l4);
        gm_f_lex_dc8mbmf((void *)&F.l6, (void *)NULL, cx, NULL, t17, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l7 = ((uint32_t)0ULL);
    }
    {
        {
            uint32_t t21 = gm_l5;
            void * t22 = (void *)((uint8_t *)&F.l6);
            void * t23 = (void *)((uint8_t *)&gm_l7);
            void * t24 = (void *)((uint8_t *)&F.l4);
            gm_t08olq97rscn3 t25;
            gm_f_parse_file_70g7vok((void *)&t25, (void *)NULL, cx, NULL, t21, t22, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l4);
        uint8_t t28 = gm_f_has_errors_if7ai0m(t27);
        if (!t28) goto ie26;
        {
            gm_t8vuock3jnv59 t30 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            gm_tfaata5ba51n4 t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            ((uint8_t *)&t31)[0] = 0;
            GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t31 + 8), t30);
            gm_tfaata5ba51n4 t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
        gm_ti9v8ce4a1rnh t35;
        gm_f_format_source_a5lvenl((void *)&t35, (void *)NULL, cx, NULL, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ti9v8ce4a1rnh t36 = t35;
        gm_b16_8 t37;
        if (!((((uint8_t *)&t36))[0] != 0)) {
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
            t37 = t38;
        } else {
            gm_t8vuock3jnv59 t39 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            gm_tfaata5ba51n4 t40;
            GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
            ((uint8_t *)&t40)[0] = 0;
            GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t40 + 8), t39);
            gm_tfaata5ba51n4 t41 = t40;
            uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
            F.ret = t40;
            goto gm_fret;
        }
        gm_l8 = t37;
    }
    {
        gm_b16_8 t44 = gm_l8;
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
        gm_b16_8 t46 = t44;
        gm_b16_8 t47 = t45;
        uint8_t t48 = (gm_eq0iqiubfm7e(((uint8_t *)&t46), ((uint8_t *)&t47)));
        if (!t48) goto ie43;
        {
            gm_b16_8 t50 = gm_f_empty_slice_dheabhi_3();
            gm_b16_8 t51 = t50;
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
            gm_t8vuock3jnv59 t53;
            gm_f_array_k32ana3((void *)&t53, (void *)NULL, cx, NULL, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tfaata5ba51n4 t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
            ((uint8_t *)&t54)[0] = 0;
            GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t54 + 8), t53);
            gm_tfaata5ba51n4 t55 = t54;
            uint8_t t56 = ((((uint8_t *)&t55))[0] != 0);
            F.ret = t54;
            goto gm_fret;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
        gm_b16_8 t58 = gm_l8;
        void * t59 = gm_l0;
        gm_t9qkh0rsooc8d t60 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t59) + 0));
        gm_t8vuock3jnv59 t61;
        gm_f_full_edit_82h286k((void *)&t61, (void *)NULL, cx, NULL, t57, t58, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfaata5ba51n4 t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
        ((uint8_t *)&t62)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t62 + 8), t61);
        gm_tfaata5ba51n4 t63 = t62;
        uint8_t t64 = ((((uint8_t *)&t63))[0] != 0);
        F.ret = t62;
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
inline __attribute__((aligned(4))) gm_tk5oh6c9vhlab gm_f_handle_incoming_uj1ifur(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_handle_incoming_uj1ifur F;
    void * gm_l0; /* srv */
    gm_t3ujhu6qph7de gm_l1; /* msg */
    gm_b16_8 gm_l2; /* e */
    gm_b16_8 gm_l3; /* bytes */
    gm_tekvgkj2hms37 gm_l4; /* e */
    gm_tk5oh6c9vhlab gm_l6; /* flow */
    GM_STACK_CHECK(&F, 659u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t4 != ((uint8_t)1ULL)) goto mn3;
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
        gm_l2 = t5;
        {
            gm_t8vuock3jnv59 t7 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            gm_b16_8 t8 = gm_l2;
            gm_b0_1 t9 = gm_f_respond_error_5vpslhl((void *)NULL, cx, NULL, &t7, ((int64_t)-32700LL), t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tk5oh6c9vhlab t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto me2;
mn3:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t13 != ((uint8_t)0ULL)) goto mn12;
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
        gm_l3 = t14;
        {
            gm_b16_8 t16 = gm_l3;
            gm_b16_8 t17 = t16;
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
            gm_tit4ajsn3pv91 t19;
            gm_f_parse_invslf8((void *)&t19, (void *)NULL, cx, NULL, t18, ((uint64_t)512ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tit4ajsn3pv91 t20 = t19;
            gm_thshlh2q7ekfv t21;
            if (!((((uint8_t *)&t20))[0] != 0)) {
                gm_thshlh2q7ekfv t22 = GM_LD(gm_thshlh2q7ekfv, (((uint8_t *)&t20) + 8));
                t21 = t22;
            } else {
                gm_tekvgkj2hms37 t23 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t20) + 8));
                gm_l4 = t23;
                {
                    gm_t8vuock3jnv59 t25 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
                    gm_b16_8 t26 = gm_l3;
                    gm_b16_8 t27 = t26;
                    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
                    gm_tekvgkj2hms37 t29 = gm_l4;
                    gm_b16_8 t30 = gm_f_parse_error_text_pasvuia((void *)NULL, cx, NULL, t28, t29);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b0_1 t31 = gm_f_respond_error_5vpslhl((void *)NULL, cx, NULL, &t25, ((int64_t)-32700LL), t30);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                gm_tk5oh6c9vhlab t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
                uint8_t t33 = 0;
                F.ret = t32;
                goto gm_fret;
            }
            F.l5 = t21;
        }
        {
            void * t34 = gm_l0;
            gm_tk5oh6c9vhlab t35 = gm_f_handle_uop7uti((void *)NULL, cx, NULL, t34, &(*(gm_t8vuock3jnv59 *)(((uint8_t *)&F.l5) + 0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t35;
        }
        {
            void * t36 = (void *)((uint8_t *)&F.l5);
            gm_f_deinit_l8j91qd(t36);
        }
        {
            gm_tk5oh6c9vhlab t37 = gm_l6;
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
        goto me2;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 130, 5);
me2:;
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
inline __attribute__((aligned(4))) gm_tk5oh6c9vhlab gm_f_handle_uop7uti(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_handle_uop7uti F;
    void * gm_l0; /* srv */
    gm_t8vuock3jnv59 gm_l1; /* msg */
    gm_b16_8 gm_l2; /* method */
    gm_t8vuock3jnv59 gm_l3; /* params */
    gm_t3ph3kbiqt1ah gm_l4; /* id */
    gm_t8vuock3jnv59 gm_l5; /* i */
    gm_t8vuock3jnv59 gm_l6; /* i */
    GM_STACK_CHECK(&F, 471u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sg7of7eks8a5u));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
        gm_t8vuock3jnv59 t3;
        gm_f_field_7904fsq((void *)&t3, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l1)), t2);
        gm_th2lqbo77pvsc t4;
        gm_f_as_str_9rhg9v0((void *)&t4, &t3);
        gm_th2lqbo77pvsc t5 = t4;
        gm_b16_8 t6;
        if (((((uint8_t *)&t5))[16] != 0)) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tk5oh6c9vhlab t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)0ULL));
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sfkmkcagsnqnb));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(6));
        gm_f_field_7904fsq((void *)&gm_l3, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l1)), t10);
    }
    {
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sq3gi0chb3cs2));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(2));
        gm_f_get_fjdb5ou((void *)&gm_l4, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l1)), t11);
    }
    {
        void * t13 = gm_l0;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t13) + 289));
        uint8_t t15 = (uint8_t)!t14;
        if (!t15) goto ie12;
        uint8_t t16 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_smga32pj96fal, 10) == 0));
        if (!t16) goto ie12;
        {
            if (!((((uint8_t *)&gm_l4))[24] != 0)) goto ie18;
            gm_t8vuock3jnv59 t19 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)&gm_l4));
            gm_l5 = t19;
            {
                gm_b16_8 t21;
                GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s8uvhki57nqp6));
                GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(29));
                gm_b0_1 t22 = gm_f_respond_error_5vpslhl((void *)NULL, cx, NULL, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l5)), ((int64_t)-32002LL), t21);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix18;
ie18:;
            uint8_t t24 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s37v6u2dqp80f, 4) == 0));
            if (!t24) goto ie23;
            {
                void * t26 = gm_l0;
                gm_b16_8 t27 = gm_l2;
                gm_tk5oh6c9vhlab t28 = gm_f_notification_6t090qd((void *)NULL, cx, NULL, t26, t27, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l3)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t29 = 0;
                F.ret = t28;
                goto gm_fret;
            }
            goto ix23;
ie23:;
ix23:;
ix18:;
        }
        {
            gm_tk5oh6c9vhlab t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            uint8_t t31 = 0;
            F.ret = t30;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        if (!((((uint8_t *)&gm_l4))[24] != 0)) goto ie32;
        gm_t8vuock3jnv59 t33 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)&gm_l4));
        gm_l6 = t33;
        {
            void * t35 = gm_l0;
            gm_b16_8 t36 = gm_l2;
            gm_b0_1 t37 = gm_f_request_nncosh4((void *)NULL, cx, NULL, t35, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l6)), t36, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tk5oh6c9vhlab t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        void * t40 = gm_l0;
        gm_b16_8 t41 = gm_l2;
        gm_tk5oh6c9vhlab t42 = gm_f_notification_6t090qd((void *)NULL, cx, NULL, t40, t41, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t43 = 0;
        F.ret = t42;
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
__attribute__((aligned(4))) void gm_f_initialize_q3q654t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_initialize_q3q654t F;
    void * gm_l0; /* srv */
    gm_t8vuock3jnv59 gm_l1; /* params */
    gm_th2lqbo77pvsc gm_l2; /* encs */
    gm_b16_8 gm_l3; /* list_of */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t8vuock3jnv59 gm_l6; /* e */
    gm_b16_8 gm_l7; /* s */
    gm_t8vuock3jnv59 gm_l8; /* reg */
    gm_tq47bkpmd2b9a gm_l14; /* <temporary array> */
    gm_tq47bkpmd2b9a gm_l16; /* <temporary array> */
    GM_STACK_CHECK(&F, 2205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sb3sdaa9odag6));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spasf3abnfbjh));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_t8vuock3jnv59 t4;
        gm_f_path2_enc7vaj((void *)&t4, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a1)), t2, t3);
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_scbq2ng311vcu));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(17));
        gm_t8vuock3jnv59 t6;
        gm_f_field_7904fsq((void *)&t6, &t4, t5);
        gm_f_as_array_nfvj8pk((void *)&gm_l2, &t6);
    }
    {
        if (!((((uint8_t *)&gm_l2))[16] != 0)) goto ie7;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
        gm_l3 = t8;
        {
            {
                gm_b16_8 t11 = gm_l3;
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
                        gm_t8vuock3jnv59 t23 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t19 * 24));
                        gm_l6 = t23;
                        {
                            gm_th2lqbo77pvsc t26;
                            gm_f_as_str_9rhg9v0((void *)&t26, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l6)));
                            gm_th2lqbo77pvsc t27 = t26;
                            if (!((((uint8_t *)&t27))[16] != 0)) goto ie25;
                            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
                            gm_l7 = t28;
                            {
                                uint8_t t31 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_st0lgjd5uosa2, 5) == 0));
                                if (!t31) goto ie30;
                                {
                                    void * t33 = gm_l0;
                                    gm_t9qkh0rsooc8d t34;
                                    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
                                    GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)t33) + 0), t34);
                                }
                                goto ix30;
ie30:;
ix30:;
                            }
                            goto ix25;
ie25:;
ix25:;
                        }
                        goto me21;
mn22:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 646, 9);
me21:;
                    }
lc12:;
                    {
                        uint64_t t36 = gm_l5;
                        uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                        uint64_t t39 = r37;
                        gm_l5 = t39;
                    }
                }
lb12:;
            }
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sb3sdaa9odag6));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(12));
        gm_t8vuock3jnv59 t41;
        gm_f_field_7904fsq((void *)&t41, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a1)), t40);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s3o8dsbklmfsh));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(9));
        gm_t8vuock3jnv59 t43;
        gm_f_field_7904fsq((void *)&t43, &t41, t42);
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s0lh70hs4of8e));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(21));
        gm_t8vuock3jnv59 t45;
        gm_f_field_7904fsq((void *)&t45, &t43, t44);
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_segm7or15hs5h));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(19));
        gm_f_field_7904fsq((void *)&gm_l8, &t45, t46);
    }
    {
        void * t47 = gm_l0;
        uint8_t *t48 = (((uint8_t *)t47) + 290);
        uint8_t t49 = gm_f_is_true_3osltpn(&(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l8)));
        GM_ST(uint8_t, t48, t49);
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t50 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_segcvuj6bu7e4));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(16));
        void * t52 = gm_l0;
        gm_t9qkh0rsooc8d t53 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t52) + 0));
        gm_b16_8 t54 = gm_f_encoding_name_m6926rk(t53);
        gm_t8vuock3jnv59 t55;
        gm_f_text_ls49dp6((void *)&t55, t54);
        gm_b0_1 t56 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t50, t51, &t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t57 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t58;
        GM_ST(void *, (uint8_t *)&t58, (void *)(gm_s274psem80gbe));
        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(9));
        gm_t8vuock3jnv59 t59;
        gm_f_bool_value_j9bnrp8((void *)&t59, ((uint8_t)1));
        gm_b0_1 t60 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t57, t58, &t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t61 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t62;
        GM_ST(void *, (uint8_t *)&t62, (void *)(gm_so4t3hmedbui5));
        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(6));
        gm_t8vuock3jnv59 t63;
        gm_f_int_95caidg((void *)&t63, (void *)NULL, cx, NULL, ((int64_t)1LL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t64 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t61, t62, &t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t65 = (void *)((uint8_t *)&F.l11);
        gm_b16_8 t66;
        GM_ST(void *, (uint8_t *)&t66, (void *)(gm_s227dnd7n32vg));
        GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(11));
        gm_t8vuock3jnv59 t67;
        gm_f_bool_value_j9bnrp8((void *)&t67, ((uint8_t)0));
        gm_b0_1 t68 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t65, t66, &t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t69 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t70;
        GM_ST(void *, (uint8_t *)&t70, (void *)(gm_suod5ocif7o5v));
        GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(4));
        void * t71 = (void *)((uint8_t *)&F.l11);
        gm_t8vuock3jnv59 t72;
        gm_f_value_pkuqal3((void *)&t72, t71);
        gm_b0_1 t73 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t69, t70, &t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t74 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t75;
        GM_ST(void *, (uint8_t *)&t75, (void *)(gm_s3i94v2v4ifm3));
        GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(16));
        void * t76 = (void *)((uint8_t *)&F.l10);
        gm_t8vuock3jnv59 t77;
        gm_f_value_pkuqal3((void *)&t77, t76);
        gm_b0_1 t78 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t74, t75, &t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t79 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t80;
        GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sgpokbp1avpcv));
        GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(13));
        gm_t8vuock3jnv59 t81;
        gm_f_bool_value_j9bnrp8((void *)&t81, ((uint8_t)1));
        gm_b0_1 t82 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t79, t80, &t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t83 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t84;
        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_s5g1e6qrps9q6));
        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(18));
        gm_t8vuock3jnv59 t85;
        gm_f_bool_value_j9bnrp8((void *)&t85, ((uint8_t)1));
        gm_b0_1 t86 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t83, t84, &t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t87 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sk4i403fuak6l));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(18));
        gm_t8vuock3jnv59 t89;
        gm_f_bool_value_j9bnrp8((void *)&t89, ((uint8_t)1));
        gm_b0_1 t90 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t87, t88, &t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t91 = (void *)((uint8_t *)&F.l12);
        gm_b16_8 t92;
        GM_ST(void *, (uint8_t *)&t92, (void *)(gm_s5o6ultk7ms9u));
        GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(15));
        gm_t8vuock3jnv59 t93;
        gm_f_bool_value_j9bnrp8((void *)&t93, ((uint8_t)1));
        gm_b0_1 t94 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t91, t92, &t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t95 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t96;
        GM_ST(void *, (uint8_t *)&t96, (void *)(gm_ska23dbjvthl3));
        GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(14));
        void * t97 = (void *)((uint8_t *)&F.l12);
        gm_t8vuock3jnv59 t98;
        gm_f_value_pkuqal3((void *)&t98, t97);
        gm_b0_1 t99 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t95, t96, &t98);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t100 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t101;
        GM_ST(void *, (uint8_t *)&t101, (void *)(gm_s2ht50jletjjv));
        GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(22));
        gm_t8vuock3jnv59 t102;
        gm_f_bool_value_j9bnrp8((void *)&t102, ((uint8_t)1));
        gm_b0_1 t103 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t100, t101, &t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t104 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t105;
        GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sd80su3vlvhjt));
        GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(26));
        gm_t8vuock3jnv59 t106;
        gm_f_bool_value_j9bnrp8((void *)&t106, ((uint8_t)1));
        gm_b0_1 t107 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t104, t105, &t106);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t108 = (void *)((uint8_t *)&F.l13);
        gm_b16_8 t109;
        GM_ST(void *, (uint8_t *)&t109, (void *)(gm_sn5naisar83li));
        GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(15));
        {
            gm_b16_8 t111;
            GM_ST(void *, (uint8_t *)&t111, (void *)(gm_srtlfdmpif4qk));
            GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(8));
            gm_t8vuock3jnv59 t112;
            gm_f_text_ls49dp6((void *)&t112, t111);
            GM_ST(gm_t8vuock3jnv59, (uint8_t *)&(*(gm_tq47bkpmd2b9a *)((uint8_t *)&gm_l14)) + 0, t112);
        }
        void * t113 = ((uint8_t *)&gm_l14);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcfpg6jsb3cfp6, 672, 37);
        gm_b16_8 t114;
        GM_ST(void *, (uint8_t *)&t114, (void *)(((uint8_t *)t113 + (uintptr_t)0 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t115 = t114;
        gm_t8vuock3jnv59 t116;
        gm_f_array_k32ana3((void *)&t116, (void *)NULL, cx, NULL, t115);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t117 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t108, t109, &t116);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t118 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t119;
        GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sjl83c3218sse));
        GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(18));
        void * t120 = (void *)((uint8_t *)&F.l13);
        gm_t8vuock3jnv59 t121;
        gm_f_value_pkuqal3((void *)&t121, t120);
        gm_b0_1 t122 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t118, t119, &t121);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t123 = (void *)((uint8_t *)&F.l15);
        gm_b16_8 t124;
        GM_ST(void *, (uint8_t *)&t124, (void *)(gm_siv8n5ojlp0dd));
        GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(17));
        {
            gm_b16_8 t126;
            GM_ST(void *, (uint8_t *)&t126, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(1));
            gm_t8vuock3jnv59 t127;
            gm_f_text_ls49dp6((void *)&t127, t126);
            GM_ST(gm_t8vuock3jnv59, (uint8_t *)&(*(gm_tq47bkpmd2b9a *)((uint8_t *)&gm_l16)) + 0, t127);
        }
        void * t128 = ((uint8_t *)&gm_l16);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcfpg6jsb3cfp6, 675, 41);
        gm_b16_8 t129;
        GM_ST(void *, (uint8_t *)&t129, (void *)(((uint8_t *)t128 + (uintptr_t)0 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t130 = t129;
        gm_t8vuock3jnv59 t131;
        gm_f_array_k32ana3((void *)&t131, (void *)NULL, cx, NULL, t130);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t132 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t123, t124, &t131);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t133 = (void *)((uint8_t *)&F.l15);
        gm_b16_8 t134;
        GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sdopt7plujc7n));
        GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(15));
        gm_t8vuock3jnv59 t135;
        gm_f_bool_value_j9bnrp8((void *)&t135, ((uint8_t)0));
        gm_b0_1 t136 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t133, t134, &t135);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t137 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t138;
        GM_ST(void *, (uint8_t *)&t138, (void *)(gm_shqein5ip3gpn));
        GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(18));
        void * t139 = (void *)((uint8_t *)&F.l15);
        gm_t8vuock3jnv59 t140;
        gm_f_value_pkuqal3((void *)&t140, t139);
        gm_b0_1 t141 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t137, t138, &t140);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t142 = (void *)((uint8_t *)&F.l17);
        gm_b16_8 t143;
        GM_ST(void *, (uint8_t *)&t143, (void *)(gm_siv8n5ojlp0dd));
        GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(17));
        {
            gm_b16_8 t145;
            GM_ST(void *, (uint8_t *)&t145, (void *)(gm_smooaouqmm8dj));
            GM_ST(uintptr_t, (uint8_t *)&t145 + 8, (uintptr_t)(1));
            gm_t8vuock3jnv59 t146;
            gm_f_text_ls49dp6((void *)&t146, t145);
            gm_b16_8 t147;
            GM_ST(void *, (uint8_t *)&t147, (void *)(gm_sc8o4oaph9gbr));
            GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(1));
            gm_t8vuock3jnv59 t148;
            gm_f_text_ls49dp6((void *)&t148, t147);
            GM_ST(gm_t8vuock3jnv59, (uint8_t *)&(*(gm_tg1c132scqcte *)((uint8_t *)&F.l18)) + 0, t146);
            GM_ST(gm_t8vuock3jnv59, (uint8_t *)&(*(gm_tg1c132scqcte *)((uint8_t *)&F.l18)) + 24, t148);
        }
        void * t149 = ((uint8_t *)&F.l18);
        if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcfpg6jsb3cfp6, 679, 40);
        gm_b16_8 t150;
        GM_ST(void *, (uint8_t *)&t150, (void *)(((uint8_t *)t149 + (uintptr_t)0 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
        gm_b16_8 t151 = t150;
        gm_t8vuock3jnv59 t152;
        gm_f_array_k32ana3((void *)&t152, (void *)NULL, cx, NULL, t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t153 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t142, t143, &t152);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t154 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t155;
        GM_ST(void *, (uint8_t *)&t155, (void *)(gm_sttasaa2bp05i));
        GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(21));
        void * t156 = (void *)((uint8_t *)&F.l17);
        gm_t8vuock3jnv59 t157;
        gm_f_value_pkuqal3((void *)&t157, t156);
        gm_b0_1 t158 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t154, t155, &t157);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l19, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t159 = (void *)((uint8_t *)&F.l19);
        gm_b16_8 t160;
        GM_ST(void *, (uint8_t *)&t160, (void *)(gm_sius9pki5u4e3));
        GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(4));
        gm_b16_8 t161;
        GM_ST(void *, (uint8_t *)&t161, (void *)(gm_s19jg4ct3aq11));
        GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(5));
        gm_t8vuock3jnv59 t162;
        gm_f_text_ls49dp6((void *)&t162, t161);
        gm_b0_1 t163 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t159, t160, &t162);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t164 = (void *)((uint8_t *)&F.l19);
        gm_b16_8 t165;
        GM_ST(void *, (uint8_t *)&t165, (void *)(gm_sla3alt5340mf));
        GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)(7));
        gm_b16_8 t166 = GM_LD(gm_b16_8, gm_ctdm11uuaj6);
        gm_t8vuock3jnv59 t167;
        gm_f_text_ls49dp6((void *)&t167, t166);
        gm_b0_1 t168 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t164, t165, &t167);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t169 = (void *)((uint8_t *)&F.l20);
        gm_b16_8 t170;
        GM_ST(void *, (uint8_t *)&t170, (void *)(gm_sb3sdaa9odag6));
        GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(12));
        void * t171 = (void *)((uint8_t *)&F.l9);
        gm_t8vuock3jnv59 t172;
        gm_f_value_pkuqal3((void *)&t172, t171);
        gm_b0_1 t173 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t169, t170, &t172);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t174 = (void *)((uint8_t *)&F.l20);
        gm_b16_8 t175;
        GM_ST(void *, (uint8_t *)&t175, (void *)(gm_sb745qqv6kdrg));
        GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(10));
        void * t176 = (void *)((uint8_t *)&F.l19);
        gm_t8vuock3jnv59 t177;
        gm_f_value_pkuqal3((void *)&t177, t176);
        gm_b0_1 t178 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t174, t175, &t177);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t179 = (void *)((uint8_t *)&F.l20);
        gm_f_value_pkuqal3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), t179);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_invalidate_uhhh539(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_invalidate_uhhh539 F;
    gm_b16_8 gm_l1; /* path */
    void * gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        gm_b16_8 t3 = gm_f_dir_of_ho73hue((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t3;
    }
    {
        void * t4 = F.l0;
        void * t5 = (void *)(((uint8_t *)t4) + 120);
        gm_b16_8 t6 = F.l2;
        gm_tftb1odc739t5 t7 = gm_f_put_5c6qgui(t5, t6, ((uint8_t)1));
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcfpg6jsb3cfp6, 486, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        {
            void * t11 = F.l0;
            void * t12 = (void *)(((uint8_t *)t11) + 64);
            gm_l3 = t12;
        }
        gm_hval t13 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn14 = {.eff = 917785847, .kind = GM_LIT, .clauses = gm_hlvop6ag413a, .env = t13.env, .hctx = t13.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        void * t17 = gm_l3;
        gm_b0_1 t18 = gm_f_entries_5dln36o_s7v20pl((void *)NULL, (&wn14), NULL, t17);
        if (gm_unw.kind) {
            goto gm_ll16;
        }
        F.w0 = t18;
        goto gm_ls19;
gm_ll16:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we15; }
        goto gm_lad;
gm_ls19:;
we15:;
        gm_b0_1 t20 = F.w0;
        gm_b0_1 t21 = t20;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_gm_file_0ofcrmu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_gm_file_0ofcrmu F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 175u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_th2lqbo77pvsc t3;
        gm_f_file_name_em7v8lv((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t4 = t3;
        gm_b16_8 t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        uint8_t t8 = 0;
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t10 = (t9 > ((uint64_t)3ULL));
        if (t10) {
            gm_b16_8 t11 = gm_l1;
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s0ucactj6hj0a));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(3));
            uint8_t t13 = gm_f_ends_with_biriu5l(t11, t12);
            t8 = t13;
        } else t8 = 0;
        uint8_t t14 = 0;
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
__attribute__((aligned(4))) void gm_f_language_request_21k642b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_language_request_21k642b F;
    void * gm_l0; /* srv */
    gm_b16_8 gm_l1; /* method */
    gm_t8vuock3jnv59 gm_l2; /* params */
    gm_b16_8 gm_l3; /* path */
    gm_b16_8 gm_l4; /* dir */
    void * gm_l7; /* a */
    uint32_t gm_l8; /* file */
    gm_tf361jk9mn3il gm_l9; /* cx */
    gm_t8vuock3jnv59 gm_l10; /* pos */
    uint32_t gm_l11; /* off */
    uint8_t gm_l13; /* include */
    gm_b16_8 gm_l14; /* e */
    gm_t8vuock3jnv59 gm_l15; /* r */
    gm_b16_8 gm_l16; /* new */
    gm_b16_8 gm_l17; /* e */
    gm_t8vuock3jnv59 gm_l18; /* r */
    GM_STACK_CHECK(&F, 2844u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_tm2ah4hc06bl5 t2;
        gm_f_target_j5nr6ju((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm2ah4hc06bl5 t3 = t2;
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_thgmovcqlsivv t4 = GM_LD(gm_thgmovcqlsivv, (((uint8_t *)&t3) + 8));
            gm_tfaata5ba51n4 t5;
            GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_thgmovcqlsivv, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 8));
        gm_l3 = t6;
    }
    {
        uint8_t t8 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 23) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_solaeibjdukdj, 23) == 0));
        if (!t8) goto ie7;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_l3;
            gm_f_formatting_cu98209((void *)&(*(gm_tfaata5ba51n4 *)gm_out), (void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 8);
        gm_b16_8 t15 = gm_l3;
        uint8_t t16 = gm_f_contains_eq02jnv(t14, t15);
        uint8_t t17 = (uint8_t)!t16;
        if (!t17) goto ie12;
        {
            gm_t8vuock3jnv59 t19 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            gm_tfaata5ba51n4 t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
            ((uint8_t *)&t20)[0] = 0;
            GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t20 + 8), t19);
            gm_tfaata5ba51n4 t21 = t20;
            uint8_t t22 = ((((uint8_t *)&t21))[0] != 0);
            F.ret = t20;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        gm_b16_8 t23 = gm_l3;
        gm_b16_8 t24 = gm_f_dir_of_ho73hue((void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t24;
    }
    {
        void * t25 = gm_l0;
        gm_b16_8 t26 = gm_l4;
        gm_b0_1 t27 = gm_f_ensure_9tp264o((void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t28 = gm_l0;
        gm_f_uri_map_mhud6ak((void *)&F.l5, (void *)NULL, cx, NULL, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t29 = gm_l0;
        gm_f_overlays_6rdbiin((void *)&F.l6, (void *)NULL, cx, NULL, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t30 = gm_l0;
        void * t31 = (void *)(((uint8_t *)t30) + 64);
        gm_b16_8 t32 = gm_l4;
        void * t33 = gm_f_get_lco3u6e(t31, t32);
        void * t34 = t33;
        void * t35;
        if ((GM_LD(void *, ((uint8_t *)&t34)) != 0)) {
            void * t36 = GM_LD(void *, (((uint8_t *)&t34) + 0));
            t35 = t36;
        } else {
            gm_t8vuock3jnv59 t37 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            gm_tfaata5ba51n4 t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
            ((uint8_t *)&t38)[0] = 0;
            GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t38 + 8), t37);
            gm_tfaata5ba51n4 t39 = t38;
            uint8_t t40 = ((((uint8_t *)&t39))[0] != 0);
            F.ret = t38;
            goto gm_fret;
        }
        gm_l7 = t35;
    }
    {
        void * t41 = gm_l7;
        gm_b16_8 t42 = gm_l3;
        gm_t9805qtonbsag t43 = gm_f_file_id_mif69v8(t41, t42);
        gm_t9805qtonbsag t44 = t43;
        uint32_t t45;
        if (((((uint8_t *)&t44))[4] != 0)) {
            uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)&t44) + 0));
            t45 = t46;
        } else {
            gm_t8vuock3jnv59 t47 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            gm_tfaata5ba51n4 t48;
            GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
            ((uint8_t *)&t48)[0] = 0;
            GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t48 + 8), t47);
            gm_tfaata5ba51n4 t49 = t48;
            uint8_t t50 = ((((uint8_t *)&t49))[0] != 0);
            F.ret = t48;
            goto gm_fret;
        }
        gm_l8 = t45;
    }
    {
        memset((uint8_t *)&gm_l9 + 9, 0, 7);
        void * t51 = gm_l7;
        GM_ST(void *, (uint8_t *)&gm_l9 + 0, t51);
        void * t52 = gm_l0;
        gm_t9qkh0rsooc8d t53 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t52) + 0));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&gm_l9 + 8, t53);
        void * t54 = (void *)((uint8_t *)&F.l5);
        GM_ST(void *, (uint8_t *)&gm_l9 + 16, t54);
    }
    {
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_s16vnj1m5rn6t));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(8));
        gm_f_field_7904fsq((void *)&gm_l10, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a2)), t55);
    }
    {
        void * t56 = (void *)((uint8_t *)&gm_l9);
        uint32_t t57 = gm_l8;
        gm_b16_8 t58;
        GM_ST(void *, (uint8_t *)&t58, (void *)(gm_shf0g22rjm3o7));
        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(4));
        gm_t8vuock3jnv59 t59;
        gm_f_field_7904fsq((void *)&t59, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l10)), t58);
        uint32_t t60 = gm_f_component_k9b35ju(&t59);
        gm_b16_8 t61;
        GM_ST(void *, (uint8_t *)&t61, (void *)(gm_s28fmt6o4roj7));
        GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(9));
        gm_t8vuock3jnv59 t62;
        gm_f_field_7904fsq((void *)&t62, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l10)), t61);
        uint32_t t63 = gm_f_component_k9b35ju(&t62);
        uint32_t t64 = gm_f_offset_at_73sli4j((void *)NULL, cx, NULL, t56, t57, t60, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t64;
    }
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 18) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_svrjvsqtthedc, 18) == 0)) goto mn66;
        void * t67 = (void *)((uint8_t *)&gm_l9);
        uint32_t t68 = gm_l8;
        uint32_t t69 = gm_l11;
        gm_t8vuock3jnv59 t70;
        gm_f_hover_ub02g3b((void *)&t70, (void *)NULL, cx, NULL, t67, t68, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfaata5ba51n4 t71;
        GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
        ((uint8_t *)&t71)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t71 + 8), t70);
        gm_tfaata5ba51n4 t72 = t71;
        uint8_t t73 = ((((uint8_t *)&t72))[0] != 0);
        F.ret = t71;
        goto gm_fret;
        goto me65;
mn66:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 23) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_spgshrus7teti, 23) == 0)) goto mn74;
        void * t75 = (void *)((uint8_t *)&gm_l9);
        uint32_t t76 = gm_l8;
        uint32_t t77 = gm_l11;
        gm_t8vuock3jnv59 t78;
        gm_f_definition_kaqo3c6((void *)&t78, (void *)NULL, cx, NULL, t75, t76, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfaata5ba51n4 t79;
        GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
        ((uint8_t *)&t79)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t79 + 8), t78);
        gm_tfaata5ba51n4 t80 = t79;
        uint8_t t81 = ((((uint8_t *)&t80))[0] != 0);
        F.ret = t79;
        goto gm_fret;
        goto me65;
mn74:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 23) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sj3m78jf2dftr, 23) == 0)) goto pa84;
        goto po83;
pa84:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 26) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sgh9nkjgl73of, 26) == 0)) goto pa85;
        goto po83;
pa85:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 19) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_surog9s0b8r6p, 19) == 0)) goto pa86;
        goto po83;
pa86:;
        goto mn82;
po83:;
        {
            memset((uint8_t *)&F.l12 + 49, 0, 7);
            void * t88 = gm_l7;
            GM_ST(void *, (uint8_t *)&F.l12 + 0, t88);
            void * t89 = gm_l0;
            void * t90 = (void *)(((uint8_t *)t89) + 232);
            GM_ST(void *, (uint8_t *)&F.l12 + 8, t90);
            gm_b16_8 t91 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&F.l12 + 16, t91);
            gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t93 = t92;
            gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
            GM_ST(gm_b16_8, (uint8_t *)&F.l12 + 32, t94);
            void * t95 = gm_l0;
            gm_t9qkh0rsooc8d t96 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t95) + 0));
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l12 + 48, t96);
            void * t97 = (void *)((uint8_t *)&F.l5);
            GM_ST(void *, (uint8_t *)&F.l12 + 56, t97);
        }
        {
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 23) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sj3m78jf2dftr, 23) == 0)) goto mn99;
            {
                gm_b16_8 t101;
                GM_ST(void *, (uint8_t *)&t101, (void *)(gm_sq0p2ifj52tjf));
                GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(7));
                gm_b16_8 t102;
                GM_ST(void *, (uint8_t *)&t102, (void *)(gm_st3mnofi7dhrs));
                GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(18));
                gm_t8vuock3jnv59 t103;
                gm_f_path2_enc7vaj((void *)&t103, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a2)), t101, t102);
                gm_tqs5p4n2tq19v t104 = gm_f_as_bool_g9idbdk(&t103);
                gm_tqs5p4n2tq19v t105 = t104;
                uint8_t t106;
                if (((((uint8_t *)&t105))[1] != 0)) {
                    uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&t105) + 0));
                    t106 = t107;
                } else {
                    t106 = ((uint8_t)1);
                }
                gm_l13 = t106;
            }
            {
                void * t108 = (void *)((uint8_t *)&F.l12);
                uint32_t t109 = gm_l8;
                uint32_t t110 = gm_l11;
                uint8_t t111 = gm_l13;
                gm_t8vuock3jnv59 t112;
                gm_f_references_all_9v25v88((void *)&t112, (void *)NULL, cx, NULL, t108, t109, t110, t111);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tfaata5ba51n4 t113;
                GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
                ((uint8_t *)&t113)[0] = 0;
                GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t113 + 8), t112);
                gm_tfaata5ba51n4 t114 = t113;
                uint8_t t115 = ((((uint8_t *)&t114))[0] != 0);
                F.ret = t113;
                goto gm_fret;
            }
            goto me98;
mn99:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 26) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sgh9nkjgl73of, 26) == 0)) goto mn116;
            {
                void * t118 = (void *)((uint8_t *)&F.l12);
                uint32_t t119 = gm_l8;
                uint32_t t120 = gm_l11;
                gm_t9l835tjp727s t121;
                gm_f_prepare_rename_i0odtgj((void *)&t121, (void *)NULL, cx, NULL, t118, t119, t120);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t9l835tjp727s t122 = t121;
                gm_t8vuock3jnv59 t123;
                if (!((((uint8_t *)&t122))[0] != 0)) {
                    gm_t8vuock3jnv59 t124 = GM_LD(gm_t8vuock3jnv59, (((uint8_t *)&t122) + 8));
                    t123 = t124;
                } else {
                    gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&t122) + 8));
                    gm_l14 = t125;
                    gm_b16_8 t127 = gm_l14;
                    gm_thgmovcqlsivv t128;
                    gm_f_fail_8tj0r64((void *)&t128, ((int64_t)-32803LL), t127);
                    gm_tfaata5ba51n4 t129;
                    GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
                    ((uint8_t *)&t129)[0] = 1;
                    GM_ST(gm_thgmovcqlsivv, ((uint8_t *)&t129 + 8), t128);
                    gm_tfaata5ba51n4 t130 = t129;
                    uint8_t t131 = ((((uint8_t *)&t130))[0] != 0);
                    F.ret = t129;
                    goto gm_fret;
                }
                gm_l15 = t123;
            }
            {
                gm_t8vuock3jnv59 t132 = gm_l15;
                gm_tfaata5ba51n4 t133;
                GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
                ((uint8_t *)&t133)[0] = 0;
                GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t133 + 8), t132);
                gm_tfaata5ba51n4 t134 = t133;
                uint8_t t135 = ((((uint8_t *)&t134))[0] != 0);
                F.ret = t133;
                goto gm_fret;
            }
            goto me98;
mn116:;
            {
                gm_b16_8 t138;
                GM_ST(void *, (uint8_t *)&t138, (void *)(gm_s041tbii048j3));
                GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(7));
                gm_t8vuock3jnv59 t139;
                gm_f_field_7904fsq((void *)&t139, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a2)), t138);
                gm_th2lqbo77pvsc t140;
                gm_f_as_str_9rhg9v0((void *)&t140, &t139);
                gm_th2lqbo77pvsc t141 = t140;
                gm_b16_8 t142;
                if (((((uint8_t *)&t141))[16] != 0)) {
                    gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&t141) + 0));
                    t142 = t143;
                } else {
                    gm_b16_8 t144;
                    GM_ST(void *, (uint8_t *)&t144, (void *)(gm_so44j4cr69vhs));
                    GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)(15));
                    gm_thgmovcqlsivv t145;
                    gm_f_fail_8tj0r64((void *)&t145, ((int64_t)-32602LL), t144);
                    gm_tfaata5ba51n4 t146;
                    GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
                    ((uint8_t *)&t146)[0] = 1;
                    GM_ST(gm_thgmovcqlsivv, ((uint8_t *)&t146 + 8), t145);
                    gm_tfaata5ba51n4 t147 = t146;
                    uint8_t t148 = ((((uint8_t *)&t147))[0] != 0);
                    F.ret = t146;
                    goto gm_fret;
                }
                gm_l16 = t142;
            }
            {
                void * t149 = (void *)((uint8_t *)&F.l12);
                uint32_t t150 = gm_l8;
                uint32_t t151 = gm_l11;
                gm_b16_8 t152 = gm_l16;
                gm_t9l835tjp727s t153;
                gm_f_rename_4pp7bs8((void *)&t153, (void *)NULL, cx, NULL, t149, t150, t151, t152);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t9l835tjp727s t154 = t153;
                gm_t8vuock3jnv59 t155;
                if (!((((uint8_t *)&t154))[0] != 0)) {
                    gm_t8vuock3jnv59 t156 = GM_LD(gm_t8vuock3jnv59, (((uint8_t *)&t154) + 8));
                    t155 = t156;
                } else {
                    gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&t154) + 8));
                    gm_l17 = t157;
                    gm_b16_8 t159 = gm_l17;
                    gm_thgmovcqlsivv t160;
                    gm_f_fail_8tj0r64((void *)&t160, ((int64_t)-32803LL), t159);
                    gm_tfaata5ba51n4 t161;
                    GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
                    ((uint8_t *)&t161)[0] = 1;
                    GM_ST(gm_thgmovcqlsivv, ((uint8_t *)&t161 + 8), t160);
                    gm_tfaata5ba51n4 t162 = t161;
                    uint8_t t163 = ((((uint8_t *)&t162))[0] != 0);
                    F.ret = t161;
                    goto gm_fret;
                }
                gm_l18 = t155;
            }
            {
                gm_t8vuock3jnv59 t164 = gm_l18;
                gm_tfaata5ba51n4 t165;
                GM_ST(uint8_t, (uint8_t *)&t165 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t165 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
                ((uint8_t *)&t165)[0] = 0;
                GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t165 + 8), t164);
                gm_tfaata5ba51n4 t166 = t165;
                uint8_t t167 = ((((uint8_t *)&t166))[0] != 0);
                F.ret = t165;
                goto gm_fret;
            }
            goto me98;
mn136:;
            gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 721, 13);
me98:;
        }
        goto me65;
mn82:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 27) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_srod2911mer3j, 27) == 0)) goto mn168;
        void * t169 = (void *)((uint8_t *)&gm_l9);
        uint32_t t170 = gm_l8;
        gm_t8vuock3jnv59 t171;
        gm_f_document_symbols_0nlnk2i((void *)&t171, (void *)NULL, cx, NULL, t169, t170);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfaata5ba51n4 t172;
        GM_ST(uint8_t, (uint8_t *)&t172 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t172 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t172 + 4, 0);
        ((uint8_t *)&t172)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t172 + 8), t171);
        gm_tfaata5ba51n4 t173 = t172;
        uint8_t t174 = ((((uint8_t *)&t173))[0] != 0);
        F.ret = t172;
        goto gm_fret;
        goto me65;
mn168:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 26) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s4n5m9kt2s9ld, 26) == 0)) goto mn175;
        void * t176 = (void *)((uint8_t *)&gm_l9);
        uint32_t t177 = gm_l8;
        uint32_t t178 = gm_l11;
        gm_t8vuock3jnv59 t179;
        gm_f_signature_help_16c2a8f((void *)&t179, (void *)NULL, cx, NULL, t176, t177, t178);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfaata5ba51n4 t180;
        GM_ST(uint8_t, (uint8_t *)&t180 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t180 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t180 + 4, 0);
        ((uint8_t *)&t180)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t180 + 8), t179);
        gm_tfaata5ba51n4 t181 = t180;
        uint8_t t182 = ((((uint8_t *)&t181))[0] != 0);
        F.ret = t180;
        goto gm_fret;
        goto me65;
mn175:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 23) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_slogp373o1rii, 23) == 0)) goto mn183;
        void * t184 = gm_l0;
        gm_b16_8 t185 = gm_l3;
        uint32_t t186 = gm_l11;
        gm_t8vuock3jnv59 t187;
        gm_f_completion_r10tmh9((void *)&t187, (void *)NULL, cx, NULL, t184, t185, t186);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfaata5ba51n4 t188;
        GM_ST(uint8_t, (uint8_t *)&t188 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t188 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t188 + 4, 0);
        ((uint8_t *)&t188)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t188 + 8), t187);
        gm_tfaata5ba51n4 t189 = t188;
        uint8_t t190 = ((((uint8_t *)&t189))[0] != 0);
        F.ret = t188;
        goto gm_fret;
        goto me65;
mn183:;
        gm_t8vuock3jnv59 t192 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
        gm_tfaata5ba51n4 t193;
        GM_ST(uint8_t, (uint8_t *)&t193 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t193 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t193 + 4, 0);
        ((uint8_t *)&t193)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t193 + 8), t192);
        gm_tfaata5ba51n4 t194 = t193;
        uint8_t t195 = ((((uint8_t *)&t194))[0] != 0);
        F.ret = t193;
        goto gm_fret;
        goto me65;
mn191:;
        gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 716, 5);
me65:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_needs_analysis_l0op99b(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_needs_analysis_l0op99b F;
    void * gm_l0; /* srv */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_b16_8 gm_l3; /* d */
    GM_STACK_CHECK(&F, 297u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            gm_trqfvmjtt4sij t4;
            gm_f_open_dirs_9ud02ot((void *)&t4, (void *)NULL, cx, NULL, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trqfvmjtt4sij t5 = t4;
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
            gm_l1 = t6;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l2;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 16));
                    gm_l3 = t18;
                    {
                        uint8_t t21 = 0;
                        void * t22 = gm_l0;
                        void * t23 = (void *)(((uint8_t *)t22) + 64);
                        gm_b16_8 t24 = gm_l3;
                        uint8_t t25 = gm_f_contains_c624pm6(t23, t24);
                        uint8_t t26 = (uint8_t)!t25;
                        if (!t26) {
                            void * t27 = gm_l0;
                            void * t28 = (void *)(((uint8_t *)t27) + 120);
                            gm_b16_8 t29 = gm_l3;
                            uint8_t t30 = gm_f_contains_57n7jqd(t28, t29);
                            t21 = t30;
                        } else t21 = 1;
                        if (!t21) goto ie20;
                        {
                            uint8_t t32 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix20;
ie20:;
ix20:;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 536, 5);
me16:;
                }
lc7:;
                {
                    uint64_t t34 = gm_l2;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l2 = t37;
                }
            }
lb7:;
        }
    }
    {
        uint8_t t38 = 0;
        F.ret = ((uint8_t)0);
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
inline __attribute__((aligned(4))) gm_tk5oh6c9vhlab gm_f_notification_6t090qd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_notification_6t090qd F;
    void * gm_l0; /* srv */
    gm_b16_8 gm_l1; /* method */
    gm_t8vuock3jnv59 gm_l2; /* params */
    gm_b16_8 gm_l3; /* uri */
    gm_b16_8 gm_l4; /* raw */
    GM_STACK_CHECK(&F, 382u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s37v6u2dqp80f, 4) == 0)) goto mn3;
        uint8_t t4;
        void * t6 = gm_l0;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t6) + 288));
        if (!t7) goto ie5;
        uint8_t t9 = ((uint8_t)0ULL);
        t4 = t9;
        goto ix5;
ie5:;
        uint8_t t11 = ((uint8_t)1ULL);
        t4 = t11;
ix5:;
        gm_tk5oh6c9vhlab t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)1ULL));
        GM_ST(uint8_t, ((uint8_t *)&t12 + 1), t4);
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
        goto me2;
mn3:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 20) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s2431f6f423u2, 20) == 0)) goto mn14;
        void * t15 = gm_l0;
        gm_b0_1 t16 = gm_f_did_open_9b7n0ug((void *)NULL, cx, NULL, t15, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me2;
mn14:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 22) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sn5qnjkjnmu95, 22) == 0)) goto mn17;
        void * t18 = gm_l0;
        gm_b0_1 t19 = gm_f_did_change_v6sghvq((void *)NULL, cx, NULL, t18, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me2;
mn17:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 20) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s8ilcu32tpscb, 20) == 0)) goto mn20;
        {
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s4j3ecogbjkvn));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(12));
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sbo52miqad506));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(3));
            gm_t8vuock3jnv59 t25;
            gm_f_path2_enc7vaj((void *)&t25, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a2)), t23, t24);
            gm_th2lqbo77pvsc t26;
            gm_f_as_str_9rhg9v0((void *)&t26, &t25);
            gm_th2lqbo77pvsc t27 = t26;
            if (!((((uint8_t *)&t27))[16] != 0)) goto ie22;
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            gm_l3 = t28;
            {
                gm_b16_8 t31 = gm_l3;
                gm_th2lqbo77pvsc t32;
                gm_f_uri_to_path_kbof22r((void *)&t32, (void *)NULL, cx, NULL, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t33 = t32;
                if (!((((uint8_t *)&t33))[16] != 0)) goto ie30;
                gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                gm_l4 = t34;
                {
                    void * t36 = gm_l0;
                    gm_b16_8 t37 = gm_l4;
                    gm_b16_8 t38 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t37);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b0_1 t39 = gm_f_invalidate_uhhh539((void *)NULL, cx, NULL, t36, t38);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix30;
ie30:;
ix30:;
            }
            goto ix22;
ie22:;
ix22:;
        }
        goto me2;
mn20:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 11) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s0o9ngaj56qhu, 11) == 0)) goto mn40;
        void * t41 = gm_l0;
        gm_b0_1 t42 = gm_f_register_watchers_k2akg1c((void *)NULL, cx, NULL, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me2;
mn40:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 31) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sgge76tb2bmvi, 31) == 0)) goto mn43;
        void * t44 = gm_l0;
        gm_b0_1 t45 = gm_f_watched_files_changed_qvnrc63((void *)NULL, cx, NULL, t44, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me2;
mn43:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 21) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sktnfpvsdatf3, 21) == 0)) goto mn46;
        void * t47 = gm_l0;
        gm_b0_1 t48 = gm_f_did_close_f9lpcgi((void *)NULL, cx, NULL, t47, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me2;
mn46:;
        goto me2;
mn49:;
        gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 362, 5);
me2:;
    }
    {
        gm_tk5oh6c9vhlab t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)0ULL));
        uint8_t t52 = 0;
        F.ret = t51;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_notify_ls6n69d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_notify_ls6n69d F;
    gm_b16_8 gm_l0; /* method */
    gm_t8vuock3jnv59 gm_l1; /* params */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_envelope_4dp2tms((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg7of7eks8a5u));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        gm_b16_8 t4 = gm_l0;
        gm_t8vuock3jnv59 t5;
        gm_f_text_ls49dp6((void *)&t5, t4);
        gm_b0_1 t6 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t2, t3, &t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sfkmkcagsnqnb));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(6));
        gm_b0_1 t9 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t7, t8, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t10 = (void *)((uint8_t *)&F.l2);
        gm_t8vuock3jnv59 t11;
        gm_f_value_pkuqal3((void *)&t11, t10);
        gm_b0_1 t12 = gm_f_send_cg3jp71((void *)NULL, cx, NULL, &t11);
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
inline __attribute__((aligned(4))) void gm_f_open_dirs_9ud02ot(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_open_dirs_9ud02ot F;
    void * gm_l0; /* srv */
    void * gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 128u);
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
        {
            void * t3 = gm_l0;
            void * t4 = (void *)(((uint8_t *)t3) + 8);
            gm_l2 = t4;
        }
        gm_hval t5 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn6 = {.eff = 1408113935, .kind = GM_LIT, .clauses = gm_hlgmnqv27ug4, .env = t5.env, .hctx = t5.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        void * t9 = gm_l2;
        gm_b0_1 t10 = gm_f_entries_qfi2q5v_smjejb0((void *)NULL, (&wn6), NULL, t9);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        F.w0 = t10;
        goto gm_ls11;
gm_ll8:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
        goto gm_lad;
gm_ls11:;
we7:;
        gm_b0_1 t12 = F.w0;
        gm_b0_1 t13 = t12;
    }
    {
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_closure t15 = ((gm_closure){GM_PTR_TAG(gm_f_fn_6m9fu50), NULL, cx});
        gm_f_sort_by_s9l01h6(t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t16 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint64_t t18 = gm_f_dedup_6a2r2j0(t17);
        gm_f_truncate_vir0vu7(t16, t18);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) void gm_f_overlays_6rdbiin(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_overlays_6rdbiin F;
    void * gm_l0; /* srv */
    void * gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_j41vvuc((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            void * t4 = (void *)(((uint8_t *)t3) + 8);
            gm_l2 = t4;
        }
        gm_hval t5 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn6 = {.eff = 1408113935, .kind = GM_LIT, .clauses = gm_hl0b9itmh1di, .env = t5.env, .hctx = t5.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        void * t9 = gm_l2;
        gm_b0_1 t10 = gm_f_entries_qfi2q5v_secjmhg((void *)NULL, (&wn6), NULL, t9);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        F.w0 = t10;
        goto gm_ls11;
gm_ll8:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
        goto gm_lad;
gm_ls11:;
we7:;
        gm_b0_1 t12 = F.w0;
        gm_b0_1 t13 = t12;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 40);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_parse_error_text_pasvuia(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tekvgkj2hms37 a1) {
    struct gm_fr_parse_error_text_pasvuia F;
    gm_b16_8 gm_l0; /* bytes */
    gm_tekvgkj2hms37 gm_l1; /* e */
    uint64_t gm_l2; /* offset */
    gm_b16_8 gm_l3; /* what */
    uint8_t gm_l4; /* at_eof */
    uint64_t gm_l5; /* o */
    uint64_t gm_l6; /* o */
    uint64_t gm_l7; /* o */
    uint64_t gm_l8; /* o */
    uint64_t gm_l9; /* o */
    uint64_t gm_l10; /* o */
    uint64_t gm_l11; /* o */
    uint64_t gm_l12; /* o */
    uint64_t gm_l13; /* o */
    gm_t2lqu8jsj511d gm_l14; /* p */
    uint64_t gm_l15; /* column */
    GM_STACK_CHECK(&F, 824u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s7s8viq00e4g2));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
        gm_l3 = t2;
    }
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma13; default: goto ma14; }
ma4:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t17 != ((uint8_t)0ULL)) goto mn16;
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_l5 = t18;
        {
            uint64_t t20 = gm_l5;
            gm_l2 = t20;
        }
        {
            gm_b16_8 t21 = gm_l0;
            uint64_t t22 = gm_l5;
            gm_b16_8 t23 = gm_f_syntax_error_4bqcc39((void *)NULL, cx, NULL, t21, t22, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t23;
        }
        {
            gm_l4 = ((uint8_t)1);
        }
        goto me3;
mn16:;
ma5:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t25 != ((uint8_t)1ULL)) goto mn24;
        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_l6 = t26;
        {
            uint64_t t28 = gm_l6;
            gm_l2 = t28;
        }
        {
            gm_b16_8 t29 = gm_l0;
            uint64_t t30 = gm_l6;
            gm_b16_8 t31 = gm_f_syntax_error_4bqcc39((void *)NULL, cx, NULL, t29, t30, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t31;
        }
        goto me3;
mn24:;
ma6:;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t33 != ((uint8_t)2ULL)) goto mn32;
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_l7 = t34;
        {
            uint64_t t36 = gm_l7;
            gm_l2 = t36;
        }
        {
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_scj618tc62815));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(14));
            gm_l3 = t37;
        }
        goto me3;
mn32:;
ma7:;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t39 != ((uint8_t)3ULL)) goto mn38;
        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_l8 = t40;
        {
            uint64_t t42 = gm_l8;
            gm_l2 = t42;
        }
        {
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sh0h8t5us2qkm));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(14));
            gm_l3 = t43;
        }
        goto me3;
mn38:;
ma8:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t45 != ((uint8_t)4ULL)) goto mn44;
        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_l9 = t46;
        {
            uint64_t t48 = gm_l9;
            gm_l2 = t48;
        }
        {
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_stv59gn45ivj9));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(36));
            gm_l3 = t49;
        }
        goto me3;
mn44:;
ma9:;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t51 != ((uint8_t)5ULL)) goto mn50;
        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_l10 = t52;
        {
            uint64_t t54 = gm_l10;
            gm_l2 = t54;
        }
        {
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(gm_stpc2dcrflnth));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(62));
            gm_l3 = t55;
        }
        goto me3;
mn50:;
ma10:;
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t57 != ((uint8_t)6ULL)) goto mn56;
        uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_l11 = t58;
        {
            uint64_t t60 = gm_l11;
            gm_l2 = t60;
        }
        {
            gm_b16_8 t61;
            GM_ST(void *, (uint8_t *)&t61, (void *)(gm_soh6rol6f5r5k));
            GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(26));
            gm_l3 = t61;
        }
        goto me3;
mn56:;
ma11:;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t63 != ((uint8_t)7ULL)) goto mn62;
        uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_l12 = t64;
        {
            uint64_t t66 = gm_l12;
            gm_l2 = t66;
        }
        {
            gm_b16_8 t67;
            GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sgpr1l6rnrmel));
            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(19));
            gm_l3 = t67;
        }
        goto me3;
mn62:;
ma12:;
        uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t69 != ((uint8_t)8ULL)) goto mn68;
        uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_l13 = t70;
        {
            uint64_t t72 = gm_l13;
            gm_l2 = t72;
        }
        {
            gm_b16_8 t73;
            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_s0h2dl2t3n8dh));
            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(24));
            gm_l3 = t73;
        }
        goto me3;
mn68:;
ma13:;
        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t75 != ((uint8_t)9ULL)) goto mn74;
        gm_b16_8 t76;
        GM_ST(void *, (uint8_t *)&t76, (void *)(gm_sidpdvffdra02));
        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(13));
        uint8_t t77 = 0;
        F.ret = t76;
        goto gm_fret;
        goto me3;
mn74:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 249, 5);
me3:;
    }
    {
        gm_b16_8 t78 = gm_l0;
        uint64_t t79 = gm_l2;
        gm_t2lqu8jsj511d t80 = GM_CONV(gm_t2lqu8jsj511d, gm_f_position_f7cepnk(t78, t79));
        gm_l14 = t80;
    }
    {
        uint64_t t81;
        uint8_t t83 = gm_l4;
        if (!t83) goto ie82;
        uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
        uint8_t t85 = (t84 > ((uint64_t)0ULL));
        if (!t85) goto ie82;
        uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
        uint64_t r88; bool o89 = __builtin_sub_overflow(t87, ((uint64_t)1ULL), &r88);
        if (o89) gm_trap("integer overflow in `-`", gm_srcfpg6jsb3cfp6, 290, 44);
        uint64_t t90 = r88;
        uint64_t t91 = t90;
        t81 = t91;
        goto ix82;
ie82:;
        uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
        uint64_t t94 = t93;
        t81 = t94;
ix82:;
        gm_l15 = t81;
    }
    {
        gm_tgbgti82mf879 t95;
        gm_b16_8 t96 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t95 + 0, t96);
        uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 0));
        GM_ST(uint64_t, (uint8_t *)&t95 + 16, t97);
        uint64_t t98 = gm_l15;
        GM_ST(uint64_t, (uint8_t *)&t95 + 24, t98);
        gm_tabb710d9mq2e t99;
        gm_f_format_kb96ddt((void *)&t99, (void *)NULL, cx, NULL, &t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t100 = t99;
        if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 8, gm_srcfpg6jsb3cfp6, 291, 12);
        gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&t100) + 8));
        gm_b16_8 t102 = t101;
        gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
        uint8_t t104 = 0;
        F.ret = t103;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_publish_empty_lh10nsv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_publish_empty_lh10nsv F;
    gm_b16_8 gm_l0; /* uri */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_obj_aoa7fl7((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbo52miqad506));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_b16_8 t4 = gm_l0;
        gm_t8vuock3jnv59 t5;
        gm_f_text_ls49dp6((void *)&t5, t4);
        gm_b0_1 t6 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t2, t3, &t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sq8aqd2uj6ppq));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(11));
        gm_b16_8 t9 = gm_f_empty_slice_dheabhi_3();
        gm_b16_8 t10 = t9;
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        gm_t8vuock3jnv59 t12;
        gm_f_array_k32ana3((void *)&t12, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t13 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t7, t8, &t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_se9jjcum4l3k9));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(31));
        void * t15 = (void *)((uint8_t *)&F.l1);
        gm_t8vuock3jnv59 t16;
        gm_f_value_pkuqal3((void *)&t16, t15);
        gm_b0_1 t17 = gm_f_notify_ls6n69d((void *)NULL, cx, NULL, t14, &t16);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_publish_put2rqb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_publish_put2rqb F;
    void * gm_l0; /* srv */
    void * gm_l2; /* a */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l9; /* <tmp> */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* p */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l16; /* path */
    gm_b16_8 gm_l17; /* items */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    GM_STACK_CHECK(&F, 1809u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_f_uri_map_mhud6ak((void *)&F.l3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l2;
        void * t4 = gm_l0;
        gm_t9qkh0rsooc8d t5 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t4) + 0));
        void * t6 = (void *)((uint8_t *)&F.l3);
        gm_f_diagnostics_7ntqr3v((void *)&F.l4, (void *)NULL, cx, NULL, t3, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_l6 = t8;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l7;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l7;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_tl7hpf5nlilqq t20 = GM_LD(gm_tl7hpf5nlilqq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t16 * 56));
                    F.l8 = t20;
                    {
                        void * t22 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                        gm_tftb1odc739t5 t24 = gm_f_push_3ef746b(t22, t23);
                        gm_tftb1odc739t5 t25 = t24;
                        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_srcfpg6jsb3cfp6, 585, 28);
                        gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 585, 5);
me18:;
                }
lc9:;
                {
                    uint64_t t28 = gm_l7;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l7 = t31;
                }
            }
lb9:;
        }
    }
    {
        {
            void * t33 = gm_l0;
            void * t34 = (void *)(((uint8_t *)t33) + 8);
            gm_l9 = t34;
        }
        gm_hval t35 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn36 = {.eff = 1408113935, .kind = GM_LIT, .clauses = gm_hlk5mm1461td, .env = t35.env, .hctx = t35.cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
        void * t39 = gm_l9;
        gm_b0_1 t40 = gm_f_entries_qfi2q5v_sogpj69((void *)NULL, (&wn36), NULL, t39);
        if (gm_unw.kind) {
            goto gm_ll38;
        }
        F.w1 = t40;
        goto gm_ls41;
gm_ll38:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we37; }
        goto gm_lad;
gm_ls41:;
we37:;
        gm_b0_1 t42 = F.w1;
        gm_b0_1 t43 = t42;
    }
    {
        void * t45 = gm_l0;
        void * t46 = (void *)(((uint8_t *)t45) + 176);
        gm_b16_8 t47 = F.l1;
        gm_tqetrgo0q24b5 t48;
        gm_f_get_ahntg11((void *)&t48, t46, t47);
        gm_tqetrgo0q24b5 t49 = t48;
        if (!((((uint8_t *)&t49))[40] != 0)) goto ie44;
        gm_trqfvmjtt4sij t50 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t49));
        F.l10 = t50;
        {
            {
                gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                gm_l11 = t53;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t55 = gm_l12;
                    uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t57 = (t55 < t56);
                    if (!t57) goto lb54;
                    {
                        uint64_t t60 = gm_l12;
                        uint64_t t61 = (uintptr_t)t60;
                        uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t61 * 16));
                        gm_l13 = t65;
                        {
                            gm_b16_8 t68 = gm_l13;
                            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                            gm_b16_8 t70 = t69;
                            uint8_t t71 = 0;
                            gm_b16_8 t72 = t68;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t70) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t70))) + i * 16, ((uint8_t *)&t72))) { t71 = 1; break; }
                            uint8_t t73 = (uint8_t)(!t71);
                            if (!t73) goto ie67;
                            {
                                void * t75 = (void *)((uint8_t *)&F.l5);
                                gm_b16_8 t76 = gm_l13;
                                gm_tftb1odc739t5 t77 = gm_f_push_3ef746b(t75, t76);
                                gm_tftb1odc739t5 t78 = t77;
                                if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 1, gm_srcfpg6jsb3cfp6, 591, 41);
                                gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 1));
                            }
                            goto ix67;
ie67:;
ix67:;
                        }
                        goto me63;
mn64:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 590, 9);
me63:;
                    }
lc54:;
                    {
                        uint64_t t81 = gm_l12;
                        uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                        uint64_t t84 = r82;
                        gm_l12 = t84;
                    }
                }
lb54:;
            }
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_closure t86 = ((gm_closure){GM_PTR_TAG(gm_f_fn_pt42u9t), NULL, cx});
        gm_f_sort_by_s9l01h6(t85, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t87 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        uint64_t t89 = gm_f_dedup_6a2r2j0(t88);
        gm_f_truncate_vir0vu7(t87, t89);
    }
    {
        {
            gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_l14 = t91;
        }
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t93 = gm_l15;
                uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint8_t t95 = (t93 < t94);
                if (!t95) goto lb92;
                {
                    uint64_t t98 = gm_l15;
                    uint64_t t99 = (uintptr_t)t98;
                    uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t99 * 16));
                    gm_l16 = t103;
                    {
                        gm_b16_8 t105 = gm_f_empty_slice_dheabhi_3();
                        gm_l17 = t105;
                    }
                    {
                        {
                            gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                            gm_l18 = t107;
                        }
                        {
                            gm_l19 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t109 = gm_l19;
                                uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                uint8_t t111 = (t109 < t110);
                                if (!t111) goto lb108;
                                {
                                    uint64_t t114 = gm_l19;
                                    uint64_t t115 = (uintptr_t)t114;
                                    uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                    gm_tl7hpf5nlilqq t119 = GM_LD(gm_tl7hpf5nlilqq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t115 * 56));
                                    F.l20 = t119;
                                    {
                                        gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                                        gm_b16_8 t123 = gm_l16;
                                        gm_b16_8 t124 = t122;
                                        gm_b16_8 t125 = t123;
                                        uint8_t t126 = (gm_eq0iqiubfm7e(((uint8_t *)&t124), ((uint8_t *)&t125)));
                                        if (!t126) goto ie121;
                                        {
                                            gm_b16_8 t128 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l20) + 16) + 0));
                                            gm_l17 = t128;
                                        }
                                        goto ix121;
ie121:;
ix121:;
                                    }
                                    goto me117;
mn118:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 599, 9);
me117:;
                                }
lc108:;
                                {
                                    uint64_t t130 = gm_l19;
                                    uint64_t r131; bool o132 = __builtin_add_overflow(t130, ((uint64_t)1ULL), &r131);
                                    uint64_t t133 = r131;
                                    gm_l19 = t133;
                                }
                            }
lb108:;
                        }
                    }
                    {
                        gm_f_obj_aoa7fl7((void *)&F.l21, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll96;
                        }
                    }
                    {
                        void * t134 = (void *)((uint8_t *)&F.l21);
                        gm_b16_8 t135;
                        GM_ST(void *, (uint8_t *)&t135, (void *)(gm_sbo52miqad506));
                        GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(3));
                        void * t136 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t137 = gm_l16;
                        gm_b16_8 t138 = gm_f_uri_of_t6r79gp((void *)NULL, cx, NULL, t136, t137);
                        if (gm_unw.kind) {
                            goto gm_ll96;
                        }
                        gm_t8vuock3jnv59 t139;
                        gm_f_text_ls49dp6((void *)&t139, t138);
                        gm_b0_1 t140 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t134, t135, &t139);
                        if (gm_unw.kind) {
                            goto gm_ll96;
                        }
                    }
                    {
                        void * t141 = (void *)((uint8_t *)&F.l21);
                        gm_b16_8 t142;
                        GM_ST(void *, (uint8_t *)&t142, (void *)(gm_sq8aqd2uj6ppq));
                        GM_ST(uintptr_t, (uint8_t *)&t142 + 8, (uintptr_t)(11));
                        gm_b16_8 t143 = gm_l17;
                        gm_b16_8 t144 = t143;
                        gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
                        gm_t8vuock3jnv59 t146;
                        gm_f_array_k32ana3((void *)&t146, (void *)NULL, cx, NULL, t145);
                        if (gm_unw.kind) {
                            goto gm_ll96;
                        }
                        gm_b0_1 t147 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t141, t142, &t146);
                        if (gm_unw.kind) {
                            goto gm_ll96;
                        }
                    }
                    {
                        void * t149 = gm_l0;
                        void * t150 = (void *)(((uint8_t *)t149) + 8);
                        gm_b16_8 t151 = gm_l16;
                        gm_tmle170q2vbs1 t152;
                        gm_f_get_t10cn5s((void *)&t152, t150, t151);
                        gm_tmle170q2vbs1 t153 = t152;
                        if (!((((uint8_t *)&t153))[40] != 0)) goto ie148;
                        gm_tqj9esv5ukti3 t154 = GM_LD(gm_tqj9esv5ukti3, ((uint8_t *)&t153));
                        F.l22 = t154;
                        {
                            void * t156 = (void *)((uint8_t *)&F.l21);
                            gm_b16_8 t157;
                            GM_ST(void *, (uint8_t *)&t157, (void *)(gm_sla3alt5340mf));
                            GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(7));
                            int64_t t158 = GM_LD(int64_t, (((uint8_t *)&F.l22) + 16));
                            gm_t8vuock3jnv59 t159;
                            gm_f_int_95caidg((void *)&t159, (void *)NULL, cx, NULL, t158);
                            if (gm_unw.kind) {
                                goto gm_ll96;
                            }
                            gm_b0_1 t160 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t156, t157, &t159);
                            if (gm_unw.kind) {
                                goto gm_ll96;
                            }
                        }
                        goto ix148;
ie148:;
ix148:;
                    }
                    {
                        gm_b16_8 t161;
                        GM_ST(void *, (uint8_t *)&t161, (void *)(gm_se9jjcum4l3k9));
                        GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(31));
                        void * t162 = (void *)((uint8_t *)&F.l21);
                        gm_t8vuock3jnv59 t163;
                        gm_f_value_pkuqal3((void *)&t163, t162);
                        gm_b0_1 t164 = gm_f_notify_ls6n69d((void *)NULL, cx, NULL, t161, &t163);
                        if (gm_unw.kind) {
                            goto gm_ll96;
                        }
                    }
                    goto me101;
mn102:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 597, 5);
me101:;
                }
lc92:;
                {
                    uint64_t t166 = gm_l15;
                    uint64_t r167; bool o168 = __builtin_add_overflow(t166, ((uint64_t)1ULL), &r167);
                    uint64_t t169 = r167;
                    gm_l15 = t169;
                }
            }
gm_ll96:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb92; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc92; }
            }
            goto gm_lad;
lb92:;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l23, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t171 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_l24 = t171;
        }
        {
            gm_l25 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t173 = gm_l25;
                uint64_t t174 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                uint8_t t175 = (t173 < t174);
                if (!t175) goto lb172;
                {
                    uint64_t t178 = gm_l25;
                    uint64_t t179 = (uintptr_t)t178;
                    uint64_t t180 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                    gm_tl7hpf5nlilqq t183 = GM_LD(gm_tl7hpf5nlilqq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t179 * 56));
                    F.l26 = t183;
                    {
                        void * t185 = (void *)((uint8_t *)&F.l23);
                        gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
                        gm_tftb1odc739t5 t187 = gm_f_push_3ef746b(t185, t186);
                        gm_tftb1odc739t5 t188 = t187;
                        if (((((uint8_t *)&t188))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t188) + 1, gm_srcfpg6jsb3cfp6, 609, 28);
                        gm_b0_1 t189 = GM_LD(gm_b0_1, (((uint8_t *)&t188) + 1));
                    }
                    goto me181;
mn182:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 609, 5);
me181:;
                }
lc172:;
                {
                    uint64_t t191 = gm_l25;
                    uint64_t r192; bool o193 = __builtin_add_overflow(t191, ((uint64_t)1ULL), &r192);
                    uint64_t t194 = r192;
                    gm_l25 = t194;
                }
            }
lb172:;
        }
    }
    {
        void * t195 = gm_l0;
        void * t196 = (void *)(((uint8_t *)t195) + 176);
        gm_b16_8 t197 = F.l1;
        gm_tftb1odc739t5 t198 = gm_f_put_eppc06m(t196, t197, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l23)));
        gm_tftb1odc739t5 t199 = t198;
        if (((((uint8_t *)&t199))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t199) + 1, gm_srcfpg6jsb3cfp6, 610, 5);
        gm_b0_1 t200 = GM_LD(gm_b0_1, (((uint8_t *)&t199) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_refresh_rsuaicn(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_refresh_rsuaicn F;
    void * gm_l0; /* srv */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_b16_8 gm_l3; /* d */
    GM_STACK_CHECK(&F, 249u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            gm_trqfvmjtt4sij t4;
            gm_f_open_dirs_9ud02ot((void *)&t4, (void *)NULL, cx, NULL, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trqfvmjtt4sij t5 = t4;
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
            gm_l1 = t6;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l2;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 16));
                    gm_l3 = t18;
                    {
                        void * t20 = gm_l0;
                        gm_b16_8 t21 = gm_l3;
                        gm_b0_1 t22 = gm_f_ensure_9tp264o((void *)NULL, cx, NULL, t20, t21);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 544, 5);
me16:;
                }
lc7:;
                {
                    uint64_t t24 = gm_l2;
                    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                    uint64_t t27 = r25;
                    gm_l2 = t27;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_register_watchers_k2akg1c(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_register_watchers_k2akg1c F;
    void * gm_l0; /* srv */
    gm_b16_8 gm_l1; /* id */
    gm_tq47bkpmd2b9a gm_l8; /* <temporary array> */
    GM_STACK_CHECK(&F, 1043u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 290));
        uint8_t t5 = (uint8_t)!t4;
        if (!t5) goto ie2;
        {
            gm_b0_1 t7;
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
        void * t10 = gm_l0;
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 296));
        uint64_t r12; bool o13 = __builtin_add_overflow(t11, ((uint64_t)1ULL), &r12);
        if (o13) gm_trap("integer overflow in `+`", gm_srcfpg6jsb3cfp6, 413, 5);
        uint64_t t14 = r12;
        GM_ST(uint64_t, (((uint8_t *)t9) + 296), t14);
    }
    {
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 296));
        gm_t4ce0jd3tdro4 t17;
        GM_ST(uint64_t, ((uint8_t *)&t17 + 0), t16);
        gm_tabb710d9mq2e t18;
        gm_f_format_p7shig5((void *)&t18, (void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t19 = t18;
        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_srcfpg6jsb3cfp6, 414, 11);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        gm_l1 = t20;
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t21 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_srv7efdnr211j));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(11));
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_se287i5v97ugv));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(7));
        gm_t8vuock3jnv59 t24;
        gm_f_text_ls49dp6((void *)&t24, t23);
        gm_b0_1 t25 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t21, t22, &t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(gm_srv7efdnr211j));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(11));
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_stq6t1a4hvefn));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(13));
        gm_t8vuock3jnv59 t29;
        gm_f_text_ls49dp6((void *)&t29, t28);
        gm_b0_1 t30 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t26, t27, &t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_0202sdh_3((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t31 = (void *)((uint8_t *)&F.l4);
        void * t32 = (void *)((uint8_t *)&F.l2);
        gm_t8vuock3jnv59 t33;
        gm_f_value_pkuqal3((void *)&t33, t32);
        gm_tftb1odc739t5 t34 = gm_f_push_0pgacsj_3(t31, &t33);
        gm_tftb1odc739t5 t35 = t34;
        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_srcfpg6jsb3cfp6, 420, 5);
        gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
    }
    {
        void * t37 = (void *)((uint8_t *)&F.l4);
        void * t38 = (void *)((uint8_t *)&F.l3);
        gm_t8vuock3jnv59 t39;
        gm_f_value_pkuqal3((void *)&t39, t38);
        gm_tftb1odc739t5 t40 = gm_f_push_0pgacsj_3(t37, &t39);
        gm_tftb1odc739t5 t41 = t40;
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcfpg6jsb3cfp6, 421, 5);
        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t43 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_schmrvl8mk83l));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(8));
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t46 = t45;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
        gm_t8vuock3jnv59 t48;
        gm_f_array_k32ana3((void *)&t48, (void *)NULL, cx, NULL, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t49 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t43, t44, &t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t50 = (void *)((uint8_t *)&F.l6);
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sq3gi0chb3cs2));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(2));
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s8v8krjfns655));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(19));
        gm_t8vuock3jnv59 t53;
        gm_f_text_ls49dp6((void *)&t53, t52);
        gm_b0_1 t54 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t50, t51, &t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t55 = (void *)((uint8_t *)&F.l6);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sg7of7eks8a5u));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(6));
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sgge76tb2bmvi));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(31));
        gm_t8vuock3jnv59 t58;
        gm_f_text_ls49dp6((void *)&t58, t57);
        gm_b0_1 t59 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t55, t56, &t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t60 = (void *)((uint8_t *)&F.l6);
        gm_b16_8 t61;
        GM_ST(void *, (uint8_t *)&t61, (void *)(gm_s0jtk73jm5ft9));
        GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(15));
        void * t62 = (void *)((uint8_t *)&F.l5);
        gm_t8vuock3jnv59 t63;
        gm_f_value_pkuqal3((void *)&t63, t62);
        gm_b0_1 t64 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t60, t61, &t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t65 = (void *)((uint8_t *)&F.l7);
        gm_b16_8 t66;
        GM_ST(void *, (uint8_t *)&t66, (void *)(gm_s7mc3uqhc5a41));
        GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(13));
        {
            void * t68 = (void *)((uint8_t *)&F.l6);
            gm_t8vuock3jnv59 t69;
            gm_f_value_pkuqal3((void *)&t69, t68);
            GM_ST(gm_t8vuock3jnv59, (uint8_t *)&(*(gm_tq47bkpmd2b9a *)((uint8_t *)&gm_l8)) + 0, t69);
        }
        void * t70 = ((uint8_t *)&gm_l8);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcfpg6jsb3cfp6, 429, 34);
        gm_b16_8 t71;
        GM_ST(void *, (uint8_t *)&t71, (void *)(((uint8_t *)t70 + (uintptr_t)0 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t72 = t71;
        gm_t8vuock3jnv59 t73;
        gm_f_array_k32ana3((void *)&t73, (void *)NULL, cx, NULL, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t74 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t65, t66, &t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_envelope_4dp2tms((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t75 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t76;
        GM_ST(void *, (uint8_t *)&t76, (void *)(gm_sq3gi0chb3cs2));
        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(2));
        gm_b16_8 t77 = gm_l1;
        gm_b16_8 t78 = t77;
        gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
        gm_t8vuock3jnv59 t80;
        gm_f_text_ls49dp6((void *)&t80, t79);
        gm_b0_1 t81 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t75, t76, &t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t82 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t83;
        GM_ST(void *, (uint8_t *)&t83, (void *)(gm_sg7of7eks8a5u));
        GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(6));
        gm_b16_8 t84;
        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_sdo5tfkgp02tp));
        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(25));
        gm_t8vuock3jnv59 t85;
        gm_f_text_ls49dp6((void *)&t85, t84);
        gm_b0_1 t86 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t82, t83, &t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t87 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sfkmkcagsnqnb));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(6));
        void * t89 = (void *)((uint8_t *)&F.l7);
        gm_t8vuock3jnv59 t90;
        gm_f_value_pkuqal3((void *)&t90, t89);
        gm_b0_1 t91 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t87, t88, &t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t92 = (void *)((uint8_t *)&F.l9);
        gm_t8vuock3jnv59 t93;
        gm_f_value_pkuqal3((void *)&t93, t92);
        gm_b0_1 t94 = gm_f_send_cg3jp71((void *)NULL, cx, NULL, &t93);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_request_nncosh4(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, const void * a3) {
    struct gm_fr_request_nncosh4 F;
    void * gm_l0; /* srv */
    gm_t8vuock3jnv59 gm_l1; /* id */
    gm_b16_8 gm_l2; /* method */
    gm_t8vuock3jnv59 gm_l3; /* params */
    gm_thgmovcqlsivv gm_l4; /* e */
    gm_t8vuock3jnv59 gm_l5; /* r */
    GM_STACK_CHECK(&F, 628u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 288));
        if (!t4) goto ie2;
        uint8_t t5 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_scbch1b3qo8hr, 8) == 0));
        if (!t5) goto ie2;
        {
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_spqqkr6igks2d));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(27));
            gm_b0_1 t8 = gm_f_respond_error_5vpslhl((void *)NULL, cx, NULL, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a1)), ((int64_t)-32600LL), t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_smga32pj96fal, 10) == 0)) goto mn12;
        {
            void * t14 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t14) + 289), ((uint8_t)1));
        }
        {
            gm_t8vuock3jnv59 t15 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)a1));
            void * t16 = gm_l0;
            gm_t8vuock3jnv59 t17;
            gm_f_initialize_q3q654t((void *)&t17, (void *)NULL, cx, NULL, t16, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t18 = gm_f_respond_vfmbdjk((void *)NULL, cx, NULL, &t15, &t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me11;
mn12:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_scbch1b3qo8hr, 8) == 0)) goto mn19;
        {
            void * t21 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t21) + 288), ((uint8_t)1));
        }
        {
            gm_t8vuock3jnv59 t22 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            gm_b0_1 t23 = gm_f_respond_vfmbdjk((void *)NULL, cx, NULL, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a1)), &t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me11;
mn19:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 23) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s4dqb8b35j0gp, 23) == 0)) goto mn24;
        gm_t8vuock3jnv59 t25 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)a1));
        gm_t8vuock3jnv59 t26;
        gm_f_code_actions_k897tcq((void *)&t26, (void *)NULL, cx, NULL, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t27 = gm_f_respond_vfmbdjk((void *)NULL, cx, NULL, &t25, &t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me11;
mn24:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 18) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_svrjvsqtthedc, 18) == 0)) goto pa30;
        goto po29;
pa30:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 23) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_spgshrus7teti, 23) == 0)) goto pa31;
        goto po29;
pa31:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 23) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sj3m78jf2dftr, 23) == 0)) goto pa32;
        goto po29;
pa32:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 26) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sgh9nkjgl73of, 26) == 0)) goto pa33;
        goto po29;
pa33:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 19) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_surog9s0b8r6p, 19) == 0)) goto pa34;
        goto po29;
pa34:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 23) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_slogp373o1rii, 23) == 0)) goto pa35;
        goto po29;
pa35:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 27) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_srod2911mer3j, 27) == 0)) goto pa36;
        goto po29;
pa36:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 26) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s4n5m9kt2s9ld, 26) == 0)) goto pa37;
        goto po29;
pa37:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 23) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_solaeibjdukdj, 23) == 0)) goto pa38;
        goto po29;
pa38:;
        goto mn28;
po29:;
        {
            void * t40 = gm_l0;
            gm_b16_8 t41 = gm_l2;
            gm_tfaata5ba51n4 t42;
            gm_f_language_request_21k642b((void *)&t42, (void *)NULL, cx, NULL, t40, t41, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tfaata5ba51n4 t43 = t42;
            gm_t8vuock3jnv59 t44;
            if (!((((uint8_t *)&t43))[0] != 0)) {
                gm_t8vuock3jnv59 t45 = GM_LD(gm_t8vuock3jnv59, (((uint8_t *)&t43) + 8));
                t44 = t45;
            } else {
                gm_thgmovcqlsivv t46 = GM_LD(gm_thgmovcqlsivv, (((uint8_t *)&t43) + 8));
                gm_l4 = t46;
                {
                    int64_t t48 = GM_LD(int64_t, (((uint8_t *)&gm_l4) + 0));
                    gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                    gm_b0_1 t50 = gm_f_respond_error_5vpslhl((void *)NULL, cx, NULL, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a1)), t48, t49);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                gm_b0_1 t51;
                uint8_t t52 = 0;
                F.ret = t51;
                goto gm_fret;
            }
            gm_l5 = t44;
        }
        {
            gm_b0_1 t53 = gm_f_respond_vfmbdjk((void *)NULL, cx, NULL, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a1)), &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l5)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me11;
mn28:;
        gm_t8vuock3jnv59 t55 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)a1));
        gm_b16_8 t56 = gm_l2;
        gm_tnpifv20irnph t57;
        GM_ST(gm_b16_8, ((uint8_t *)&t57 + 0), t56);
        gm_tabb710d9mq2e t58;
        gm_f_format_hr0sl7f((void *)&t58, (void *)NULL, cx, NULL, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t59 = t58;
        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 8, gm_srcfpg6jsb3cfp6, 639, 50);
        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t59) + 8));
        gm_b16_8 t61 = t60;
        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
        gm_b0_1 t63 = gm_f_respond_error_5vpslhl((void *)NULL, cx, NULL, &t55, ((int64_t)-32601LL), t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me11;
mn54:;
        gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 620, 5);
me11:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_respond_error_5vpslhl(void *env, gm_node *cx, gm_node *inst, const void * a0, int64_t a1, gm_b16_8 a2) {
    struct gm_fr_respond_error_5vpslhl F;
    gm_t8vuock3jnv59 gm_l0; /* id */
    int64_t gm_l1; /* code */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_obj_aoa7fl7((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2e2o3u4kr3q0));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        int64_t t4 = gm_l1;
        gm_t8vuock3jnv59 t5;
        gm_f_int_95caidg((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t6 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t2, t3, &t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sujli4dml3ipq));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(7));
        gm_b16_8 t9 = gm_l2;
        gm_t8vuock3jnv59 t10;
        gm_f_text_ls49dp6((void *)&t10, t9);
        gm_b0_1 t11 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t7, t8, &t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_envelope_4dp2tms((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sq3gi0chb3cs2));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(2));
        gm_b0_1 t14 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t12, t13, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t15 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s79s3h98matgr));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(5));
        void * t17 = (void *)((uint8_t *)&F.l3);
        gm_t8vuock3jnv59 t18;
        gm_f_value_pkuqal3((void *)&t18, t17);
        gm_b0_1 t19 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t15, t16, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t20 = (void *)((uint8_t *)&F.l4);
        gm_t8vuock3jnv59 t21;
        gm_f_value_pkuqal3((void *)&t21, t20);
        gm_b0_1 t22 = gm_f_send_cg3jp71((void *)NULL, cx, NULL, &t21);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_respond_vfmbdjk(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_respond_vfmbdjk F;
    gm_t8vuock3jnv59 gm_l0; /* id */
    gm_t8vuock3jnv59 gm_l1; /* result */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        gm_f_envelope_4dp2tms((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq3gi0chb3cs2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
        gm_b0_1 t4 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t2, t3, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_spm7tm8a050b3));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(6));
        gm_b0_1 t7 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t5, t6, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t8 = (void *)((uint8_t *)&F.l2);
        gm_t8vuock3jnv59 t9;
        gm_f_value_pkuqal3((void *)&t9, t8);
        gm_b0_1 t10 = gm_f_send_cg3jp71((void *)NULL, cx, NULL, &t9);
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
inline __attribute__((aligned(4))) uint8_t gm_f_run_rkae3vt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_run_rkae3vt F;
    void * gm_l0; /* srv */
    void * gm_l1; /* inbox */
    gm_t3ujhu6qph7de gm_l2; /* msg */
    gm_t3ujhu6qph7de gm_l3; /* m */
    gm_t3ujhu6qph7de gm_l4; /* m */
    uint8_t gm_l5; /* code */
    GM_STACK_CHECK(&F, 446u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb2;
            {
                gm_b16_8 t5;
                GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(0));
                gm_t3ujhu6qph7de t6;
                GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t6 + 8), t5);
                gm_l2 = t6;
            }
            {
                void * t8 = gm_l0;
                uint8_t t9 = gm_f_needs_analysis_l0op99b((void *)NULL, cx, NULL, t8);
                if (gm_unw.kind) {
                    goto gm_ll3;
                }
                if (!t9) goto ie7;
                {
                    void * t11 = gm_l1;
                    gm_t715ajl6o1ipo t12;
                    gm_f_try_take_gd0vuic((void *)&t12, t11);
                    gm_t715ajl6o1ipo t13 = t12;
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
                    switch (t19) { case ((uint8_t)0ULL): goto ma15; case ((uint8_t)1ULL): goto ma16; case ((uint8_t)2ULL): goto ma17; default: goto ma18; }
ma15:;
                    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
                    if (t21 != ((uint8_t)0ULL)) goto mn20;
                    gm_t3ujhu6qph7de t22 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&t13) + 8));
                    gm_l3 = t22;
                    {
                        gm_t3ujhu6qph7de t24 = gm_l3;
                        gm_l2 = t24;
                    }
                    goto me14;
mn20:;
ma16:;
                    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
                    if (t26 != ((uint8_t)1ULL)) goto mn25;
                    {
                        void * t28 = gm_l0;
                        gm_b0_1 t29 = gm_f_refresh_rsuaicn((void *)NULL, cx, NULL, t28);
                        if (gm_unw.kind) {
                            goto gm_ll3;
                        }
                    }
                    {
                        goto lc2;
                    }
                    goto me14;
mn25:;
ma17:;
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
                    if (t31 != ((uint8_t)2ULL)) goto mn30;
                    goto lb2;
                    goto me14;
mn30:;
ma18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 108, 13);
me14:;
                }
                goto ix7;
ie7:;
                {
                    void * t33 = gm_l1;
                    gm_tnnrsfd8ehhme t34;
                    gm_f_take_mg8jq2q((void *)&t34, t33);
                    gm_tnnrsfd8ehhme t35 = t34;
                    gm_t3ujhu6qph7de t36;
                    if (((((uint8_t *)&t35))[24] != 0)) {
                        gm_t3ujhu6qph7de t37 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&t35) + 0));
                        t36 = t37;
                    } else {
                        goto lb2;
                    }
                    gm_l4 = t36;
                }
                {
                    gm_t3ujhu6qph7de t38 = gm_l4;
                    gm_l2 = t38;
                }
ix7:;
            }
            {
                void * t39 = gm_l0;
                gm_tk5oh6c9vhlab t40 = gm_f_handle_incoming_uj1ifur((void *)NULL, cx, NULL, t39, &(*(gm_t3ujhu6qph7de *)((uint8_t *)&gm_l2)));
                if (gm_unw.kind) {
                    goto gm_ll3;
                }
                gm_tk5oh6c9vhlab t41 = t40;
                uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&t41) + 0));
                if (t44 != ((uint8_t)1ULL)) goto mn43;
                uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t41) + 1));
                gm_l5 = t45;
                uint8_t t46 = gm_l5;
                uint8_t t47 = 0;
                F.ret = t46;
                goto gm_fret;
                goto me42;
mn43:;
                uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t41) + 0));
                if (t49 != ((uint8_t)0ULL)) goto mn48;
                goto me42;
mn48:;
                gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 120, 9);
me42:;
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
        uint8_t t51;
        void * t53 = gm_l0;
        uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)t53) + 288));
        if (!t54) goto ie52;
        uint8_t t56 = ((uint8_t)0ULL);
        t51 = t56;
        goto ix52;
ie52:;
        uint8_t t58 = ((uint8_t)1ULL);
        t51 = t58;
ix52:;
        uint8_t t59 = 0;
        F.ret = t51;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_send_cg3jp71(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_server_6g9m6gg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_server_6g9m6gg F;
    GM_STACK_CHECK(&F, 281u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        memset((uint8_t *)&(*(gm_tmnkfo49014kk *)gm_out) + 1, 0, 7);
        memset((uint8_t *)&(*(gm_tmnkfo49014kk *)gm_out) + 291, 0, 5);
        gm_t9qkh0rsooc8d t2;
        GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)1ULL));
        gm_t4c7ipjj9s3fp t3;
        gm_f_hash_map_irta8m9((void *)&t3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t4;
        gm_f_hash_map_s1unuf2((void *)&t4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t5;
        gm_f_hash_map_cuh4fs9((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t6;
        gm_f_hash_map_mkadlk3((void *)&t6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttiitul0263ld t7;
        gm_f_workspace_ubjeikp((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tmnkfo49014kk *)gm_out) + 0, t2);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_tmnkfo49014kk *)gm_out) + 8, t3);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_tmnkfo49014kk *)gm_out) + 64, t4);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_tmnkfo49014kk *)gm_out) + 120, t5);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_tmnkfo49014kk *)gm_out) + 176, t6);
        GM_ST(gm_ttiitul0263ld, (uint8_t *)&(*(gm_tmnkfo49014kk *)gm_out) + 232, t7);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmnkfo49014kk *)gm_out) + 288, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmnkfo49014kk *)gm_out) + 289, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmnkfo49014kk *)gm_out) + 290, ((uint8_t)0));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tmnkfo49014kk *)gm_out) + 296, ((uint64_t)0ULL));
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
__attribute__((aligned(4))) gm_b16_8 gm_f_syntax_error_4bqcc39(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint8_t a2) {
    struct gm_fr_syntax_error_4bqcc39 F;
    gm_b16_8 gm_l0; /* bytes */
    uint64_t gm_l1; /* offset */
    uint8_t gm_l2; /* at_end */
    uint64_t gm_l4; /* i */
    uint64_t gm_l5; /* word_start */
    uint64_t gm_l6; /* word_end */
    uint8_t gm_l7; /* open_string */
    uint8_t gm_l8; /* c */
    uint64_t gm_l9; /* top */
    uint8_t gm_l10; /* st */
    uint8_t gm_l11; /* value_start */
    uint8_t gm_l12; /* done */
    uint8_t gm_l13; /* st */
    gm_b16_8 gm_l14; /* word */
    GM_STACK_CHECK(&F, 1557u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l3);
        gm_tftb1odc739t5 t3 = gm_f_push_t83rg2m(t2, ((uint8_t)48ULL));
        gm_tftb1odc739t5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srcfpg6jsb3cfp6, 154, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        gm_l6 = ((uint64_t)0ULL);
    }
    {
        gm_l7 = ((uint8_t)0);
    }
    {
        for (;;) {
            uint64_t t7 = gm_l4;
            uint64_t t8 = gm_l1;
            uint8_t t9 = (t7 < t8);
            if (!t9) goto lb6;
            uint64_t t10 = gm_l4;
            uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t12 = (t10 < t11);
            if (!t12) goto lb6;
            {
                uint64_t t15 = gm_l4;
                uint64_t t16 = (uintptr_t)t15;
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_srcfpg6jsb3cfp6, 160, 14);
                uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t16 * 1));
                gm_l8 = t18;
            }
            {
                uint8_t t20 = 0;
                uint8_t t21 = 0;
                uint8_t t22 = 0;
                uint8_t t23 = gm_l8;
                uint8_t t24 = ((t23 == ((uint8_t)32ULL)));
                if (!t24) {
                    uint8_t t25 = gm_l8;
                    uint8_t t26 = ((t25 == ((uint8_t)9ULL)));
                    t22 = t26;
                } else t22 = 1;
                if (!t22) {
                    uint8_t t27 = gm_l8;
                    uint8_t t28 = ((t27 == ((uint8_t)10ULL)));
                    t21 = t28;
                } else t21 = 1;
                if (!t21) {
                    uint8_t t29 = gm_l8;
                    uint8_t t30 = ((t29 == ((uint8_t)13ULL)));
                    t20 = t30;
                } else t20 = 1;
                if (!t20) goto ie19;
                {
                    uint64_t t32 = gm_l4;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    if (o34) gm_trap("integer overflow in `+`", gm_srcfpg6jsb3cfp6, 162, 13);
                    uint64_t t35 = r33;
                    gm_l4 = t35;
                }
                {
                    goto lc6;
                }
                goto ix19;
ie19:;
ix19:;
            }
            {
                uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                uint64_t r37; bool o38 = __builtin_sub_overflow(t36, ((uint64_t)1ULL), &r37);
                if (o38) gm_trap("integer overflow in `-`", gm_srcfpg6jsb3cfp6, 165, 16);
                uint64_t t39 = r37;
                gm_l9 = t39;
            }
            {
                uint64_t t40 = gm_l9;
                uint64_t t41 = (uintptr_t)t40;
                uint64_t t42 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcfpg6jsb3cfp6, 166, 15);
                uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t41 * 1));
                gm_l10 = t43;
            }
            {
                uint8_t t44 = 0;
                uint8_t t45 = 0;
                uint8_t t46 = 0;
                uint8_t t47 = gm_l10;
                uint8_t t48 = ((t47 == ((uint8_t)48ULL)));
                if (!t48) {
                    uint8_t t49 = gm_l10;
                    uint8_t t50 = ((t49 == ((uint8_t)118ULL)));
                    t46 = t50;
                } else t46 = 1;
                if (!t46) {
                    uint8_t t51 = gm_l10;
                    uint8_t t52 = ((t51 == ((uint8_t)91ULL)));
                    t45 = t52;
                } else t45 = 1;
                if (!t45) {
                    uint8_t t53 = gm_l10;
                    uint8_t t54 = ((t53 == ((uint8_t)97ULL)));
                    t44 = t54;
                } else t44 = 1;
                gm_l11 = t44;
            }
            {
                gm_l12 = ((uint8_t)0);
            }
            {
                uint8_t t56 = 0;
                uint8_t t57 = 0;
                uint8_t t58 = gm_l11;
                if (!t58) {
                    uint8_t t59 = gm_l10;
                    uint8_t t60 = ((t59 == ((uint8_t)123ULL)));
                    t57 = t60;
                } else t57 = 1;
                if (!t57) {
                    uint8_t t61 = gm_l10;
                    uint8_t t62 = ((t61 == ((uint8_t)107ULL)));
                    t56 = t62;
                } else t56 = 1;
                if (!t56) goto ie55;
                uint8_t t63 = gm_l8;
                uint8_t t64 = ((t63 == ((uint8_t)34ULL)));
                if (!t64) goto ie55;
                {
                    uint64_t t66 = gm_l4;
                    uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
                    if (o68) gm_trap("integer overflow in `+`", gm_srcfpg6jsb3cfp6, 170, 13);
                    uint64_t t69 = r67;
                    gm_l4 = t69;
                }
                {
                    for (;;) {
                        uint64_t t71 = gm_l4;
                        uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t73 = (t71 < t72);
                        if (!t73) goto lb70;
                        uint64_t t74 = gm_l4;
                        uint64_t t75 = (uintptr_t)t74;
                        uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t75 >= (uintptr_t)(t76)) gm_trap_index(t75, t76, gm_srcfpg6jsb3cfp6, 171, 37);
                        uint8_t t77 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t75 * 1));
                        uint8_t t78 = (!(t77 == ((uint8_t)34ULL)));
                        if (!t78) goto lb70;
                        {
                            uint64_t t82 = gm_l4;
                            uint64_t t83 = (uintptr_t)t82;
                            uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t83 >= (uintptr_t)(t84)) gm_trap_index(t83, t84, gm_srcfpg6jsb3cfp6, 172, 20);
                            uint8_t t85 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t83 * 1));
                            uint8_t t86 = ((t85 == ((uint8_t)92ULL)));
                            if (!t86) goto ie81;
                            {
                                uint64_t t88 = gm_l4;
                                uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
                                if (o90) gm_trap("integer overflow in `+`", gm_srcfpg6jsb3cfp6, 172, 39);
                                uint64_t t91 = r89;
                                gm_l4 = t91;
                            }
                            goto ix81;
ie81:;
ix81:;
                        }
                        {
                            uint64_t t92 = gm_l4;
                            uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                            if (o94) gm_trap("integer overflow in `+`", gm_srcfpg6jsb3cfp6, 173, 17);
                            uint64_t t95 = r93;
                            gm_l4 = t95;
                        }
lc70:;
                    }
lb70:;
                }
                {
                    uint64_t t97 = gm_l4;
                    uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t99 = (t97 >= t98);
                    if (!t99) goto ie96;
                    {
                        gm_l7 = ((uint8_t)1);
                    }
                    goto ix96;
ie96:;
ix96:;
                }
                {
                    uint64_t t101 = gm_l4;
                    uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                    if (o103) gm_trap("integer overflow in `+`", gm_srcfpg6jsb3cfp6, 176, 13);
                    uint64_t t104 = r102;
                    gm_l4 = t104;
                }
                {
                    gm_l12 = ((uint8_t)1);
                }
                {
                    uint64_t t105 = gm_l9;
                    uint64_t t106 = (uintptr_t)t105;
                    uint64_t t107 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                    if (t106 >= (uintptr_t)(t107)) gm_trap_index(t106, t107, gm_srcfpg6jsb3cfp6, 178, 13);
                    uint8_t *t108 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t106 * 1);
                    uint8_t t109;
                    uint8_t t111 = gm_l11;
                    if (!t111) goto ie110;
                    uint8_t t113 = gm_l10;
                    uint8_t t114 = gm_f_after_value_1vi101i(t113);
                    uint8_t t115 = t114;
                    t109 = t115;
                    goto ix110;
ie110:;
                    uint8_t t117 = ((uint8_t)58ULL);
                    t109 = t117;
ix110:;
                    GM_ST(uint8_t, t108, t109);
                }
                goto ix55;
ie55:;
                uint8_t t119 = gm_l11;
                if (!t119) goto ie118;
                uint8_t t120 = 0;
                uint8_t t121 = gm_l8;
                uint8_t t122 = ((t121 == ((uint8_t)123ULL)));
                if (!t122) {
                    uint8_t t123 = gm_l8;
                    uint8_t t124 = ((t123 == ((uint8_t)91ULL)));
                    t120 = t124;
                } else t120 = 1;
                if (!t120) goto ie118;
                {
                    uint64_t t126 = gm_l9;
                    uint64_t t127 = (uintptr_t)t126;
                    uint64_t t128 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                    if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcfpg6jsb3cfp6, 180, 13);
                    uint8_t *t129 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t127 * 1);
                    uint8_t t130 = gm_l10;
                    uint8_t t131 = gm_f_after_value_1vi101i(t130);
                    GM_ST(uint8_t, t129, t131);
                }
                {
                    void * t132 = (void *)((uint8_t *)&F.l3);
                    uint8_t t133 = gm_l8;
                    gm_tftb1odc739t5 t134 = gm_f_push_t83rg2m(t132, t133);
                    gm_tftb1odc739t5 t135 = t134;
                    if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_srcfpg6jsb3cfp6, 181, 13);
                    gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
                }
                {
                    uint64_t t137 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                    uint64_t r138; bool o139 = __builtin_sub_overflow(t137, ((uint64_t)1ULL), &r138);
                    if (o139) gm_trap("integer overflow in `-`", gm_srcfpg6jsb3cfp6, 182, 25);
                    uint64_t t140 = r138;
                    uint64_t t141 = (uintptr_t)t140;
                    uint64_t t142 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                    if (t141 >= (uintptr_t)(t142)) gm_trap_index(t141, t142, gm_srcfpg6jsb3cfp6, 182, 13);
                    uint8_t *t143 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t141 * 1);
                    uint8_t t144;
                    uint8_t t146 = gm_l8;
                    uint8_t t147 = ((t146 == ((uint8_t)123ULL)));
                    if (!t147) goto ie145;
                    uint8_t t149 = ((uint8_t)123ULL);
                    t144 = t149;
                    goto ix145;
ie145:;
                    uint8_t t151 = ((uint8_t)91ULL);
                    t144 = t151;
ix145:;
                    GM_ST(uint8_t, t143, t144);
                }
                {
                    uint64_t t152 = gm_l4;
                    uint64_t r153; bool o154 = __builtin_add_overflow(t152, ((uint64_t)1ULL), &r153);
                    if (o154) gm_trap("integer overflow in `+`", gm_srcfpg6jsb3cfp6, 183, 13);
                    uint64_t t155 = r153;
                    gm_l4 = t155;
                }
                {
                    gm_l12 = ((uint8_t)1);
                }
                goto ix118;
ie118:;
                uint8_t t157 = 0;
                uint8_t t158 = 0;
                uint8_t t159 = 0;
                uint8_t t160 = gm_l10;
                uint8_t t161 = ((t160 == ((uint8_t)123ULL)));
                if (!t161) {
                    uint8_t t162 = gm_l10;
                    uint8_t t163 = ((t162 == ((uint8_t)44ULL)));
                    t159 = t163;
                } else t159 = 1;
                if (t159) {
                    uint8_t t164 = gm_l8;
                    uint8_t t165 = ((t164 == ((uint8_t)125ULL)));
                    t158 = t165;
                } else t158 = 0;
                if (!t158) {
                    uint8_t t166 = 0;
                    uint8_t t167 = 0;
                    uint8_t t168 = gm_l10;
                    uint8_t t169 = ((t168 == ((uint8_t)91ULL)));
                    if (!t169) {
                        uint8_t t170 = gm_l10;
                        uint8_t t171 = ((t170 == ((uint8_t)59ULL)));
                        t167 = t171;
                    } else t167 = 1;
                    if (t167) {
                        uint8_t t172 = gm_l8;
                        uint8_t t173 = ((t172 == ((uint8_t)93ULL)));
                        t166 = t173;
                    } else t166 = 0;
                    t157 = t166;
                } else t157 = 1;
                if (!t157) goto ie156;
                {
                    void * t175 = (void *)((uint8_t *)&F.l3);
                    uint64_t t176 = gm_l9;
                    gm_f_truncate_ov4pgoq(t175, t176);
                }
                {
                    uint64_t t177 = gm_l4;
                    uint64_t r178; bool o179 = __builtin_add_overflow(t177, ((uint64_t)1ULL), &r178);
                    if (o179) gm_trap("integer overflow in `+`", gm_srcfpg6jsb3cfp6, 187, 13);
                    uint64_t t180 = r178;
                    gm_l4 = t180;
                }
                {
                    gm_l12 = ((uint8_t)1);
                }
                goto ix156;
ie156:;
                uint8_t t182 = gm_l11;
                if (!t182) goto ie181;
                uint8_t t183 = 0;
                uint8_t t184 = 0;
                uint8_t t185 = 0;
                uint8_t t186 = gm_l8;
                uint8_t t187 = ((t186 == ((uint8_t)93ULL)));
                if (!t187) {
                    uint8_t t188 = gm_l8;
                    uint8_t t189 = ((t188 == ((uint8_t)125ULL)));
                    t185 = t189;
                } else t185 = 1;
                if (!t185) {
                    uint8_t t190 = gm_l8;
                    uint8_t t191 = ((t190 == ((uint8_t)44ULL)));
                    t184 = t191;
                } else t184 = 1;
                if (!t184) {
                    uint8_t t192 = gm_l8;
                    uint8_t t193 = ((t192 == ((uint8_t)58ULL)));
                    t183 = t193;
                } else t183 = 1;
                uint8_t t194 = (uint8_t)!t183;
                if (!t194) goto ie181;
                {
                    uint64_t t196 = gm_l4;
                    gm_l5 = t196;
                }
                {
                    for (;;) {
                        uint64_t t198 = gm_l4;
                        uint64_t t199 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t200 = (t198 < t199);
                        if (!t200) goto lb197;
                        uint8_t t201 = 0;
                        uint8_t t202 = 0;
                        uint8_t t203 = 0;
                        uint8_t t204 = 0;
                        uint8_t t205 = 0;
                        uint8_t t206 = 0;
                        uint64_t t207 = gm_l4;
                        uint64_t t208 = (uintptr_t)t207;
                        uint64_t t209 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t208 >= (uintptr_t)(t209)) gm_trap_index(t208, t209, gm_srcfpg6jsb3cfp6, 192, 39);
                        uint8_t t210 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t208 * 1));
                        uint8_t t211 = (t210 >= ((uint8_t)97ULL));
                        if (t211) {
                            uint64_t t212 = gm_l4;
                            uint64_t t213 = (uintptr_t)t212;
                            uint64_t t214 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t213 >= (uintptr_t)(t214)) gm_trap_index(t213, t214, gm_srcfpg6jsb3cfp6, 192, 59);
                            uint8_t t215 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t213 * 1));
                            uint8_t t216 = (t215 <= ((uint8_t)122ULL));
                            t206 = t216;
                        } else t206 = 0;
                        if (!t206) {
                            uint8_t t217 = 0;
                            uint64_t t218 = gm_l4;
                            uint64_t t219 = (uintptr_t)t218;
                            uint64_t t220 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t219 >= (uintptr_t)(t220)) gm_trap_index(t219, t220, gm_srcfpg6jsb3cfp6, 192, 80);
                            uint8_t t221 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t219 * 1));
                            uint8_t t222 = (t221 >= ((uint8_t)48ULL));
                            if (t222) {
                                uint64_t t223 = gm_l4;
                                uint64_t t224 = (uintptr_t)t223;
                                uint64_t t225 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                if (t224 >= (uintptr_t)(t225)) gm_trap_index(t224, t225, gm_srcfpg6jsb3cfp6, 192, 100);
                                uint8_t t226 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t224 * 1));
                                uint8_t t227 = (t226 <= ((uint8_t)57ULL));
                                t217 = t227;
                            } else t217 = 0;
                            t205 = t217;
                        } else t205 = 1;
                        if (!t205) {
                            uint64_t t228 = gm_l4;
                            uint64_t t229 = (uintptr_t)t228;
                            uint64_t t230 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t229 >= (uintptr_t)(t230)) gm_trap_index(t229, t230, gm_srcfpg6jsb3cfp6, 192, 120);
                            uint8_t t231 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t229 * 1));
                            uint8_t t232 = ((t231 == ((uint8_t)45ULL)));
                            t204 = t232;
                        } else t204 = 1;
                        if (!t204) {
                            uint64_t t233 = gm_l4;
                            uint64_t t234 = (uintptr_t)t233;
                            uint64_t t235 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t234 >= (uintptr_t)(t235)) gm_trap_index(t234, t235, gm_srcfpg6jsb3cfp6, 192, 139);
                            uint8_t t236 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t234 * 1));
                            uint8_t t237 = ((t236 == ((uint8_t)43ULL)));
                            t203 = t237;
                        } else t203 = 1;
                        if (!t203) {
                            uint64_t t238 = gm_l4;
                            uint64_t t239 = (uintptr_t)t238;
                            uint64_t t240 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t239 >= (uintptr_t)(t240)) gm_trap_index(t239, t240, gm_srcfpg6jsb3cfp6, 192, 158);
                            uint8_t t241 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t239 * 1));
                            uint8_t t242 = ((t241 == ((uint8_t)46ULL)));
                            t202 = t242;
                        } else t202 = 1;
                        if (!t202) {
                            uint64_t t243 = gm_l4;
                            uint64_t t244 = (uintptr_t)t243;
                            uint64_t t245 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t244 >= (uintptr_t)(t245)) gm_trap_index(t244, t245, gm_srcfpg6jsb3cfp6, 192, 177);
                            uint8_t t246 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t244 * 1));
                            uint8_t t247 = ((t246 == ((uint8_t)69ULL)));
                            t201 = t247;
                        } else t201 = 1;
                        if (!t201) goto lb197;
                        {
                            uint64_t t250 = gm_l4;
                            uint64_t r251; bool o252 = __builtin_add_overflow(t250, ((uint64_t)1ULL), &r251);
                            if (o252) gm_trap("integer overflow in `+`", gm_srcfpg6jsb3cfp6, 193, 17);
                            uint64_t t253 = r251;
                            gm_l4 = t253;
                        }
lc197:;
                    }
lb197:;
                }
                {
                    uint64_t t254 = gm_l4;
                    gm_l6 = t254;
                }
                {
                    uint64_t t255 = gm_l9;
                    uint64_t t256 = (uintptr_t)t255;
                    uint64_t t257 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                    if (t256 >= (uintptr_t)(t257)) gm_trap_index(t256, t257, gm_srcfpg6jsb3cfp6, 196, 13);
                    uint8_t *t258 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t256 * 1);
                    uint8_t t259 = gm_l10;
                    uint8_t t260 = gm_f_after_value_1vi101i(t259);
                    GM_ST(uint8_t, t258, t260);
                }
                {
                    gm_l12 = ((uint8_t)1);
                }
                goto ix181;
ie181:;
                uint8_t t262 = gm_l10;
                uint8_t t263 = ((t262 == ((uint8_t)58ULL)));
                if (!t263) goto ie261;
                uint8_t t264 = gm_l8;
                uint8_t t265 = ((t264 == ((uint8_t)58ULL)));
                if (!t265) goto ie261;
                {
                    uint64_t t267 = gm_l9;
                    uint64_t t268 = (uintptr_t)t267;
                    uint64_t t269 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                    if (t268 >= (uintptr_t)(t269)) gm_trap_index(t268, t269, gm_srcfpg6jsb3cfp6, 199, 13);
                    GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t268 * 1), ((uint8_t)118ULL));
                }
                {
                    uint64_t t270 = gm_l4;
                    uint64_t r271; bool o272 = __builtin_add_overflow(t270, ((uint64_t)1ULL), &r271);
                    if (o272) gm_trap("integer overflow in `+`", gm_srcfpg6jsb3cfp6, 200, 13);
                    uint64_t t273 = r271;
                    gm_l4 = t273;
                }
                {
                    gm_l12 = ((uint8_t)1);
                }
                goto ix261;
ie261:;
                uint8_t t275 = gm_l10;
                uint8_t t276 = ((t275 == ((uint8_t)44ULL)));
                if (!t276) goto ie274;
                uint8_t t277 = gm_l8;
                uint8_t t278 = ((t277 == ((uint8_t)44ULL)));
                if (!t278) goto ie274;
                {
                    uint64_t t280 = gm_l9;
                    uint64_t t281 = (uintptr_t)t280;
                    uint64_t t282 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                    if (t281 >= (uintptr_t)(t282)) gm_trap_index(t281, t282, gm_srcfpg6jsb3cfp6, 203, 13);
                    GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t281 * 1), ((uint8_t)107ULL));
                }
                {
                    uint64_t t283 = gm_l4;
                    uint64_t r284; bool o285 = __builtin_add_overflow(t283, ((uint64_t)1ULL), &r284);
                    if (o285) gm_trap("integer overflow in `+`", gm_srcfpg6jsb3cfp6, 204, 13);
                    uint64_t t286 = r284;
                    gm_l4 = t286;
                }
                {
                    gm_l12 = ((uint8_t)1);
                }
                goto ix274;
ie274:;
                uint8_t t288 = gm_l10;
                uint8_t t289 = ((t288 == ((uint8_t)59ULL)));
                if (!t289) goto ie287;
                uint8_t t290 = gm_l8;
                uint8_t t291 = ((t290 == ((uint8_t)44ULL)));
                if (!t291) goto ie287;
                {
                    uint64_t t293 = gm_l9;
                    uint64_t t294 = (uintptr_t)t293;
                    uint64_t t295 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                    if (t294 >= (uintptr_t)(t295)) gm_trap_index(t294, t295, gm_srcfpg6jsb3cfp6, 207, 13);
                    GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t294 * 1), ((uint8_t)97ULL));
                }
                {
                    uint64_t t296 = gm_l4;
                    uint64_t r297; bool o298 = __builtin_add_overflow(t296, ((uint64_t)1ULL), &r297);
                    if (o298) gm_trap("integer overflow in `+`", gm_srcfpg6jsb3cfp6, 208, 13);
                    uint64_t t299 = r297;
                    gm_l4 = t299;
                }
                {
                    gm_l12 = ((uint8_t)1);
                }
                goto ix287;
ie287:;
ix287:;
ix274:;
ix261:;
ix181:;
ix156:;
ix118:;
ix55:;
            }
            {
                uint8_t t301 = gm_l12;
                uint8_t t302 = (uint8_t)!t301;
                if (!t302) goto ie300;
                {
                    goto lb6;
                }
                goto ix300;
ie300:;
ix300:;
            }
lc6:;
        }
lb6:;
    }
    {
        uint64_t t304 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
        uint64_t r305; bool o306 = __builtin_sub_overflow(t304, ((uint64_t)1ULL), &r305);
        if (o306) gm_trap("integer overflow in `-`", gm_srcfpg6jsb3cfp6, 213, 23);
        uint64_t t307 = r305;
        uint64_t t308 = (uintptr_t)t307;
        uint64_t t309 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
        if (t308 >= (uintptr_t)(t309)) gm_trap_index(t308, t309, gm_srcfpg6jsb3cfp6, 213, 11);
        uint8_t t310 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t308 * 1));
        gm_l13 = t310;
    }
    {
        uint64_t t312 = gm_l6;
        uint64_t t313 = gm_l1;
        uint8_t t314 = ((t312 == t313));
        if (!t314) goto ie311;
        uint64_t t315 = gm_l6;
        uint64_t t316 = gm_l5;
        uint8_t t317 = (t315 > t316);
        if (!t317) goto ie311;
        uint64_t t318 = gm_l5;
        uint64_t t319 = (uintptr_t)t318;
        uint64_t t320 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t319 >= (uintptr_t)(t320)) gm_trap_index(t319, t320, gm_srcfpg6jsb3cfp6, 215, 57);
        uint8_t t321 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t319 * 1));
        uint8_t t322 = (t321 >= ((uint8_t)97ULL));
        if (!t322) goto ie311;
        uint64_t t323 = gm_l5;
        uint64_t t324 = (uintptr_t)t323;
        uint64_t t325 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t324 >= (uintptr_t)(t325)) gm_trap_index(t324, t325, gm_srcfpg6jsb3cfp6, 215, 86);
        uint8_t t326 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t324 * 1));
        uint8_t t327 = (t326 <= ((uint8_t)122ULL));
        if (!t327) goto ie311;
        {
            uint64_t t329 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t330 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t331 = gm_l5;
            uint64_t t332 = (uintptr_t)t331;
            uint64_t t333 = gm_l6;
            uint64_t t334 = (uintptr_t)t333;
            if (t332 > t334 || t334 > (uintptr_t)t329) gm_trap_slice(t332, t334, t329, gm_srcfpg6jsb3cfp6, 216, 17);
            gm_b16_8 t335;
            GM_ST(void *, (uint8_t *)&t335, (void *)(((uint8_t *)t330 + t332 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t335 + 8, (uintptr_t)(t334 - t332));
            gm_l14 = t335;
        }
        {
            uint8_t t337 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l14)), gm_sehko5bb96s6p, 4) == 0));
            if (!t337) goto ie336;
            uint8_t t338 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l14)), gm_sogehh8t1m7tn, 5) == 0));
            if (!t338) goto ie336;
            uint8_t t339 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l14)), gm_squedv51820an, 4) == 0));
            if (!t339) goto ie336;
            {
                gm_b16_8 t341;
                GM_ST(void *, (uint8_t *)&t341, (void *)(gm_spnjhvas2rum4));
                GM_ST(uintptr_t, (uint8_t *)&t341 + 8, (uintptr_t)(14));
                uint8_t t342 = 0;
                F.ret = t341;
                goto gm_fret;
            }
            goto ix336;
ie336:;
ix336:;
        }
        goto ix311;
ie311:;
ix311:;
    }
    {
        uint8_t t344 = gm_l2;
        if (!t344) goto ie343;
        {
            uint8_t t347 = gm_l7;
            if (!t347) goto ie346;
            {
                gm_b16_8 t349;
                GM_ST(void *, (uint8_t *)&t349, (void *)(gm_s06kk1tae680i));
                GM_ST(uintptr_t, (uint8_t *)&t349 + 8, (uintptr_t)(26));
                uint8_t t350 = 0;
                F.ret = t349;
                goto gm_fret;
            }
            goto ix346;
ie346:;
ix346:;
        }
        {
            uint8_t t351 = gm_l13;
            uint8_t t352 = t351;
            gm_b16_8 t353;
            uint8_t t358 = GM_LD(uint8_t, ((uint8_t *)&t352));
            if (!(t358 == ((uint8_t)123ULL))) goto pa357;
            goto po356;
pa357:;
            uint8_t t360 = GM_LD(uint8_t, ((uint8_t *)&t352));
            if (!(t360 == ((uint8_t)107ULL))) goto pa359;
            goto po356;
pa359:;
            uint8_t t362 = GM_LD(uint8_t, ((uint8_t *)&t352));
            if (!(t362 == ((uint8_t)58ULL))) goto pa361;
            goto po356;
pa361:;
            uint8_t t364 = GM_LD(uint8_t, ((uint8_t *)&t352));
            if (!(t364 == ((uint8_t)44ULL))) goto pa363;
            goto po356;
pa363:;
            goto mn355;
po356:;
            gm_b16_8 t365;
            GM_ST(void *, (uint8_t *)&t365, (void *)(gm_smhebipq24jdo));
            GM_ST(uintptr_t, (uint8_t *)&t365 + 8, (uintptr_t)(27));
            t353 = t365;
            goto me354;
mn355:;
            uint8_t t367 = GM_LD(uint8_t, ((uint8_t *)&t352));
            if (!(t367 == ((uint8_t)59ULL))) goto mn366;
            gm_b16_8 t368;
            GM_ST(void *, (uint8_t *)&t368, (void *)(gm_s0odi4lbspe7f));
            GM_ST(uintptr_t, (uint8_t *)&t368 + 8, (uintptr_t)(24));
            t353 = t368;
            goto me354;
mn366:;
            gm_b16_8 t370;
            GM_ST(void *, (uint8_t *)&t370, (void *)(gm_su0qghe21d3de));
            GM_ST(uintptr_t, (uint8_t *)&t370 + 8, (uintptr_t)(25));
            t353 = t370;
            goto me354;
mn369:;
            gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 221, 16);
me354:;
            uint8_t t371 = 0;
            F.ret = t353;
            goto gm_fret;
        }
        goto ix343;
ie343:;
ix343:;
    }
    {
        uint64_t t373 = gm_l1;
        uint64_t t374 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t375 = (t373 < t374);
        if (!t375) goto ie372;
        uint8_t t376 = 0;
        uint8_t t377 = 0;
        uint8_t t378 = gm_l13;
        uint8_t t379 = ((t378 == ((uint8_t)97ULL)));
        if (t379) {
            uint64_t t380 = gm_l1;
            uint64_t t381 = (uintptr_t)t380;
            uint64_t t382 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t381 >= (uintptr_t)(t382)) gm_trap_index(t381, t382, gm_srcfpg6jsb3cfp6, 227, 47);
            uint8_t t383 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t381 * 1));
            uint8_t t384 = ((t383 == ((uint8_t)93ULL)));
            t377 = t384;
        } else t377 = 0;
        if (!t377) {
            uint8_t t385 = 0;
            uint8_t t386 = gm_l13;
            uint8_t t387 = ((t386 == ((uint8_t)107ULL)));
            if (t387) {
                uint64_t t388 = gm_l1;
                uint64_t t389 = (uintptr_t)t388;
                uint64_t t390 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t389 >= (uintptr_t)(t390)) gm_trap_index(t389, t390, gm_srcfpg6jsb3cfp6, 227, 87);
                uint8_t t391 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t389 * 1));
                uint8_t t392 = ((t391 == ((uint8_t)125ULL)));
                t385 = t392;
            } else t385 = 0;
            t376 = t385;
        } else t376 = 1;
        if (!t376) goto ie372;
        {
            gm_b16_8 t394;
            GM_ST(void *, (uint8_t *)&t394, (void *)(gm_sl775fgtt04iu));
            GM_ST(uintptr_t, (uint8_t *)&t394 + 8, (uintptr_t)(14));
            uint8_t t395 = 0;
            F.ret = t394;
            goto gm_fret;
        }
        goto ix372;
ie372:;
ix372:;
    }
    {
        uint8_t t396 = gm_l13;
        uint8_t t397 = t396;
        gm_b16_8 t398;
        uint8_t t403 = GM_LD(uint8_t, ((uint8_t *)&t397));
        if (!(t403 == ((uint8_t)123ULL))) goto pa402;
        goto po401;
pa402:;
        uint8_t t405 = GM_LD(uint8_t, ((uint8_t *)&t397));
        if (!(t405 == ((uint8_t)107ULL))) goto pa404;
        goto po401;
pa404:;
        goto mn400;
po401:;
        gm_b16_8 t406;
        GM_ST(void *, (uint8_t *)&t406, (void *)(gm_sfc9avb7sceba));
        GM_ST(uintptr_t, (uint8_t *)&t406 + 8, (uintptr_t)(20));
        t398 = t406;
        goto me399;
mn400:;
        uint8_t t408 = GM_LD(uint8_t, ((uint8_t *)&t397));
        if (!(t408 == ((uint8_t)58ULL))) goto mn407;
        gm_b16_8 t409;
        GM_ST(void *, (uint8_t *)&t409, (void *)(gm_st9dp4tkd7f7l));
        GM_ST(uintptr_t, (uint8_t *)&t409 + 8, (uintptr_t)(12));
        t398 = t409;
        goto me399;
mn407:;
        uint8_t t411 = GM_LD(uint8_t, ((uint8_t *)&t397));
        if (!(t411 == ((uint8_t)44ULL))) goto mn410;
        gm_b16_8 t412;
        GM_ST(void *, (uint8_t *)&t412, (void *)(gm_srssp1uhlkrll));
        GM_ST(uintptr_t, (uint8_t *)&t412 + 8, (uintptr_t)(19));
        t398 = t412;
        goto me399;
mn410:;
        uint8_t t414 = GM_LD(uint8_t, ((uint8_t *)&t397));
        if (!(t414 == ((uint8_t)59ULL))) goto mn413;
        gm_b16_8 t415;
        GM_ST(void *, (uint8_t *)&t415, (void *)(gm_s62maodcvu607));
        GM_ST(uintptr_t, (uint8_t *)&t415 + 8, (uintptr_t)(19));
        t398 = t415;
        goto me399;
mn413:;
        gm_b16_8 t417;
        GM_ST(void *, (uint8_t *)&t417, (void *)(gm_s7s8viq00e4g2));
        GM_ST(uintptr_t, (uint8_t *)&t417 + 8, (uintptr_t)(14));
        t398 = t417;
        goto me399;
mn416:;
        gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 228, 12);
me399:;
        uint8_t t418 = 0;
        F.ret = t398;
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
inline __attribute__((aligned(4))) void gm_f_target_j5nr6ju(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_target_j5nr6ju F;
    gm_t8vuock3jnv59 gm_l0; /* params */
    gm_b16_8 gm_l1; /* uri */
    gm_b16_8 gm_l2; /* raw */
    GM_STACK_CHECK(&F, 707u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4j3ecogbjkvn));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbo52miqad506));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_t8vuock3jnv59 t4;
        gm_f_path2_enc7vaj((void *)&t4, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a0)), t2, t3);
        gm_th2lqbo77pvsc t5;
        gm_f_as_str_9rhg9v0((void *)&t5, &t4);
        gm_th2lqbo77pvsc t6 = t5;
        gm_b16_8 t7;
        if (((((uint8_t *)&t6))[16] != 0)) {
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sc6qhprasnd4f));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(24));
            gm_thgmovcqlsivv t10;
            gm_f_fail_8tj0r64((void *)&t10, ((int64_t)-32602LL), t9);
            gm_tm2ah4hc06bl5 t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_thgmovcqlsivv, ((uint8_t *)&t11 + 8), t10);
            gm_tm2ah4hc06bl5 t12 = t11;
            uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
            F.ret = t11;
            goto gm_fret;
        }
        gm_l1 = t7;
    }
    {
        gm_b16_8 t14 = gm_l1;
        gm_th2lqbo77pvsc t15;
        gm_f_uri_to_path_kbof22r((void *)&t15, (void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t16 = t15;
        gm_b16_8 t17;
        if (((((uint8_t *)&t16))[16] != 0)) {
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 0));
            t17 = t18;
        } else {
            gm_b16_8 t19 = gm_l1;
            gm_tnpifv20irnph t20;
            GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t19);
            gm_tabb710d9mq2e t21;
            gm_f_format_shkdld2((void *)&t21, (void *)NULL, cx, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_srcfpg6jsb3cfp6, 693, 63);
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
            gm_b16_8 t24 = t23;
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
            gm_thgmovcqlsivv t26;
            gm_f_fail_8tj0r64((void *)&t26, ((int64_t)-32602LL), t25);
            gm_tm2ah4hc06bl5 t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
            ((uint8_t *)&t27)[0] = 1;
            GM_ST(gm_thgmovcqlsivv, ((uint8_t *)&t27 + 8), t26);
            gm_tm2ah4hc06bl5 t28 = t27;
            uint8_t t29 = ((((uint8_t *)&t28))[0] != 0);
            F.ret = t27;
            goto gm_fret;
        }
        gm_l2 = t17;
    }
    {
        gm_b16_8 t30 = gm_l2;
        gm_b16_8 t31 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm2ah4hc06bl5 t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t32 + 24, 0);
        ((uint8_t *)&t32)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 8), t31);
        gm_tm2ah4hc06bl5 t33 = t32;
        uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
        F.ret = t32;
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
inline __attribute__((aligned(4))) void gm_f_uri_map_mhud6ak(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_uri_map_mhud6ak F;
    void * gm_l0; /* srv */
    void * gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_hash_map_p3t5tj8((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 0)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            void * t4 = (void *)(((uint8_t *)t3) + 8);
            gm_l2 = t4;
        }
        gm_hval t5 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn6 = {.eff = 1408113935, .kind = GM_LIT, .clauses = gm_hl8iqbv5s08g, .env = t5.env, .hctx = t5.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        void * t9 = gm_l2;
        gm_b0_1 t10 = gm_f_entries_qfi2q5v_son0uk9((void *)NULL, (&wn6), NULL, t9);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        F.w0 = t10;
        goto gm_ls11;
gm_ll8:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
        goto gm_lad;
gm_ls11:;
we7:;
        gm_b0_1 t12 = F.w0;
        gm_b0_1 t13 = t12;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_watched_files_changed_qvnrc63(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_watched_files_changed_qvnrc63 F;
    gm_t8vuock3jnv59 gm_l1; /* params */
    uint8_t gm_l2; /* everything */
    gm_b16_8 gm_l3; /* changes */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t8vuock3jnv59 gm_l6; /* change */
    gm_b16_8 gm_l7; /* uri */
    gm_b16_8 gm_l8; /* raw */
    gm_b16_8 gm_l9; /* path */
    void * gm_l10; /* <tmp> */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* d */
    GM_STACK_CHECK(&F, 1179u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_l2 = ((uint8_t)0);
    }
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ssvcneuafnfds));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(7));
        gm_t8vuock3jnv59 t3;
        gm_f_field_7904fsq((void *)&t3, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a1)), t2);
        gm_th2lqbo77pvsc t4;
        gm_f_as_array_nfvj8pk((void *)&t4, &t3);
        gm_th2lqbo77pvsc t5 = t4;
        gm_b16_8 t6;
        if (((((uint8_t *)&t5))[16] != 0)) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_b16_8 t8 = gm_f_empty_slice_dheabhi_3();
            t6 = t8;
        }
        gm_l3 = t6;
    }
    {
        {
            gm_b16_8 t10 = gm_l3;
            gm_l4 = t10;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t12 = gm_l5;
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    uint64_t t17 = gm_l5;
                    uint64_t t18 = (uintptr_t)t17;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t8vuock3jnv59 t22 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t18 * 24));
                    gm_l6 = t22;
                    {
                        gm_b16_8 t24;
                        GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sbo52miqad506));
                        GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(3));
                        gm_t8vuock3jnv59 t25;
                        gm_f_field_7904fsq((void *)&t25, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l6)), t24);
                        gm_th2lqbo77pvsc t26;
                        gm_f_as_str_9rhg9v0((void *)&t26, &t25);
                        gm_th2lqbo77pvsc t27 = t26;
                        gm_b16_8 t28;
                        if (((((uint8_t *)&t27))[16] != 0)) {
                            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 0));
                            t28 = t29;
                        } else {
                            goto lc11;
                        }
                        gm_l7 = t28;
                    }
                    {
                        gm_b16_8 t30 = gm_l7;
                        gm_th2lqbo77pvsc t31;
                        gm_f_uri_to_path_kbof22r((void *)&t31, (void *)NULL, cx, NULL, t30);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        gm_th2lqbo77pvsc t32 = t31;
                        gm_b16_8 t33;
                        if (((((uint8_t *)&t32))[16] != 0)) {
                            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 0));
                            t33 = t34;
                        } else {
                            goto lc11;
                        }
                        gm_l8 = t33;
                    }
                    {
                        gm_b16_8 t35 = gm_l8;
                        gm_b16_8 t36 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t35);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        gm_l9 = t36;
                    }
                    {
                        gm_b16_8 t38 = gm_l9;
                        uint8_t t39 = gm_f_is_gm_file_0ofcrmu((void *)NULL, cx, NULL, t38);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        if (!t39) goto ie37;
                        {
                            gm_b16_8 t42;
                            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s2737i1iq57nf));
                            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(4));
                            gm_t8vuock3jnv59 t43;
                            gm_f_field_7904fsq((void *)&t43, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l6)), t42);
                            gm_t913bb735pvt5 t44 = gm_f_as_i64_iojmg3q(&t43);
                            gm_t913bb735pvt5 t45 = t44;
                            int64_t t46;
                            if (((((uint8_t *)&t45))[8] != 0)) {
                                int64_t t47 = GM_LD(int64_t, (((uint8_t *)&t45) + 0));
                                t46 = t47;
                            } else {
                                t46 = ((int64_t)0LL);
                            }
                            uint8_t t48 = ((t46 == ((int64_t)2LL)));
                            if (!t48) goto ie41;
                            {
                                void * t50 = F.l0;
                                gm_b16_8 t51 = gm_l9;
                                gm_b0_1 t52 = gm_f_invalidate_uhhh539((void *)NULL, cx, NULL, t50, t51);
                                if (gm_unw.kind) {
                                    goto gm_ll15;
                                }
                            }
                            {
                                goto lc11;
                            }
                            goto ix41;
ie41:;
ix41:;
                        }
                        goto ix37;
ie37:;
                        gm_b16_8 t54 = gm_l9;
                        gm_th2lqbo77pvsc t55;
                        gm_f_file_name_em7v8lv((void *)&t55, (void *)NULL, cx, NULL, t54);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        gm_th2lqbo77pvsc t56 = t55;
                        gm_b16_8 t57;
                        if (((((uint8_t *)&t56))[16] != 0)) {
                            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 0));
                            t57 = t58;
                        } else {
                            gm_b16_8 t59;
                            GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(0));
                            t57 = t59;
                        }
                        gm_b16_8 t60 = t57;
                        uint8_t t61 = (!((GM_LD(uintptr_t, ((uint8_t *)&t60) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&t60)), gm_se24c7hpp401n, 10) == 0));
                        if (!t61) goto ie53;
                        {
                            goto lc11;
                        }
                        goto ix53;
ie53:;
ix53:;
ix37:;
                    }
                    {
                        gm_l2 = ((uint8_t)1);
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 504, 5);
me20:;
                }
lc11:;
                {
                    uint64_t t64 = gm_l5;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                    uint64_t t67 = r65;
                    gm_l5 = t67;
                }
            }
gm_ll15:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
            }
            goto gm_lad;
lb11:;
        }
    }
    {
        uint8_t t69 = gm_l2;
        if (!t69) goto ie68;
        {
            {
                void * t72 = F.l0;
                void * t73 = (void *)(((uint8_t *)t72) + 64);
                gm_l10 = t73;
            }
            gm_hval t74 = ((gm_hval){(void *)&F.h, cx});
            gm_node wn75 = {.eff = 917785847, .kind = GM_LIT, .clauses = gm_hltn0orqptuk, .env = t74.env, .hctx = t74.cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
            void * t78 = gm_l10;
            gm_b0_1 t79 = gm_f_entries_5dln36o_s7sgbc0((void *)NULL, (&wn75), NULL, t78);
            if (gm_unw.kind) {
                goto gm_ll77;
            }
            F.w1 = t79;
            goto gm_ls80;
gm_ll77:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we76; }
            goto gm_lad;
gm_ls80:;
we76:;
            gm_b0_1 t81 = F.w1;
            gm_b0_1 t82 = t81;
        }
        {
            {
                void * t84 = F.l0;
                gm_trqfvmjtt4sij t85;
                gm_f_open_dirs_9ud02ot((void *)&t85, (void *)NULL, cx, NULL, t84);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_trqfvmjtt4sij t86 = t85;
                gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 0));
                gm_l11 = t87;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t89 = gm_l12;
                    uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t91 = (t89 < t90);
                    if (!t91) goto lb88;
                    {
                        uint64_t t94 = gm_l12;
                        uint64_t t95 = (uintptr_t)t94;
                        uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t95 * 16));
                        gm_l13 = t99;
                        {
                            void * t101 = F.l0;
                            void * t102 = (void *)(((uint8_t *)t101) + 120);
                            gm_b16_8 t103 = gm_l13;
                            gm_tftb1odc739t5 t104 = gm_f_put_5c6qgui(t102, t103, ((uint8_t)1));
                            gm_tftb1odc739t5 t105 = t104;
                            if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 1, gm_srcfpg6jsb3cfp6, 521, 42);
                            gm_b0_1 t106 = GM_LD(gm_b0_1, (((uint8_t *)&t105) + 1));
                        }
                        goto me97;
mn98:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 521, 9);
me97:;
                    }
lc88:;
                    {
                        uint64_t t108 = gm_l12;
                        uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                        uint64_t t111 = r109;
                        gm_l12 = t111;
                    }
                }
lb88:;
            }
        }
        goto ix68;
ie68:;
ix68:;
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
