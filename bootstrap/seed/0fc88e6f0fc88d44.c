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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; void * f1; } gm_t22qobfhrnoif;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
_Static_assert(sizeof(gm_t22qobfhrnoif) == 16 && _Alignof(gm_t22qobfhrnoif) == 8 && offsetof(gm_t22qobfhrnoif, f0) == 0 && offsetof(gm_t22qobfhrnoif, f1) == 8, "layout of `(i: usize, x: ^Pat)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
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
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
typedef struct { uint8_t f0; union { gm_ts6oqppjh0ba3 v0; } f1; } gm_tjsp7bhdn2e8j;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tjsp7bhdn2e8j) == 3 && _Alignof(gm_tjsp7bhdn2e8j) == 1 && offsetof(gm_tjsp7bhdn2e8j, f0) == 0 && offsetof(gm_tjsp7bhdn2e8j, f1) == 1, "layout of `AssignOp`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_t930q5l38hdap f0; uint32_t f1; uint8_t pe[4]; } gm_tfj8gdjm726n4;
_Static_assert(sizeof(gm_tfj8gdjm726n4) == 216 && _Alignof(gm_tfj8gdjm726n4) == 8 && offsetof(gm_tfj8gdjm726n4, f0) == 0 && offsetof(gm_tfj8gdjm726n4, f1) == 208, "layout of `(binding: LocalBinding, depth: u32)`");
typedef struct { gm_tfj8gdjm726n4 f0; uint8_t f1; uint8_t pe[7]; } gm_tsf6kp4ohnuqs;
_Static_assert(sizeof(gm_tsf6kp4ohnuqs) == 224 && _Alignof(gm_tsf6kp4ohnuqs) == 8 && offsetof(gm_tsf6kp4ohnuqs, f0) == 0 && offsetof(gm_tsf6kp4ohnuqs, f1) == 216, "layout of `?(binding: LocalBinding, depth: u32)`");
struct gm_fr_assign_a0848vq {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* stmts */
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_assign_place_aq16ukh {
    gm_frame h;
    void * l1; /* place */
    gm_tl874t2atnlh3 l5; /* span */
    gm_t88rcucg1b7uk l9; /* pty */
    uint32_t l10; /* tmp */
    gm_trqfvmjtt4sij l14; /* stmts */
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_assign_tuple_m023dpj {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* targets */
    gm_trqfvmjtt4sij l14; /* ts */
    gm_trqfvmjtt4sij l24; /* ts */
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_assignable_name_73f68mv {
    gm_frame h;
    gm_tfj8gdjm726n4 l4; /* found */
    uint8_t ret;
};
struct gm_fr_block_of_2gjl2i1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_compound_op_krvsbol {
    gm_frame h;
    void * ret;
};
struct gm_fr_copy_expr_nebhehp {
    gm_frame h;
    void * ret;
};
struct gm_fr_depth_fr7pn6n {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_deref_86inrqe {
    gm_frame h;
    void * ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo {
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
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_simple_place_6fb9rhd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pmp06kq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_numeric_in5abdm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_one_expr_q6kn0hn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_b16_8 ret;
};
struct gm_fr_place_mutability_0tlm4g8 {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_require_mutable_place_semqr6v {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_fsnl6fa {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_tuple_l3i6trt {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ohk62mc {
    gm_frame h;
    void * ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_arith_op_hsstk43(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_assign_a0848vq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tjsp7bhdn2e8j a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_assign_place_aq16ukh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tjsp7bhdn2e8j a3, void * a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_assign_tuple_m023dpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tjsp7bhdn2e8j a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_assignable_name_73f68mv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_binary_op_7ocdl4v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_compound_op_krvsbol(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_copy_expr_nebhehp(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint32_t gm_f_depth_fr7pn6n(void * a0);
__attribute__((aligned(4))) void * gm_f_deref_86inrqe(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) void gm_f_format_2snkf9s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ivf9m1s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void * gm_f_index_value_ufcur8j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_simple_place_6fb9rhd(void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_local_at_p388mm4(void *gm_out, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_lookup_local_at_ntm4scs(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_lossless_a54o69s(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_numeric_in5abdm(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_expr_q6kn0hn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_place_mutability_0tlm4g8(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_require_mutable_place_semqr6v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_soa_deref_uiefo9t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_soa_of_jufdcj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_soa_store_d7ivlu4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, void * a4, gm_tl874t2atnlh3 a5);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_tuple_l3i6trt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_ksdcjqjfqsj[40] __attribute__((aligned(8)));
extern const uint8_t gm_s5trirh6cc9u8[86];
extern const uint8_t gm_sa91t1vl6p044[34];
extern const uint8_t gm_sckv4ch46j06c[36];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sj7i3vhsq2ifl[38];
extern const uint8_t gm_sog4l3h5q9oa6[84];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcg82mujgottmf[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_block_of_2gjl2i1 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* stmts */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 109u);
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
        gm_b16_8 t5 = gm_l1;
        gm_tl874t2atnlh3 t6 = gm_l2;
        void * t7 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t4, t5, ((void *)0), t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) uint32_t gm_f_depth_fr7pn6n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_depth_fr7pn6n F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK_SHALLOW(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)t1) + 576) + 0) + 8);
    uint32_t t3 = (uint32_t)t2;
    F.ret = t3;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8q3kspo F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 4)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_89glhjr F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        gm_tl874t2atnlh3 t5 = gm_l1;
        gm_b16_8 t6 = gm_l2;
        gm_b0_1 t7 = gm_f_report_eardf07((void *)NULL, cx, NULL, t4, t5, t6);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_9huj44a F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* msg */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_tl874t2atnlh3 t4 = gm_l1;
        gm_b16_8 t5 = gm_l2;
        gm_b0_1 t6 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t3, t4, t5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)26ULL)));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_8d4f9ek F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 142u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t10 != ((uint8_t)0ULL)) goto pa9;
    goto po8;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t12 != ((uint8_t)1ULL)) goto pa11;
    goto po8;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t14 != ((uint8_t)2ULL)) goto pa13;
    goto po8;
pa13:;
    goto mn7;
po8:;
    t5 = ((uint8_t)1);
    goto me6;
mn7:;
    t5 = ((uint8_t)0);
    goto me6;
mn15:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_jcnt4hm F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
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
    gm_t53at80pfdus3 t6 = GM_LD(gm_t53at80pfdus3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 56));
    F.ret = t6;
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
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nof7vfj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_rcd2jln();
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
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pmp06kq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8q3kspo();
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_g3tddrb_2();
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
    GM_STACK_CHECK(&F, 176u);
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
    gm_tq7s98ihonkju t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t6 = GM_LD(void *, (((uint8_t *)&t5) + 8));
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
inline __attribute__((aligned(4))) uint8_t gm_f_numeric_in5abdm(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_numeric_in5abdm F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 186u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_integer_8d4f9ek(t4, GM_CONV(gm_csguankko4lmhd, t5));
    if (!t6) {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        gm_t88rcucg1b7uk t10 = gm_l1;
        gm_t53at80pfdus3 t11;
        gm_f_kind_jcnt4hm((void *)&t11, t9, GM_CONV(gm_csguankko4lmhd, t10));
        gm_t53at80pfdus3 t12 = t11;
        uint8_t t13;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t16 != ((uint8_t)3ULL)) goto mn15;
        t13 = ((uint8_t)1);
        goto me14;
mn15:;
        t13 = ((uint8_t)0);
        goto me14;
mn17:;
        gm_trap("no match arm matched the scrutinee", gm_srcg82mujgottmf, 135, 70);
me14:;
        t1 = t13;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_expr_q6kn0hn(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_one_expr_q6kn0hn F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_vp1djv1_2((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        void * t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_push_d943vgp_2(t2, &(*(gm_t69m56meqefs8 *)((uint8_t *)t3)));
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcg82mujgottmf, 140, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
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
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_printer_h7jigd7 F;
    void * gm_l0; /* t */
    void * gm_l1; /* out */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    GM_ST(void *, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 0, t1);
    void * t2 = gm_l1;
    GM_ST(void *, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 8, t2);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 16, ((uint32_t)0ULL));
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 20, ((uint32_t)0ULL));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_ptr_t1h2m2d F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)8ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_report_eardf07 F;
    void * gm_l0; /* ds */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_b16_8 t4 = gm_l2;
        gm_t4gut2c9fj1kt t5;
        gm_f_error_s6hn871((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_push_o4ckdr1(t2, &t5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_8uijkbs F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_fsnl6fa F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_kmjeh8s F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 145u);
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
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_f_printer_h7jigd7((void *)&gm_l3, t2, t3);
    }
    {
        void * t4 = (void *)((uint8_t *)&gm_l3);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_b0_1 t6 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        uint8_t t10 = 0;
        F.ret = t9;
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
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_temp_ilbvj8q F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shv1k5m05egbe));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = gm_l2;
    uint32_t t5 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t1, t2, t3, ((uint8_t)1), t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_tuple_l3i6trt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_tuple_l3i6trt F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* elems */
    GM_STACK_CHECK(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t5 = t4;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = t3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t5);
    gm_b16_8 t6 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t8);
    gm_t53at80pfdus3 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t9 + 8), t2);
    gm_t88rcucg1b7uk t10 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t10;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_unit_ohk62mc F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_f_empty_slice_g3tddrb_2();
    gm_teti411eujvf4 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_tl874t2atnlh3 t6 = gm_l1;
    void * t7 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t3, &t5, t6);
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
__attribute__((aligned(4))) void gm_f_assign_a0848vq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tjsp7bhdn2e8j a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_assign_a0848vq F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* places */
    gm_tjsp7bhdn2e8j gm_l2; /* op */
    void * gm_l3; /* value */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* place_ast */
    void * gm_l6; /* v */
    void * gm_l8; /* base */
    void * gm_l9; /* index */
    void * gm_l10; /* b */
    gm_tjpqvfd72o62u gm_l11; /* info */
    GM_STACK_CHECK(&F, 977u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = (!(t3 == ((uint64_t)1ULL)));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            gm_b16_8 t7 = gm_l1;
            gm_tjsp7bhdn2e8j t8 = gm_l2;
            void * t9 = gm_l3;
            gm_tl874t2atnlh3 t10 = gm_l4;
            gm_f_assign_tuple_m023dpj((void *)&(*(gm_trcmmd12cefb1 *)gm_out), (void *)NULL, cx, NULL, t6, t7, t8, t9, t10);
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
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t11 >= (uintptr_t)(t12)) gm_trap_index(t11, t12, gm_srcg82mujgottmf, 53, 19);
        void * t13 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t11 * 88);
        gm_l5 = t13;
    }
    {
        void * t15 = gm_l5;
        uint8_t t16 = ((GM_LD(uint8_t, (((uint8_t *)t15) + 8) + 0) == ((uint8_t)7ULL)));
        if (!t16) goto ie14;
        {
            uint8_t t19 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
            if (!t19) goto ie18;
            {
                void * t21 = gm_l0;
                gm_tl874t2atnlh3 t22 = gm_l4;
                gm_b16_8 t23;
                GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sa91t1vl6p044));
                GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(33));
                gm_b0_1 t24 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t21, t22, t23);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix18;
