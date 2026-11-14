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
typedef struct gm_csfq64m1m1rt6m { int64_t f0; } gm_csfq64m1m1rt6m;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t a[4]; } gm_tpvg3mjn3dp06;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_csfq64m1m1rt6m) == 8 && _Alignof(gm_csfq64m1m1rt6m) == 8 && offsetof(gm_csfq64m1m1rt6m, f0) == 0, "layout of `Dir`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tm98ap4112hee f0; gm_b16_8 f1; } gm_t3olsle24rveu;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_tbl8il92l97ut f0; uint8_t f1; uint8_t pe[7]; } gm_t5lf9i3n1r3co;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { uint64_t f0; gm_tf5srh4mp4b3i f1; } gm_tk4qs2jduhfn8;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tnsjse4su5jue;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_b16_8 f2; uint32_t f3; gm_tl874t2atnlh3 f4; } gm_tonn1ba3fjh5g;
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { gm_tm98ap4112hee f0; uint8_t f1; uint8_t pe[7]; } gm_tppo2hmut88dq;
_Static_assert(sizeof(gm_tpvg3mjn3dp06) == 4 && _Alignof(gm_tpvg3mjn3dp06) == 1 && offsetof(gm_tpvg3mjn3dp06, a) == 0, "layout of `[4]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; void * f2; gm_b16_8 f3; } gm_tsbt7g382sgof;
typedef struct { gm_b16_8 f0; uint16_t f1; uint8_t pe[6]; } gm_tvaas22dl64e0;
typedef struct { gm_t76123gfet73g f0; uint8_t f1; uint8_t pe[7]; } gm_t07lf02ct4q61;
_Static_assert(sizeof(gm_t3olsle24rveu) == 24 && _Alignof(gm_t3olsle24rveu) == 8 && offsetof(gm_t3olsle24rveu, f0) == 0 && offsetof(gm_t3olsle24rveu, f1) == 8, "layout of `(file: File, buf: []u8)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t5lf9i3n1r3co) == 24 && _Alignof(gm_t5lf9i3n1r3co) == 8 && offsetof(gm_t5lf9i3n1r3co, f0) == 0 && offsetof(gm_t5lf9i3n1r3co, f1) == 16, "layout of `?SpecCall`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tk4qs2jduhfn8 v1; uint64_t v2; void * v3; } f1; } gm_tdkjirf1g83ud;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_thf5kr0is9u9i f0; gm_tppo2hmut88dq f1; } gm_tfvg1o4f8mmhi;
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tsbt7g382sgof v0; gm_tonn1ba3fjh5g v1; gm_t79rbdv4rafe5 v2; } f1; } gm_th5tat709fgha;
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tk4qs2jduhfn8) == 24 && _Alignof(gm_tk4qs2jduhfn8) == 8 && offsetof(gm_tk4qs2jduhfn8, f0) == 0 && offsetof(gm_tk4qs2jduhfn8, f1) == 8, "layout of `(seen: u64, deadline: ?u64)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t3olsle24rveu v1; gm_tvaas22dl64e0 v2; gm_tm98ap4112hee v3; gm_t3olsle24rveu v4; gm_t3olsle24rveu v5; } f1; } gm_tmvdbo28o1i6h;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnsjse4su5jue) == 48 && _Alignof(gm_tnsjse4su5jue) == 8 && offsetof(gm_tnsjse4su5jue, f0) == 0 && offsetof(gm_tnsjse4su5jue, f1) == 4 && offsetof(gm_tnsjse4su5jue, f2) == 8 && offsetof(gm_tnsjse4su5jue, f3) == 16 && offsetof(gm_tnsjse4su5jue, f4) == 32, "layout of `(fiber: u32, node: u32, op: u32, args: []const []u8, span: Span)`");
typedef struct { gm_trqfvmjtt4sij f0; uint32_t f1; uint32_t f2; uint64_t f3; void * f4; gm_tqs5p4n2tq19v f5; uint8_t pe[6]; } gm_tobnqifq2n0g5;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; } gm_tolm6p7jp1hb3;
_Static_assert(sizeof(gm_tonn1ba3fjh5g) == 56 && _Alignof(gm_tonn1ba3fjh5g) == 8 && offsetof(gm_tonn1ba3fjh5g, f0) == 0 && offsetof(gm_tonn1ba3fjh5g, f1) == 16 && offsetof(gm_tonn1ba3fjh5g, f2) == 24 && offsetof(gm_tonn1ba3fjh5g, f3) == 40 && offsetof(gm_tonn1ba3fjh5g, f4) == 44, "layout of `(f: []u8, fty: Ty, arg: []u8, cx: u32, span: Span)`");
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_tppo2hmut88dq) == 16 && _Alignof(gm_tppo2hmut88dq) == 8 && offsetof(gm_tppo2hmut88dq, f0) == 0 && offsetof(gm_tppo2hmut88dq, f1) == 8, "layout of `?Child`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsbt7g382sgof) == 40 && _Alignof(gm_tsbt7g382sgof) == 8 && offsetof(gm_tsbt7g382sgof, f0) == 0 && offsetof(gm_tsbt7g382sgof, f1) == 8 && offsetof(gm_tsbt7g382sgof, f2) == 16 && offsetof(gm_tsbt7g382sgof, f3) == 24, "layout of `(fr: ^Frame, cx: u32, body: ^Node, dst: []u8)`");
_Static_assert(sizeof(gm_tvaas22dl64e0) == 24 && _Alignof(gm_tvaas22dl64e0) == 8 && offsetof(gm_tvaas22dl64e0, f0) == 0 && offsetof(gm_tvaas22dl64e0, f1) == 16, "layout of `(string, u16)`");
_Static_assert(sizeof(gm_t07lf02ct4q61) == 32 && _Alignof(gm_t07lf02ct4q61) == 8 && offsetof(gm_t07lf02ct4q61, f0) == 0 && offsetof(gm_t07lf02ct4q61, f1) == 24, "layout of `?Flow`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_tmvdbo28o1i6h f0; gm_tfvg1o4f8mmhi f1; uint32_t f2; uint8_t pe[4]; } gm_t683gnj9k3p7d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_tfvg1o4f8mmhi v1; } f1; } gm_t6j8vdmi81h7c;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tdkjirf1g83ud f1; uint8_t f2; uint8_t pe[7]; } gm_t9jnd88rq7ul8;
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdkjirf1g83ud) == 32 && _Alignof(gm_tdkjirf1g83ud) == 8 && offsetof(gm_tdkjirf1g83ud, f0) == 0 && offsetof(gm_tdkjirf1g83ud, f1) == 8, "layout of `ThreadState`");
_Static_assert(sizeof(gm_tfvg1o4f8mmhi) == 32 && _Alignof(gm_tfvg1o4f8mmhi) == 8 && offsetof(gm_tfvg1o4f8mmhi, f0) == 0 && offsetof(gm_tfvg1o4f8mmhi, f1) == 16, "layout of `HostResult`");
_Static_assert(sizeof(gm_th5tat709fgha) == 64 && _Alignof(gm_th5tat709fgha) == 8 && offsetof(gm_th5tat709fgha, f0) == 0 && offsetof(gm_th5tat709fgha, f1) == 8, "layout of `Start`");
_Static_assert(sizeof(gm_tmvdbo28o1i6h) == 32 && _Alignof(gm_tmvdbo28o1i6h) == 8 && offsetof(gm_tmvdbo28o1i6h, f0) == 0 && offsetof(gm_tmvdbo28o1i6h, f1) == 8, "layout of `HostOp`");
_Static_assert(sizeof(gm_tobnqifq2n0g5) == 72 && _Alignof(gm_tobnqifq2n0g5) == 8 && offsetof(gm_tobnqifq2n0g5, f0) == 0 && offsetof(gm_tobnqifq2n0g5, f1) == 40 && offsetof(gm_tobnqifq2n0g5, f2) == 44 && offsetof(gm_tobnqifq2n0g5, f3) == 48 && offsetof(gm_tobnqifq2n0g5, f4) == 56 && offsetof(gm_tobnqifq2n0g5, f5) == 64, "layout of `Threads`");
_Static_assert(sizeof(gm_tolm6p7jp1hb3) == 72 && _Alignof(gm_tolm6p7jp1hb3) == 8 && offsetof(gm_tolm6p7jp1hb3, f0) == 0 && offsetof(gm_tolm6p7jp1hb3, f1) == 16 && offsetof(gm_tolm6p7jp1hb3, f2) == 32, "layout of `EvalTrap`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t07lf02ct4q61 f1; } gm_tslj4ghflekdk;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t683gnj9k3p7d) == 72 && _Alignof(gm_t683gnj9k3p7d) == 8 && offsetof(gm_t683gnj9k3p7d, f0) == 0 && offsetof(gm_t683gnj9k3p7d, f1) == 32 && offsetof(gm_t683gnj9k3p7d, f2) == 64, "layout of `HostCall`");
_Static_assert(sizeof(gm_t6j8vdmi81h7c) == 40 && _Alignof(gm_t6j8vdmi81h7c) == 8 && offsetof(gm_t6j8vdmi81h7c, f0) == 0 && offsetof(gm_t6j8vdmi81h7c, f1) == 8, "layout of `Flow!HostResult`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tnsjse4su5jue v1; gm_tslj4ghflekdk v2; } f1; } gm_t9hl37efuvhj8;
_Static_assert(sizeof(gm_t9jnd88rq7ul8) == 48 && _Alignof(gm_t9jnd88rq7ul8) == 8 && offsetof(gm_t9jnd88rq7ul8, f0) == 0 && offsetof(gm_t9jnd88rq7ul8, f1) == 8 && offsetof(gm_t9jnd88rq7ul8, f2) == 40, "layout of `ThreadRec`");
_Static_assert(sizeof(gm_tslj4ghflekdk) == 40 && _Alignof(gm_tslj4ghflekdk) == 8 && offsetof(gm_tslj4ghflekdk, f0) == 0 && offsetof(gm_tslj4ghflekdk, f1) == 8, "layout of `(fiber: u32, flow: ?Flow)`");
_Static_assert(sizeof(gm_t9hl37efuvhj8) == 56 && _Alignof(gm_t9hl37efuvhj8) == 8 && offsetof(gm_t9hl37efuvhj8, f0) == 0 && offsetof(gm_t9hl37efuvhj8, f1) == 8, "layout of `Msg`");
struct gm_fr_check_fatal_dgfalcm {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_connected_a7ps01v {
    gm_frame h;
    gm_tfvg1o4f8mmhi ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_destroy_o4n4nl0 {
    gm_frame h;
    gm_tb40qn7hk0tl9 l1; /* site */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_5j6fqpv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8othtri_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_env_exit_prbvpjt {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_04kmcqn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_func_ene90ss {
    gm_frame h;
    void * ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_host_main_55puiu2 {
    gm_frame h;
    void * l0; /* j */
    gm_b0_1 ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_join_r8j2dp1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_j8jr9cr_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_jh4e1ji {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_new_coro_06nq12a {
    gm_frame h;
    gm_th5tat709fgha l1; /* start */
    uint32_t ret;
};
struct gm_fr_on_host_thread_kmegc07 {
    gm_frame h;
    gm_t6j8vdmi81h7c ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_runnable_o1q905g {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sleep_gqtie06 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_switch_thread_m7aoqko {
    gm_frame h;
    gm_t9jnd88rq7ul8 l8; /* t */
    gm_t9jnd88rq7ul8 l18; /* t */
    gm_t9jnd88rq7ul8 l25; /* t */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_thread_main_n6r943c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_thread_op_pfli7b1 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_threads_kh5dldj {
    gm_frame h;
    gm_trqfvmjtt4sij l0; /* l */
    gm_tobnqifq2n0g5 ret;
};
struct gm_fr_tick_6bsfu3n {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
__attribute__((aligned(4))) uint32_t gm_f_add_coro_k13bkpe(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_call_function_u7ovth6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, gm_b16_8 a3, void * a4, uint32_t a5, const void * a6, uint8_t a7, gm_b16_8 a8);
__attribute__((aligned(4))) void gm_f_check_fatal_dgfalcm(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tcfa6l7reoosn gm_f_check_fatal_dgfalcm_c(void * a0);
__attribute__((aligned(4))) void gm_f_connected_a7ps01v(void *gm_out, gm_t62ebbi2i5rkd a0);
__attribute__((aligned(4))) gm_tfvg1o4f8mmhi gm_f_connected_a7ps01v_c(gm_t62ebbi2i5rkd a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_defaults_p077jgf(gm_closure a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_destroy_o4n4nl0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5j6fqpv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8othtri_2(void);
__attribute__((aligned(4))) void gm_f_env_exit_prbvpjt(void *gm_out, void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tcfa6l7reoosn gm_f_env_exit_prbvpjt_c(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_04kmcqn(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_from_addr_7a6bpqd(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_format_uh8vg1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_handoff_mtsh8r2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_host_main_55puiu2(void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_join_r8j2dp1(gm_csfq64m1m1rt6m a0);
inline __attribute__((aligned(4))) void gm_f_list_j8jr9cr_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_jh4e1ji(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_at_6u81jcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_new_coro_06nq12a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint32_t a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_g0l0iur(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_jd11mvt(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_on_host_thread_kmegc07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bn7ud1g(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_runnable_o1q905g(void * a0, void * a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_sleep_gqtie06(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_spawn_2tmnfpi(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_switch_thread_m7aoqko(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_thread_main_n6r943c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_thread_op_pfli7b1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_threads_kh5dldj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_tick_6bsfu3n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
extern uint8_t gm_cusfosftpfn[4] __attribute__((aligned(4)));
extern uint8_t gm_k02l1tj96tm[40] __attribute__((aligned(8)));
extern uint8_t gm_ki1ml5fl8pd[40] __attribute__((aligned(8)));
extern uint8_t gm_kjs2b0icc8d[40] __attribute__((aligned(8)));
extern uint8_t gm_klqd6h436uv[40] __attribute__((aligned(8)));
extern uint8_t gm_kovthkfk7ta[40] __attribute__((aligned(8)));
extern uint8_t gm_kujrnb073g1[40] __attribute__((aligned(8)));
extern const uint8_t gm_s632oftf9nssu[43];
extern const uint8_t gm_sb5pfucassrk8[34];
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srchntnl6m6g3am[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcp0l171g2m7ho[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_svu0pqir618v4[39];
extern const gm_type gm_tyo86ks9qkvt;
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
            gm_b16_8 t6 = t5;
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t6) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t6)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = gm_l1;
        gm_b16_8 t10 = t9;
        if (GM_LD(uintptr_t, ((uint8_t *)&t8) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t8)), GM_LD(void *, ((uint8_t *)&t10)), GM_LD(uintptr_t, ((uint8_t *)&t8) + 8) * 1);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_destroy_o4n4nl0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_destroy_o4n4nl0 F;
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
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t9))(t6, t8, ((uint64_t)72ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5j6fqpv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5j6fqpv F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8othtri_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8othtri_2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
                    uint64_t t12 = t11;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l5 = t15;
                    {
                        uint64_t t17 = gm_l5;
                        uint64_t t18 = (uintptr_t)t17;
                        void * t19 = gm_l2;
                        uint8_t t20 = gm_l1;
                        GM_ST(uint8_t, ((uint8_t *)t19 + t18 * 1), t20);
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t22 = gm_l4;
                    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                    uint64_t t25 = r23;
                    gm_l4 = t25;
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
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_func_ene90ss F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    uint32_t t3 = gm_l1;
    void * t4 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_host_main_55puiu2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_host_main_55puiu2 F;
    GM_STACK_CHECK(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_closure t2 = ((gm_closure){(void *)gm_f_fn_04kmcqn, (void *)&F.h, cx});
        gm_f_defaults_p077jgf(t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = F.l0;
        void * t4 = (void *)(((uint8_t *)t3) + 64);
        gm_t9qkh0rsooc8d t5;
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)2ULL));
        gm_t9qkh0rsooc8d t6 = t5;
        __atomic_store_n(((uint32_t *)t4), ((uint32_t)1ULL), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t6))]);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ci6airg F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    uint16_t t3 = gm_l2;
    gm_tjcj4j2gbh6c8 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(uint16_t, ((uint8_t *)&t4 + 2), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
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
inline __attribute__((aligned(4))) void gm_f_join_r8j2dp1(gm_csfq64m1m1rt6m a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_join_r8j2dp1 F;
    gm_tm98ap4112hee gm_l0; /* t */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tm98ap4112hee, a0);
    {
        {
            int64_t t3 = GM_LD(int64_t, (((uint8_t *)&gm_l0) + 0));
            int64_t t4 = gm_thread_join(t3);
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_j8jr9cr_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_j8jr9cr_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8othtri_2();
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
inline __attribute__((aligned(4))) void gm_f_list_jh4e1ji(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_jh4e1ji F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_5j6fqpv();
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
inline __attribute__((aligned(4))) uint32_t gm_f_new_coro_06nq12a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint32_t a2) {
    struct gm_fr_new_coro_06nq12a F;
    void * gm_l0; /* m */
    uint32_t gm_l2; /* thread */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    void * t1 = gm_l0;
    unsigned __int128 n2 = (unsigned __int128)((uint64_t)20ULL);
    if (n2 >= 64) gm_trap_shift((uint64_t)n2, "usize", gm_srcp0l171g2m7ho, 376, 96);
    uint64_t r3 = (uint64_t)((uint64_t)((uint64_t)8ULL) << (int)n2);
    bool o4 = (uint64_t)(r3 >> (int)n2) != ((uint64_t)8ULL);
    if (o4) gm_trap("integer overflow in `<<`", gm_srcp0l171g2m7ho, 376, 96);
    uint64_t t5 = r3;
    uint32_t t6 = gm_l2;
    uint32_t t7 = gm_f_add_coro_k13bkpe((void *)NULL, cx, NULL, t1, &(*(gm_th5tat709fgha *)((uint8_t *)&F.l1)), t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t7;
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
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_b16_8 t3 = t2;
    uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
    void * t5 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t3)));
    uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 176));
    uint64_t t9 = gm_f_us_cuv01fp(t8);
    uint64_t t10 = (uintptr_t)t9;
    if (t6 > t10 || t10 > (uintptr_t)t4) gm_trap_slice(t6, t10, t4, gm_srcnqtpfma6qe4a, 104, 68);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t5 + t6 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10 - t6));
    uint64_t t12 = gm_f_get_u64_tgpb2vh(t1, t11);
    F.ret = t12;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 360);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 40));
        uint64_t t6 = (uintptr_t)t5;
        void * t7 = gm_l2;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 0) + 0) + 8);
        if (t6 >= (uintptr_t)(t8)) gm_trap_index(t6, t8, gm_srcpit8mdn0781q, 386, 14);
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 0) + 0)) + t6 * 16));
        gm_l3 = t9;
    }
    {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t11 = (uintptr_t)t10;
        gm_l4 = t11;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l2;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
        uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t17 = r15;
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t20 = r18;
        uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
        uint64_t t22 = (uint64_t)~t21;
        uint64_t t23 = (uint64_t)(t20 & t22);
        uint64_t t24 = gm_l4;
        uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
        if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 390, 12);
        uint64_t t27 = r25;
        gm_l5 = t27;
    }
    {
        uint64_t t28 = gm_l1;
        uint64_t t29 = gm_f_us_cuv01fp(t28);
        gm_l6 = t29;
    }
    {
        uint8_t t31 = 0;
        uint64_t t32 = gm_l1;
        uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
        if (!t33) {
            uint64_t t34 = gm_l5;
            uint64_t t35 = gm_l6;
            uint64_t r36; bool o37 = __builtin_add_overflow(t34, t35, &r36);
            if (o37) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 392, 18);
            uint64_t t38 = r36;
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t40 = (t38 > t39);
            t31 = t40;
        } else t31 = 1;
        if (!t31) goto ie30;
        {
            void * t42 = gm_l0;
            uint64_t t43 = gm_l1;
            gm_b16_8 t44 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t47 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t48 = gm_l5;
        uint64_t t49 = (uintptr_t)t48;
        uint64_t t50 = gm_l5;
        uint64_t t51 = gm_l6;
        uint64_t r52; bool o53 = __builtin_add_overflow(t50, t51, &r52);
        if (o53) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 21);
        uint64_t t54 = r52;
        uint64_t t55 = (uintptr_t)t54;
        if (t49 > t55 || t55 > (uintptr_t)t46) gm_trap_slice(t49, t55, t46, gm_srcpit8mdn0781q, 393, 10);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t47 + t49 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55 - t49));
        gm_l7 = t56;
    }
    {
        void * t57 = gm_l2;
        uint64_t t58 = gm_l5;
        uint64_t t59 = gm_l6;
        uint64_t r60; bool o61 = __builtin_add_overflow(t58, t59, &r60);
        if (o61) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 14);
        uint64_t t62 = r60;
        GM_ST(uint64_t, (((uint8_t *)t57) + 48), t62);
    }
    {
        gm_b16_8 t63 = gm_l7;
        gm_f_fill_abmf5g5(t63, ((uint8_t)0ULL));
    }
    {
        gm_b16_8 t64 = gm_l7;
        uint8_t t65 = 0;
        F.ret = t64;
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
inline __attribute__((aligned(4))) void gm_f_check_fatal_dgfalcm(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_check_fatal_dgfalcm F;
    void * gm_l0; /* m */
    void * gm_l1; /* t */
    uint8_t gm_l2; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 309u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((((uint8_t *)t4) + 1656) + 56));
            void * t6 = t5;
            if (!(GM_LD(void *, ((uint8_t *)&t6)) != 0)) goto ie3;
            void * t7 = GM_LD(void *, ((uint8_t *)&t6));
            gm_l1 = t7;
            {
                void * t9 = gm_l1;
                gm_t76123gfet73g t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)4ULL));
                GM_ST(void *, ((uint8_t *)&t10 + 8), t9);
                gm_tcfa6l7reoosn t11;
                GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
                ((uint8_t *)&t11)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t11 + 8), t10);
                gm_tcfa6l7reoosn t12 = t11;
                uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
                F.ret = t11;
                goto gm_fret;
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            void * t15 = gm_l0;
            if (!((((((uint8_t *)t15) + 1656) + 64))[1] != 0)) goto ie14;
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t15) + 1656) + 64));
            gm_l2 = t16;
            {
                uint8_t t18 = gm_l2;
                gm_t76123gfet73g t19;
                GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t19 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t19 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t19 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)3ULL));
                GM_ST(uint8_t, ((uint8_t *)&t19 + 8), t18);
                gm_tcfa6l7reoosn t20;
                GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
                ((uint8_t *)&t20)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t20 + 8), t19);
                gm_tcfa6l7reoosn t21 = t20;
                uint8_t t22 = ((((uint8_t *)&t21))[0] != 0);
                F.ret = t20;
                goto gm_fret;
            }
            goto ix14;