ie18:;
ix18:;
        }
        {
            void * t25 = gm_l0;
            void * t26 = gm_l3;
            gm_tkn42u7na3t81 t27;
            memset(&t27, 0, sizeof t27);
            void * t28 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t25, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t28;
        }
        {
            gm_f_list_ir0h3tc_2((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t29 = (void *)((uint8_t *)&F.l7);
            void * t30 = gm_l6;
            gm_trcmmd12cefb1 t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t31 + 8), t30);
            gm_tftb1odc739t5 t32 = gm_f_push_d6c76ud_2(t29, &t31);
            gm_tftb1odc739t5 t33 = t32;
            if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcg82mujgottmf, 58, 9);
            gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
        }
        {
            void * t35 = gm_l0;
            void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_tl874t2atnlh3 t38 = gm_l4;
            void * t39 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t36, t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trcmmd12cefb1 t40;
            GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t40 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t40 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t40 + 8), t39);
            uint8_t t41 = 0;
            F.ret = t40;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t43 = gm_l0;
        void * t44 = gm_l5;
        uint8_t t45 = gm_f_assignable_name_73f68mv((void *)NULL, cx, NULL, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t46 = (uint8_t)!t45;
        if (!t46) goto ie42;
        {
            {
                void * t49 = gm_l0;
                void * t50 = gm_l3;
                gm_tkn42u7na3t81 t51;
                memset(&t51, 0, sizeof t51);
                void * t52 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t49, t50, t51);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t53 = gm_l0;
            void * t54 = GM_LD(void *, (((uint8_t *)t53) + 0));
            gm_tl874t2atnlh3 t55 = gm_l4;
            void * t56 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trcmmd12cefb1 t57;
            GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t57 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t57 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t57 + 8), t56);
            uint8_t t58 = 0;
            F.ret = t57;
            goto gm_fret;
        }
        goto ix42;
ie42:;
ix42:;
    }
    {
        void * t60 = gm_l5;
        uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
        if (t61 != ((uint8_t)20ULL)) goto ie59;
        void * t62 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 0));
        gm_l8 = t62;
        void * t63 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
        gm_l9 = t63;
        {
            void * t65 = gm_l0;
            void * t66 = gm_l0;
            void * t67 = gm_l8;
            gm_tkn42u7na3t81 t68;
            memset(&t68, 0, sizeof t68);
            void * t69 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t66, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t70 = gm_f_soa_deref_uiefo9t((void *)NULL, cx, NULL, t65, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t70;
        }
        {
            void * t72 = gm_l0;
            void * t73 = gm_l10;
            gm_t8cc1mtbrhlpt t74;
            gm_f_soa_of_jufdcj2((void *)&t74, (void *)NULL, cx, NULL, t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t8cc1mtbrhlpt t75 = t74;
            if (!((((uint8_t *)&t75))[32] != 0)) goto ie71;
            gm_tjpqvfd72o62u t76 = GM_LD(gm_tjpqvfd72o62u, ((uint8_t *)&t75));
            gm_l11 = t76;
            {
                uint8_t t79 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
                if (!t79) goto ie78;
                {
                    void * t81 = gm_l0;
                    gm_tl874t2atnlh3 t82 = gm_l4;
                    gm_b16_8 t83;
                    GM_ST(void *, (uint8_t *)&t83, (void *)(gm_s5trirh6cc9u8));
                    GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(85));
                    gm_b0_1 t84 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t81, t82, t83);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix78;
ie78:;
ix78:;
            }
            {
                void * t85 = gm_l0;
                void * t86 = gm_l10;
                void * t87 = gm_l9;
                void * t88 = gm_l3;
                gm_tl874t2atnlh3 t89 = gm_l4;
                gm_f_soa_store_d7ivlu4((void *)&(*(gm_trcmmd12cefb1 *)gm_out), (void *)NULL, cx, NULL, t85, t86, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l11)), t87, t88, t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix71;
ie71:;
ix71:;
        }
        {
            void * t90 = gm_l0;
            void * t91 = gm_l0;
            void * t92 = gm_l10;
            void * t93 = gm_l9;
            void * t94 = gm_l5;
            gm_tl874t2atnlh3 t95 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t94) + 72));
            void * t96 = gm_f_index_value_ufcur8j((void *)NULL, cx, NULL, t91, t92, t93, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t97 = gm_l5;
            gm_tjsp7bhdn2e8j t98 = gm_l2;
            void * t99 = gm_l3;
            gm_tl874t2atnlh3 t100 = gm_l4;
            gm_f_assign_place_aq16ukh((void *)&(*(gm_trcmmd12cefb1 *)gm_out), (void *)NULL, cx, NULL, t90, t96, t97, t98, t99, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        void * t101 = gm_l0;
        void * t102 = gm_l0;
        void * t103 = gm_l5;
        gm_tkn42u7na3t81 t104;
        memset(&t104, 0, sizeof t104);
        void * t105 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t102, t103, t104);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t106 = gm_l5;
        gm_tjsp7bhdn2e8j t107 = gm_l2;
        void * t108 = gm_l3;
        gm_tl874t2atnlh3 t109 = gm_l4;
        gm_f_assign_place_aq16ukh((void *)&(*(gm_trcmmd12cefb1 *)gm_out), (void *)NULL, cx, NULL, t101, t105, t106, t107, t108, t109);
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
__attribute__((aligned(4))) void gm_f_assign_place_aq16ukh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tjsp7bhdn2e8j a3, void * a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_assign_place_aq16ukh F;
    void * gm_l0; /* cx */
    void * gm_l2; /* place_ast */
    gm_tjsp7bhdn2e8j gm_l3; /* op */
    void * gm_l4; /* value */
    void * gm_l6; /* v */
    gm_ts6oqppjh0ba3 gm_l7; /* bop */
    void * gm_l8; /* rhs */
    void * gm_l11; /* addr */
    gm_closure gm_l12; /* deref */
    void * gm_l13; /* rhs */
    GM_STACK_CHECK(&F, 1033u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    F.l5 = a5;
    {
        void * t2 = gm_l0;
        void * t3 = F.l1;
        void * t4 = gm_l2;
        gm_tl874t2atnlh3 t5 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t4) + 72));
        gm_b0_1 t6 = gm_f_require_mutable_place_semqr6v((void *)NULL, cx, NULL, t2, t3, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (t9 != ((uint8_t)0ULL)) goto mn8;
        {
            void * t11 = gm_l0;
            void * t12 = gm_l4;
            void * t13 = F.l1;
            gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 0));
            gm_tkn42u7na3t81 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[4] = 1;
            void * t16 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t11, t12, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t16;
        }
        {
            void * t17 = F.l1;
            void * t18 = gm_l0;
            void * t19 = gm_l6;
            void * t20 = F.l1;
            gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t20) + 0));
            void * t22 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t18, t19, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t3mf58gv9kf3n t23;
            GM_ST(void *, ((uint8_t *)&t23 + 0), t17);
            GM_ST(void *, ((uint8_t *)&t23 + 8), t22);
            gm_trcmmd12cefb1 t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t24 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t24 + 8), t23);
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto me7;
mn8:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (t27 != ((uint8_t)1ULL)) goto mn26;
        gm_ts6oqppjh0ba3 t28 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l3) + 1));
        gm_l7 = t28;
        {
            void * t31 = F.l1;
            uint8_t t32 = gm_f_is_simple_place_6fb9rhd(t31);
            if (!t32) goto ie30;
            {
                void * t34 = gm_l0;
                gm_ts6oqppjh0ba3 t35 = gm_l7;
                void * t36 = F.l1;
                void * t37 = gm_f_copy_expr_nebhehp((void *)NULL, cx, NULL, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t38 = gm_l4;
                gm_tl874t2atnlh3 t39 = F.l5;
                void * t40 = gm_f_compound_op_krvsbol((void *)NULL, cx, NULL, t34, t35, t37, t38, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t40;
            }
            {
                void * t41 = F.l1;
                void * t42 = gm_l0;
                void * t43 = gm_l8;
                void * t44 = F.l1;
                gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t44) + 0));
                void * t46 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t42, t43, t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t3mf58gv9kf3n t47;
                GM_ST(void *, ((uint8_t *)&t47 + 0), t41);
                GM_ST(void *, ((uint8_t *)&t47 + 8), t46);
                gm_trcmmd12cefb1 t48;
                GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t48 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t48 + 8), t47);
                uint8_t t49 = 0;
                F.ret = t48;
                goto gm_fret;
            }
            goto ix30;
ie30:;
ix30:;
        }
        {
            void * t50 = gm_l0;
            void * t51 = GM_LD(void *, (((uint8_t *)t50) + 0));
            void * t52 = (void *)(((uint8_t *)t51) + 8);
            void * t53 = F.l1;
            gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t53) + 0));
            gm_t88rcucg1b7uk t55 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t52, ((uint8_t)0), t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l9 = t55;
        }
        {
            void * t56 = gm_l0;
            gm_t88rcucg1b7uk t57 = F.l9;
            gm_tl874t2atnlh3 t58 = F.l5;
            uint32_t t59 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t56, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l10 = t59;
        }
        {
            gm_t88rcucg1b7uk t60 = F.l9;
            void * t61 = F.l1;
            gm_teti411eujvf4 t62;
            memset(&t62, 0, sizeof t62);
            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)24ULL));
            GM_ST(void *, ((uint8_t *)&t62 + 8), t61);
            gm_tl874t2atnlh3 t63 = F.l5;
            void * t64 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t60, &t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t64;
        }
        {
            gm_closure t65 = ((gm_closure){(void *)gm_f_deref_86inrqe, (void *)&F.h, cx});
            gm_l12 = t65;
        }
        {
            void * t66 = gm_l0;
            gm_ts6oqppjh0ba3 t67 = gm_l7;
            gm_closure t68 = gm_l12;
            gm_closure t69 = t68;
            gm_node *ccx70 = t69.cx;
            gm_node cn71; { gm_node *f = gm_find(cx, 587034433); if (f) { cn71 = *f; cn71.next = ccx70; ccx70 = &cn71; } }
            void * t72 = t69.code;
            void * t73;
            if (GM_PTR_TAGGED(t72)) {
                t73 = ((void * (*)(void))GM_PTR_UNTAG(t72))();
            } else {
                t73 = ((void * (*)(void *, gm_node *, gm_node *))t72)(t69.env, ccx70, NULL);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t74 = gm_l4;
            gm_tl874t2atnlh3 t75 = F.l5;
            void * t76 = gm_f_compound_op_krvsbol((void *)NULL, cx, NULL, t66, t67, t73, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t76;
        }
        {
            void * t77 = gm_l0;
            void * t78 = gm_l13;
            void * t79 = F.l1;
            gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t79) + 0));
            void * t81 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t77, t78, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t81;
        }
        {
            gm_f_list_ir0h3tc_2((void *)&F.l14, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t82 = (void *)((uint8_t *)&F.l14);
            uint32_t t83 = F.l10;
            void * t84 = gm_l11;
            gm_ttggq7ng4kp3p t85;
            GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t85 + 0), t83);
            GM_ST(void *, ((uint8_t *)&t85 + 8), t84);
            gm_trcmmd12cefb1 t86;
            GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t86 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)0ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t86 + 8), t85);
            gm_tftb1odc739t5 t87 = gm_f_push_d6c76ud_2(t82, &t86);
            gm_tftb1odc739t5 t88 = t87;
            if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 1, gm_srcg82mujgottmf, 100, 13);
            gm_b0_1 t89 = GM_LD(gm_b0_1, (((uint8_t *)&t88) + 1));
        }
        {
            void * t90 = (void *)((uint8_t *)&F.l14);
            gm_closure t91 = gm_l12;
            gm_closure t92 = t91;
            gm_node *ccx93 = t92.cx;
            gm_node cn94; { gm_node *f = gm_find(cx, 587034433); if (f) { cn94 = *f; cn94.next = ccx93; ccx93 = &cn94; } }
            void * t95 = t92.code;
            void * t96;
            if (GM_PTR_TAGGED(t95)) {
                t96 = ((void * (*)(void))GM_PTR_UNTAG(t95))();
            } else {
                t96 = ((void * (*)(void *, gm_node *, gm_node *))t95)(t92.env, ccx93, NULL);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t97 = gm_l13;
            gm_t3mf58gv9kf3n t98;
            GM_ST(void *, ((uint8_t *)&t98 + 0), t96);
            GM_ST(void *, ((uint8_t *)&t98 + 8), t97);
            gm_trcmmd12cefb1 t99;
            GM_ST(uint8_t, (uint8_t *)&t99 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t99 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t99 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t99 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t99 + 8), t98);
            gm_tftb1odc739t5 t100 = gm_f_push_d6c76ud_2(t90, &t99);
            gm_tftb1odc739t5 t101 = t100;
            if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 1, gm_srcg82mujgottmf, 101, 13);
            gm_b0_1 t102 = GM_LD(gm_b0_1, (((uint8_t *)&t101) + 1));
        }
        {
            void * t103 = gm_l0;
            void * t104 = GM_LD(void *, (((uint8_t *)t103) + 0));
            gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
            gm_tl874t2atnlh3 t106 = F.l5;
            void * t107 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t104, t105, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trcmmd12cefb1 t108;
            GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t108 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t108 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t108 + 8), t107);
            uint8_t t109 = 0;
            F.ret = t108;
            goto gm_fret;
        }
        goto me7;
mn26:;
        gm_trap("no match arm matched the scrutinee", gm_srcg82mujgottmf, 82, 5);
me7:;
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
__attribute__((aligned(4))) void gm_f_assign_tuple_m023dpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tjsp7bhdn2e8j a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_assign_tuple_m023dpj F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* places */
    gm_tjsp7bhdn2e8j gm_l2; /* op */
    void * gm_l3; /* value */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    uint8_t gm_l7; /* all_known */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    void * gm_l11; /* p */
    void * gm_l12; /* place */
    gm_tkn42u7na3t81 gm_l13; /* expect */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    void * gm_l17; /* t */
    void * gm_l18; /* x */
    void * gm_l19; /* v */
    gm_t88rcucg1b7uk gm_l20; /* vt */
    gm_b16_8 gm_l21; /* elems */
    gm_b16_8 gm_l22; /* es */
    uint64_t gm_l23; /* n */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* i */
    void * gm_l28; /* t */
    void * gm_l29; /* x */
    GM_STACK_CHECK(&F, 1933u);
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
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        uint8_t t6 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
        if (!t6) goto ie5;
        {
            void * t8 = gm_l0;
            gm_tl874t2atnlh3 t9 = gm_l4;
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sj7i3vhsq2ifl));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(37));
            gm_b0_1 t11 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t8, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_f_list_pmp06kq((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l7 = ((uint8_t)1);
    }
    {
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l8 = t13;
        }
        {
            for (;;) {
                uint64_t t15 = gm_l9;
                uint64_t t16 = gm_l8;
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l9;
                    uint64_t t21 = t20;
                    uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)&t21));
                    gm_l10 = t24;
                    {
                        uint64_t t26 = gm_l10;
                        uint64_t t27 = (uintptr_t)t26;
                        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_srcg82mujgottmf, 11, 15);
                        void * t29 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t27 * 88);
                        gm_l11 = t29;
                    }
                    {
                        void * t31 = gm_l11;
                        uint8_t t32 = ((GM_LD(uint8_t, (((uint8_t *)t31) + 8) + 0) == ((uint8_t)7ULL)));
                        if (!t32) goto ie30;
                        {
                            void * t34 = (void *)((uint8_t *)&F.l6);
                            gm_tftb1odc739t5 t35 = gm_f_push_r2k6rjf(t34, ((void *)0));
                            gm_tftb1odc739t5 t36 = t35;
                            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srcg82mujgottmf, 13, 13);
                            gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
                        }
                        {
                            gm_l7 = ((uint8_t)0);
                        }
                        {
                            goto lc14;
                        }
                        goto ix30;