ie14:;
ix14:;
        }
    }
    gm_b0_1 t23;
    gm_tcfa6l7reoosn t24;
    GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t24 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t24 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t24 + 24, 0);
    ((uint8_t *)&t24)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t24 + 8), t23);
    gm_tcfa6l7reoosn t25 = t24;
    F.ret = t25;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tcfa6l7reoosn gm_f_check_fatal_dgfalcm_c(void * a0) { gm_tcfa6l7reoosn gm_r; gm_f_check_fatal_dgfalcm(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_connected_a7ps01v(void *gm_out, gm_t62ebbi2i5rkd a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_connected_a7ps01v F;
    gm_t62ebbi2i5rkd gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_t62ebbi2i5rkd t2 = gm_l0;
        gm_t62ebbi2i5rkd t3 = t2;
        gm_tm98ap4112hee t4;
        if (!((((uint8_t *)&t3))[0] != 0)) {
            gm_tm98ap4112hee t5 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t3) + 8));
            t4 = t5;
        } else {
            gm_toe5fmr3vvf4d t6 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t3) + 8));
            gm_l1 = t6;
            gm_toe5fmr3vvf4d t7 = gm_l1;
            gm_thf5kr0is9u9i t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t8 + 8), t7);
            gm_tppo2hmut88dq t9;
            memset(&t9, 0, sizeof t9);
            GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&(*(gm_tfvg1o4f8mmhi *)gm_out) + 0, t8);
            GM_ST(gm_tppo2hmut88dq, (uint8_t *)&(*(gm_tfvg1o4f8mmhi *)gm_out) + 16, t9);
            goto gm_fdone;
        }
        gm_l2 = t4;
    }
    {
        gm_thf5kr0is9u9i t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
        ((uint8_t *)&t10)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t10 + 8), ((uint64_t)0ULL));
        gm_tm98ap4112hee t11 = gm_l2;
        gm_tppo2hmut88dq t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t12 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t12 + 12, 0);
        GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t12), t11);
        ((uint8_t *)&t12)[8] = 1;
        GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&(*(gm_tfvg1o4f8mmhi *)gm_out) + 0, t10);
        GM_ST(gm_tppo2hmut88dq, (uint8_t *)&(*(gm_tfvg1o4f8mmhi *)gm_out) + 16, t12);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tfvg1o4f8mmhi gm_f_connected_a7ps01v_c(gm_t62ebbi2i5rkd a0) { gm_tfvg1o4f8mmhi gm_r; gm_f_connected_a7ps01v(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_env_exit_prbvpjt(void *gm_out, void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_env_exit_prbvpjt F;
    void * gm_l0; /* m */
    uint8_t gm_l1; /* code */
    GM_STACK_CHECK_SHALLOW(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, ((((uint8_t *)t3) + 1656) + 40));
        uint8_t t5 = (!(t4 == ((uint32_t)0ULL)));
        if (!t5) goto ie2;
        void * t6 = gm_l0;
        uint8_t t7;
        if (((((((uint8_t *)t6) + 1656) + 64))[1] != 0)) goto mn9;
        t7 = ((uint8_t)1);
        goto me8;
mn9:;
        t7 = ((uint8_t)0);
        goto me8;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 136, 35);
me8:;
        if (!t7) goto ie2;
        {
            void * t12 = gm_l0;
            uint8_t t13 = gm_l1;
            gm_tqs5p4n2tq19v t14;
            GM_ST(uint8_t, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[1] = 1;
            GM_ST(gm_tqs5p4n2tq19v, ((((uint8_t *)t12) + 1656) + 64), t14);
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t15 = gm_l1;
        gm_t76123gfet73g t16;
        GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t16 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t16 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t16 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)3ULL));
        GM_ST(uint8_t, ((uint8_t *)&t16 + 8), t15);
        gm_tcfa6l7reoosn t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
        ((uint8_t *)&t17)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t17 + 8), t16);
        gm_tcfa6l7reoosn t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tcfa6l7reoosn gm_f_env_exit_prbvpjt_c(void * a0, uint8_t a1) { gm_tcfa6l7reoosn gm_r; gm_f_env_exit_prbvpjt(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_04kmcqn(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_04kmcqn F;
    gm_b16_8 gm_l0; /* buf */
    gm_tm98ap4112hee gm_l1; /* f */
    gm_b16_8 gm_l2; /* buf */
    gm_tm98ap4112hee gm_l3; /* s */
    gm_b16_8 gm_l4; /* data */
    gm_tm98ap4112hee gm_l5; /* s */
    gm_b16_8 gm_l6; /* buf */
    gm_b16_8 gm_l7; /* host */
    uint16_t gm_l8; /* port */
    gm_tm98ap4112hee gm_l9; /* l */
    GM_STACK_CHECK(&F, 757u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_host_main_55puiu2 *)((&F.h)->up))->l0));
        uint8_t *t3 = (((uint8_t *)t2) + 32);
        void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_host_main_55puiu2 *)((&F.h)->up))->l0));
        gm_tfvg1o4f8mmhi t5;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma10; default: goto ma13; }