ie30:;
ix30:;
                    }
                    {
                        void * t39 = gm_l0;
                        void * t40 = gm_l11;
                        uint8_t t41 = gm_f_assignable_name_73f68mv((void *)NULL, cx, NULL, t39, t40);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                        uint8_t t42 = (uint8_t)!t41;
                        if (!t42) goto ie38;
                        {
                            {
                                void * t45 = gm_l0;
                                void * t46 = gm_l3;
                                gm_tkn42u7na3t81 t47;
                                memset(&t47, 0, sizeof t47);
                                void * t48 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t45, t46, t47);
                                if (gm_unw.kind) {
                                    goto gm_ll18;
                                }
                            }
                        }
                        {
                            void * t49 = gm_l0;
                            void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
                            gm_tl874t2atnlh3 t51 = gm_l4;
                            void * t52 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t50, t51);
                            if (gm_unw.kind) {
                                goto gm_ll18;
                            }
                            gm_trcmmd12cefb1 t53;
                            GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t53 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t53 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)4ULL));
                            GM_ST(void *, ((uint8_t *)&t53 + 8), t52);
                            uint8_t t54 = 0;
                            F.ret = t53;
                            goto gm_fret;
                        }
                        goto ix38;
ie38:;
ix38:;
                    }
                    {
                        void * t55 = gm_l0;
                        void * t56 = gm_l11;
                        gm_tkn42u7na3t81 t57;
                        memset(&t57, 0, sizeof t57);
                        void * t58 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t55, t56, t57);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                        gm_l12 = t58;
                    }
                    {
                        void * t59 = gm_l0;
                        void * t60 = gm_l12;
                        void * t61 = gm_l11;
                        gm_tl874t2atnlh3 t62 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t61) + 72));
                        gm_b0_1 t63 = gm_f_require_mutable_place_semqr6v((void *)NULL, cx, NULL, t59, t60, t62);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    {
                        void * t64 = (void *)((uint8_t *)&F.l6);
                        void * t65 = gm_l12;
                        gm_tftb1odc739t5 t66 = gm_f_push_r2k6rjf(t64, t65);
                        gm_tftb1odc739t5 t67 = t66;
                        if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 1, gm_srcg82mujgottmf, 23, 9);
                        gm_b0_1 t68 = GM_LD(gm_b0_1, (((uint8_t *)&t67) + 1));
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg82mujgottmf, 10, 5);
me22:;
                }
lc14:;
                {
                    uint64_t t70 = gm_l9;
                    uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                    uint64_t t73 = r71;
                    gm_l9 = t73;
                }
            }
gm_ll18:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb14; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc14; }
            }
            goto gm_lad;
lb14:;
        }
    }
    {
        gm_tkn42u7na3t81 t74;
        memset(&t74, 0, sizeof t74);
        gm_l13 = t74;
    }
    {
        uint8_t t76 = gm_l7;
        if (!t76) goto ie75;
        {
            gm_f_list_nof7vfj((void *)&F.l14, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                gm_l15 = t79;
            }
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t81 = gm_l16;
                    uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t83 = (t81 < t82);
                    if (!t83) goto lb80;
                    {
                        uint64_t t86 = gm_l16;
                        uint64_t t87 = (uintptr_t)t86;
                        uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        void * t89 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t87 * 8));
                        void * t90 = t89;
                        void * t93 = GM_LD(void *, ((uint8_t *)&t90));
                        gm_l17 = t93;
                        {
                            void * t96 = gm_l17;
                            void * t97 = t96;
                            if (!(GM_LD(void *, ((uint8_t *)&t97)) != 0)) goto ie95;
                            void * t98 = GM_LD(void *, ((uint8_t *)&t97));
                            gm_l18 = t98;
                            {
                                void * t100 = (void *)((uint8_t *)&F.l14);
                                void * t101 = gm_l18;
                                gm_t88rcucg1b7uk t102 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t101) + 0));
                                gm_tftb1odc739t5 t103 = gm_f_push_k3l9nic(t100, GM_CONV(gm_csguankko4lmhd, t102));
                                gm_tftb1odc739t5 t104 = t103;
                                if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_srcg82mujgottmf, 29, 25);
                                gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
                            }
                            goto ix95;
ie95:;
ix95:;
                        }
                        goto me91;
mn92:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg82mujgottmf, 28, 9);
me91:;
                    }
lc80:;
                    {
                        uint64_t t107 = gm_l16;
                        uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                        uint64_t t110 = r108;
                        gm_l16 = t110;
                    }
                }
lb80:;
            }
        }
        {
            void * t111 = gm_l5;
            gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
            gm_b16_8 t113 = t112;
            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
            gm_t88rcucg1b7uk t115 = gm_f_tuple_l3i6trt((void *)NULL, cx, NULL, t111, t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t116;
            GM_ST(uint8_t, (uint8_t *)&t116 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t116 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t116), t115);
            ((uint8_t *)&t116)[4] = 1;
            gm_l13 = t116;
        }
        goto ix75;
ie75:;
ix75:;
    }
    {
        void * t117 = gm_l0;
        void * t118 = gm_l3;
        gm_tkn42u7na3t81 t119 = gm_l13;
        void * t120 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t117, t118, t119);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t120;
    }
    {
        void * t121 = gm_l0;
        void * t122 = (void *)(((uint8_t *)t121) + 96);
        void * t123 = gm_l0;
        void * t124 = GM_LD(void *, (((uint8_t *)t123) + 0));
        void * t125 = gm_l19;
        gm_t88rcucg1b7uk t126 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t125) + 0));
        gm_t88rcucg1b7uk t127 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t122, t124, t126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l20 = t127;
    }
    {
        gm_b16_8 t128 = gm_f_empty_slice_rcd2jln();
        gm_b16_8 t129 = t128;
        gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
        gm_l21 = t130;
    }
    {
        void * t131 = gm_l5;
        gm_t88rcucg1b7uk t132 = gm_l20;
        gm_t53at80pfdus3 t133;
        gm_f_kind_jcnt4hm((void *)&t133, t131, GM_CONV(gm_csguankko4lmhd, t132));
        gm_t53at80pfdus3 t134 = t133;
        uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&t134) + 0));
        switch (t140) { case ((uint8_t)0ULL): goto ma138; case ((uint8_t)1ULL): goto ma138; case ((uint8_t)2ULL): goto ma138; case ((uint8_t)3ULL): goto ma138; case ((uint8_t)4ULL): goto ma138; case ((uint8_t)5ULL): goto ma138; case ((uint8_t)6ULL): goto ma138; case ((uint8_t)7ULL): goto ma138; case ((uint8_t)8ULL): goto ma138; case ((uint8_t)9ULL): goto ma138; case ((uint8_t)10ULL): goto ma138; case ((uint8_t)11ULL): goto ma138; case ((uint8_t)12ULL): goto ma136; case ((uint8_t)13ULL): goto ma138; case ((uint8_t)14ULL): goto ma138; case ((uint8_t)15ULL): goto ma138; case ((uint8_t)16ULL): goto ma138; case ((uint8_t)17ULL): goto ma138; case ((uint8_t)18ULL): goto ma138; case ((uint8_t)19ULL): goto ma138; case ((uint8_t)20ULL): goto ma138; case ((uint8_t)21ULL): goto ma138; case ((uint8_t)22ULL): goto ma138; case ((uint8_t)23ULL): goto ma138; case ((uint8_t)24ULL): goto ma138; case ((uint8_t)25ULL): goto ma138; case ((uint8_t)26ULL): goto ma137; default: goto ma138; }
ma136:;
        uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&t134) + 0));
        if (t142 != ((uint8_t)12ULL)) goto mn141;
        gm_b16_8 t143 = GM_LD(gm_b16_8, ((((uint8_t *)&t134) + 8) + 0));
        gm_l22 = t143;
        uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
        uint64_t t146 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
        uint8_t t147 = ((t145 == t146));
        if (!t147) goto ie144;
        {
            gm_b16_8 t149 = gm_l22;
            gm_l21 = t149;
        }
        goto ix144;
ie144:;
ix144:;
        goto me135;
mn141:;
ma137:;
        uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&t134) + 0));
        if (t151 != ((uint8_t)26ULL)) goto mn150;
        void * t152 = gm_l0;
        void * t153 = GM_LD(void *, (((uint8_t *)t152) + 0));
        gm_tl874t2atnlh3 t154 = gm_l4;
        void * t155 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t153, t154);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trcmmd12cefb1 t156;
        GM_ST(uint8_t, (uint8_t *)&t156 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t156 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t156 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t156 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t156 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t156 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t156 + 8), t155);
        uint8_t t157 = 0;
        F.ret = t156;
        goto gm_fret;
        goto me135;
mn150:;
ma138:;
        goto me135;
mn158:;
ma139:;
        gm_trap("no match arm matched the scrutinee", gm_srcg82mujgottmf, 36, 5);
me135:;
    }
    {
        uint64_t t161 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
        uint64_t t162 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
        uint8_t t163 = (!(t161 == t162));
        if (!t163) goto ie160;
        {
            uint64_t t165 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
            gm_l23 = t165;
        }
        {
            void * t166 = gm_l0;
            void * t167 = gm_l3;
            gm_tl874t2atnlh3 t168 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t167) + 72));
            gm_tbccvikg6b96e t169;
            uint64_t t170 = gm_l23;
            GM_ST(uint64_t, (uint8_t *)&t169 + 0, t170);
            uint64_t t171 = gm_l23;
            GM_ST(uint64_t, (uint8_t *)&t169 + 8, t171);
            void * t172 = gm_l0;
            gm_t88rcucg1b7uk t173 = gm_l20;
            gm_b16_8 t174 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t172, t173);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t169 + 16, t174);
            gm_tabb710d9mq2e t175;
            gm_f_format_ivf9m1s((void *)&t175, (void *)NULL, cx, NULL, &t169);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t176 = t175;
            if (((((uint8_t *)&t176))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t176) + 8, gm_srcg82mujgottmf, 43, 30);
            gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&t176) + 8));
            gm_b16_8 t178 = t177;
            gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
            gm_b0_1 t180 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t166, t168, t179);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t181 = gm_l0;
            void * t182 = GM_LD(void *, (((uint8_t *)t181) + 0));
            gm_tl874t2atnlh3 t183 = gm_l4;
            void * t184 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t182, t183);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trcmmd12cefb1 t185;
            GM_ST(uint8_t, (uint8_t *)&t185 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t185 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t185 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t185 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t185 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t185 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t185 + 8), t184);
            uint8_t t186 = 0;
            F.ret = t185;
            goto gm_fret;
        }
        goto ix160;
ie160:;
ix160:;
    }
    {
        gm_f_list_nof7vfj((void *)&F.l24, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t189 = t188;
            gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)&t189));
            gm_l25 = t190;
        }
        {
            gm_l26 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t192 = gm_l26;
                uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                uint8_t t194 = (t192 < t193);
                if (!t194) goto lb191;
                {
                    uint64_t t197 = gm_l26;
                    uint64_t t198 = gm_l26;
                    uint64_t t199 = (uintptr_t)t198;
                    uint64_t t200 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                    void * t201 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t199 * 8));
                    gm_t22qobfhrnoif t202;
                    GM_ST(uint64_t, ((uint8_t *)&t202 + 0), t197);
                    GM_ST(void *, ((uint8_t *)&t202 + 8), t201);
                    gm_t22qobfhrnoif t203 = t202;
                    uint64_t t206 = GM_LD(uint64_t, (((uint8_t *)&t203) + 0));
                    gm_l27 = t206;
                    void * t207 = GM_LD(void *, (((uint8_t *)&t203) + 8));
                    gm_l28 = t207;
                    {
                        void * t209 = (void *)((uint8_t *)&F.l24);
                        gm_t88rcucg1b7uk t210;
                        void * t212 = gm_l28;
                        void * t213 = t212;
                        if (!(GM_LD(void *, ((uint8_t *)&t213)) != 0)) goto ie211;
                        void * t214 = GM_LD(void *, ((uint8_t *)&t213));
                        gm_l29 = t214;
                        void * t216 = gm_l29;
                        gm_t88rcucg1b7uk t217 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t216) + 0));
                        gm_t88rcucg1b7uk t218 = t217;
                        t210 = t218;
                        goto ix211;
ie211:;
                        uint64_t t220 = gm_l27;
                        uint64_t t221 = (uintptr_t)t220;
                        uint64_t t222 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                        if (t221 >= (uintptr_t)(t222)) gm_trap_index(t221, t222, gm_srcg82mujgottmf, 47, 78);
                        gm_t88rcucg1b7uk t223 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t221 * 4));
                        gm_t88rcucg1b7uk t224 = t223;
                        t210 = t224;
ix211:;
                        gm_tftb1odc739t5 t225 = gm_f_push_k3l9nic(t209, GM_CONV(gm_csguankko4lmhd, t210));
                        gm_tftb1odc739t5 t226 = t225;
                        if (((((uint8_t *)&t226))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t226) + 1, gm_srcg82mujgottmf, 47, 44);
                        gm_b0_1 t227 = GM_LD(gm_b0_1, (((uint8_t *)&t226) + 1));
                    }
                    goto me204;
mn205:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg82mujgottmf, 47, 5);
me204:;
                }
lc191:;
                {
                    uint64_t t229 = gm_l26;
                    uint64_t r230; bool o231 = __builtin_add_overflow(t229, ((uint64_t)1ULL), &r230);
                    uint64_t t232 = r230;
                    gm_l26 = t232;
                }
            }
lb191:;
        }
    }
    {
        gm_tpe1hv3fekn05 t233;
        gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t233 + 0, t234);
        void * t235 = gm_l0;
        void * t236 = gm_l19;
        void * t237 = gm_l5;
        gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
        gm_b16_8 t239 = t238;
        gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)&t239));
        gm_t88rcucg1b7uk t241 = gm_f_tuple_l3i6trt((void *)NULL, cx, NULL, t237, t240);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t242 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t235, t236, t241);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t233 + 16, t242);
        gm_trcmmd12cefb1 t243;
        GM_ST(uint8_t, (uint8_t *)&t243 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t243 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t243 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t243 + 0), ((uint8_t)3ULL));
        GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t243 + 8), t233);
        uint8_t t244 = 0;
        F.ret = t243;
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
__attribute__((aligned(4))) uint8_t gm_f_assignable_name_73f68mv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_assignable_name_73f68mv F;
    void * gm_l0; /* cx */
    void * gm_l1; /* place */
    gm_t08olq97rscn3 gm_l2; /* id */
    uint8_t gm_l3; /* immutable */
    uint32_t gm_l5; /* l */
    gm_tf5srh4mp4b3i gm_l6; /* d */
    uint64_t gm_l7; /* x */
    GM_STACK_CHECK(&F, 1591u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l2 = t5;
        {
            gm_l3 = ((uint8_t)1);
        }
        {
            void * t8 = gm_l0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
            gm_tsf6kp4ohnuqs t10;
            gm_f_lookup_local_at_ntm4scs((void *)&t10, t8, t9);
            gm_tsf6kp4ohnuqs t11 = t10;
            if (!((((uint8_t *)&t11))[216] != 0)) goto ie7;
            gm_tfj8gdjm726n4 t12 = GM_LD(gm_tfj8gdjm726n4, ((uint8_t *)&t11));
            F.l4 = t12;
            {
                uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
                if (t15 != ((uint8_t)0ULL)) goto ie14;
                uint32_t t16 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 0) + 8));
                gm_l5 = t16;
                {
                    void * t18 = gm_l0;
                    uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 208));
                    uint32_t t20 = gm_l5;
                    gm_tsoie1ppgdvcg t21;
                    gm_f_local_at_p388mm4((void *)&t21, t18, t19, t20);
                    gm_tsoie1ppgdvcg t22 = t21;
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t22) + 20));
                    uint8_t t24 = (uint8_t)!t23;
                    gm_l3 = t24;
                }
                goto ix14;
ie14:;
ix14:;
            }
            goto ix7;
ie7:;
            {
                void * t26 = gm_l0;
                void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
                void * t28 = gm_l0;
                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 8));
                gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                gm_tf5srh4mp4b3i t31 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t27, t29, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t31;
            }
            {
                uint8_t t32;
                if (!((((uint8_t *)&gm_l6))[8] != 0)) goto ie33;
                uint64_t t34 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
                gm_l7 = t34;
                void * t36 = gm_l0;
                void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
                uint64_t t38 = gm_l7;
                gm_t6qooom8qa2do t39;
                gm_f_resolve_decl_pus042v((void *)&t39, (void *)NULL, cx, NULL, t37, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6qooom8qa2do t40 = t39;
                uint8_t t41;
                uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t40) + 0));
                switch (t47) { case ((uint8_t)0ULL): goto ma45; case ((uint8_t)1ULL): goto ma45; case ((uint8_t)2ULL): goto ma45; case ((uint8_t)3ULL): goto ma45; case ((uint8_t)4ULL): goto ma45; case ((uint8_t)5ULL): goto ma45; case ((uint8_t)6ULL): goto ma45; case ((uint8_t)7ULL): goto ma45; case ((uint8_t)8ULL): goto ma45; case ((uint8_t)9ULL): goto ma45; case ((uint8_t)10ULL): goto ma44; case ((uint8_t)11ULL): goto ma43; default: goto ma45; }
ma43:;
                uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t40) + 0));
                if (t49 != ((uint8_t)11ULL)) goto mn48;
                t41 = ((uint8_t)0);
                goto me42;
mn48:;
ma44:;
                uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t40) + 0));
                if (t51 != ((uint8_t)10ULL)) goto mn50;
                t41 = ((uint8_t)0);
                goto me42;
mn50:;
ma45:;
                t41 = ((uint8_t)1);
                goto me42;
mn52:;
ma46:;
                gm_trap("no match arm matched the scrutinee", gm_srcg82mujgottmf, 153, 33);
me42:;
                uint8_t t53 = t41;
                t32 = t53;
                goto ix33;
ie33:;
                uint8_t t55 = ((uint8_t)1);
                t32 = t55;
ix33:;
                gm_l3 = t32;
            }
ix7:;
        }
        {
            uint8_t t57 = gm_l3;
            if (!t57) goto ie56;
            {
                void * t59 = gm_l0;
                void * t60 = gm_l1;
                gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t60) + 72));
                gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                gm_tnpifv20irnph t63;
                GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t62);
                gm_tabb710d9mq2e t64;
                gm_f_format_2snkf9s((void *)&t64, (void *)NULL, cx, NULL, t63);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t65 = t64;
                if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 8, gm_srcg82mujgottmf, 156, 30);
                gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 8));
                gm_b16_8 t67 = t66;
                gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                gm_b0_1 t69 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t59, t61, t68);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix56;