ma7:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t16 != ((uint8_t)0ULL)) goto mn15;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 0) + 8));
        gm_l0 = t17;
        gm_tfvg1o4f8mmhi t18;
        gm_b16_8 t19 = gm_l0;
        gm_node *pn20 = gm_find(cx, 1018230276);
        if (!pn20) gm_trap("no handler installed for effect `In`", gm_srchntnl6m6g3am, 256, 46);
        gm_thf5kr0is9u9i t21;
        t21 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn20->clauses[0])(pn20->env, pn20->hctx, pn20, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&t18 + 0, t21);
        gm_tppo2hmut88dq t22;
        memset(&t22, 0, sizeof t22);
        GM_ST(gm_tppo2hmut88dq, (uint8_t *)&t18 + 16, t22);
        t5 = t18;
        goto me6;
mn15:;
ma8:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t24 != ((uint8_t)1ULL)) goto mn23;
        gm_tm98ap4112hee t25 = GM_LD(gm_tm98ap4112hee, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l1 = t25;
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l2 = t26;
        gm_tfvg1o4f8mmhi t27;
        gm_tm98ap4112hee t28 = gm_l1;
        gm_b16_8 t29 = gm_l2;
        gm_node *pn30 = gm_find(cx, 2126215853);
        if (!pn30) gm_trap("no handler installed for effect `Fs`", gm_srchntnl6m6g3am, 257, 48);
        gm_thf5kr0is9u9i t31;
        t31 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn30->clauses[2])(pn30->env, pn30->hctx, pn30, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&t27 + 0, t31);
        gm_tppo2hmut88dq t32;
        memset(&t32, 0, sizeof t32);
        GM_ST(gm_tppo2hmut88dq, (uint8_t *)&t27 + 16, t32);
        t5 = t27;
        goto me6;
mn23:;
ma9:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t34 != ((uint8_t)4ULL)) goto mn33;
        gm_tm98ap4112hee t35 = GM_LD(gm_tm98ap4112hee, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l3 = t35;
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l4 = t36;
        gm_tfvg1o4f8mmhi t37;
        gm_tm98ap4112hee t38 = gm_l3;
        gm_b16_8 t39 = gm_l4;
        gm_node *pn40 = gm_find(cx, 163658498);
        if (!pn40) gm_trap("no handler installed for effect `Net`", gm_srchntnl6m6g3am, 258, 49);
        gm_thf5kr0is9u9i t41;
        t41 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn40->clauses[4])(pn40->env, pn40->hctx, pn40, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&t37 + 0, t41);
        gm_tppo2hmut88dq t42;
        memset(&t42, 0, sizeof t42);
        GM_ST(gm_tppo2hmut88dq, (uint8_t *)&t37 + 16, t42);
        t5 = t37;
        goto me6;
mn33:;
ma10:;
        uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t44 != ((uint8_t)5ULL)) goto mn43;
        gm_tm98ap4112hee t45 = GM_LD(gm_tm98ap4112hee, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l5 = t45;
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l6 = t46;
        gm_tfvg1o4f8mmhi t47;
        gm_tm98ap4112hee t48 = gm_l5;
        gm_b16_8 t49 = gm_l6;
        gm_node *pn50 = gm_find(cx, 163658498);
        if (!pn50) gm_trap("no handler installed for effect `Net`", gm_srchntnl6m6g3am, 259, 48);
        gm_thf5kr0is9u9i t51;
        t51 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn50->clauses[5])(pn50->env, pn50->hctx, pn50, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&t47 + 0, t51);
        gm_tppo2hmut88dq t52;
        memset(&t52, 0, sizeof t52);
        GM_ST(gm_tppo2hmut88dq, (uint8_t *)&t47 + 16, t52);
        t5 = t47;
        goto me6;
mn43:;
ma11:;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t54 != ((uint8_t)2ULL)) goto mn53;
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l7 = t55;
        uint16_t t56 = GM_LD(uint16_t, (((((uint8_t *)t4) + 0) + 8) + 16));
        gm_l8 = t56;
        gm_b16_8 t57 = gm_l7;
        uint16_t t58 = gm_l8;
        gm_node *pn59 = gm_find(cx, 163658498);
        if (!pn59) gm_trap("no handler installed for effect `Net`", gm_srchntnl6m6g3am, 260, 47);
        gm_t62ebbi2i5rkd t60;
        t60 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, uint16_t))pn59->clauses[0])(pn59->env, pn59->hctx, pn59, t57, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfvg1o4f8mmhi t61;
        gm_f_connected_a7ps01v((void *)&t61, t60);
        t5 = t61;
        goto me6;
mn53:;
ma12:;
        uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t63 != ((uint8_t)3ULL)) goto mn62;
        gm_tm98ap4112hee t64 = GM_LD(gm_tm98ap4112hee, ((((uint8_t *)t4) + 0) + 8));
        gm_l9 = t64;
        gm_tm98ap4112hee t65 = gm_l9;
        gm_node *pn66 = gm_find(cx, 163658498);
        if (!pn66) gm_trap("no handler installed for effect `Net`", gm_srchntnl6m6g3am, 261, 37);
        gm_t62ebbi2i5rkd t67;
        t67 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn66->clauses[3])(pn66->env, pn66->hctx, pn66, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfvg1o4f8mmhi t68;
        gm_f_connected_a7ps01v((void *)&t68, t67);
        t5 = t68;
        goto me6;
mn62:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 255, 20);
me6:;
        GM_ST(gm_tfvg1o4f8mmhi, t3, t5);
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
extern __attribute__((aligned(4))) void gm_f_host_main_55puiu2(void * a0);
inline __attribute__((aligned(4))) void gm_f_on_host_thread_kmegc07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_on_host_thread_kmegc07 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tmvdbo28o1i6h gm_l2; /* op */
    void * gm_l3; /* j */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_tm98ap4112hee gm_l5; /* t */
    gm_tcfa6l7reoosn gm_l6; /* r */
    gm_tfvg1o4f8mmhi gm_l7; /* result */
    GM_STACK_CHECK(&F, 918u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    {
        gm_t683gnj9k3p7d t2;
        memset((uint8_t *)&t2 + 68, 0, 4);
        memcpy((uint8_t *)&t2 + 0, ((uint8_t *)&gm_l2), 32);
        gm_thf5kr0is9u9i t3;
        GM_ST(uint8_t, (uint8_t *)&t3 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t3 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t3 + 4, 0);
        ((uint8_t *)&t3)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t3 + 8), ((uint64_t)0ULL));
        GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&(*(gm_tfvg1o4f8mmhi *)((uint8_t *)&t2 + 32)) + 0, t3);
        gm_tppo2hmut88dq t4;
        memset(&t4, 0, sizeof t4);
        GM_ST(gm_tppo2hmut88dq, (uint8_t *)&(*(gm_tfvg1o4f8mmhi *)((uint8_t *)&t2 + 32)) + 16, t4);
        GM_ST(uint32_t, (uint8_t *)&t2 + 64, ((uint32_t)0ULL));
        gm_tq7s98ihonkju t5 = gm_f_new_g0l0iur((void *)NULL, cx, NULL, &t2, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klqd6h436uv)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srchntnl6m6g3am, 275, 10);
        void * t7 = GM_LD(void *, (((uint8_t *)&t6) + 8));
        gm_l3 = t7;
    }
    {
        void * t8 = gm_l3;
        gm_t62ebbi2i5rkd t9 = gm_f_spawn_2tmnfpi(((void *)gm_f_host_main_55puiu2), t8);
        gm_t62ebbi2i5rkd t10 = t9;
        gm_tm98ap4112hee t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_tm98ap4112hee t12 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t10) + 8));
            t11 = t12;
        } else {
            gm_toe5fmr3vvf4d t13 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t10) + 8));
            gm_l4 = t13;
            {
                void * t15 = gm_l3;
                gm_b0_1 t16 = gm_f_destroy_o4n4nl0((void *)NULL, cx, NULL, t15, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ki1ml5fl8pd)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_tfvg1o4f8mmhi t17;
            gm_toe5fmr3vvf4d t18 = gm_l4;
            gm_thf5kr0is9u9i t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
            ((uint8_t *)&t19)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t19 + 8), t18);
            GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&t17 + 0, t19);
            gm_tppo2hmut88dq t20;
            memset(&t20, 0, sizeof t20);
            GM_ST(gm_tppo2hmut88dq, (uint8_t *)&t17 + 16, t20);
            gm_t6j8vdmi81h7c t21;
            GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
            ((uint8_t *)&t21)[0] = 0;
            GM_ST(gm_tfvg1o4f8mmhi, ((uint8_t *)&t21 + 8), t17);
            gm_t6j8vdmi81h7c t22 = t21;
            uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
            F.ret = t21;
            goto gm_fret;
        }
        gm_l5 = t11;
    }
    {
        void * t24 = gm_l0;
        uint32_t t25 = GM_LD(uint32_t, ((((uint8_t *)t24) + 1656) + 40));
        uint64_t t26 = (uint64_t)t25;
        uint64_t t27 = (uintptr_t)t26;
        void * t28 = gm_l0;
        uint64_t t29 = GM_LD(uintptr_t, (((((uint8_t *)t28) + 1656) + 0) + 0) + 8);
        if (t27 >= (uintptr_t)(t29)) gm_trap_index(t27, t29, gm_srchntnl6m6g3am, 280, 5);
        void * t30 = gm_l3;
        void * t31 = (void *)(((uint8_t *)t30) + 64);
        gm_tdkjirf1g83ud t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t32 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t32 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t32 + 8), t31);
        GM_ST(gm_tdkjirf1g83ud, (((uint8_t *)GM_LD(void *, (((((uint8_t *)t28) + 1656) + 0) + 0)) + t27 * 48) + 8), t32);
    }
    {
        void * t33 = gm_l0;
        gm_tl874t2atnlh3 t34 = gm_l1;
        gm_f_switch_thread_m7aoqko((void *)&gm_l6, (void *)NULL, cx, NULL, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tm98ap4112hee t35 = gm_l5;
        gm_f_join_r8j2dp1(GM_CONV(gm_csfq64m1m1rt6m, t35));
    }
    {
        void * t36 = gm_l3;
        gm_tfvg1o4f8mmhi t37 = GM_LD(gm_tfvg1o4f8mmhi, (((uint8_t *)t36) + 32));
        gm_l7 = t37;
    }
    {
        void * t38 = gm_l3;
        gm_b0_1 t39 = gm_f_destroy_o4n4nl0((void *)NULL, cx, NULL, t38, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kovthkfk7ta)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tcfa6l7reoosn t40 = gm_l6;
        gm_tcfa6l7reoosn t41 = t40;
        if (((((uint8_t *)&t41))[0] != 0)) {
            gm_t76123gfet73g t42 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t41) + 8));
            gm_t6j8vdmi81h7c t43;
            GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t43 + 32, 0);
            ((uint8_t *)&t43)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t43 + 8), t42);
            F.ret = t43;
            goto gm_fret;
        }
        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 8));
    }
    {
        gm_tfvg1o4f8mmhi t45 = gm_l7;
        gm_t6j8vdmi81h7c t46;
        GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
        ((uint8_t *)&t46)[0] = 0;
        GM_ST(gm_tfvg1o4f8mmhi, ((uint8_t *)&t46 + 8), t45);
        gm_t6j8vdmi81h7c t47 = t46;
        uint8_t t48 = ((((uint8_t *)&t47))[0] != 0);
        F.ret = t46;
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
__attribute__((aligned(4))) uint8_t gm_f_runnable_o1q905g(void * a0, void * a1, gm_tf5srh4mp4b3i a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_runnable_o1q905g F;
    void * gm_l0; /* m */
    void * gm_l1; /* t */
    gm_tf5srh4mp4b3i gm_l2; /* now */
    uint32_t gm_l3; /* h */
    uint64_t gm_l4; /* deadline */
    void * gm_l5; /* done */
    uint64_t gm_l6; /* seen */
    gm_tf5srh4mp4b3i gm_l7; /* deadline */
    GM_STACK_CHECK_SHALLOW(&F, 376u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l1;
    uint8_t t2;
    uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma9; default: goto ma10; }
ma4:;
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t13 != ((uint8_t)0ULL)) goto mn12;
    t2 = ((uint8_t)1);
    goto me3;
mn12:;
ma5:;
    uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t15 != ((uint8_t)1ULL)) goto mn14;
    uint32_t t16 = GM_LD(uint32_t, ((((uint8_t *)t1) + 8) + 8));
    gm_l3 = t16;
    uint32_t t17 = gm_l3;
    uint64_t t18 = (uint64_t)t17;
    uint64_t t19 = (uintptr_t)t18;
    void * t20 = gm_l0;
    uint64_t t21 = GM_LD(uintptr_t, (((((uint8_t *)t20) + 1656) + 0) + 0) + 8);
    if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srchntnl6m6g3am, 71, 17);
    uint8_t t22 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((((uint8_t *)t20) + 1656) + 0) + 0)) + t19 * 48) + 8) + 0) == ((uint8_t)5ULL)));
    t2 = t22;
    goto me3;
mn14:;
ma6:;
    uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t24 != ((uint8_t)3ULL)) goto mn23;
    uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t1) + 8) + 8));
    gm_l4 = t25;
    uint8_t t26 = 0;
    uint8_t t27;
    if (((((uint8_t *)&gm_l2))[8] != 0)) goto mn29;
    t27 = ((uint8_t)0);
    goto me28;
mn29:;
    t27 = ((uint8_t)1);
    goto me28;
mn30:;
    gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 72, 25);
me28:;
    if (t27) {
        gm_tf5srh4mp4b3i t31 = gm_l2;
        gm_tf5srh4mp4b3i t32 = t31;
        uint64_t t33;
        if (((((uint8_t *)&t32))[8] != 0)) {
            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&t32) + 0));
            t33 = t34;
        } else {
            t33 = ((uint64_t)0ULL);
        }
        uint64_t t35 = gm_l4;
        uint8_t t36 = (t33 >= t35);
        t26 = t36;
    } else t26 = 0;
    t2 = t26;
    goto me3;
mn23:;
ma7:;
    uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t38 != ((uint8_t)4ULL)) goto mn37;
    void * t39 = GM_LD(void *, ((((uint8_t *)t1) + 8) + 8));
    gm_l5 = t39;
    void * t40 = gm_l5;
    gm_t9qkh0rsooc8d t41;
    GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t42 = t41;
    uint32_t t43 = __atomic_load_n(((uint32_t *)t40), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t42))]);
    uint8_t t44 = (!(t43 == ((uint32_t)0ULL)));
    t2 = t44;
    goto me3;
mn37:;
ma8:;
    uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t46 != ((uint8_t)2ULL)) goto mn45;
    uint64_t t47 = GM_LD(uint64_t, (((((uint8_t *)t1) + 8) + 8) + 0));
    gm_l6 = t47;
    gm_tf5srh4mp4b3i t48 = GM_LD(gm_tf5srh4mp4b3i, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l7 = t48;
    uint8_t t49 = 0;
    uint64_t t50 = gm_l6;
    void * t51 = gm_l0;
    uint64_t t52 = GM_LD(uint64_t, ((((uint8_t *)t51) + 1656) + 48));
    uint8_t t53 = (!(t50 == t52));
    if (!t53) {
        uint8_t t54 = 0;
        uint8_t t55 = 0;
        uint8_t t56;
        if (((((uint8_t *)&gm_l7))[8] != 0)) goto mn58;
        t56 = ((uint8_t)0);
        goto me57;
mn58:;
        t56 = ((uint8_t)1);
        goto me57;
mn59:;
        gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 74, 68);
me57:;
        if (t56) {
            uint8_t t60;
            if (((((uint8_t *)&gm_l2))[8] != 0)) goto mn62;
            t60 = ((uint8_t)0);
            goto me61;
mn62:;
            t60 = ((uint8_t)1);
            goto me61;
mn63:;
            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 74, 93);
me61:;
            t55 = t60;
        } else t55 = 0;
        if (t55) {
            gm_tf5srh4mp4b3i t64 = gm_l2;
            gm_tf5srh4mp4b3i t65 = t64;
            uint64_t t66;
            if (((((uint8_t *)&t65))[8] != 0)) {
                uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&t65) + 0));
                t66 = t67;
            } else {
                t66 = ((uint64_t)0ULL);
            }
            gm_tf5srh4mp4b3i t68 = gm_l7;
            gm_tf5srh4mp4b3i t69 = t68;
            uint64_t t70;
            if (((((uint8_t *)&t69))[8] != 0)) {
                uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&t69) + 0));
                t70 = t71;
            } else {
                t70 = ((uint64_t)0ULL);
            }
            uint8_t t72 = (t66 >= t70);
            t54 = t72;
        } else t54 = 0;
        t49 = t54;
    } else t49 = 1;
    t2 = t49;
    goto me3;
mn45:;
ma9:;
    uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t74 != ((uint8_t)5ULL)) goto mn73;
    t2 = ((uint8_t)0);
    goto me3;
mn73:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 69, 72);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sleep_gqtie06(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2) {
    struct gm_fr_sleep_gqtie06 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint64_t gm_l2; /* ns */
    GM_STACK_CHECK(&F, 454u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t4 = gm_l0;
            uint64_t t5 = GM_LD(uintptr_t, (((((uint8_t *)t4) + 1656) + 0) + 0) + 8);
            uint8_t t6 = ((t5 == ((uint64_t)1ULL)));
            if (!t6) goto ie3;
            {
                uint64_t t8 = gm_l2;
                gm_t4ce0jd3tdro4 t9;
                GM_ST(uint64_t, ((uint8_t *)&t9 + 0), t8);
                gm_node *pn10 = gm_find(cx, 1579066750);
                if (!pn10) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 222, 9);
                gm_b0_1 t11;
                t11 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t4ce0jd3tdro4))pn10->clauses[2])(pn10->env, pn10->hctx, pn10, t9);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t12;
                gm_tcfa6l7reoosn t13;
                GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t13 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t13 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t13 + 24, 0);
                ((uint8_t *)&t13)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t13 + 8), t12);
                gm_tcfa6l7reoosn t14 = t13;
                uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
                F.ret = t13;
                goto gm_fret;
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            void * t16 = gm_l0;
            uint32_t t17 = GM_LD(uint32_t, ((((uint8_t *)t16) + 1656) + 40));
            uint64_t t18 = (uint64_t)t17;
            uint64_t t19 = (uintptr_t)t18;
            void * t20 = gm_l0;
            uint64_t t21 = GM_LD(uintptr_t, (((((uint8_t *)t20) + 1656) + 0) + 0) + 8);
            if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srchntnl6m6g3am, 225, 5);
            uint8_t *t22 = (((uint8_t *)GM_LD(void *, (((((uint8_t *)t20) + 1656) + 0) + 0)) + t19 * 48) + 8);
            gm_node *pn23 = gm_find(cx, 1579066750);
            if (!pn23) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 225, 63);
            gm_t4ce0jd3tdro4 t24;
            t24 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn23->clauses[0])(pn23->env, pn23->hctx, pn23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t4ce0jd3tdro4 t25 = t24;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&t25) + 0));
            uint64_t t27 = gm_l2;
            uint64_t r28; bool o29 = __builtin_add_overflow(t26, t27, &r28);
            uint64_t t30 = o29 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r28;
            gm_tdkjirf1g83ud t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)3ULL));
            GM_ST(uint64_t, ((uint8_t *)&t31 + 8), t30);
            GM_ST(gm_tdkjirf1g83ud, t22, t31);
        }
        {
            void * t32 = gm_l0;
            gm_tl874t2atnlh3 t33 = gm_l1;
            gm_tcfa6l7reoosn t34;
            gm_f_switch_thread_m7aoqko((void *)&t34, (void *)NULL, cx, NULL, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t35 = t34;
            if (((((uint8_t *)&t35))[0] != 0)) {
                gm_t76123gfet73g t36 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t35) + 8));
                gm_tcfa6l7reoosn t37;
                GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
                ((uint8_t *)&t37)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t37 + 8), t36);
                F.ret = t37;
                goto gm_fret;
            }
            gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 8));
        }
    }
    gm_b0_1 t39;
    gm_tcfa6l7reoosn t40;
    GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t40 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t40 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t40 + 24, 0);
    ((uint8_t *)&t40)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t40 + 8), t39);
    gm_tcfa6l7reoosn t41 = t40;
    F.ret = t41;
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
__attribute__((aligned(4))) void gm_f_switch_thread_m7aoqko(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_switch_thread_m7aoqko F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* th */
    uint32_t gm_l3; /* me */
    uint64_t gm_l4; /* n */
    gm_tf5srh4mp4b3i gm_l5; /* now */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tf5srh4mp4b3i gm_l9; /* d */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* k */
    uint64_t gm_l13; /* i */
    void * gm_l14; /* t */
    gm_tf5srh4mp4b3i gm_l15; /* earliest */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_tf5srh4mp4b3i gm_l19; /* d */
    uint64_t gm_l20; /* e */
    uint64_t gm_l21; /* d */
    uint64_t gm_l22; /* e */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l26; /* e */
    uint64_t gm_l27; /* t */
    GM_STACK_CHECK(&F, 1955u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 1656);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 40));
        gm_l3 = t5;
    }
    {
        uint32_t t6 = gm_l3;
        uint64_t t7 = (uint64_t)t6;
        uint64_t t8 = (uintptr_t)t7;
        void * t9 = gm_l2;
        uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 0) + 0) + 8);
        if (t8 >= (uintptr_t)(t10)) gm_trap_index(t8, t10, gm_srchntnl6m6g3am, 83, 5);
        void * t11 = gm_l0;
        uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)t11) + 1088) + 0));
        GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t9) + 0) + 0)) + t8 * 48) + 0), t12);
    }
    {
        void * t13 = gm_l2;
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 0) + 0) + 8);
        gm_l4 = t14;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb15;
            {
                gm_tf5srh4mp4b3i t18;
                memset(&t18, 0, sizeof t18);
                gm_l5 = t18;
            }
            {
                {
                    void * t20 = gm_l2;
                    gm_b16_8 t21 = GM_LD(gm_b16_8, ((((uint8_t *)t20) + 0) + 0));
                    gm_l6 = t21;
                }
                {
                    gm_l7 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t23 = gm_l7;
                        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        uint8_t t25 = (t23 < t24);
                        if (!t25) goto lb22;
                        {
                            uint64_t t28 = gm_l7;
                            uint64_t t29 = (uintptr_t)t28;
                            uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            gm_t9jnd88rq7ul8 t33 = GM_LD(gm_t9jnd88rq7ul8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t29 * 48));
                            F.l8 = t33;
                            {
                                uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 8) + 0));
                                if (t36 != ((uint8_t)2ULL)) goto ie35;
                                gm_tf5srh4mp4b3i t37 = GM_LD(gm_tf5srh4mp4b3i, (((((uint8_t *)&F.l8) + 8) + 8) + 8));
                                gm_l9 = t37;
                                uint8_t t38;
                                if (((((uint8_t *)&gm_l9))[8] != 0)) goto mn40;
                                t38 = ((uint8_t)0);
                                goto me39;
mn40:;
                                t38 = ((uint8_t)1);
                                goto me39;
mn41:;
                                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 89, 45);
me39:;
                                if (!t38) goto ie35;
                                {
                                    gm_node *pn43 = gm_find(cx, 1579066750);
                                    if (!pn43) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 89, 67);
                                    gm_t4ce0jd3tdro4 t44;
                                    t44 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn43->clauses[0])(pn43->env, pn43->hctx, pn43);
                                    if (gm_unw.kind) {
                                        goto gm_ll26;
                                    }
                                    gm_t4ce0jd3tdro4 t45 = t44;
                                    uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&t45) + 0));
                                    gm_tf5srh4mp4b3i t47;
                                    GM_ST(uint8_t, (uint8_t *)&t47 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t47 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t47 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t47), t46);
                                    ((uint8_t *)&t47)[8] = 1;
                                    gm_l5 = t47;
                                }
                                goto ix35;
ie35:;
ix35:;
                            }
                            {
                                uint8_t t49;
                                uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 8) + 0));
                                if (t52 != ((uint8_t)3ULL)) goto mn51;
                                t49 = ((uint8_t)1);
                                goto me50;
mn51:;
                                t49 = ((uint8_t)0);
                                goto me50;
mn53:;
                                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 90, 16);
me50:;
                                if (!t49) goto ie48;
                                {
                                    gm_node *pn55 = gm_find(cx, 1579066750);
                                    if (!pn55) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 90, 45);
                                    gm_t4ce0jd3tdro4 t56;
                                    t56 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn55->clauses[0])(pn55->env, pn55->hctx, pn55);
                                    if (gm_unw.kind) {
                                        goto gm_ll26;
                                    }
                                    gm_t4ce0jd3tdro4 t57 = t56;
                                    uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&t57) + 0));
                                    gm_tf5srh4mp4b3i t59;
                                    GM_ST(uint8_t, (uint8_t *)&t59 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t59 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t59 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t59), t58);
                                    ((uint8_t *)&t59)[8] = 1;
                                    gm_l5 = t59;
                                }
                                goto ix48;
ie48:;
ix48:;
                            }
                            goto me31;
mn32:;
                            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 88, 9);
me31:;
                        }
lc22:;
                        {
                            uint64_t t61 = gm_l7;
                            uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                            uint64_t t64 = r62;
                            gm_l7 = t64;
                        }
                    }
gm_ll26:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb22; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc22; }
                    }
                    goto gm_ll16;