ie56:;
ix56:;
        }
        {
            uint8_t t70 = gm_l3;
            uint8_t t71 = (uint8_t)!t70;
            uint8_t t72 = 0;
            F.ret = t71;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t74 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
ix2:;
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
__attribute__((aligned(4))) void * gm_f_compound_op_krvsbol(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_compound_op_krvsbol F;
    void * gm_l0; /* cx */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* place */
    void * gm_l3; /* value */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint8_t gm_l5; /* arith */
    gm_t88rcucg1b7uk gm_l6; /* pt */
    void * gm_l7; /* r */
    gm_t88rcucg1b7uk gm_l8; /* rt */
    GM_STACK_CHECK(&F, 550u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint8_t t2;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t7 != ((uint8_t)0ULL)) goto pa6;
        goto po5;
pa6:;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t9 != ((uint8_t)1ULL)) goto pa8;
        goto po5;
pa8:;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t11 != ((uint8_t)2ULL)) goto pa10;
        goto po5;
pa10:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t13 != ((uint8_t)4ULL)) goto pa12;
        goto po5;
pa12:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t15 != ((uint8_t)5ULL)) goto pa14;
        goto po5;
pa14:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t17 != ((uint8_t)7ULL)) goto pa16;
        goto po5;
pa16:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t19 != ((uint8_t)8ULL)) goto pa18;
        goto po5;
pa18:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t21 != ((uint8_t)9ULL)) goto pa20;
        goto po5;
pa20:;
        goto mn4;
po5:;
        t2 = ((uint8_t)1);
        goto me3;
mn4:;
        t2 = ((uint8_t)0);
        goto me3;
mn22:;
        gm_trap("no match arm matched the scrutinee", gm_srcg82mujgottmf, 121, 14);
me3:;
        gm_l5 = t2;
    }
    {
        void * t23 = gm_l0;
        void * t24 = (void *)(((uint8_t *)t23) + 96);
        void * t25 = gm_l0;
        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
        void * t27 = gm_l2;
        gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 0));
        gm_t88rcucg1b7uk t29 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t24, t26, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t29;
    }
    {
        uint8_t t31 = 0;
        uint8_t t32 = gm_l5;
        uint8_t t33 = (uint8_t)!t32;
        if (!t33) {
            void * t34 = gm_l0;
            gm_t88rcucg1b7uk t35 = gm_l6;
            uint8_t t36 = gm_f_numeric_in5abdm(t34, GM_CONV(gm_csguankko4lmhd, t35));
            uint8_t t37 = (uint8_t)!t36;
            t31 = t37;
        } else t31 = 1;
        if (!t31) goto ie30;
        {
            void * t39 = gm_l0;
            gm_ts6oqppjh0ba3 t40 = gm_l1;
            void * t41 = gm_l2;
            void * t42 = gm_l3;
            gm_tl874t2atnlh3 t43 = gm_l4;
            void * t44 = gm_f_binary_op_7ocdl4v((void *)NULL, cx, NULL, t39, t40, t41, t42, t43);
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
        void * t46 = gm_l0;
        void * t47 = gm_l3;
        void * t48 = gm_l2;
        gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t48) + 0));
        gm_tkn42u7na3t81 t50;
        GM_ST(uint8_t, (uint8_t *)&t50 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t50 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t50), t49);
        ((uint8_t *)&t50)[4] = 1;
        void * t51 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t46, t47, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t51;
    }
    {
        void * t52 = gm_l0;
        void * t53 = (void *)(((uint8_t *)t52) + 96);
        void * t54 = gm_l0;
        void * t55 = GM_LD(void *, (((uint8_t *)t54) + 0));
        void * t56 = gm_l7;
        gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t56) + 0));
        gm_t88rcucg1b7uk t58 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t53, t55, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t58;
    }
    {
        gm_t88rcucg1b7uk t60 = gm_l6;
        gm_t88rcucg1b7uk t61 = gm_l8;
        gm_t88rcucg1b7uk t62 = t60;
        gm_t88rcucg1b7uk t63 = t61;
        uint8_t t64 = (!gm_eq9m6mvtmgde(((uint8_t *)&t62), ((uint8_t *)&t63)));
        if (!t64) goto ie59;
        void * t65 = gm_l0;
        gm_t88rcucg1b7uk t66 = gm_l8;
        uint8_t t67 = gm_f_numeric_in5abdm(t65, GM_CONV(gm_csguankko4lmhd, t66));
        if (!t67) goto ie59;
        void * t68 = gm_l0;
        void * t69 = GM_LD(void *, (((uint8_t *)t68) + 0));
        gm_t88rcucg1b7uk t70 = gm_l8;
        gm_t88rcucg1b7uk t71 = gm_l6;
        uint8_t t72 = gm_f_lossless_a54o69s(t69, GM_CONV(gm_csguankko4lmhd, t70), GM_CONV(gm_csguankko4lmhd, t71));
        if (!t72) goto ie59;
        {
            gm_t88rcucg1b7uk t74 = gm_l6;
            gm_tt8o5kebou62r t75;
            memset((uint8_t *)&t75 + 12, 0, 4);
            gm_tvdg94bk7aa22 t76;
            GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t76 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t75 + 0, t76);
            void * t77 = gm_l7;
            gm_b16_8 t78 = gm_f_one_expr_q6kn0hn((void *)NULL, cx, NULL, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t75 + 16, t78);
            gm_teti411eujvf4 t79;
            GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t79 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t79 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t79 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)33ULL));
            GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t79 + 8), t75);
            void * t80 = gm_l7;
            gm_tl874t2atnlh3 t81 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t80) + 72));
            void * t82 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t74, &t79, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t82;
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        void * t83 = gm_l0;
        gm_ts6oqppjh0ba3 t84 = gm_l1;
        void * t85 = gm_l2;
        void * t86 = gm_l7;
        gm_tl874t2atnlh3 t87 = gm_l4;
        void * t88 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t83, t84, t85, t86, t87);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t89 = 0;
        F.ret = t88;
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
inline __attribute__((aligned(4))) void * gm_f_copy_expr_nebhehp(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_copy_expr_nebhehp F;
    void * gm_l0; /* e */
    void * gm_l1; /* c */
    void * gm_l2; /* base */
    uint32_t gm_l3; /* index */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 352u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_tq7s98ihonkju t3 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_t69m56meqefs8 *)((uint8_t *)t2)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ksdcjqjfqsj)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcg82mujgottmf, 109, 10);
        void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
        gm_l1 = t5;
    }
    {
        void * t6 = gm_l1;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma10; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma10; case ((uint8_t)31ULL): goto ma10; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma10; case ((uint8_t)34ULL): goto ma10; case ((uint8_t)35ULL): goto ma10; case ((uint8_t)36ULL): goto ma10; case ((uint8_t)37ULL): goto ma10; case ((uint8_t)38ULL): goto ma10; case ((uint8_t)39ULL): goto ma10; case ((uint8_t)40ULL): goto ma10; case ((uint8_t)41ULL): goto ma10; case ((uint8_t)42ULL): goto ma10; case ((uint8_t)43ULL): goto ma10; case ((uint8_t)44ULL): goto ma10; case ((uint8_t)45ULL): goto ma10; case ((uint8_t)46ULL): goto ma10; case ((uint8_t)47ULL): goto ma10; case ((uint8_t)48ULL): goto ma10; case ((uint8_t)49ULL): goto ma10; case ((uint8_t)50ULL): goto ma10; case ((uint8_t)51ULL): goto ma10; case ((uint8_t)52ULL): goto ma10; case ((uint8_t)53ULL): goto ma10; case ((uint8_t)54ULL): goto ma10; case ((uint8_t)55ULL): goto ma10; case ((uint8_t)56ULL): goto ma10; case ((uint8_t)57ULL): goto ma10; case ((uint8_t)58ULL): goto ma10; default: goto ma10; }
ma8:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
        if (t14 != ((uint8_t)19ULL)) goto mn13;
        void * t15 = GM_LD(void *, (((((uint8_t *)t6) + 8) + 8) + 0));
        gm_l2 = t15;
        uint32_t t16 = GM_LD(uint32_t, (((((uint8_t *)t6) + 8) + 8) + 8));
        gm_l3 = t16;
        {
            void * t18 = gm_l1;
            uint8_t *t19 = (((uint8_t *)t18) + 8);
            void * t20 = gm_l2;
            void * t21 = gm_f_copy_expr_nebhehp((void *)NULL, cx, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t22 = gm_l3;
            gm_tbl8il92l97ut t23;
            GM_ST(uint32_t, (uint8_t *)&t23 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t23 + 0), t21);
            GM_ST(uint32_t, ((uint8_t *)&t23 + 8), t22);
            gm_teti411eujvf4 t24;
            memset(&t24, 0, sizeof t24);
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t24 + 8), t23);
            GM_ST(gm_teti411eujvf4, t19, t24);
        }
        goto me7;
mn13:;
ma9:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
        if (t26 != ((uint8_t)18ULL)) goto mn25;
        void * t27 = GM_LD(void *, ((((uint8_t *)t6) + 8) + 8));
        gm_l4 = t27;
        {
            void * t29 = gm_l1;
            uint8_t *t30 = (((uint8_t *)t29) + 8);
            void * t31 = gm_l4;
            void * t32 = gm_f_copy_expr_nebhehp((void *)NULL, cx, NULL, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_teti411eujvf4 t33;
            memset(&t33, 0, sizeof t33);
            GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)18ULL));
            GM_ST(void *, ((uint8_t *)&t33 + 8), t32);
            GM_ST(gm_teti411eujvf4, t30, t33);
        }
        goto me7;