lb22:;
                }
            }
            {
                {
                    gm_l11 = ((uint64_t)1ULL);
                }
                {
                    uint64_t t66 = gm_l4;
                    uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
                    if (o68) gm_trap("integer overflow in `+`", gm_srchntnl6m6g3am, 92, 21);
                    uint64_t t69 = r67;
                    gm_l10 = t69;
                }
                {
                    for (;;) {
                        uint64_t t71 = gm_l11;
                        uint64_t t72 = gm_l10;
                        uint8_t t73 = (t71 < t72);
                        if (!t73) goto lb70;
                        {
                            uint64_t t76 = gm_l11;
                            uint64_t t77 = t76;
                            uint64_t t80 = GM_LD(uint64_t, ((uint8_t *)&t77));
                            gm_l12 = t80;
                            {
                                uint32_t t82 = gm_l3;
                                uint64_t t83 = (uint64_t)t82;
                                uint64_t t84 = gm_l12;
                                uint64_t r85; bool o86 = __builtin_add_overflow(t83, t84, &r85);
                                if (o86) gm_trap("integer overflow in `+`", gm_srchntnl6m6g3am, 93, 19);
                                uint64_t t87 = r85;
                                uint64_t t88 = gm_l4;
                                if (t88 == 0) gm_trap("division by zero", gm_srchntnl6m6g3am, 93, 18);
                                uint64_t t89 = (uint64_t)(t87 % t88);
                                gm_l13 = t89;
                            }
                            {
                                uint64_t t90 = gm_l13;
                                uint64_t t91 = (uintptr_t)t90;
                                void * t92 = gm_l2;
                                uint64_t t93 = GM_LD(uintptr_t, ((((uint8_t *)t92) + 0) + 0) + 8);
                                if (t91 >= (uintptr_t)(t93)) gm_trap_index(t91, t93, gm_srchntnl6m6g3am, 94, 19);
                                void * t94 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t92) + 0) + 0)) + t91 * 48);
                                gm_l14 = t94;
                            }
                            {
                                void * t96 = gm_l0;
                                void * t97 = gm_l14;
                                gm_tf5srh4mp4b3i t98 = gm_l5;
                                uint8_t t99 = gm_f_runnable_o1q905g(t96, t97, t98);
                                uint8_t t100 = (uint8_t)!t99;
                                if (!t100) goto ie95;
                                {
                                    goto lc70;
                                }
                                goto ix95;
ie95:;
ix95:;
                            }
                            {
                                void * t102 = gm_l14;
                                gm_tdkjirf1g83ud t103;
                                GM_ST(uint8_t, (uint8_t *)&t103 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t103 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t103 + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&t103 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t103 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_tdkjirf1g83ud, (((uint8_t *)t102) + 8), t103);
                            }
                            {
                                uint64_t t105 = gm_l13;
                                uint32_t t106 = gm_l3;
                                uint64_t t107 = (uint64_t)t106;
                                uint8_t t108 = ((t105 == t107));
                                if (!t108) goto ie104;
                                {
                                    void * t110 = gm_l0;
                                    gm_f_check_fatal_dgfalcm((void *)&(*(gm_tcfa6l7reoosn *)gm_out), t110);
                                    goto gm_fdone;
                                }
                                goto ix104;
ie104:;
ix104:;
                            }
                            {
                                void * t111 = gm_l2;
                                uint64_t t112 = gm_l13;
                                uint32_t t113 = (uint32_t)t112;
                                GM_ST(uint32_t, (((uint8_t *)t111) + 40), t113);
                            }
                            {
                                {
                                    void * t115 = gm_l0;
                                    void * t116 = gm_l14;
                                    uint32_t t117 = GM_LD(uint32_t, (((uint8_t *)t116) + 0));
                                    gm_t9hl37efuvhj8 t118;
                                    memset(&t118, 0, sizeof t118);
                                    GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)0ULL));
                                    gm_t9hl37efuvhj8 t119;
                                    gm_f_handoff_mtsh8r2((void *)&t119, (void *)NULL, cx, NULL, t115, t117, &t118);
                                    if (gm_unw.kind) {
                                        goto gm_ll74;
                                    }
                                }
                            }
                            {
                                void * t120 = gm_l0;
                                uint32_t t121 = gm_l3;
                                GM_ST(uint32_t, ((((uint8_t *)t120) + 1656) + 40), t121);
                            }
                            {
                                uint32_t t123 = gm_l3;
                                uint64_t t124 = (uint64_t)t123;
                                uint64_t t125 = (uintptr_t)t124;
                                void * t126 = gm_l0;
                                uint64_t t127 = GM_LD(uintptr_t, (((((uint8_t *)t126) + 1656) + 0) + 0) + 8);
                                if (t125 >= (uintptr_t)(t127)) gm_trap_index(t125, t127, gm_srchntnl6m6g3am, 101, 16);
                                uint8_t t128 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((((uint8_t *)t126) + 1656) + 0) + 0)) + t125 * 48) + 8) + 0) == ((uint8_t)0ULL)));
                                if (!t128) goto ie122;
                                {
                                    void * t130 = gm_l0;
                                    gm_f_check_fatal_dgfalcm((void *)&(*(gm_tcfa6l7reoosn *)gm_out), t130);
                                    goto gm_fdone;
                                }
                                goto ix122;
ie122:;
ix122:;
                            }
                            {
                                goto lb70;
                            }
                            goto me78;
mn79:;
                            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 92, 9);
me78:;
                        }
lc70:;
                        {
                            uint64_t t132 = gm_l11;
                            uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                            uint64_t t135 = r133;
                            gm_l11 = t135;
                        }
                    }
gm_ll74:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb70; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc70; }
                    }
                    goto gm_ll16;
lb70:;
                }
            }
            {
                void * t137 = gm_l0;
                uint32_t t138 = gm_l3;
                uint64_t t139 = (uint64_t)t138;
                uint64_t t140 = (uintptr_t)t139;
                void * t141 = gm_l2;
                uint64_t t142 = GM_LD(uintptr_t, ((((uint8_t *)t141) + 0) + 0) + 8);
                if (t140 >= (uintptr_t)(t142)) gm_trap_index(t140, t142, gm_srchntnl6m6g3am, 105, 24);
                void * t143 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t141) + 0) + 0)) + t140 * 48);
                gm_tf5srh4mp4b3i t144 = gm_l5;
                uint8_t t145 = gm_f_runnable_o1q905g(t137, t143, t144);
                if (!t145) goto ie136;
                {
                    goto lc15;
                }
                goto ix136;
ie136:;
ix136:;
            }
            {
                gm_tf5srh4mp4b3i t147;
                memset(&t147, 0, sizeof t147);
                gm_l15 = t147;
            }
            {
                {
                    void * t149 = gm_l2;
                    gm_b16_8 t150 = GM_LD(gm_b16_8, ((((uint8_t *)t149) + 0) + 0));
                    gm_l16 = t150;
                }
                {
                    gm_l17 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t152 = gm_l17;
                        uint64_t t153 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        uint8_t t154 = (t152 < t153);
                        if (!t154) goto lb151;
                        {
                            uint64_t t157 = gm_l17;
                            uint64_t t158 = (uintptr_t)t157;
                            uint64_t t159 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                            gm_t9jnd88rq7ul8 t162 = GM_LD(gm_t9jnd88rq7ul8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t158 * 48));
                            F.l18 = t162;
                            {
                                uint8_t t165 = GM_LD(uint8_t, ((((uint8_t *)&F.l18) + 8) + 0));
                                if (t165 != ((uint8_t)2ULL)) goto ie164;
                                gm_tf5srh4mp4b3i t166 = GM_LD(gm_tf5srh4mp4b3i, (((((uint8_t *)&F.l18) + 8) + 8) + 8));
                                gm_l19 = t166;
                                uint8_t t167;
                                if (((((uint8_t *)&gm_l19))[8] != 0)) goto mn169;
                                t167 = ((uint8_t)0);
                                goto me168;
mn169:;
                                t167 = ((uint8_t)1);
                                goto me168;
mn170:;
                                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 109, 45);
me168:;
                                if (!t167) goto ie164;
                                {
                                    if (!((((uint8_t *)&gm_l15))[8] != 0)) goto ie172;
                                    uint64_t t173 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                    gm_l20 = t173;
                                    uint64_t t174 = gm_l20;
                                    gm_tf5srh4mp4b3i t175 = gm_l19;
                                    gm_tf5srh4mp4b3i t176 = t175;
                                    uint64_t t177;
                                    if (((((uint8_t *)&t176))[8] != 0)) {
                                        uint64_t t178 = GM_LD(uint64_t, (((uint8_t *)&t176) + 0));
                                        t177 = t178;
                                    } else {
                                        t177 = ((uint64_t)0ULL);
                                    }
                                    uint8_t t179 = (t174 <= t177);
                                    if (!t179) goto ie172;
                                    goto ix172;
ie172:;
                                    {
                                        gm_tf5srh4mp4b3i t182 = gm_l19;
                                        gm_l15 = t182;
                                    }
ix172:;
                                }
                                goto ix164;
ie164:;
ix164:;
                            }
                            {
                                uint8_t t184 = GM_LD(uint8_t, ((((uint8_t *)&F.l18) + 8) + 0));
                                if (t184 != ((uint8_t)3ULL)) goto ie183;
                                uint64_t t185 = GM_LD(uint64_t, ((((uint8_t *)&F.l18) + 8) + 8));
                                gm_l21 = t185;
                                {
                                    if (!((((uint8_t *)&gm_l15))[8] != 0)) goto ie187;
                                    uint64_t t188 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                    gm_l22 = t188;
                                    uint64_t t189 = gm_l22;
                                    uint64_t t190 = gm_l21;
                                    uint8_t t191 = (t189 <= t190);
                                    if (!t191) goto ie187;
                                    goto ix187;
ie187:;
                                    {
                                        uint64_t t194 = gm_l21;
                                        gm_tf5srh4mp4b3i t195;
                                        GM_ST(uint8_t, (uint8_t *)&t195 + 9, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t195 + 10, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t195 + 12, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t195), t194);
                                        ((uint8_t *)&t195)[8] = 1;
                                        gm_l15 = t195;
                                    }
ix187:;
                                }
                                goto ix183;
ie183:;
ix183:;
                            }
                            goto me160;
mn161:;
                            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 108, 9);
me160:;
                        }
lc151:;
                        {
                            uint64_t t197 = gm_l17;
                            uint64_t r198; bool o199 = __builtin_add_overflow(t197, ((uint64_t)1ULL), &r198);
                            uint64_t t200 = r198;
                            gm_l17 = t200;
                        }
                    }
lb151:;
                }
            }
            {
                {
                    void * t202 = gm_l2;
                    gm_b16_8 t203 = GM_LD(gm_b16_8, ((((uint8_t *)t202) + 0) + 0));
                    gm_l23 = t203;
                }
                {
                    gm_l24 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t205 = gm_l24;
                        uint64_t t206 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                        uint8_t t207 = (t205 < t206);
                        if (!t207) goto lb204;
                        {
                            uint64_t t210 = gm_l24;
                            uint64_t t211 = (uintptr_t)t210;
                            uint64_t t212 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                            gm_t9jnd88rq7ul8 t215 = GM_LD(gm_t9jnd88rq7ul8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t211 * 48));
                            F.l25 = t215;
                            {
                                uint8_t t218;
                                uint8_t t221 = GM_LD(uint8_t, ((((uint8_t *)&F.l25) + 8) + 0));
                                if (t221 != ((uint8_t)4ULL)) goto mn220;
                                t218 = ((uint8_t)1);
                                goto me219;
mn220:;
                                t218 = ((uint8_t)0);
                                goto me219;
mn222:;
                                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 118, 16);
me219:;
                                if (!t218) goto ie217;
                                uint8_t t223;
                                if (((((uint8_t *)&gm_l15))[8] != 0)) goto mn225;
                                t223 = ((uint8_t)1);
                                goto me224;
mn225:;
                                t223 = ((uint8_t)0);
                                goto me224;
mn226:;
                                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 118, 40);
me224:;
                                if (!t223) goto ie217;
                                {
                                    gm_tf5srh4mp4b3i t228 = gm_l5;
                                    gm_tf5srh4mp4b3i t229 = t228;
                                    uint64_t t230;
                                    if (((((uint8_t *)&t229))[8] != 0)) {
                                        uint64_t t231 = GM_LD(uint64_t, (((uint8_t *)&t229) + 0));
                                        t230 = t231;
                                    } else {
                                        gm_node *pn232 = gm_find(cx, 1579066750);
                                        if (!pn232) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 118, 80);
                                        gm_t4ce0jd3tdro4 t233;
                                        t233 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn232->clauses[0])(pn232->env, pn232->hctx, pn232);
                                        if (gm_unw.kind) {
                                            goto gm_ll208;
                                        }
                                        gm_t4ce0jd3tdro4 t234 = t233;
                                        uint64_t t235 = GM_LD(uint64_t, (((uint8_t *)&t234) + 0));
                                        t230 = t235;
                                    }
                                    uint64_t r236; bool o237 = __builtin_add_overflow(t230, ((uint64_t)1000000ULL), &r236);
                                    uint64_t t238 = o237 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r236;
                                    gm_tf5srh4mp4b3i t239;
                                    GM_ST(uint8_t, (uint8_t *)&t239 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t239 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t239 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t239), t238);
                                    ((uint8_t *)&t239)[8] = 1;
                                    gm_l15 = t239;
                                }
                                goto ix217;
ie217:;
ix217:;
                            }
                            goto me213;
mn214:;
                            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 117, 9);
me213:;
                        }
lc204:;
                        {
                            uint64_t t241 = gm_l24;
                            uint64_t r242; bool o243 = __builtin_add_overflow(t241, ((uint64_t)1ULL), &r242);
                            uint64_t t244 = r242;
                            gm_l24 = t244;
                        }
                    }
gm_ll208:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb204; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc204; }
                    }
                    goto gm_ll16;
lb204:;
                }
            }
            {
                gm_tf5srh4mp4b3i t245 = gm_l15;
                gm_tf5srh4mp4b3i t246 = t245;
                uint64_t t247;
                if (((((uint8_t *)&t246))[8] != 0)) {
                    uint64_t t248 = GM_LD(uint64_t, (((uint8_t *)&t246) + 0));
                    t247 = t248;
                } else {
                    void * t249 = gm_l0;
                    gm_tl874t2atnlh3 t250 = gm_l1;
                    gm_b16_8 t251;
                    GM_ST(void *, (uint8_t *)&t251, (void *)(gm_sb5pfucassrk8));
                    GM_ST(uintptr_t, (uint8_t *)&t251 + 8, (uintptr_t)(33));
                    gm_t76123gfet73g t252;
                    gm_f_trap_qh84l9d((void *)&t252, (void *)NULL, cx, NULL, t249, t250, t251);
                    if (gm_unw.kind) {
                        goto gm_ll16;
                    }
                    gm_tcfa6l7reoosn t253;
                    GM_ST(uint8_t, (uint8_t *)&t253 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t253 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t253 + 4, 0);
                    ((uint8_t *)&t253)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t253 + 8), t252);
                    gm_tcfa6l7reoosn t254 = t253;
                    uint8_t t255 = ((((uint8_t *)&t254))[0] != 0);
                    F.ret = t253;
                    goto gm_fret;
                }
                gm_l26 = t247;
            }
            {
                gm_tf5srh4mp4b3i t256 = gm_l5;
                gm_tf5srh4mp4b3i t257 = t256;
                uint64_t t258;
                if (((((uint8_t *)&t257))[8] != 0)) {
                    uint64_t t259 = GM_LD(uint64_t, (((uint8_t *)&t257) + 0));
                    t258 = t259;
                } else {
                    gm_node *pn260 = gm_find(cx, 1579066750);
                    if (!pn260) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 122, 23);
                    gm_t4ce0jd3tdro4 t261;
                    t261 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn260->clauses[0])(pn260->env, pn260->hctx, pn260);
                    if (gm_unw.kind) {
                        goto gm_ll16;
                    }
                    gm_t4ce0jd3tdro4 t262 = t261;
                    uint64_t t263 = GM_LD(uint64_t, (((uint8_t *)&t262) + 0));
                    t258 = t263;
                }
                gm_l27 = t258;
            }
            {
                uint64_t t265 = gm_l26;
                uint64_t t266 = gm_l27;
                uint8_t t267 = (t265 > t266);
                if (!t267) goto ie264;
                {
                    uint64_t t269 = gm_l26;
                    uint64_t t270 = gm_l27;
                    uint64_t r271; bool o272 = __builtin_sub_overflow(t269, t270, &r271);
                    if (o272) gm_trap("integer overflow in `-`", gm_srchntnl6m6g3am, 123, 45);
                    uint64_t t273 = r271;
                    gm_t4ce0jd3tdro4 t274;
                    GM_ST(uint64_t, ((uint8_t *)&t274 + 0), t273);
                    gm_node *pn275 = gm_find(cx, 1579066750);
                    if (!pn275) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 123, 20);
                    gm_b0_1 t276;
                    t276 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t4ce0jd3tdro4))pn275->clauses[2])(pn275->env, pn275->hctx, pn275, t274);
                    if (gm_unw.kind) {
                        goto gm_ll16;
                    }
                }
                goto ix264;
ie264:;
ix264:;
            }
lc15:;
        }
gm_ll16:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb15; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc15; }
        }
        goto gm_lad;
lb15:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_thread_main_n6r943c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_thread_main_n6r943c F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* arg */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tpmdr2hmdhsqr gm_l4; /* args */
    void * gm_l5; /* def */
    gm_b16_8 gm_l6; /* ret */
    gm_t76123gfet73g gm_l7; /* fl */
    uint8_t gm_l8; /* c */
    void * gm_l9; /* x */
    void * gm_l10; /* t */
    GM_STACK_CHECK(&F, 734u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_b16_8 t2 = gm_l2;
        gm_tpmdr2hmdhsqr t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        uint32_t t5 = gm_l1;
        void * t6 = gm_f_func_ene90ss((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t6;
    }
    {
        void * t7 = gm_l0;
        void * t8 = gm_l0;
        void * t9 = gm_l5;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 64));
        uint64_t t11 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t12 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t7, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t12;
    }
    {
        void * t13 = gm_l0;
        gm_tl874t2atnlh3 t14 = gm_l3;
        uint32_t t15 = gm_l1;
        void * t16 = ((uint8_t *)&gm_l4);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srchntnl6m6g3am, 208, 30);
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t16 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t18 = t17;
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        gm_t5lf9i3n1r3co t20;
        memset(&t20, 0, sizeof t20);
        gm_b16_8 t21 = gm_l6;
        gm_tcfa6l7reoosn t22;
        gm_f_call_function_u7ovth6((void *)&t22, (void *)NULL, cx, NULL, t13, t14, t15, t19, ((void *)0), ((uint32_t)0ULL), &t20, ((uint8_t)0), t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t23 = t22;
        gm_b0_1 t24;
        if (!((((uint8_t *)&t23))[0] != 0)) {
            gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 8));
            t24 = t25;
        } else {
            gm_t76123gfet73g t26 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t23) + 8));
            gm_l7 = t26;
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            if (t29 != ((uint8_t)3ULL)) goto ie28;
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 8));
            gm_l8 = t30;
            {
                void * t33 = gm_l0;
                uint8_t t34;
                if (((((((uint8_t *)t33) + 1656) + 64))[1] != 0)) goto mn36;
                t34 = ((uint8_t)1);
                goto me35;
mn36:;
                t34 = ((uint8_t)0);
                goto me35;
mn37:;
                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 210, 16);
me35:;
                if (!t34) goto ie32;
                {
                    void * t39 = gm_l0;
                    uint8_t t40 = gm_l8;
                    gm_tqs5p4n2tq19v t41;
                    GM_ST(uint8_t, ((uint8_t *)&t41), t40);
                    ((uint8_t *)&t41)[1] = 1;
                    GM_ST(gm_tqs5p4n2tq19v, ((((uint8_t *)t39) + 1656) + 64), t41);
                }
                goto ix32;
ie32:;
ix32:;
            }
            goto ix28;
ie28:;
            void * t43 = gm_l0;
            void * t44 = GM_LD(void *, ((((uint8_t *)t43) + 1656) + 56));
            void * t45 = t44;
            uint8_t t46;
            if ((GM_LD(void *, ((uint8_t *)&t45)) != 0)) goto mn48;
            t46 = ((uint8_t)1);
            goto me47;
mn48:;
            t46 = ((uint8_t)0);
            goto me47;
mn49:;
            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 211, 19);
me47:;
            if (!t46) goto ie42;
            {
                void * t51;
                uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                if (t53 != ((uint8_t)4ULL)) goto ie52;
                void * t54 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                gm_l9 = t54;
                void * t56 = gm_l9;
                void * t57 = t56;
                t51 = t57;
                goto ix52;
ie52:;
                gm_tolm6p7jp1hb3 t59;
                memset((uint8_t *)&t59 + 28, 0, 4);
                gm_b16_8 t60;
                GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s632oftf9nssu));
                GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(42));
                GM_ST(gm_b16_8, (uint8_t *)&t59 + 0, t60);
                gm_tl874t2atnlh3 t61 = gm_l3;
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t59 + 16, t61);
                gm_f_list_j8jr9cr_2((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t59 + 32)), (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tq7s98ihonkju t62 = gm_f_new_jd11mvt((void *)NULL, cx, NULL, &t59, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kujrnb073g1)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tq7s98ihonkju t63 = t62;
                if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srchntnl6m6g3am, 212, 55);
                void * t64 = GM_LD(void *, (((uint8_t *)&t63) + 8));
                void * t65 = t64;
                t51 = t65;
ix52:;
                gm_l10 = t51;
            }
            {
                void * t66 = gm_l0;
                void * t67 = gm_l10;
                GM_ST(void *, ((((uint8_t *)t66) + 1656) + 56), t67);
            }
            goto ix42;
ie42:;
ix42:;
ix28:;
            gm_b0_1 t68 = GM_ZERO(gm_b0_1);
            t24 = t68;
        }
    }
    {
        uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t71 = (t70 > ((uint64_t)0ULL));
        if (!t71) goto ie69;
        {
            gm_b16_8 t73 = gm_l2;
            gm_b16_8 t74 = t73;
            gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
            gm_b0_1 t76 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t75, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k02l1tj96tm)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix69;
ie69:;
ix69:;
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
__attribute__((aligned(4))) void gm_f_thread_op_pfli7b1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_thread_op_pfli7b1 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* vals */
    gm_b16_8 gm_l4; /* res */
    uint64_t gm_l5; /* addr */
    uint32_t gm_l6; /* f */
    gm_b16_8 gm_l7; /* arg */
    uint32_t gm_l8; /* id */
    uint32_t gm_l9; /* c */
    __int128 gm_l10; /* h */
    uint32_t gm_l11; /* t */
    __int128 gm_l12; /* h */
    uint64_t gm_l13; /* addr */
    gm_tpvg3mjn3dp06 gm_l14; /* word */
    unsigned __int128 gm_l15; /* expected */
    __int128 gm_l16; /* timeout */
    gm_tf5srh4mp4b3i gm_l17; /* deadline */
    GM_STACK_CHECK(&F, 4045u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma10; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma10; case ((uint8_t)31ULL): goto ma10; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma10; case ((uint8_t)34ULL): goto ma10; case ((uint8_t)35ULL): goto ma10; case ((uint8_t)36ULL): goto ma10; case ((uint8_t)37ULL): goto ma10; case ((uint8_t)38ULL): goto ma10; case ((uint8_t)39ULL): goto ma10; case ((uint8_t)40ULL): goto ma10; case ((uint8_t)41ULL): goto ma10; case ((uint8_t)42ULL): goto ma10; case ((uint8_t)43ULL): goto ma10; case ((uint8_t)44ULL): goto ma10; case ((uint8_t)45ULL): goto ma10; case ((uint8_t)46ULL): goto ma10; case ((uint8_t)47ULL): goto ma4; case ((uint8_t)48ULL): goto ma5; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma7; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma9; case ((uint8_t)53ULL): goto ma10; case ((uint8_t)54ULL): goto ma10; default: goto ma10; }
ma4:;
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t14 != ((uint8_t)47ULL)) goto mn13;
            {
                void * t16 = gm_l0;
                uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srchntnl6m6g3am, 144, 30);
                gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t17 * 16));
                gm_b16_8 t20 = t19;
                gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
                uint64_t t22 = gm_f_ptr_of_b77b4c9(t16, t21);
                gm_l5 = t22;
            }
            {
                void * t23 = gm_l0;
                uint64_t t24 = gm_l5;
                gm_t9805qtonbsag t25 = gm_f_fn_from_addr_7a6bpqd(t23, t24);
                gm_t9805qtonbsag t26 = t25;
                uint32_t t27;
                if (((((uint8_t *)&t26))[4] != 0)) {
                    uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&t26) + 0));
                    t27 = t28;
                } else {
                    void * t29 = gm_l0;
                    gm_tl874t2atnlh3 t30 = gm_l1;
                    uint64_t t31 = gm_l5;
                    gm_t4ce0jd3tdro4 t32;
                    GM_ST(uint64_t, ((uint8_t *)&t32 + 0), t31);
                    gm_tabb710d9mq2e t33;
                    gm_f_format_uh8vg1k((void *)&t33, (void *)NULL, cx, NULL, t32);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t34 = t33;
                    if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srchntnl6m6g3am, 145, 65);
                    gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
                    gm_b16_8 t36 = t35;
                    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                    gm_t76123gfet73g t38;
                    gm_f_trap_qh84l9d((void *)&t38, (void *)NULL, cx, NULL, t29, t30, t37);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t39;
                    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                    ((uint8_t *)&t39)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t39 + 8), t38);
                    gm_tcfa6l7reoosn t40 = t39;
                    uint8_t t41 = ((((uint8_t *)&t40))[0] != 0);
                    F.ret = t39;
                    goto gm_fret;
                }
                gm_l6 = t27;
            }
            {
                uint64_t t42 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t42 >= (uintptr_t)(t43)) gm_trap_index(t42, t43, gm_srchntnl6m6g3am, 146, 29);
                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t42 * 16) + 8);
                uint64_t t45 = (t44 > ((uint64_t)1ULL) ? t44 : ((uint64_t)1ULL));
                gm_tabb710d9mq2e t46;
                gm_f_make_2ip78bj((void *)&t46, (void *)NULL, cx, NULL, t45, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjs2b0icc8d)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t47 = t46;
                if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 8, gm_srchntnl6m6g3am, 146, 20);
                gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
                gm_l7 = t48;
            }
            {
                uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                void * t50 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                uint64_t t51 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t52 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t52 >= (uintptr_t)(t53)) gm_trap_index(t52, t53, gm_srchntnl6m6g3am, 147, 20);
                uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t52 * 16) + 8);
                uint64_t t55 = (uintptr_t)t54;
                if (t51 > t55 || t55 > (uintptr_t)t49) gm_trap_slice(t51, t55, t49, gm_srchntnl6m6g3am, 147, 13);
                gm_b16_8 t56;
                GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t50 + t51 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55 - t51));
                uint64_t t57 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t57 >= (uintptr_t)(t58)) gm_trap_index(t57, t58, gm_srchntnl6m6g3am, 147, 43);
                gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t57 * 16));
                gm_b16_8 t60 = t59;
                gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                gm_f_copy_from_f7j6p05(t56, t61);
            }
            {
                void * t62 = gm_l0;
                uint64_t t63 = GM_LD(uintptr_t, (((((uint8_t *)t62) + 1656) + 0) + 0) + 8);
                uint32_t t64 = (uint32_t)t63;
                gm_l8 = t64;
            }
            {
                void * t65 = gm_l0;
                gm_t79rbdv4rafe5 t66;
                memset((uint8_t *)&t66 + 4, 0, 4);
                memset((uint8_t *)&t66 + 36, 0, 4);
                uint32_t t67 = gm_l6;
                GM_ST(uint32_t, (uint8_t *)&t66 + 0, t67);
                uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                void * t69 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                uint64_t t70 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t71 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t71 >= (uintptr_t)(t72)) gm_trap_index(t71, t72, gm_srchntnl6m6g3am, 149, 47);
                uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t71 * 16) + 8);
                uint64_t t74 = (uintptr_t)t73;
                if (t70 > t74 || t74 > (uintptr_t)t68) gm_trap_slice(t70, t74, t68, gm_srchntnl6m6g3am, 149, 40);
                gm_b16_8 t75;
                GM_ST(void *, (uint8_t *)&t75, (void *)(((uint8_t *)t69 + t70 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(t74 - t70));
                GM_ST(gm_b16_8, (uint8_t *)&t66 + 8, t75);
                gm_tl874t2atnlh3 t76 = gm_l1;
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t66 + 24, t76);
                gm_th5tat709fgha t77;
                GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t77 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t77 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t77 + 8), t66);
                uint32_t t78 = gm_l8;
                uint32_t t79 = gm_f_new_coro_06nq12a((void *)NULL, cx, NULL, t65, &t77, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t79;
            }
            {
                void * t80 = gm_l0;
                void * t81 = (void *)((((uint8_t *)t80) + 1656) + 0);
                gm_t9jnd88rq7ul8 t82;
                memset((uint8_t *)&t82 + 4, 0, 4);
                memset((uint8_t *)&t82 + 41, 0, 7);
                uint32_t t83 = gm_l9;
                GM_ST(uint32_t, (uint8_t *)&t82 + 0, t83);
                gm_tdkjirf1g83ud t84;
                GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t84 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t84 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t84 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tdkjirf1g83ud, (uint8_t *)&t82 + 8, t84);
                GM_ST(uint8_t, (uint8_t *)&t82 + 40, ((uint8_t)0));
                gm_tftb1odc739t5 t85 = gm_f_push_bn7ud1g(t81, &t82);
                gm_tftb1odc739t5 t86 = t85;
                if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srchntnl6m6g3am, 150, 13);
                gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
            }
            {
                void * t88 = gm_l0;
                gm_b16_8 t89 = gm_l4;
                uint32_t t90 = gm_l8;
                unsigned __int128 t91 = (unsigned __int128)t90;
                gm_f_put_uint_cddin99(t88, t89, t91);
            }
            goto me3;