mn25:;
ma10:;
        goto me7;
mn34:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcg82mujgottmf, 110, 5);
me7:;
    }
    {
        void * t36 = gm_l1;
        uint8_t t37 = 0;
        F.ret = t36;
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
inline __attribute__((aligned(4))) void * gm_f_deref_86inrqe(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_deref_86inrqe F;
    GM_STACK_CHECK(&F, 204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t1 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_assign_place_aq16ukh *)((&F.h)->up))->l1));
    gm_t88rcucg1b7uk t2 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_assign_place_aq16ukh *)((&F.h)->up))->l9));
    uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_assign_place_aq16ukh *)((&F.h)->up))->l10));
    gm_teti411eujvf4 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t5 + 8), t4);
    gm_tl874t2atnlh3 t6 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&((struct gm_fr_assign_place_aq16ukh *)((&F.h)->up))->l5));
    void * t7 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t3, &t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_teti411eujvf4 t8;
    memset(&t8, 0, sizeof t8);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)18ULL));
    GM_ST(void *, ((uint8_t *)&t8 + 8), t7);
    gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&((struct gm_fr_assign_place_aq16ukh *)((&F.h)->up))->l5));
    void * t10 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t2, &t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t10;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_simple_place_6fb9rhd(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_simple_place_6fb9rhd F;
    void * gm_l0; /* e */
    void * gm_l1; /* base */
    void * gm_l2; /* p */
    GM_STACK_CHECK(&F, 79u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma4; case ((uint8_t)17ULL): goto ma4; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma5; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma7; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; case ((uint8_t)51ULL): goto ma7; case ((uint8_t)52ULL): goto ma7; case ((uint8_t)53ULL): goto ma7; case ((uint8_t)54ULL): goto ma7; case ((uint8_t)55ULL): goto ma7; case ((uint8_t)56ULL): goto ma7; case ((uint8_t)57ULL): goto ma7; case ((uint8_t)58ULL): goto ma7; default: goto ma7; }
ma4:;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t13 != ((uint8_t)15ULL)) goto pa12;
        goto po11;
pa12:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)16ULL)) goto pa14;
        goto po11;
pa14:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)17ULL)) goto pa16;
        goto po11;
pa16:;
        goto mn10;
po11:;
        uint8_t t18 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn10:;
ma5:;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t20 != ((uint8_t)19ULL)) goto mn19;
        void * t21 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l1 = t21;
        void * t22 = gm_l1;
        uint8_t t23 = gm_f_is_simple_place_6fb9rhd(t22);
        uint8_t t24 = 0;
        F.ret = t23;
        goto gm_fret;
        goto me3;
mn19:;
ma6:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t26 != ((uint8_t)18ULL)) goto mn25;
        void * t27 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t27;
        void * t28 = gm_l2;
        uint8_t t29;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
        if (t32 != ((uint8_t)15ULL)) goto mn31;
        t29 = ((uint8_t)1);
        goto me30;
mn31:;
        t29 = ((uint8_t)0);
        goto me30;
mn33:;
        gm_trap("no match arm matched the scrutinee", gm_srcg82mujgottmf, 202, 29);
me30:;
        uint8_t t34 = 0;
        F.ret = t29;
        goto gm_fret;
        goto me3;
mn25:;
ma7:;
        uint8_t t36 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn35:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcg82mujgottmf, 199, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_numeric_in5abdm(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_one_expr_q6kn0hn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_place_mutability_0tlm4g8(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_place_mutability_0tlm4g8 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* tys */
    uint32_t gm_l3; /* l */
    uint32_t gm_l4; /* levels */
    uint32_t gm_l5; /* l */
    void * gm_l6; /* p */
    uint8_t gm_l7; /* c */
    void * gm_l8; /* base */
    void * gm_l9; /* base */
    uint8_t gm_l10; /* c */
    uint8_t gm_l11; /* c */
    GM_STACK_CHECK(&F, 659u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l2 = t4;
    }
    {
        void * t6 = gm_l2;
        void * t7 = gm_l1;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t7) + 0));
        uint8_t t9 = gm_f_is_error_8k27gd1(t6, GM_CONV(gm_csguankko4lmhd, t8));
        if (!t9) goto ie5;
        {
            gm_tqs5p4n2tq19v t11;
            GM_ST(uint8_t, ((uint8_t *)&t11), ((uint8_t)1));
            ((uint8_t *)&t11)[1] = 1;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t13 = gm_l1;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
        switch (t24) { case ((uint8_t)0ULL): goto ma22; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma22; case ((uint8_t)4ULL): goto ma22; case ((uint8_t)5ULL): goto ma22; case ((uint8_t)6ULL): goto ma22; case ((uint8_t)7ULL): goto ma22; case ((uint8_t)8ULL): goto ma22; case ((uint8_t)9ULL): goto ma22; case ((uint8_t)10ULL): goto ma22; case ((uint8_t)11ULL): goto ma22; case ((uint8_t)12ULL): goto ma22; case ((uint8_t)13ULL): goto ma22; case ((uint8_t)14ULL): goto ma18; case ((uint8_t)15ULL): goto ma15; case ((uint8_t)16ULL): goto ma16; case ((uint8_t)17ULL): goto ma17; case ((uint8_t)18ULL): goto ma19; case ((uint8_t)19ULL): goto ma20; case ((uint8_t)20ULL): goto ma21; case ((uint8_t)21ULL): goto ma22; case ((uint8_t)22ULL): goto ma22; case ((uint8_t)23ULL): goto ma22; case ((uint8_t)24ULL): goto ma22; case ((uint8_t)25ULL): goto ma22; case ((uint8_t)26ULL): goto ma22; case ((uint8_t)27ULL): goto ma22; case ((uint8_t)28ULL): goto ma22; case ((uint8_t)29ULL): goto ma22; case ((uint8_t)30ULL): goto ma22; case ((uint8_t)31ULL): goto ma22; case ((uint8_t)32ULL): goto ma22; case ((uint8_t)33ULL): goto ma22; case ((uint8_t)34ULL): goto ma22; case ((uint8_t)35ULL): goto ma22; case ((uint8_t)36ULL): goto ma22; case ((uint8_t)37ULL): goto ma22; case ((uint8_t)38ULL): goto ma22; case ((uint8_t)39ULL): goto ma22; case ((uint8_t)40ULL): goto ma22; case ((uint8_t)41ULL): goto ma22; case ((uint8_t)42ULL): goto ma22; case ((uint8_t)43ULL): goto ma22; case ((uint8_t)44ULL): goto ma22; case ((uint8_t)45ULL): goto ma22; case ((uint8_t)46ULL): goto ma22; case ((uint8_t)47ULL): goto ma22; case ((uint8_t)48ULL): goto ma22; case ((uint8_t)49ULL): goto ma22; case ((uint8_t)50ULL): goto ma22; case ((uint8_t)51ULL): goto ma22; case ((uint8_t)52ULL): goto ma22; case ((uint8_t)53ULL): goto ma22; case ((uint8_t)54ULL): goto ma22; case ((uint8_t)55ULL): goto ma22; case ((uint8_t)56ULL): goto ma22; case ((uint8_t)57ULL): goto ma22; case ((uint8_t)58ULL): goto ma22; default: goto ma22; }
ma15:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
        if (t26 != ((uint8_t)15ULL)) goto mn25;
        uint32_t t27 = GM_LD(uint32_t, ((((uint8_t *)t13) + 8) + 8));
        gm_l3 = t27;
        uint32_t t28 = gm_l3;
        uint64_t t29 = (uint64_t)t28;
        uint64_t t30 = (uintptr_t)t29;
        void * t31 = gm_l0;
        uint64_t t32 = GM_LD(uintptr_t, ((((uint8_t *)t31) + 16) + 0) + 8);
        if (t30 >= (uintptr_t)(t32)) gm_trap_index(t30, t32, gm_srcg82mujgottmf, 179, 29);
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t31) + 16) + 0)) + t30 * 40) + 20));
        gm_tqs5p4n2tq19v t34;
        GM_ST(uint8_t, ((uint8_t *)&t34), t33);
        ((uint8_t *)&t34)[1] = 1;
        uint8_t t35 = 0;
        F.ret = t34;
        goto gm_fret;
        goto me14;
mn25:;
ma16:;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
        if (t37 != ((uint8_t)16ULL)) goto mn36;
        uint32_t t38 = GM_LD(uint32_t, (((((uint8_t *)t13) + 8) + 8) + 0));
        gm_l4 = t38;
        uint32_t t39 = GM_LD(uint32_t, (((((uint8_t *)t13) + 8) + 8) + 4));
        gm_l5 = t39;
        void * t40 = gm_l0;
        void * t41 = gm_l0;
        uint32_t t42 = gm_f_depth_fr7pn6n(t41);
        uint32_t t43 = gm_l4;
        uint32_t r44; bool o45 = __builtin_sub_overflow(t42, t43, &r44);
        if (o45) gm_trap("integer overflow in `-`", gm_srcg82mujgottmf, 180, 49);
        uint32_t t46 = r44;
        uint32_t t47 = gm_l5;
        gm_tsoie1ppgdvcg t48;
        gm_f_local_at_p388mm4((void *)&t48, t40, t46, t47);
        gm_tsoie1ppgdvcg t49 = t48;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t49) + 20));
        gm_tqs5p4n2tq19v t51;
        GM_ST(uint8_t, ((uint8_t *)&t51), t50);
        ((uint8_t *)&t51)[1] = 1;
        uint8_t t52 = 0;
        F.ret = t51;
        goto gm_fret;
        goto me14;