mn13:;
ma5:;
            uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t93 != ((uint8_t)48ULL)) goto mn92;
            {
                void * t95 = gm_l0;
                void * t96 = gm_l0;
                void * t97 = GM_LD(void *, (((uint8_t *)t96) + 8));
                void * t98 = (void *)(((uint8_t *)t97) + 8);
                gm_t88rcucg1b7uk t99 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t98, ((uint8_t)1), ((uint16_t)64ULL));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t100 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t100 >= (uintptr_t)(t101)) gm_trap_index(t100, t101, gm_srchntnl6m6g3am, 154, 51);
                gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t100 * 16));
                gm_b16_8 t103 = t102;
                gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                unsigned __int128 t105 = gm_f_get_int_pg7p4gc(t95, GM_CONV(gm_csguankko4lmhd, t99), t104);
                unsigned __int128 t106 = t105;
                __int128 t107;
                memset(&t107, 0, sizeof t107); memcpy(&t107, ((uint8_t *)&t106), sizeof t107 < 16 ? sizeof t107 : 16);
                gm_l10 = t107;
            }
            {
                uint8_t t109 = 0;
                uint8_t t110 = 0;
                __int128 t111 = gm_l10;
                uint8_t t112 = (t111 <= ((__int128)(((unsigned __int128)0u << 64) | 0u)));
                if (!t112) {
                    __int128 t113 = gm_l10;
                    void * t114 = gm_l0;
                    uint64_t t115 = GM_LD(uintptr_t, (((((uint8_t *)t114) + 1656) + 0) + 0) + 8);
                    __int128 t116 = (__int128)t115;
                    uint8_t t117 = (t113 >= t116);
                    t110 = t117;
                } else t110 = 1;
                if (!t110) {
                    void * t118 = gm_l0;
                    gm_b16_8 t119 = GM_LD(gm_b16_8, (((((uint8_t *)t118) + 1656) + 0) + 0));
                    gm_b16_8 t120 = t119;
                    __int128 t121 = gm_l10;
                    uint32_t t122 = (uint32_t)t121;
                    uint64_t t123 = (uint64_t)t122;
                    uint64_t t124 = (uintptr_t)t123;
                    gm_b16_8 t125 = t120;
                    uint64_t t126 = GM_LD(uintptr_t, ((uint8_t *)&t125) + 8);
                    if (t124 >= (uintptr_t)(t126)) gm_trap_index(t124, t126, gm_srchntnl6m6g3am, 156, 65);
                    uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t125)) + t124 * 48) + 40));
                    t109 = t127;
                } else t109 = 1;
                if (!t109) goto ie108;
                {
                    void * t129 = gm_l0;
                    gm_b16_8 t130 = gm_l4;
                    unsigned __int128 r131; bool o132 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), ((unsigned __int128)(((unsigned __int128)0u << 64) | 1003u)), &r131);
                    unsigned __int128 t133 = r131;
                    gm_f_put_uint_cddin99(t129, t130, t133);
                }
                {
                    gm_b0_1 t134;
                    gm_tcfa6l7reoosn t135;
                    GM_ST(uint8_t, (uint8_t *)&t135 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t135 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t135 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t135 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t135 + 24, 0);
                    ((uint8_t *)&t135)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t135 + 8), t134);
                    gm_tcfa6l7reoosn t136 = t135;
                    uint8_t t137 = ((((uint8_t *)&t136))[0] != 0);
                    F.ret = t135;
                    goto gm_fret;
                }
                goto ix108;
ie108:;
ix108:;
            }
            {
                __int128 t138 = gm_l10;
                uint32_t t139 = (uint32_t)t138;
                gm_l11 = t139;
            }
            {
                uint32_t t140 = gm_l11;
                uint64_t t141 = (uint64_t)t140;
                uint64_t t142 = (uintptr_t)t141;
                void * t143 = gm_l0;
                uint64_t t144 = GM_LD(uintptr_t, (((((uint8_t *)t143) + 1656) + 0) + 0) + 8);
                if (t142 >= (uintptr_t)(t144)) gm_trap_index(t142, t144, gm_srchntnl6m6g3am, 161, 13);
                GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, (((((uint8_t *)t143) + 1656) + 0) + 0)) + t142 * 48) + 40), ((uint8_t)1));
            }
            {
                uint32_t t146 = gm_l11;
                uint64_t t147 = (uint64_t)t146;
                uint64_t t148 = (uintptr_t)t147;
                void * t149 = gm_l0;
                uint64_t t150 = GM_LD(uintptr_t, (((((uint8_t *)t149) + 1656) + 0) + 0) + 8);
                if (t148 >= (uintptr_t)(t150)) gm_trap_index(t148, t150, gm_srchntnl6m6g3am, 162, 16);
                uint8_t t151 = (!(GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((((uint8_t *)t149) + 1656) + 0) + 0)) + t148 * 48) + 8) + 0) == ((uint8_t)5ULL)));
                if (!t151) goto ie145;
                {
                    void * t153 = gm_l0;
                    uint32_t t154 = GM_LD(uint32_t, ((((uint8_t *)t153) + 1656) + 40));
                    uint64_t t155 = (uint64_t)t154;
                    uint64_t t156 = (uintptr_t)t155;
                    void * t157 = gm_l0;
                    uint64_t t158 = GM_LD(uintptr_t, (((((uint8_t *)t157) + 1656) + 0) + 0) + 8);
                    if (t156 >= (uintptr_t)(t158)) gm_trap_index(t156, t158, gm_srchntnl6m6g3am, 163, 17);
                    uint32_t t159 = gm_l11;
                    gm_tdkjirf1g83ud t160;
                    GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t160 + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&t160 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t160 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)1ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t160 + 8), t159);
                    GM_ST(gm_tdkjirf1g83ud, (((uint8_t *)GM_LD(void *, (((((uint8_t *)t157) + 1656) + 0) + 0)) + t156 * 48) + 8), t160);
                }
                {
                    void * t161 = gm_l0;
                    gm_tl874t2atnlh3 t162 = gm_l1;
                    gm_tcfa6l7reoosn t163;
                    gm_f_switch_thread_m7aoqko((void *)&t163, (void *)NULL, cx, NULL, t161, t162);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t164 = t163;
                    if (((((uint8_t *)&t164))[0] != 0)) {
                        gm_t76123gfet73g t165 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t164) + 8));
                        gm_tcfa6l7reoosn t166;
                        GM_ST(uint8_t, (uint8_t *)&t166 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t166 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t166 + 4, 0);
                        ((uint8_t *)&t166)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t166 + 8), t165);
                        F.ret = t166;
                        goto gm_fret;
                    }
                    gm_b0_1 t167 = GM_LD(gm_b0_1, (((uint8_t *)&t164) + 8));
                }
                goto ix145;
ie145:;
ix145:;
            }
            {
                void * t168 = gm_l0;
                gm_tcfa6l7reoosn t169;
                gm_f_check_fatal_dgfalcm((void *)&t169, t168);
                gm_tcfa6l7reoosn t170 = t169;
                if (((((uint8_t *)&t170))[0] != 0)) {
                    gm_t76123gfet73g t171 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t170) + 8));
                    gm_tcfa6l7reoosn t172;
                    GM_ST(uint8_t, (uint8_t *)&t172 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t172 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t172 + 4, 0);
                    ((uint8_t *)&t172)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t172 + 8), t171);
                    F.ret = t172;
                    goto gm_fret;
                }
                gm_b0_1 t173 = GM_LD(gm_b0_1, (((uint8_t *)&t170) + 8));
            }
            {
                void * t174 = gm_l0;
                gm_b16_8 t175 = gm_l4;
                gm_f_put_uint_cddin99(t174, t175, ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
            }
            goto me3;
mn92:;
ma6:;
            uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t177 != ((uint8_t)49ULL)) goto mn176;
            {
                void * t179 = gm_l0;
                void * t180 = gm_l0;
                void * t181 = GM_LD(void *, (((uint8_t *)t180) + 8));
                void * t182 = (void *)(((uint8_t *)t181) + 8);
                gm_t88rcucg1b7uk t183 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t182, ((uint8_t)1), ((uint16_t)64ULL));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t184 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t185 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t184 >= (uintptr_t)(t185)) gm_trap_index(t184, t185, gm_srchntnl6m6g3am, 170, 51);
                gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t184 * 16));
                gm_b16_8 t187 = t186;
                gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
                unsigned __int128 t189 = gm_f_get_int_pg7p4gc(t179, GM_CONV(gm_csguankko4lmhd, t183), t188);
                unsigned __int128 t190 = t189;
                __int128 t191;
                memset(&t191, 0, sizeof t191); memcpy(&t191, ((uint8_t *)&t190), sizeof t191 < 16 ? sizeof t191 : 16);
                gm_l12 = t191;
            }
            {
                uint8_t t193 = 0;
                uint8_t t194 = 0;
                __int128 t195 = gm_l12;
                uint8_t t196 = (t195 <= ((__int128)(((unsigned __int128)0u << 64) | 0u)));
                if (!t196) {
                    __int128 t197 = gm_l12;
                    void * t198 = gm_l0;
                    uint64_t t199 = GM_LD(uintptr_t, (((((uint8_t *)t198) + 1656) + 0) + 0) + 8);
                    __int128 t200 = (__int128)t199;
                    uint8_t t201 = (t197 >= t200);
                    t194 = t201;
                } else t194 = 1;
                if (!t194) {
                    void * t202 = gm_l0;
                    gm_b16_8 t203 = GM_LD(gm_b16_8, (((((uint8_t *)t202) + 1656) + 0) + 0));
                    gm_b16_8 t204 = t203;
                    __int128 t205 = gm_l12;
                    uint32_t t206 = (uint32_t)t205;
                    uint64_t t207 = (uint64_t)t206;
                    uint64_t t208 = (uintptr_t)t207;
                    gm_b16_8 t209 = t204;
                    uint64_t t210 = GM_LD(uintptr_t, ((uint8_t *)&t209) + 8);
                    if (t208 >= (uintptr_t)(t210)) gm_trap_index(t208, t210, gm_srchntnl6m6g3am, 171, 65);
                    uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t209)) + t208 * 48) + 40));
                    t193 = t211;
                } else t193 = 1;
                if (!t193) goto ie192;
                {
                    void * t213 = gm_l0;
                    gm_b16_8 t214 = gm_l4;
                    unsigned __int128 r215; bool o216 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), ((unsigned __int128)(((unsigned __int128)0u << 64) | 1003u)), &r215);
                    unsigned __int128 t217 = r215;
                    gm_f_put_uint_cddin99(t213, t214, t217);
                }
                {
                    gm_b0_1 t218;
                    gm_tcfa6l7reoosn t219;
                    GM_ST(uint8_t, (uint8_t *)&t219 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t219 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t219 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t219 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t219 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t219 + 24, 0);
                    ((uint8_t *)&t219)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t219 + 8), t218);
                    gm_tcfa6l7reoosn t220 = t219;
                    uint8_t t221 = ((((uint8_t *)&t220))[0] != 0);
                    F.ret = t219;
                    goto gm_fret;
                }
                goto ix192;
ie192:;
ix192:;
            }
            {
                void * t222 = gm_l0;
                gm_b16_8 t223 = GM_LD(gm_b16_8, (((((uint8_t *)t222) + 1656) + 0) + 0));
                gm_b16_8 t224 = t223;
                __int128 t225 = gm_l12;
                uint32_t t226 = (uint32_t)t225;
                uint64_t t227 = (uint64_t)t226;
                uint64_t t228 = (uintptr_t)t227;
                gm_b16_8 t229 = t224;
                uint64_t t230 = GM_LD(uintptr_t, ((uint8_t *)&t229) + 8);
                if (t228 >= (uintptr_t)(t230)) gm_trap_index(t228, t230, gm_srchntnl6m6g3am, 175, 13);
                GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t229)) + t228 * 48) + 40), ((uint8_t)1));
            }
            {
                void * t231 = gm_l0;
                gm_b16_8 t232 = gm_l4;
                gm_f_put_uint_cddin99(t231, t232, ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
            }
            goto me3;
mn176:;
ma7:;
            uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t234 != ((uint8_t)50ULL)) goto mn233;
            void * t235 = gm_l0;
            gm_tl874t2atnlh3 t236 = gm_l1;
            gm_tcfa6l7reoosn t237;
            gm_f_switch_thread_m7aoqko((void *)&t237, (void *)NULL, cx, NULL, t235, t236);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t238 = t237;
            if (((((uint8_t *)&t238))[0] != 0)) {
                gm_t76123gfet73g t239 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t238) + 8));
                gm_tcfa6l7reoosn t240;
                GM_ST(uint8_t, (uint8_t *)&t240 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t240 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t240 + 4, 0);
                ((uint8_t *)&t240)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t240 + 8), t239);
                F.ret = t240;
                goto gm_fret;
            }
            gm_b0_1 t241 = GM_LD(gm_b0_1, (((uint8_t *)&t238) + 8));
            goto me3;