mn36:;
ma17:;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
        if (t54 != ((uint8_t)17ULL)) goto mn53;
        gm_tqs5p4n2tq19v t55;
        GM_ST(uint8_t, ((uint8_t *)&t55), ((uint8_t)1));
        ((uint8_t *)&t55)[1] = 1;
        uint8_t t56 = 0;
        F.ret = t55;
        goto gm_fret;
        goto me14;
mn53:;
ma18:;
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
        if (t58 != ((uint8_t)14ULL)) goto mn57;
        gm_tqs5p4n2tq19v t59;
        GM_ST(uint8_t, ((uint8_t *)&t59), ((uint8_t)0));
        ((uint8_t *)&t59)[1] = 1;
        uint8_t t60 = 0;
        F.ret = t59;
        goto gm_fret;
        goto me14;
mn57:;
ma19:;
        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
        if (t62 != ((uint8_t)18ULL)) goto mn61;
        void * t63 = GM_LD(void *, ((((uint8_t *)t13) + 8) + 8));
        gm_l6 = t63;
        {
            void * t66 = gm_l2;
            void * t67 = gm_l6;
            gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t67) + 0));
            gm_t53at80pfdus3 t69;
            gm_f_kind_jcnt4hm((void *)&t69, t66, GM_CONV(gm_csguankko4lmhd, t68));
            gm_t53at80pfdus3 t70 = t69;
            uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&t70) + 0));
            if (t71 != ((uint8_t)8ULL)) goto ie65;
            uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)&t70) + 8) + 0));
            gm_l7 = t72;
            {
                uint8_t t74 = gm_l7;
                uint8_t t75 = (uint8_t)!t74;
                gm_tqs5p4n2tq19v t76;
                GM_ST(uint8_t, ((uint8_t *)&t76), t75);
                ((uint8_t *)&t76)[1] = 1;
                uint8_t t77 = 0;
                F.ret = t76;
                goto gm_fret;
            }
            goto ix65;
ie65:;
ix65:;
        }
        {
            gm_tqs5p4n2tq19v t78;
            GM_ST(uint8_t, ((uint8_t *)&t78), ((uint8_t)0));
            ((uint8_t *)&t78)[1] = 1;
            uint8_t t79 = 0;
            F.ret = t78;
            goto gm_fret;
        }
        goto me14;
mn61:;
ma20:;
        uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
        if (t81 != ((uint8_t)19ULL)) goto mn80;
        void * t82 = GM_LD(void *, (((((uint8_t *)t13) + 8) + 8) + 0));
        gm_l8 = t82;
        void * t83 = gm_l0;
        void * t84 = gm_l8;
        gm_tqs5p4n2tq19v t85 = gm_f_place_mutability_0tlm4g8(t83, t84);
        uint8_t t86 = 0;
        F.ret = t85;
        goto gm_fret;
        goto me14;
mn80:;
ma21:;
        uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
        if (t88 != ((uint8_t)20ULL)) goto mn87;
        void * t89 = GM_LD(void *, (((((uint8_t *)t13) + 8) + 8) + 0));
        gm_l9 = t89;
        void * t90 = gm_l2;
        void * t91 = gm_l9;
        gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t91) + 0));
        gm_t53at80pfdus3 t93;
        gm_f_kind_jcnt4hm((void *)&t93, t90, GM_CONV(gm_csguankko4lmhd, t92));
        gm_t53at80pfdus3 t94 = t93;
        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&t94) + 0));
        switch (t100) { case ((uint8_t)0ULL): goto ma98; case ((uint8_t)1ULL): goto ma98; case ((uint8_t)2ULL): goto ma98; case ((uint8_t)3ULL): goto ma98; case ((uint8_t)4ULL): goto ma98; case ((uint8_t)5ULL): goto ma98; case ((uint8_t)6ULL): goto ma98; case ((uint8_t)7ULL): goto ma98; case ((uint8_t)8ULL): goto ma98; case ((uint8_t)9ULL): goto ma96; case ((uint8_t)10ULL): goto ma96; case ((uint8_t)11ULL): goto ma97; case ((uint8_t)12ULL): goto ma98; case ((uint8_t)13ULL): goto ma98; case ((uint8_t)14ULL): goto ma98; case ((uint8_t)15ULL): goto ma98; case ((uint8_t)16ULL): goto ma98; case ((uint8_t)17ULL): goto ma98; case ((uint8_t)18ULL): goto ma98; case ((uint8_t)19ULL): goto ma98; case ((uint8_t)20ULL): goto ma97; case ((uint8_t)21ULL): goto ma98; case ((uint8_t)22ULL): goto ma98; case ((uint8_t)23ULL): goto ma98; case ((uint8_t)24ULL): goto ma98; case ((uint8_t)25ULL): goto ma98; case ((uint8_t)26ULL): goto ma98; default: goto ma98; }
ma96:;
        uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&t94) + 0));
        if (t104 != ((uint8_t)10ULL)) goto pa103;
        uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)&t94) + 8) + 0));
        gm_l10 = t105;
        goto po102;
pa103:;
        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&t94) + 0));
        if (t107 != ((uint8_t)9ULL)) goto pa106;
        uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)&t94) + 8) + 0));
        gm_l10 = t108;
        goto po102;
pa106:;
        goto mn101;
po102:;
        uint8_t t109 = gm_l10;
        uint8_t t110 = (uint8_t)!t109;
        gm_tqs5p4n2tq19v t111;
        GM_ST(uint8_t, ((uint8_t *)&t111), t110);
        ((uint8_t *)&t111)[1] = 1;
        uint8_t t112 = 0;
        F.ret = t111;
        goto gm_fret;
        goto me95;
mn101:;
ma97:;
        uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&t94) + 0));
        if (t116 != ((uint8_t)11ULL)) goto pa115;
        goto po114;
pa115:;
        uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&t94) + 0));
        if (t118 != ((uint8_t)20ULL)) goto pa117;
        goto po114;
pa117:;
        goto mn113;
po114:;
        void * t119 = gm_l0;
        void * t120 = gm_l9;
        gm_tqs5p4n2tq19v t121 = gm_f_place_mutability_0tlm4g8(t119, t120);
        uint8_t t122 = 0;
        F.ret = t121;
        goto gm_fret;
        goto me95;
mn113:;
ma98:;
        gm_tqs5p4n2tq19v t124;
        memset(&t124, 0, sizeof t124);
        uint8_t t125 = 0;
        F.ret = t124;
        goto gm_fret;
        goto me95;
mn123:;
ma99:;
        gm_trap("no match arm matched the scrutinee", gm_srcg82mujgottmf, 188, 31);
me95:;
        goto me14;
mn87:;
ma22:;
        gm_tqs5p4n2tq19v t127;
        memset(&t127, 0, sizeof t127);
        uint8_t t128 = 0;
        F.ret = t127;
        goto gm_fret;
        goto me14;
mn126:;
ma23:;
        gm_trap("no match arm matched the scrutinee", gm_srcg82mujgottmf, 178, 5);
me14:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_require_mutable_place_semqr6v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_require_mutable_place_semqr6v F;
    void * gm_l0; /* cx */
    void * gm_l1; /* place */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_tqs5p4n2tq19v t4 = gm_f_place_mutability_0tlm4g8(t2, t3);
        gm_tqs5p4n2tq19v t5 = t4;
        if (!((((uint8_t *)&t5))[1] != 0)) goto mn7;
        uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)&t5));
        if (!(t8 == ((uint8_t)1))) goto mn7;
        goto me6;
mn7:;
        if (!((((uint8_t *)&t5))[1] != 0)) goto mn10;
        uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)&t5));
        if (!(t11 == ((uint8_t)0))) goto mn10;
        void * t12 = gm_l0;
        gm_tl874t2atnlh3 t13 = gm_l2;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sckv4ch46j06c));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(35));
        gm_b0_1 t15 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t12, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn10:;
        if (((((uint8_t *)&t5))[1] != 0)) goto mn16;
        {
            void * t19 = gm_l0;
            void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
            void * t21 = (void *)(((uint8_t *)t20) + 8);
            void * t22 = gm_l1;
            gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t22) + 0));
            uint8_t t24 = gm_f_is_error_8k27gd1(t21, GM_CONV(gm_csguankko4lmhd, t23));
            uint8_t t25 = (uint8_t)!t24;
            if (!t25) goto ie18;
            {
                void * t27 = gm_l0;
                gm_tl874t2atnlh3 t28 = gm_l2;
                gm_b16_8 t29;
                GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sog4l3h5q9oa6));
                GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(83));
                gm_b0_1 t30 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t27, t28, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix18;
ie18:;
ix18:;
        }
        goto me6;
mn16:;
        gm_trap("no match arm matched the scrutinee", gm_srcg82mujgottmf, 163, 5);
me6:;
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