mn233:;
ma8:;
            uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t243 != ((uint8_t)51ULL)) goto mn242;
            {
                void * t245 = gm_l0;
                gm_tcfa6l7reoosn t246;
                gm_f_check_fatal_dgfalcm((void *)&t246, t245);
                gm_tcfa6l7reoosn t247 = t246;
                if (((((uint8_t *)&t247))[0] != 0)) {
                    gm_t76123gfet73g t248 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t247) + 8));
                    gm_tcfa6l7reoosn t249;
                    GM_ST(uint8_t, (uint8_t *)&t249 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t249 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t249 + 4, 0);
                    ((uint8_t *)&t249)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t249 + 8), t248);
                    F.ret = t249;
                    goto gm_fret;
                }
                gm_b0_1 t250 = GM_LD(gm_b0_1, (((uint8_t *)&t247) + 8));
            }
            {
                void * t251 = gm_l0;
                uint64_t t252 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t253 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t252 >= (uintptr_t)(t253)) gm_trap_index(t252, t253, gm_srchntnl6m6g3am, 181, 30);
                gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t252 * 16));
                gm_b16_8 t255 = t254;
                gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&t255));
                uint64_t t257 = gm_f_ptr_of_b77b4c9(t251, t256);
                gm_l13 = t257;
            }
            {
                gm_tpvg3mjn3dp06 t258;
                GM_ST(uint8_t, ((uint8_t *)&t258 + 0), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t258 + 1), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t258 + 2), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t258 + 3), ((uint8_t)0ULL));
                gm_l14 = t258;
            }
            {
                void * t259 = gm_l0;
                gm_tl874t2atnlh3 t260 = gm_l1;
                uint64_t t261 = gm_l13;
                void * t262 = ((uint8_t *)&gm_l14);
                if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srchntnl6m6g3am, 183, 42);
                gm_b16_8 t263;
                GM_ST(void *, (uint8_t *)&t263, (void *)(((uint8_t *)t262 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t263 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                gm_tcfa6l7reoosn t264;
                gm_f_load_at_6u81jcc((void *)&t264, (void *)NULL, cx, NULL, t259, t260, t261, ((uint64_t)4ULL), t263);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t265 = t264;
                if (((((uint8_t *)&t265))[0] != 0)) {
                    gm_t76123gfet73g t266 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t265) + 8));
                    gm_tcfa6l7reoosn t267;
                    GM_ST(uint8_t, (uint8_t *)&t267 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t267 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t267 + 4, 0);
                    ((uint8_t *)&t267)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t267 + 8), t266);
                    F.ret = t267;
                    goto gm_fret;
                }
                gm_b0_1 t268 = GM_LD(gm_b0_1, (((uint8_t *)&t265) + 8));
            }
            {
                void * t269 = gm_l0;
                uint64_t t270 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t271 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t270 >= (uintptr_t)(t271)) gm_trap_index(t270, t271, gm_srchntnl6m6g3am, 184, 36);
                gm_b16_8 t272 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t270 * 16));
                gm_b16_8 t273 = t272;
                gm_b16_8 t274 = GM_LD(gm_b16_8, ((uint8_t *)&t273));
                unsigned __int128 t275 = gm_f_get_uint_bq8c4b7(t269, t274);
                unsigned __int128 t276 = (unsigned __int128)(t275 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 4294967295u)));
                gm_l15 = t276;
            }
            {
                void * t278 = gm_l0;
                void * t279 = ((uint8_t *)&gm_l14);
                if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srchntnl6m6g3am, 185, 27);
                gm_b16_8 t280;
                GM_ST(void *, (uint8_t *)&t280, (void *)(((uint8_t *)t279 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t280 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                gm_b16_8 t281 = t280;
                gm_b16_8 t282 = GM_LD(gm_b16_8, ((uint8_t *)&t281));
                unsigned __int128 t283 = gm_f_get_uint_bq8c4b7(t278, t282);
                unsigned __int128 t284 = gm_l15;
                uint8_t t285 = ((t283 == t284));
                if (!t285) goto ie277;
                {
                    void * t287 = gm_l0;
                    void * t288 = gm_l0;
                    void * t289 = GM_LD(void *, (((uint8_t *)t288) + 8));
                    void * t290 = (void *)(((uint8_t *)t289) + 8);
                    gm_t88rcucg1b7uk t291 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t290, ((uint8_t)1), ((uint16_t)64ULL));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t292 = (uintptr_t)((uint64_t)2ULL);
                    uint64_t t293 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t292 >= (uintptr_t)(t293)) gm_trap_index(t292, t293, gm_srchntnl6m6g3am, 186, 61);
                    gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t292 * 16));
                    gm_b16_8 t295 = t294;
                    gm_b16_8 t296 = GM_LD(gm_b16_8, ((uint8_t *)&t295));
                    unsigned __int128 t297 = gm_f_get_int_pg7p4gc(t287, GM_CONV(gm_csguankko4lmhd, t291), t296);
                    unsigned __int128 t298 = t297;
                    __int128 t299;
                    memset(&t299, 0, sizeof t299); memcpy(&t299, ((uint8_t *)&t298), sizeof t299 < 16 ? sizeof t299 : 16);
                    gm_l16 = t299;
                }
                {
                    gm_tf5srh4mp4b3i t300;
                    memset(&t300, 0, sizeof t300);
                    gm_l17 = t300;
                }
                {
                    __int128 t302 = gm_l16;
                    uint8_t t303 = (t302 >= ((__int128)(((unsigned __int128)0u << 64) | 0u)));
                    if (!t303) goto ie301;
                    {
                        gm_node *pn305 = gm_find(cx, 1579066750);
                        if (!pn305) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 188, 46);
                        gm_t4ce0jd3tdro4 t306;
                        t306 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn305->clauses[0])(pn305->env, pn305->hctx, pn305);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t4ce0jd3tdro4 t307 = t306;
                        uint64_t t308 = GM_LD(uint64_t, (((uint8_t *)&t307) + 0));
                        __int128 t309 = gm_l16;
                        uint64_t t310 = (uint64_t)t309;
                        uint64_t r311; bool o312 = __builtin_add_overflow(t308, t310, &r311);
                        uint64_t t313 = o312 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r311;
                        gm_tf5srh4mp4b3i t314;
                        GM_ST(uint8_t, (uint8_t *)&t314 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t314 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t314 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t314), t313);
                        ((uint8_t *)&t314)[8] = 1;
                        gm_l17 = t314;
                    }
                    goto ix301;
ie301:;
ix301:;
                }
                {
                    void * t315 = gm_l0;
                    uint32_t t316 = GM_LD(uint32_t, ((((uint8_t *)t315) + 1656) + 40));
                    uint64_t t317 = (uint64_t)t316;
                    uint64_t t318 = (uintptr_t)t317;
                    void * t319 = gm_l0;
                    uint64_t t320 = GM_LD(uintptr_t, (((((uint8_t *)t319) + 1656) + 0) + 0) + 8);
                    if (t318 >= (uintptr_t)(t320)) gm_trap_index(t318, t320, gm_srchntnl6m6g3am, 189, 17);
                    gm_tk4qs2jduhfn8 t321;
                    void * t322 = gm_l0;
                    uint64_t t323 = GM_LD(uint64_t, ((((uint8_t *)t322) + 1656) + 48));
                    GM_ST(uint64_t, (uint8_t *)&t321 + 0, t323);
                    gm_tf5srh4mp4b3i t324 = gm_l17;
                    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t321 + 8, t324);
                    gm_tdkjirf1g83ud t325;
                    GM_ST(uint8_t, (uint8_t *)&t325 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t325 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t325 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t325 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_tk4qs2jduhfn8, ((uint8_t *)&t325 + 8), t321);
                    GM_ST(gm_tdkjirf1g83ud, (((uint8_t *)GM_LD(void *, (((((uint8_t *)t319) + 1656) + 0) + 0)) + t318 * 48) + 8), t325);
                }
                {
                    void * t326 = gm_l0;
                    gm_tl874t2atnlh3 t327 = gm_l1;
                    gm_tcfa6l7reoosn t328;
                    gm_f_switch_thread_m7aoqko((void *)&t328, (void *)NULL, cx, NULL, t326, t327);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t329 = t328;
                    if (((((uint8_t *)&t329))[0] != 0)) {
                        gm_t76123gfet73g t330 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t329) + 8));
                        gm_tcfa6l7reoosn t331;
                        GM_ST(uint8_t, (uint8_t *)&t331 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t331 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t331 + 4, 0);
                        ((uint8_t *)&t331)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t331 + 8), t330);
                        F.ret = t331;
                        goto gm_fret;
                    }
                    gm_b0_1 t332 = GM_LD(gm_b0_1, (((uint8_t *)&t329) + 8));
                }
                goto ix277;
ie277:;
ix277:;
            }
            {
                void * t333 = gm_l0;
                gm_b16_8 t334 = gm_l4;
                gm_f_put_uint_cddin99(t333, t334, ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
            }
            goto me3;
mn242:;
ma9:;
            uint8_t t336 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t336 != ((uint8_t)52ULL)) goto mn335;
            {
                void * t338 = gm_l0;
                void * t339 = gm_l0;
                uint64_t t340 = GM_LD(uint64_t, ((((uint8_t *)t339) + 1656) + 48));
                uint64_t r341; bool o342 = __builtin_add_overflow(t340, ((uint64_t)1ULL), &r341);
                if (o342) gm_trap("integer overflow in `+`", gm_srchntnl6m6g3am, 196, 13);
                uint64_t t343 = r341;
                GM_ST(uint64_t, ((((uint8_t *)t338) + 1656) + 48), t343);
            }
            {
                void * t344 = gm_l0;
                gm_b16_8 t345 = gm_l4;
                gm_f_put_uint_cddin99(t344, t345, ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
            }
            goto me3;
mn335:;
ma10:;
            void * t347 = gm_l0;
            gm_tl874t2atnlh3 t348 = gm_l1;
            gm_b16_8 t349;
            GM_ST(void *, (uint8_t *)&t349, (void *)(gm_svu0pqir618v4));
            GM_ST(uintptr_t, (uint8_t *)&t349 + 8, (uintptr_t)(38));
            gm_t76123gfet73g t350;
            gm_f_trap_qh84l9d((void *)&t350, (void *)NULL, cx, NULL, t347, t348, t349);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t351;
            GM_ST(uint8_t, (uint8_t *)&t351 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t351 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t351 + 4, 0);
            ((uint8_t *)&t351)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t351 + 8), t350);
            gm_tcfa6l7reoosn t352 = t351;
            uint8_t t353 = ((((uint8_t *)&t352))[0] != 0);
            F.ret = t351;
            goto gm_fret;
            goto me3;
mn346:;
ma11:;
            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 142, 5);
me3:;
        }
    }
    gm_b0_1 t354;
    gm_tcfa6l7reoosn t355;
    GM_ST(uint8_t, (uint8_t *)&t355 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t355 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t355 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t355 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t355 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t355 + 24, 0);
    ((uint8_t *)&t355)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t355 + 8), t354);
    gm_tcfa6l7reoosn t356 = t355;
    F.ret = t356;
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
inline __attribute__((aligned(4))) void gm_f_threads_kh5dldj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_threads_kh5dldj F;
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_jh4e1ji((void *)&F.l0, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l0);
        gm_t9jnd88rq7ul8 t3;
        memset((uint8_t *)&t3 + 4, 0, 4);
        memset((uint8_t *)&t3 + 41, 0, 7);
        GM_ST(uint32_t, (uint8_t *)&t3 + 0, ((uint32_t)0ULL));
        gm_tdkjirf1g83ud t4;
        GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t4 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t4 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t4 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t4 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tdkjirf1g83ud, (uint8_t *)&t3 + 8, t4);
        GM_ST(uint8_t, (uint8_t *)&t3 + 40, ((uint8_t)0));
        gm_tftb1odc739t5 t5 = gm_f_push_bn7ud1g(t2, &t3);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srchntnl6m6g3am, 54, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        memset((uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 66, 0, 6);
        memcpy((uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 0, ((uint8_t *)&F.l0), 40);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 40, ((uint32_t)0ULL));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 44, ((uint32_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 48, ((uint64_t)0ULL));
        GM_ST(void *, (uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 56, ((void *)0));
        gm_tqs5p4n2tq19v t8;
        memset(&t8, 0, sizeof t8);
        GM_ST(gm_tqs5p4n2tq19v, (uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 64, t8);
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
inline __attribute__((aligned(4))) void gm_f_tick_6bsfu3n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_tick_6bsfu3n F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* th */
    GM_STACK_CHECK(&F, 444u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            void * t4 = (void *)(((uint8_t *)t3) + 1656);
            gm_l2 = t4;
        }
        {
            void * t6 = gm_l2;
            uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 0) + 0) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)1ULL)));
            if (!t8) goto ie5;
            {
                gm_b0_1 t10;
                gm_tcfa6l7reoosn t11;
                GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t11 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t11 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t11 + 24, 0);
                ((uint8_t *)&t11)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t11 + 8), t10);
                gm_tcfa6l7reoosn t12 = t11;
                uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
                F.ret = t11;
                goto gm_fret;
            }
            goto ix5;
ie5:;
ix5:;
        }
        {
            void * t14 = gm_l2;
            void * t15 = gm_l2;
            uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)t15) + 44));
            uint32_t r17; bool o18 = __builtin_add_overflow(t16, ((uint32_t)1ULL), &r17);
            if (o18) gm_trap("integer overflow in `+`", gm_srchntnl6m6g3am, 62, 5);
            uint32_t t19 = r17;
            GM_ST(uint32_t, (((uint8_t *)t14) + 44), t19);
        }
        {
            void * t21 = gm_l2;
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)t21) + 44));
            uint32_t t23 = GM_LD(uint32_t, gm_cusfosftpfn);
            uint8_t t24 = (t22 < t23);
            if (!t24) goto ie20;
            {
                gm_b0_1 t26;
                gm_tcfa6l7reoosn t27;
                GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t27 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t27 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t27 + 24, 0);
                ((uint8_t *)&t27)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t27 + 8), t26);
                gm_tcfa6l7reoosn t28 = t27;
                uint8_t t29 = ((((uint8_t *)&t28))[0] != 0);
                F.ret = t27;
                goto gm_fret;
            }
            goto ix20;
ie20:;
ix20:;
        }
        {
            void * t30 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)t30) + 44), ((uint32_t)0ULL));
        }
        {
            void * t31 = gm_l0;
            gm_tl874t2atnlh3 t32 = gm_l1;
            gm_tcfa6l7reoosn t33;
            gm_f_switch_thread_m7aoqko((void *)&t33, (void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t34 = t33;
            if (((((uint8_t *)&t34))[0] != 0)) {
                gm_t76123gfet73g t35 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t34) + 8));
                gm_tcfa6l7reoosn t36;
                GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
                ((uint8_t *)&t36)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t36 + 8), t35);
                F.ret = t36;
                goto gm_fret;
            }
            gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 8));
        }
    }
    gm_b0_1 t38;
    gm_tcfa6l7reoosn t39;
    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t39 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t39 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t39 + 24, 0);
    ((uint8_t *)&t39)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t39 + 8), t38);
    gm_tcfa6l7reoosn t40 = t39;
    F.ret = t40;
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
