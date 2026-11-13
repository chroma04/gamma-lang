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
typedef struct { void * f0; uint64_t f1; uint64_t f2; uint32_t f3; uint32_t f4; } gm_t2i0cb8fcu1gb;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint64_t a[8]; } gm_tl4e4vqe807ge;
typedef struct { void * f0; uint64_t f1; } gm_ttf6i6v1da8cm;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
_Static_assert(sizeof(gm_t2i0cb8fcu1gb) == 32 && _Alignof(gm_t2i0cb8fcu1gb) == 8 && offsetof(gm_t2i0cb8fcu1gb, f0) == 0 && offsetof(gm_t2i0cb8fcu1gb, f1) == 8 && offsetof(gm_t2i0cb8fcu1gb, f2) == 16 && offsetof(gm_t2i0cb8fcu1gb, f3) == 24 && offsetof(gm_t2i0cb8fcu1gb, f4) == 28, "layout of `Page`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_ttf6i6v1da8cm f0; uint8_t f1; uint8_t pe[7]; } gm_taordiq61jj46;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { void * f0; uint64_t f1; uint8_t f2; uint8_t f3; uint8_t f4; gm_t9qkh0rsooc8d f5; uint8_t pe[4]; } gm_tbls6nq5r2dk1;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { uint64_t f0; uint64_t f1; gm_t9qkh0rsooc8d f2; uint8_t f3; uint8_t pe[6]; } gm_tilak255t5m34;
_Static_assert(sizeof(gm_tl4e4vqe807ge) == 64 && _Alignof(gm_tl4e4vqe807ge) == 8 && offsetof(gm_tl4e4vqe807ge, a) == 0, "layout of `[8]usize`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttf6i6v1da8cm) == 16 && _Alignof(gm_ttf6i6v1da8cm) == 8 && offsetof(gm_ttf6i6v1da8cm, f0) == 0 && offsetof(gm_ttf6i6v1da8cm, f1) == 8, "layout of `(slot: ^Slot, base: u64)`");
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; gm_tl4e4vqe807ge f3; gm_trqfvmjtt4sij f4; uint64_t f5; uint64_t f6; } gm_t4m55nu62fs91;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_taordiq61jj46) == 24 && _Alignof(gm_taordiq61jj46) == 8 && offsetof(gm_taordiq61jj46, f0) == 0 && offsetof(gm_taordiq61jj46, f1) == 16, "layout of `?(slot: ^Slot, base: u64)`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbls6nq5r2dk1) == 24 && _Alignof(gm_tbls6nq5r2dk1) == 8 && offsetof(gm_tbls6nq5r2dk1, f0) == 0 && offsetof(gm_tbls6nq5r2dk1, f1) == 8 && offsetof(gm_tbls6nq5r2dk1, f2) == 16 && offsetof(gm_tbls6nq5r2dk1, f3) == 17 && offsetof(gm_tbls6nq5r2dk1, f4) == 18 && offsetof(gm_tbls6nq5r2dk1, f5) == 19, "layout of `Slot`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tilak255t5m34) == 24 && _Alignof(gm_tilak255t5m34) == 8 && offsetof(gm_tilak255t5m34, f0) == 0 && offsetof(gm_tilak255t5m34, f1) == 8 && offsetof(gm_tilak255t5m34, f2) == 16 && offsetof(gm_tilak255t5m34, f3) == 17, "layout of `(base: u64, size: u64, kind: AllocKind, live: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tilak255t5m34 f0; uint8_t f1; uint8_t pe[7]; } gm_tqvpud5umqqp1;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; uint64_t v1; } f1; } gm_t3j6egd6cfnk5;
_Static_assert(sizeof(gm_t4m55nu62fs91) == 176 && _Alignof(gm_t4m55nu62fs91) == 8 && offsetof(gm_t4m55nu62fs91, f0) == 0 && offsetof(gm_t4m55nu62fs91, f1) == 40 && offsetof(gm_t4m55nu62fs91, f2) == 48 && offsetof(gm_t4m55nu62fs91, f3) == 56 && offsetof(gm_t4m55nu62fs91, f4) == 120 && offsetof(gm_t4m55nu62fs91, f5) == 160 && offsetof(gm_t4m55nu62fs91, f6) == 168, "layout of `Memory`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; } f1; } gm_tgfdnufetdnfc;
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tqvpud5umqqp1) == 32 && _Alignof(gm_tqvpud5umqqp1) == 8 && offsetof(gm_tqvpud5umqqp1, f0) == 0 && offsetof(gm_tqvpud5umqqp1, f1) == 24, "layout of `?(base: u64, size: u64, kind: AllocKind, live: bool)`");
_Static_assert(sizeof(gm_t3j6egd6cfnk5) == 48 && _Alignof(gm_t3j6egd6cfnk5) == 8 && offsetof(gm_t3j6egd6cfnk5, f0) == 0 && offsetof(gm_t3j6egd6cfnk5, f1) == 8, "layout of `MemError!u64`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
_Static_assert(sizeof(gm_tgfdnufetdnfc) == 48 && _Alignof(gm_tgfdnufetdnfc) == 8 && offsetof(gm_tgfdnufetdnfc, f0) == 0 && offsetof(gm_tgfdnufetdnfc, f1) == 8, "layout of `MemError!()`");
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_alloc_bfo8187 {
    gm_frame h;
    gm_t3j6egd6cfnk5 ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_describe_t62jhnt {
    gm_frame h;
    gm_tqvpud5umqqp1 ret;
};
struct gm_fr_drop_oldest_o9uaetp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fault_u31b92l {
    gm_frame h;
    gm_tpekhfs32op0f ret;
};
struct gm_fr_init_9fuf99v {
    gm_frame h;
    gm_tgfdnufetdnfc ret;
};
struct gm_fr_kill_3kcavfd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_locate_ciotffk {
    gm_frame h;
    gm_taordiq61jj46 ret;
};
struct gm_fr_mem_deinit_0088958 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_mem_message_e0go901 {
    gm_frame h;
    gm_tpekhfs32op0f l0; /* e */
    gm_b16_8 ret;
};
struct gm_fr_memory_dqom95e {
    gm_frame h;
    gm_t4m55nu62fs91 ret;
};
struct gm_fr_next_pages_p5tqfl1 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_page_of_artfs32 {
    gm_frame h;
    void * ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_release_page_66inn9t {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_write_5gub0ul {
    gm_frame h;
    gm_tgfdnufetdnfc ret;
};
struct gm_fr_write_view_pebit6h {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
bool gm_eqv4kh4qul78(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_alloc_bfo8187(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3, uint8_t a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_deinit_d5p9g61(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_u4v62un(void * a0);
__attribute__((aligned(4))) void gm_f_describe_t62jhnt(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tqvpud5umqqp1 gm_f_describe_t62jhnt_c(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_destroy_l0inh8t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_drop_oldest_o9uaetp(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) gm_tpekhfs32op0f gm_f_fault_u31b92l_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) void gm_f_format_173gk69(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bovj9ap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_hvaqp8b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_itetl3f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_n0787fu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_o05ssh3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_q07lmui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_oobi9fu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_init_9fuf99v(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tgfdnufetdnfc gm_f_init_9fuf99v_c(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_kill_3kcavfd(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_bcj49nd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_locate_ciotffk(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_taordiq61jj46 gm_f_locate_ciotffk_c(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_make_fill_0p8dma0(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_make_fill_jpvm0kc(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint8_t a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mem_deinit_0088958(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_memory_dqom95e(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_skgqa77(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_pages_p5tqfl1(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_page_of_artfs32(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_20a7t0m(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_t934k1j7u70ct gm_f_read_u6r4vns_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) uint64_t gm_f_release_page_66inn9t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_truncate_ucmmehv(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_write_5gub0ul(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) gm_tgfdnufetdnfc gm_f_write_5gub0ul_c(void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_t934k1j7u70ct gm_f_write_view_pebit6h_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
extern uint8_t gm_c0n6vj9dmoi[8] __attribute__((aligned(8)));
extern uint8_t gm_c2ftoibr66a[8] __attribute__((aligned(8)));
extern uint8_t gm_c2t30gdg5i0[8] __attribute__((aligned(8)));
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_ckasdtpni1c[8] __attribute__((aligned(8)));
extern uint8_t gm_cmml7o4if0q[8] __attribute__((aligned(8)));
extern uint8_t gm_k0ddgrnig17[40] __attribute__((aligned(8)));
extern uint8_t gm_k2afmplo64l[40] __attribute__((aligned(8)));
extern uint8_t gm_k3ho3jie8mm[40] __attribute__((aligned(8)));
extern uint8_t gm_ke9p7o91039[40] __attribute__((aligned(8)));
extern uint8_t gm_keudd4gct2b[40] __attribute__((aligned(8)));
extern uint8_t gm_kgubkej3sov[40] __attribute__((aligned(8)));
extern uint8_t gm_kib6nfi69s7[40] __attribute__((aligned(8)));
extern uint8_t gm_kjq4sbqctak[40] __attribute__((aligned(8)));
extern uint8_t gm_km03kohh9a4[40] __attribute__((aligned(8)));
extern uint8_t gm_koniosmdv9m[40] __attribute__((aligned(8)));
extern uint8_t gm_kpmakm2t9q3[40] __attribute__((aligned(8)));
extern uint8_t gm_krsnqpdcjf4[40] __attribute__((aligned(8)));
extern uint8_t gm_ks0d3nb0eoq[40] __attribute__((aligned(8)));
extern const uint8_t gm_sacs352aeakr8[18];
extern const uint8_t gm_sagd3osht35og[13];
extern const uint8_t gm_sdvkaj73n9goh[24];
extern const uint8_t gm_skt7g4v99o4j0[42];
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcq7ohh8h77bn7[];
extern const uint8_t gm_suklu1i9clb5l[48];
extern const gm_type gm_tyo86ks9qkvt;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_page_of_artfs32(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_page_of_artfs32 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* p */
    GM_STACK_CHECK(&F, 171u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
        uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
        uint64_t t7 = r5;
        uint64_t t8 = GM_LD(uint64_t, gm_c6p4chcutca);
        unsigned __int128 n9 = (unsigned __int128)t8;
        if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 178, 10);
        uint64_t t10 = (uint64_t)(t7 >> (int)n9);
        gm_l2 = t10;
    }
    {
        uint64_t t12 = gm_l2;
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
        uint64_t t22 = gm_l2;
        uint64_t t23 = gm_f_us_cuv01fp(t22);
        uint64_t t24 = (uintptr_t)t23;
        void * t25 = (void *)((uint8_t *)t21 + t24 * 32);
        uint8_t t26 = 0;
        F.ret = t25;
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
    GM_STACK_CHECK(&F, 566u);
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
            gm_t934k1j7u70ct t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
            ((uint8_t *)&t9)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 8), t8);
            gm_t934k1j7u70ct t10 = t9;
            uint8_t t11 = ((((uint8_t *)&t10))[0] != 0);
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t13 = gm_l1;
        uint64_t t14 = gm_l3;
        uint8_t t15 = gm_f_aligned_q6sor4h(t13, t14);
        if (!t15) goto ie12;
        {
            void * t18 = gm_l0;
            uint64_t t19 = gm_l1;
            uint64_t t20 = gm_l2;
            void * t21 = gm_f_view_qalkbv1(t18, t19, t20, ((uint8_t)0));
            void * t22 = t21;
            if (!(GM_LD(void *, ((uint8_t *)&t22)) != 0)) goto ie17;
            void * t23 = GM_LD(void *, ((uint8_t *)&t22));
            gm_l4 = t23;
            {
                void * t25 = gm_l4;
                void * t26 = ((uint8_t *)t25);
                uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t28 = gm_l2;
                uint64_t t29 = gm_f_us_cuv01fp(t28);
                uint64_t t30 = (uintptr_t)t29;
                gm_b16_8 t31;
                GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t26 + t27 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30 - t27));
                gm_t934k1j7u70ct t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 40, 0);
                ((uint8_t *)&t32)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t32 + 8), t31);
                gm_t934k1j7u70ct t33 = t32;
                uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
                F.ret = t32;
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t35 = gm_l0;
        uint64_t t36 = gm_l1;
        uint64_t t37 = gm_l2;
        uint64_t t38 = gm_l3;
        gm_tpekhfs32op0f t39;
        gm_f_fault_u31b92l((void *)&t39, t35, t36, t37, t38, ((uint8_t)0));
        gm_t934k1j7u70ct t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        ((uint8_t *)&t40)[0] = 1;
        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t40 + 8), t39);
        gm_t934k1j7u70ct t41 = t40;
        uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
        F.ret = t40;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
    GM_STACK_CHECK(&F, 569u);
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
        void * t28 = t27;
        void * t29;
        if ((GM_LD(void *, ((uint8_t *)&t28)) != 0)) {
            void * t30 = GM_LD(void *, (((uint8_t *)&t28) + 0));
            t29 = t30;
        } else {
            uint8_t t31 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l6 = t29;
    }
    {
        uint64_t t32 = gm_l1;
        void * t33 = gm_l5;
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 8));
        uint64_t r35; bool o36 = __builtin_sub_overflow(t32, t34, &r35);
        if (o36) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
        uint64_t t37 = r35;
        gm_l7 = t37;
    }
    {
        uint64_t t38 = gm_l7;
        void * t39 = gm_l5;
        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 16));
        unsigned __int128 n41 = (unsigned __int128)t40;
        if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcq7ohh8h77bn7, 211, 10);
        uint64_t t42 = (uint64_t)(t38 >> (int)n41);
        gm_l8 = t42;
    }
    {
        void * t43 = gm_l6;
        void * t44 = t43;
        uint64_t t45 = gm_l8;
        uint64_t t46 = gm_f_us_cuv01fp(t45);
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = (void *)((uint8_t *)t44 + t47 * 24);
        gm_l9 = t48;
    }
    {
        uint64_t t49 = gm_l7;
        uint64_t t50 = gm_l8;
        void * t51 = gm_l5;
        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)t51) + 16));
        unsigned __int128 n53 = (unsigned __int128)t52;
        if (n53 >= 64) gm_trap_shift((uint64_t)n53, "u64", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t r54 = (uint64_t)((uint64_t)t50 << (int)n53);
        bool o55 = (uint64_t)(r54 >> (int)n53) != t50;
        if (o55) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t t56 = r54;
        uint64_t r57; bool o58 = __builtin_sub_overflow(t49, t56, &r57);
        if (o58) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
        uint64_t t59 = r57;
        gm_l10 = t59;
    }
    {
        uint8_t t61 = 0;
        uint8_t t62 = 0;
        uint8_t t63 = 0;
        void * t64 = gm_l9;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)t64) + 17));
        uint8_t t66 = (uint8_t)!t65;
        if (!t66) {
            uint64_t t67 = gm_l2;
            void * t68 = gm_l9;
            uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)t68) + 8));
            uint8_t t70 = (t67 > t69);
            t63 = t70;
        } else t63 = 1;
        if (!t63) {
            uint64_t t71 = gm_l10;
            void * t72 = gm_l9;
            uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)t72) + 8));
            uint64_t t74 = gm_l2;
            uint64_t r75; bool o76 = __builtin_sub_overflow(t73, t74, &r75);
            if (o76) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
            uint64_t t77 = r75;
            uint8_t t78 = (t71 > t77);
            t62 = t78;
        } else t62 = 1;
        if (!t62) {
            uint8_t t79 = 0;
            uint8_t t80 = gm_l3;
            if (t80) {
                void * t81 = gm_l9;
                uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)t81) + 18));
                uint8_t t83 = (uint8_t)!t82;
                t79 = t83;
            } else t79 = 0;
            t61 = t79;
        } else t61 = 1;
        if (!t61) goto ie60;
        {
            uint8_t t85 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix60;
ie60:;
ix60:;
    }
    {
        void * t86 = gm_l9;
        void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
        void * t88 = t87;
        uint64_t t89 = gm_l10;
        uint64_t t90 = gm_f_us_cuv01fp(t89);
        uint64_t t91 = (uintptr_t)t90;
        void * t92 = (void *)((uint8_t *)t88 + t91 * 1);
        void * t93 = t92;
        uint8_t t94 = 0;
        F.ret = t93;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_view_pebit6h F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 566u);
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
            gm_t934k1j7u70ct t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
            ((uint8_t *)&t9)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 8), t8);
            gm_t934k1j7u70ct t10 = t9;
            uint8_t t11 = ((((uint8_t *)&t10))[0] != 0);
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t13 = gm_l1;
        uint64_t t14 = gm_l3;
        uint8_t t15 = gm_f_aligned_q6sor4h(t13, t14);
        if (!t15) goto ie12;
        {
            void * t18 = gm_l0;
            uint64_t t19 = gm_l1;
            uint64_t t20 = gm_l2;
            void * t21 = gm_f_view_qalkbv1(t18, t19, t20, ((uint8_t)1));
            void * t22 = t21;
            if (!(GM_LD(void *, ((uint8_t *)&t22)) != 0)) goto ie17;
            void * t23 = GM_LD(void *, ((uint8_t *)&t22));
            gm_l4 = t23;
            {
                void * t25 = gm_l4;
                void * t26 = ((uint8_t *)t25);
                uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t28 = gm_l2;
                uint64_t t29 = gm_f_us_cuv01fp(t28);
                uint64_t t30 = (uintptr_t)t29;
                gm_b16_8 t31;
                GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t26 + t27 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30 - t27));
                gm_t934k1j7u70ct t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 40, 0);
                ((uint8_t *)&t32)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t32 + 8), t31);
                gm_t934k1j7u70ct t33 = t32;
                uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
                F.ret = t32;
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t35 = gm_l0;
        uint64_t t36 = gm_l1;
        uint64_t t37 = gm_l2;
        uint64_t t38 = gm_l3;
        gm_tpekhfs32op0f t39;
        gm_f_fault_u31b92l((void *)&t39, t35, t36, t37, t38, ((uint8_t)1));
        gm_t934k1j7u70ct t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        ((uint8_t *)&t40)[0] = 1;
        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t40 + 8), t39);
        gm_t934k1j7u70ct t41 = t40;
        uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
        F.ret = t40;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_alloc_bfo8187(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3, uint8_t a4) {
    struct gm_fr_alloc_bfo8187 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* size */
    uint64_t gm_l2; /* align */
    gm_t9qkh0rsooc8d gm_l3; /* kind */
    uint8_t gm_l4; /* mutable */
    gm_b16_8 gm_l5; /* data */
    gm_tbls6nq5r2dk1 gm_l6; /* slot */
    uint64_t gm_l7; /* need */
    uint64_t gm_l8; /* shift */
    uint64_t gm_l9; /* c */
    uint32_t gm_l10; /* per */
    uint64_t gm_l11; /* addr */
    gm_b16_8 gm_l12; /* slots */
    void * gm_l13; /* pg */
    uint32_t gm_l14; /* i */
    void * gm_l15; /* ss */
    uint64_t gm_l16; /* n */
    uint64_t gm_l17; /* addr */
    void * gm_l18; /* one */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    GM_STACK_CHECK(&F, 3155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint64_t t3 = gm_l1;
        unsigned __int128 n4 = (unsigned __int128)((uint64_t)1ULL);
        if (n4 >= 64) gm_trap_shift((uint64_t)n4, "usize", gm_srcq7ohh8h77bn7, 125, 19);
        uint64_t t5 = (uint64_t)(((uint64_t)18446744073709551615ULL) >> (int)n4);
        uint64_t t6 = (uint64_t)t5;
        uint8_t t7 = (t3 > t6);
        if (!t7) goto ie2;
        {
            uint64_t t9 = gm_l1;
            gm_t9qkh0rsooc8d t10 = gm_l3;
            gm_t617vr72bd7b3 t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t11 + 0), t9);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 8), t10);
            gm_tpekhfs32op0f t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)7ULL));
            GM_ST(gm_t617vr72bd7b3, ((uint8_t *)&t12 + 8), t11);
            gm_t3j6egd6cfnk5 t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t13 + 8), t12);
            gm_t3j6egd6cfnk5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t16 = gm_f_empty_slice_ijtp9lo();
        gm_l5 = t16;
    }
    {
        uint64_t t18 = gm_l1;
        uint8_t t19 = (t18 > ((uint64_t)0ULL));
        if (!t19) goto ie17;
        {
            uint64_t t21 = gm_l1;
            uint64_t t22 = gm_f_us_cuv01fp(t21);
            gm_tabb710d9mq2e t23;
            gm_f_make_fill_jpvm0kc((void *)&t23, (void *)NULL, cx, NULL, t22, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kib6nfi69s7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t24 = t23;
            gm_b16_8 t25;
            if (!((((uint8_t *)&t24))[0] != 0)) {
                gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
                t25 = t26;
            } else {
                uint64_t t27 = gm_l1;
                gm_t9qkh0rsooc8d t28 = gm_l3;
                gm_t617vr72bd7b3 t29;
                GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t29 + 0), t27);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
                gm_tpekhfs32op0f t30;
                GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t30 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t30 + 32, 0);
                GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)7ULL));
                GM_ST(gm_t617vr72bd7b3, ((uint8_t *)&t30 + 8), t29);
                gm_t3j6egd6cfnk5 t31;
                GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                ((uint8_t *)&t31)[0] = 1;
                GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t31 + 8), t30);
                gm_t3j6egd6cfnk5 t32 = t31;
                uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
                F.ret = t31;
                goto gm_fret;
            }
            gm_l5 = t25;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        memset((uint8_t *)&gm_l6 + 20, 0, 4);
        void * t34 = GM_LD(void *, ((uint8_t *)&gm_l5));
        GM_ST(void *, (uint8_t *)&gm_l6 + 0, t34);
        uint64_t t35 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&gm_l6 + 8, t35);
        GM_ST(uint8_t, (uint8_t *)&gm_l6 + 16, ((uint8_t)1));
        GM_ST(uint8_t, (uint8_t *)&gm_l6 + 17, ((uint8_t)1));
        uint8_t t36 = gm_l4;
        GM_ST(uint8_t, (uint8_t *)&gm_l6 + 18, t36);
        gm_t9qkh0rsooc8d t37 = gm_l3;
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&gm_l6 + 19, t37);
    }
    {
        uint64_t t38 = gm_l1;
        uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
        if (o40) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 130, 14);
        uint64_t t41 = r39;
        uint64_t t42 = gm_l2;
        uint64_t t43 = (t41 > t42 ? t41 : t42);
        gm_l7 = t43;
    }
    {
        uint64_t t45 = gm_l7;
        uint64_t t46 = GM_LD(uint64_t, gm_c0n6vj9dmoi);
        unsigned __int128 n47 = (unsigned __int128)t46;
        if (n47 >= 64) gm_trap_shift((uint64_t)n47, "u64", gm_srcq7ohh8h77bn7, 131, 17);
        uint64_t r48 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n47);
        bool o49 = (uint64_t)(r48 >> (int)n47) != ((uint64_t)1ULL);
        if (o49) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 131, 17);
        uint64_t t50 = r48;
        uint8_t t51 = (t45 <= t50);
        if (!t51) goto ie44;
        {
            uint64_t t53 = GM_LD(uint64_t, gm_cmml7o4if0q);
            gm_l8 = t53;
        }
        {
            for (;;) {
                uint64_t t55 = gm_l8;
                unsigned __int128 n56 = (unsigned __int128)t55;
                if (n56 >= 64) gm_trap_shift((uint64_t)n56, "u64", gm_srcq7ohh8h77bn7, 133, 16);
                uint64_t r57 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n56);
                bool o58 = (uint64_t)(r57 >> (int)n56) != ((uint64_t)1ULL);
                if (o58) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 133, 16);
                uint64_t t59 = r57;
                uint64_t t60 = gm_l7;
                uint8_t t61 = (t59 < t60);
                if (!t61) goto lb54;
                {
                    uint64_t t64 = gm_l8;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                    if (o66) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 133, 42);
                    uint64_t t67 = r65;
                    gm_l8 = t67;
                }
lc54:;
            }
lb54:;
        }
        {
            uint64_t t68 = gm_l8;
            uint64_t t69 = GM_LD(uint64_t, gm_cmml7o4if0q);
            uint64_t r70; bool o71 = __builtin_sub_overflow(t68, t69, &r70);
            if (o71) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 134, 17);
            uint64_t t72 = r70;
            uint64_t t73 = gm_f_us_cuv01fp(t72);
            gm_l9 = t73;
        }
        {
            uint64_t t74 = GM_LD(uint64_t, gm_ckasdtpni1c);
            uint64_t t75 = gm_l8;
            unsigned __int128 n76 = (unsigned __int128)t75;
            if (n76 >= 64) gm_trap_shift((uint64_t)n76, "u64", gm_srcq7ohh8h77bn7, 135, 17);
            uint64_t t77 = (uint64_t)(t74 >> (int)n76);
            uint32_t t78 = (uint32_t)t77;
            gm_l10 = t78;
        }
        {
            uint8_t t80 = 0;
            uint64_t t81 = gm_l9;
            uint64_t t82 = (uintptr_t)t81;
            void * t83 = gm_l0;
            if (t82 >= (uintptr_t)(8)) gm_trap_index(t82, 8, gm_srcq7ohh8h77bn7, 136, 12);
            uint64_t t84 = GM_LD(uint64_t, ((((uint8_t *)t83) + 56) + t82 * 8));
            uint8_t t85 = ((t84 == ((uint64_t)0ULL)));
            if (!t85) {
                uint64_t t86 = gm_l9;
                uint64_t t87 = (uintptr_t)t86;
                void * t88 = gm_l0;
                if (t87 >= (uintptr_t)(8)) gm_trap_index(t87, 8, gm_srcq7ohh8h77bn7, 136, 44);
                uint64_t t89 = GM_LD(uint64_t, ((((uint8_t *)t88) + 56) + t87 * 8));
                uint64_t r90; bool o91 = __builtin_sub_overflow(t89, ((uint64_t)1ULL), &r90);
                if (o91) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 136, 44);
                uint64_t t92 = r90;
                uint64_t t93 = (uintptr_t)t92;
                void * t94 = gm_l0;
                uint64_t t95 = GM_LD(uintptr_t, ((((uint8_t *)t94) + 0) + 0) + 8);
                if (t93 >= (uintptr_t)(t95)) gm_trap_index(t93, t95, gm_srcq7ohh8h77bn7, 136, 30);
                uint32_t t96 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t94) + 0) + 0)) + t93 * 32) + 24));
                uint32_t t97 = gm_l10;
                uint8_t t98 = ((t96 == t97));
                t80 = t98;
            } else t80 = 1;
            if (!t80) goto ie79;
            {
                void * t100 = gm_l0;
                gm_tf5srh4mp4b3i t101 = gm_f_next_pages_p5tqfl1(t100, ((uint64_t)1ULL));
                gm_tf5srh4mp4b3i t102 = t101;
                uint64_t t103;
                if (((((uint8_t *)&t102))[8] != 0)) {
                    uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&t102) + 0));
                    t103 = t104;
                } else {
                    {
                        gm_b16_8 t106 = gm_l5;
                        gm_b16_8 t107 = t106;
                        gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
                        gm_b0_1 t109 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t108, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_koniosmdv9m)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_tpekhfs32op0f t110;
                    memset(&t110, 0, sizeof t110);
                    GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)6ULL));
                    gm_t3j6egd6cfnk5 t111;
                    GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                    ((uint8_t *)&t111)[0] = 1;
                    GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t111 + 8), t110);
                    gm_t3j6egd6cfnk5 t112 = t111;
                    uint8_t t113 = ((((uint8_t *)&t112))[0] != 0);
                    F.ret = t111;
                    goto gm_fret;
                }
                gm_l11 = t103;
            }
            {
                uint32_t t114 = gm_l10;
                uint64_t t115 = (uint64_t)t114;
                gm_tbls6nq5r2dk1 t116;
                memset((uint8_t *)&t116 + 20, 0, 4);
                void * t117 = GM_LD(void *, ((uint8_t *)&gm_l5));
                GM_ST(void *, (uint8_t *)&t116 + 0, t117);
                GM_ST(uint64_t, (uint8_t *)&t116 + 8, ((uint64_t)0ULL));
                GM_ST(uint8_t, (uint8_t *)&t116 + 16, ((uint8_t)0));
                GM_ST(uint8_t, (uint8_t *)&t116 + 17, ((uint8_t)0));
                GM_ST(uint8_t, (uint8_t *)&t116 + 18, ((uint8_t)0));
                gm_t9qkh0rsooc8d t118;
                GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t116 + 19, t118);
                gm_tabb710d9mq2e t119;
                gm_f_make_fill_0p8dma0((void *)&t119, (void *)NULL, cx, NULL, t115, &t116, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjq4sbqctak)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t120 = t119;
                gm_b16_8 t121;
                if (!((((uint8_t *)&t120))[0] != 0)) {
                    gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&t120) + 8));
                    t121 = t122;
                } else {
                    {
                        gm_b16_8 t124 = gm_l5;
                        gm_b16_8 t125 = t124;
                        gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                        gm_b0_1 t127 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t126, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kpmakm2t9q3)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint64_t t128 = gm_l1;
                    gm_t9qkh0rsooc8d t129 = gm_l3;
                    gm_t617vr72bd7b3 t130;
                    GM_ST(uint8_t, (uint8_t *)&t130 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t130 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t130 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t130 + 0), t128);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t130 + 8), t129);
                    gm_tpekhfs32op0f t131;
                    GM_ST(uint8_t, (uint8_t *)&t131 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t131 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t131 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t131 + 32, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)7ULL));
                    GM_ST(gm_t617vr72bd7b3, ((uint8_t *)&t131 + 8), t130);
                    gm_t3j6egd6cfnk5 t132;
                    GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t132 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t132 + 4, 0);
                    ((uint8_t *)&t132)[0] = 1;
                    GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t132 + 8), t131);
                    gm_t3j6egd6cfnk5 t133 = t132;
                    uint8_t t134 = ((((uint8_t *)&t133))[0] != 0);
                    F.ret = t132;
                    goto gm_fret;
                }
                gm_l12 = t121;
            }
            {
                void * t135 = gm_l0;
                void * t136 = (void *)(((uint8_t *)t135) + 0);
                gm_t2i0cb8fcu1gb t137;
                void * t138 = GM_LD(void *, ((uint8_t *)&gm_l12));
                GM_ST(void *, (uint8_t *)&t137 + 0, t138);
                uint64_t t139 = gm_l11;
                GM_ST(uint64_t, (uint8_t *)&t137 + 8, t139);
                uint64_t t140 = gm_l8;
                GM_ST(uint64_t, (uint8_t *)&t137 + 16, t140);
                GM_ST(uint32_t, (uint8_t *)&t137 + 24, ((uint32_t)0ULL));
                GM_ST(uint32_t, (uint8_t *)&t137 + 28, ((uint32_t)0ULL));
                gm_tftb1odc739t5 t141 = gm_f_push_20a7t0m(t136, &t137);
                gm_tftb1odc739t5 t142 = t141;
                if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 1, gm_srcq7ohh8h77bn7, 145, 13);
                gm_b0_1 t143 = GM_LD(gm_b0_1, (((uint8_t *)&t142) + 1));
            }
            {
                uint64_t t144 = gm_l9;
                uint64_t t145 = (uintptr_t)t144;
                void * t146 = gm_l0;
                if (t145 >= (uintptr_t)(8)) gm_trap_index(t145, 8, gm_srcq7ohh8h77bn7, 146, 13);
                void * t147 = gm_l0;
                uint64_t t148 = GM_LD(uintptr_t, ((((uint8_t *)t147) + 0) + 0) + 8);
                GM_ST(uint64_t, ((((uint8_t *)t146) + 56) + t145 * 8), t148);
            }
            goto ix79;
ie79:;
ix79:;
        }
        {
            uint64_t t149 = gm_l9;
            uint64_t t150 = (uintptr_t)t149;
            void * t151 = gm_l0;
            if (t150 >= (uintptr_t)(8)) gm_trap_index(t150, 8, gm_srcq7ohh8h77bn7, 148, 30);
            uint64_t t152 = GM_LD(uint64_t, ((((uint8_t *)t151) + 56) + t150 * 8));
            uint64_t r153; bool o154 = __builtin_sub_overflow(t152, ((uint64_t)1ULL), &r153);
            if (o154) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 148, 30);
            uint64_t t155 = r153;
            uint64_t t156 = (uintptr_t)t155;
            void * t157 = gm_l0;
            uint64_t t158 = GM_LD(uintptr_t, ((((uint8_t *)t157) + 0) + 0) + 8);
            if (t156 >= (uintptr_t)(t158)) gm_trap_index(t156, t158, gm_srcq7ohh8h77bn7, 148, 16);
            void * t159 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t157) + 0) + 0)) + t156 * 32);
            gm_l13 = t159;
        }
        {
            void * t160 = gm_l13;
            uint32_t t161 = GM_LD(uint32_t, (((uint8_t *)t160) + 24));
            gm_l14 = t161;
        }
        {
            void * t162 = gm_l13;
            void * t163 = gm_l13;
            uint32_t t164 = GM_LD(uint32_t, (((uint8_t *)t163) + 24));
            uint32_t r165; bool o166 = __builtin_add_overflow(t164, ((uint32_t)1ULL), &r165);
            if (o166) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 150, 9);
            uint32_t t167 = r165;
            GM_ST(uint32_t, (((uint8_t *)t162) + 24), t167);
        }
        {
            void * t169 = gm_l13;
            void * t170 = GM_LD(void *, (((uint8_t *)t169) + 0));
            void * t171 = t170;
            if (!(GM_LD(void *, ((uint8_t *)&t171)) != 0)) goto ie168;
            void * t172 = GM_LD(void *, ((uint8_t *)&t171));
            gm_l15 = t172;
            {
                uint32_t t174 = gm_l14;
                uint64_t t175 = (uint64_t)t174;
                uint64_t t176 = (uintptr_t)t175;
                void * t177 = gm_l15;
                gm_tbls6nq5r2dk1 t178 = gm_l6;
                GM_ST(gm_tbls6nq5r2dk1, ((uint8_t *)t177 + t176 * 24), t178);
            }
            goto ix168;
ie168:;
ix168:;
        }
        {
            void * t179 = gm_l13;
            uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)t179) + 8));
            uint32_t t181 = gm_l14;
            uint64_t t182 = (uint64_t)t181;
            uint64_t t183 = gm_l8;
            unsigned __int128 n184 = (unsigned __int128)t183;
            if (n184 >= 64) gm_trap_shift((uint64_t)n184, "u64", gm_srcq7ohh8h77bn7, 152, 27);
            uint64_t r185 = (uint64_t)((uint64_t)t182 << (int)n184);
            bool o186 = (uint64_t)(r185 >> (int)n184) != t182;
            if (o186) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 152, 27);
            uint64_t t187 = r185;
            uint64_t r188; bool o189 = __builtin_add_overflow(t180, t187, &r188);
            if (o189) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 152, 16);
            uint64_t t190 = r188;
            gm_t3j6egd6cfnk5 t191;
            memset(&t191, 0, sizeof t191);
            ((uint8_t *)&t191)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t191 + 8), t190);
            gm_t3j6egd6cfnk5 t192 = t191;
            uint8_t t193 = ((((uint8_t *)&t192))[0] != 0);
            F.ret = t191;
            goto gm_fret;
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        for (;;) {
            void * t195 = gm_l0;
            uint64_t t196 = GM_LD(uint64_t, (((uint8_t *)t195) + 40));
            void * t197 = gm_l0;
            uint64_t t198 = GM_LD(uintptr_t, ((((uint8_t *)t197) + 0) + 0) + 8);
            uint64_t t199 = (uint64_t)t198;
            uint64_t t200 = GM_LD(uint64_t, gm_ckasdtpni1c);
            uint64_t r201; bool o202 = __builtin_mul_overflow(t199, t200, &r201);
            if (o202) gm_trap("integer overflow in `*`", gm_srcq7ohh8h77bn7, 155, 21);
            uint64_t t203 = r201;
            uint64_t r204; bool o205 = __builtin_add_overflow(t196, t203, &r204);
            if (o205) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 155, 12);
            uint64_t t206 = r204;
            uint64_t t207 = gm_l2;
            uint64_t t208 = (t207 > ((uint64_t)1ULL) ? t207 : ((uint64_t)1ULL));
            if (t208 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 155, 11);
            uint64_t t209 = (uint64_t)(t206 % t208);
            uint8_t t210 = (!(t209 == ((uint64_t)0ULL)));
            if (!t210) goto lb194;
            {
                {
                    void * t214 = gm_l0;
                    gm_tf5srh4mp4b3i t215 = gm_f_next_pages_p5tqfl1(t214, ((uint64_t)1ULL));
                    gm_tf5srh4mp4b3i t216 = t215;
                    uint64_t t217;
                    if (((((uint8_t *)&t216))[8] != 0)) {
                        uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&t216) + 0));
                        t217 = t218;
                    } else {
                        {
                            gm_b16_8 t220 = gm_l5;
                            gm_b16_8 t221 = t220;
                            gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&t221));
                            gm_b0_1 t223 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t222, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3ho3jie8mm)));
                            if (gm_unw.kind) {
                                goto gm_ll211;
                            }
                        }
                        gm_tpekhfs32op0f t224;
                        memset(&t224, 0, sizeof t224);
                        GM_ST(uint8_t, ((uint8_t *)&t224 + 0), ((uint8_t)6ULL));
                        gm_t3j6egd6cfnk5 t225;
                        GM_ST(uint8_t, (uint8_t *)&t225 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t225 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t225 + 4, 0);
                        ((uint8_t *)&t225)[0] = 1;
                        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t225 + 8), t224);
                        gm_t3j6egd6cfnk5 t226 = t225;
                        uint8_t t227 = ((((uint8_t *)&t226))[0] != 0);
                        F.ret = t225;
                        goto gm_fret;
                    }
                }
            }
            {
                void * t228 = gm_l0;
                void * t229 = (void *)(((uint8_t *)t228) + 0);
                gm_t2i0cb8fcu1gb t230;
                GM_ST(void *, (uint8_t *)&t230 + 0, ((void *)0));
                GM_ST(uint64_t, (uint8_t *)&t230 + 8, ((uint64_t)0ULL));
                uint64_t t231 = GM_LD(uint64_t, gm_c2t30gdg5i0);
                GM_ST(uint64_t, (uint8_t *)&t230 + 16, t231);
                GM_ST(uint32_t, (uint8_t *)&t230 + 24, ((uint32_t)0ULL));
                GM_ST(uint32_t, (uint8_t *)&t230 + 28, ((uint32_t)0ULL));
                gm_tftb1odc739t5 t232 = gm_f_push_20a7t0m(t229, &t230);
                gm_tftb1odc739t5 t233 = t232;
                if (((((uint8_t *)&t233))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t233) + 1, gm_srcq7ohh8h77bn7, 160, 9);
                gm_b0_1 t234 = GM_LD(gm_b0_1, (((uint8_t *)&t233) + 1));
            }
lc194:;
        }
gm_ll211:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb194; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc194; }
        }
        goto gm_lad;
lb194:;
    }
    {
        uint64_t t235 = gm_l1;
        uint64_t t236 = GM_LD(uint64_t, gm_ckasdtpni1c);
        if (t236 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 162, 10);
        uint64_t t237 = (uint64_t)(t235 / t236);
        uint64_t r238; bool o239 = __builtin_add_overflow(t237, ((uint64_t)1ULL), &r238);
        if (o239) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 162, 10);
        uint64_t t240 = r238;
        gm_l16 = t240;
    }
    {
        void * t241 = gm_l0;
        uint64_t t242 = gm_l16;
        gm_tf5srh4mp4b3i t243 = gm_f_next_pages_p5tqfl1(t241, t242);
        gm_tf5srh4mp4b3i t244 = t243;
        uint64_t t245;
        if (((((uint8_t *)&t244))[8] != 0)) {
            uint64_t t246 = GM_LD(uint64_t, (((uint8_t *)&t244) + 0));
            t245 = t246;
        } else {
            {
                gm_b16_8 t248 = gm_l5;
                gm_b16_8 t249 = t248;
                gm_b16_8 t250 = GM_LD(gm_b16_8, ((uint8_t *)&t249));
                gm_b0_1 t251 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t250, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krsnqpdcjf4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_tpekhfs32op0f t252;
            memset(&t252, 0, sizeof t252);
            GM_ST(uint8_t, ((uint8_t *)&t252 + 0), ((uint8_t)6ULL));
            gm_t3j6egd6cfnk5 t253;
            GM_ST(uint8_t, (uint8_t *)&t253 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t253 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t253 + 4, 0);
            ((uint8_t *)&t253)[0] = 1;
            GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t253 + 8), t252);
            gm_t3j6egd6cfnk5 t254 = t253;
            uint8_t t255 = ((((uint8_t *)&t254))[0] != 0);
            F.ret = t253;
            goto gm_fret;
        }
        gm_l17 = t245;
    }
    {
        gm_tq7s98ihonkju t256 = gm_f_new_skgqa77((void *)NULL, cx, NULL, &(*(gm_tbls6nq5r2dk1 *)((uint8_t *)&gm_l6)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_keudd4gct2b)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t257 = t256;
        void * t258;
        if (!((((uint8_t *)&t257))[0] != 0)) {
            void * t259 = GM_LD(void *, (((uint8_t *)&t257) + 8));
            t258 = t259;
        } else {
            {
                gm_b16_8 t261 = gm_l5;
                gm_b16_8 t262 = t261;
                gm_b16_8 t263 = GM_LD(gm_b16_8, ((uint8_t *)&t262));
                gm_b0_1 t264 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t263, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_km03kohh9a4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint64_t t265 = gm_l1;
            gm_t9qkh0rsooc8d t266 = gm_l3;
            gm_t617vr72bd7b3 t267;
            GM_ST(uint8_t, (uint8_t *)&t267 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t267 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t267 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t267 + 0), t265);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t267 + 8), t266);
            gm_tpekhfs32op0f t268;
            GM_ST(uint8_t, (uint8_t *)&t268 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t268 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t268 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t268 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t268 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t268 + 0), ((uint8_t)7ULL));
            GM_ST(gm_t617vr72bd7b3, ((uint8_t *)&t268 + 8), t267);
            gm_t3j6egd6cfnk5 t269;
            GM_ST(uint8_t, (uint8_t *)&t269 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t269 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t269 + 4, 0);
            ((uint8_t *)&t269)[0] = 1;
            GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t269 + 8), t268);
            gm_t3j6egd6cfnk5 t270 = t269;
            uint8_t t271 = ((((uint8_t *)&t270))[0] != 0);
            F.ret = t269;
            goto gm_fret;
        }
        gm_l18 = t258;
    }
    {
        {
            gm_l20 = ((uint64_t)0ULL);
        }
        {
            uint64_t t273 = gm_l16;
            uint64_t t274 = gm_f_us_cuv01fp(t273);
            gm_l19 = t274;
        }
        {
            for (;;) {
                uint64_t t276 = gm_l20;
                uint64_t t277 = gm_l19;
                uint8_t t278 = (t276 < t277);
                if (!t278) goto lb275;
                {
                    uint64_t t281 = gm_l20;
                    uint64_t t282 = t281;
                    {
                        void * t286 = gm_l0;
                        void * t287 = (void *)(((uint8_t *)t286) + 0);
                        gm_t2i0cb8fcu1gb t288;
                        void * t289 = gm_l18;
                        if ((uintptr_t)t289 % 8 != 0) gm_trap_misaligned((uintptr_t)t289, 8, gm_srcq7ohh8h77bn7, 171, 50);
                        void * t290 = t289;
                        GM_ST(void *, (uint8_t *)&t288 + 0, t290);
                        uint64_t t291 = gm_l17;
                        GM_ST(uint64_t, (uint8_t *)&t288 + 8, t291);
                        uint64_t t292 = GM_LD(uint64_t, gm_c2t30gdg5i0);
                        GM_ST(uint64_t, (uint8_t *)&t288 + 16, t292);
                        GM_ST(uint32_t, (uint8_t *)&t288 + 24, ((uint32_t)1ULL));
                        GM_ST(uint32_t, (uint8_t *)&t288 + 28, ((uint32_t)0ULL));
                        gm_tftb1odc739t5 t293 = gm_f_push_20a7t0m(t287, &t288);
                        gm_tftb1odc739t5 t294 = t293;
                        if (((((uint8_t *)&t294))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t294) + 1, gm_srcq7ohh8h77bn7, 171, 25);
                        gm_b0_1 t295 = GM_LD(gm_b0_1, (((uint8_t *)&t294) + 1));
                    }
                    goto me283;
mn284:;
                    gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 171, 5);
me283:;
                }
lc275:;
                {
                    uint64_t t297 = gm_l20;
                    uint64_t r298; bool o299 = __builtin_add_overflow(t297, ((uint64_t)1ULL), &r298);
                    uint64_t t300 = r298;
                    gm_l20 = t300;
                }
            }
lb275:;
        }
    }
    {
        uint64_t t301 = gm_l17;
        gm_t3j6egd6cfnk5 t302;
        memset(&t302, 0, sizeof t302);
        ((uint8_t *)&t302)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t302 + 8), t301);
        gm_t3j6egd6cfnk5 t303 = t302;
        uint8_t t304 = ((((uint8_t *)&t303))[0] != 0);
        F.ret = t302;
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
inline __attribute__((aligned(4))) void gm_f_describe_t62jhnt(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_describe_t62jhnt F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    gm_ttf6i6v1da8cm gm_l2; /* at */
    GM_STACK_CHECK(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_taordiq61jj46 t4;
        gm_f_locate_ciotffk((void *)&t4, t2, t3);
        gm_taordiq61jj46 t5 = t4;
        gm_ttf6i6v1da8cm t6;
        if (((((uint8_t *)&t5))[16] != 0)) {
            gm_ttf6i6v1da8cm t7 = GM_LD(gm_ttf6i6v1da8cm, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tqvpud5umqqp1 t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        gm_tilak255t5m34 t10;
        memset((uint8_t *)&t10 + 18, 0, 6);
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        GM_ST(uint64_t, (uint8_t *)&t10 + 0, t11);
        void * t12 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 8));
        GM_ST(uint64_t, (uint8_t *)&t10 + 8, t13);
        void * t14 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        gm_t9qkh0rsooc8d t15 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t14) + 19));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t10 + 16, t15);
        void * t16 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t16) + 17));
        GM_ST(uint8_t, (uint8_t *)&t10 + 17, t17);
        gm_tqvpud5umqqp1 t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 28, 0);
        GM_ST(gm_tilak255t5m34, ((uint8_t *)&t18), t10);
        ((uint8_t *)&t18)[24] = 1;
        uint8_t t19 = 0;
        F.ret = t18;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tqvpud5umqqp1 gm_f_describe_t62jhnt_c(void * a0, uint64_t a1) { gm_tqvpud5umqqp1 gm_r; gm_f_describe_t62jhnt(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_drop_oldest_o9uaetp(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_drop_oldest_o9uaetp F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* pi */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* k */
    GM_STACK_CHECK(&F, 507u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 160));
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 120) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcq7ohh8h77bn7, 287, 11);
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 120) + 0)) + t4 * 8));
        gm_l1 = t7;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 160));
        uint64_t r11; bool o12 = __builtin_add_overflow(t10, ((uint64_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 288, 5);
        uint64_t t13 = r11;
        GM_ST(uint64_t, (((uint8_t *)t8) + 160), t13);
    }
    {
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 160));
        uint8_t t17 = (t16 > ((uint64_t)1024ULL));
        if (!t17) goto ie14;
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 160));
        uint64_t r20; bool o21 = __builtin_mul_overflow(t19, ((uint64_t)2ULL), &r20);
        if (o21) gm_trap("integer overflow in `*`", gm_srcq7ohh8h77bn7, 289, 34);
        uint64_t t22 = r20;
        void * t23 = gm_l0;
        uint64_t t24 = GM_LD(uintptr_t, ((((uint8_t *)t23) + 120) + 0) + 8);
        uint8_t t25 = (t22 > t24);
        if (!t25) goto ie14;
        {
            void * t27 = gm_l0;
            uint64_t t28 = GM_LD(uintptr_t, ((((uint8_t *)t27) + 120) + 0) + 8);
            void * t29 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t27) + 120) + 0)));
            void * t30 = gm_l0;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 160));
            uint64_t t32 = (uintptr_t)t31;
            void * t33 = gm_l0;
            uint64_t t34 = GM_LD(uintptr_t, ((((uint8_t *)t33) + 120) + 0) + 8);
            uint64_t t35 = (uintptr_t)t34;
            if (t32 > t35 || t35 > (uintptr_t)t28) gm_trap_slice(t32, t35, t28, gm_srcq7ohh8h77bn7, 290, 17);
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t29 + t32 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(t35 - t32));
            gm_l2 = t36;
        }
        {
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                gm_l3 = t38;
            }
            {
                for (;;) {
                    uint64_t t40 = gm_l4;
                    uint64_t t41 = gm_l3;
                    uint8_t t42 = (t40 < t41);
                    if (!t42) goto lb39;
                    {
                        uint64_t t45 = gm_l4;
                        uint64_t t46 = t45;
                        uint64_t t49 = GM_LD(uint64_t, ((uint8_t *)&t46));
                        gm_l5 = t49;
                        {
                            uint64_t t51 = gm_l5;
                            uint64_t t52 = (uintptr_t)t51;
                            void * t53 = gm_l0;
                            uint64_t t54 = GM_LD(uintptr_t, ((((uint8_t *)t53) + 120) + 0) + 8);
                            if (t52 >= (uintptr_t)(t54)) gm_trap_index(t52, t54, gm_srcq7ohh8h77bn7, 291, 32);
                            uint64_t t55 = gm_l5;
                            uint64_t t56 = (uintptr_t)t55;
                            uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t56 >= (uintptr_t)(t57)) gm_trap_index(t56, t57, gm_srcq7ohh8h77bn7, 291, 53);
                            uint64_t t58 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t56 * 8));
                            GM_ST(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t53) + 120) + 0)) + t52 * 8), t58);
                        }
                        goto me47;
mn48:;
                        gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 291, 9);
me47:;
                    }
lc39:;
                    {
                        uint64_t t60 = gm_l4;
                        uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                        uint64_t t63 = r61;
                        gm_l4 = t63;
                    }
                }
lb39:;
            }
        }
        {
            void * t64 = gm_l0;
            void * t65 = (void *)(((uint8_t *)t64) + 120);
            uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_f_truncate_ucmmehv(t65, t66);
        }
        {
            void * t67 = gm_l0;
            GM_ST(uint64_t, (((uint8_t *)t67) + 160), ((uint64_t)0ULL));
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t68 = gm_l0;
        uint8_t *t69 = (((uint8_t *)t68) + 168);
        void * t70 = gm_l0;
        uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)t70) + 168));
        void * t72 = gm_l0;
        uint64_t t73 = gm_l1;
        uint64_t t74 = gm_f_release_page_66inn9t((void *)NULL, cx, NULL, t72, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t r75; bool o76 = __builtin_sub_overflow(t71, t74, &r75);
        if (o76) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 295, 5);
        uint64_t t77 = r75;
        GM_ST(uint64_t, t69, t77);
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
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fault_u31b92l F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    uint8_t gm_l4; /* write */
    gm_ttf6i6v1da8cm gm_l5; /* at */
    GM_STACK_CHECK(&F, 746u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint64_t t3 = gm_l1;
        uint64_t t4 = gm_l3;
        uint8_t t5 = gm_f_aligned_q6sor4h(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            uint64_t t8 = gm_l1;
            uint64_t t9 = gm_l3;
            gm_t2lqu8jsj511d t10;
            GM_ST(uint64_t, ((uint8_t *)&t10 + 0), t8);
            GM_ST(uint64_t, ((uint8_t *)&t10 + 8), t9);
            gm_tpekhfs32op0f t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)4ULL));
            GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t11 + 8), t10);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t14 = gm_l1;
        uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
        if (!t15) goto ie13;
        {
            gm_tpekhfs32op0f t17;
            memset(&t17, 0, sizeof t17);
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)0ULL));
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        void * t19 = gm_l0;
        uint64_t t20 = gm_l1;
        gm_taordiq61jj46 t21;
        gm_f_locate_ciotffk((void *)&t21, t19, t20);
        gm_taordiq61jj46 t22 = t21;
        gm_ttf6i6v1da8cm t23;
        if (((((uint8_t *)&t22))[16] != 0)) {
            gm_ttf6i6v1da8cm t24 = GM_LD(gm_ttf6i6v1da8cm, (((uint8_t *)&t22) + 0));
            t23 = t24;
        } else {
            uint64_t t25 = gm_l1;
            uint64_t t26 = gm_l2;
            gm_t2lqu8jsj511d t27;
            GM_ST(uint64_t, ((uint8_t *)&t27 + 0), t25);
            GM_ST(uint64_t, ((uint8_t *)&t27 + 8), t26);
            gm_tpekhfs32op0f t28;
            GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t28 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t28 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t28 + 8), t27);
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        gm_l5 = t23;
    }
    {
        void * t31 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)t31) + 17));
        uint8_t t33 = (uint8_t)!t32;
        if (!t33) goto ie30;
        {
            uint64_t t35 = gm_l1;
            void * t36 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
            gm_t9qkh0rsooc8d t37 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t36) + 19));
            gm_t617vr72bd7b3 t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t38 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t38 + 0), t35);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38 + 8), t37);
            gm_tpekhfs32op0f t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t39 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t39 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t617vr72bd7b3, ((uint8_t *)&t39 + 8), t38);
            uint8_t t40 = 0;
            F.ret = t39;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        uint64_t t42 = gm_l2;
        void * t43 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
        uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)t43) + 8));
        uint64_t t45 = gm_l1;
        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        uint64_t r47; bool o48 = __builtin_sub_overflow(t45, t46, &r47);
        if (o48) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 224, 30);
        uint64_t t49 = r47;
        uint64_t r50; bool o51 = __builtin_sub_overflow(t44, t49, &r50);
        if (o51) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 224, 14);
        uint64_t t52 = r50;
        uint8_t t53 = (t42 > t52);
        if (!t53) goto ie41;
        {
            gm_t66j1im9ip128 t55;
            uint64_t t56 = gm_l1;
            GM_ST(uint64_t, (uint8_t *)&t55 + 0, t56);
            uint64_t t57 = gm_l2;
            GM_ST(uint64_t, (uint8_t *)&t55 + 8, t57);
            uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            GM_ST(uint64_t, (uint8_t *)&t55 + 16, t58);
            void * t59 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
            uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)t59) + 8));
            GM_ST(uint64_t, (uint8_t *)&t55 + 24, t60);
            gm_tpekhfs32op0f t61;
            GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t66j1im9ip128, ((uint8_t *)&t61 + 8), t55);
            uint8_t t62 = 0;
            F.ret = t61;
            goto gm_fret;
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        uint8_t t64 = gm_l4;
        if (!t64) goto ie63;
        void * t65 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)t65) + 18));
        uint8_t t67 = (uint8_t)!t66;
        if (!t67) goto ie63;
        {
            uint64_t t69 = gm_l1;
            gm_tpekhfs32op0f t70;
            GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t70 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t70 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t70 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)5ULL));
            GM_ST(uint64_t, ((uint8_t *)&t70 + 8), t69);
            uint8_t t71 = 0;
            F.ret = t70;
            goto gm_fret;
        }
        goto ix63;
ie63:;
ix63:;
    }
    {
        uint64_t t72 = gm_l1;
        uint64_t t73 = gm_l2;
        gm_t2lqu8jsj511d t74;
        GM_ST(uint64_t, ((uint8_t *)&t74 + 0), t72);
        GM_ST(uint64_t, ((uint8_t *)&t74 + 8), t73);
        gm_tpekhfs32op0f t75;
        GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t75 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t75 + 32, 0);
        GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t75 + 8), t74);
        uint8_t t76 = 0;
        F.ret = t75;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tpekhfs32op0f gm_f_fault_u31b92l_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4) { gm_tpekhfs32op0f gm_r; gm_f_fault_u31b92l(&gm_r, a0, a1, a2, a3, a4); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_init_9fuf99v(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_init_9fuf99v F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    gm_b16_8 gm_l2; /* data */
    void * gm_l3; /* p */
    GM_STACK_CHECK(&F, 547u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
            if (!t5) goto ie3;
            {
                gm_b0_1 t7;
                gm_tgfdnufetdnfc t8;
                memset(&t8, 0, sizeof t8);
                ((uint8_t *)&t8)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t8 + 8), t7);
                gm_tgfdnufetdnfc t9 = t8;
                uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
                F.ret = t8;
                goto gm_fret;
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            void * t11 = gm_l0;
            uint64_t t12 = gm_l1;
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t t14 = (uint64_t)t13;
            void * t15 = gm_f_view_qalkbv1(t11, t12, t14, ((uint8_t)0));
            void * t16 = t15;
            void * t17;
            if ((GM_LD(void *, ((uint8_t *)&t16)) != 0)) {
                void * t18 = GM_LD(void *, (((uint8_t *)&t16) + 0));
                t17 = t18;
            } else {
                void * t19 = gm_l0;
                uint64_t t20 = gm_l1;
                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint64_t t22 = (uint64_t)t21;
                gm_tpekhfs32op0f t23;
                gm_f_fault_u31b92l((void *)&t23, t19, t20, t22, ((uint64_t)1ULL), ((uint8_t)0));
                gm_tgfdnufetdnfc t24;
                GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                ((uint8_t *)&t24)[0] = 1;
                GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t24 + 8), t23);
                gm_tgfdnufetdnfc t25 = t24;
                uint8_t t26 = ((((uint8_t *)&t25))[0] != 0);
                F.ret = t24;
                goto gm_fret;
            }
            gm_l3 = t17;
        }
        {
            void * t27 = gm_l3;
            void * t28 = ((uint8_t *)t27);
            uint64_t t29 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t t31 = (uintptr_t)t30;
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t28 + t29 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t29));
            gm_b16_8 t33 = gm_l2;
            gm_f_copy_from_f7j6p05(t32, t33);
        }
    }
    gm_b0_1 t34;
    gm_tgfdnufetdnfc t35;
    memset(&t35, 0, sizeof t35);
    ((uint8_t *)&t35)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t35 + 8), t34);
    gm_tgfdnufetdnfc t36 = t35;
    F.ret = t36;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tgfdnufetdnfc gm_f_init_9fuf99v_c(void * a0, uint64_t a1, gm_b16_8 a2) { gm_tgfdnufetdnfc gm_r; gm_f_init_9fuf99v(&gm_r, a0, a1, a2); return gm_r; }
__attribute__((aligned(4))) uint8_t gm_f_kill_3kcavfd(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_kill_3kcavfd F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* base */
    gm_ttf6i6v1da8cm gm_l2; /* at */
    void * gm_l3; /* s */
    uint64_t gm_l4; /* pi */
    void * gm_l5; /* pg */
    uint32_t gm_l6; /* full */
    GM_STACK_CHECK(&F, 649u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_taordiq61jj46 t4;
        gm_f_locate_ciotffk((void *)&t4, t2, t3);
        gm_taordiq61jj46 t5 = t4;
        gm_ttf6i6v1da8cm t6;
        if (((((uint8_t *)&t5))[16] != 0)) {
            gm_ttf6i6v1da8cm t7 = GM_LD(gm_ttf6i6v1da8cm, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        void * t9 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        gm_l3 = t9;
    }
    {
        uint8_t t11 = 0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t13 = gm_l1;
        uint8_t t14 = (!(t12 == t13));
        if (!t14) {
            void * t15 = gm_l3;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t15) + 17));
            uint8_t t17 = (uint8_t)!t16;
            t11 = t17;
        } else t11 = 1;
        if (!t11) goto ie10;
        {
            uint8_t t19 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t20 = gm_l3;
        GM_ST(uint8_t, (((uint8_t *)t20) + 17), ((uint8_t)0));
    }
    {
        void * t22 = gm_l3;
        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)t22) + 8));
        uint8_t t24 = (t23 > ((uint64_t)0ULL));
        if (!t24) goto ie21;
        {
            void * t26 = gm_l3;
            void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
            void * t28 = ((uint8_t *)t27);
            uint64_t t29 = (uintptr_t)((uint64_t)0ULL);
            void * t30 = gm_l3;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 8));
            uint64_t t32 = gm_f_us_cuv01fp(t31);
            uint64_t t33 = (uintptr_t)t32;
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t28 + t29 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(t33 - t29));
            gm_b16_8 t35 = t34;
            gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
            gm_b0_1 t37 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t36, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ks0d3nb0eoq)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        uint64_t t38 = gm_l1;
        void * t39 = gm_l0;
        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 40));
        uint64_t r41; bool o42 = __builtin_sub_overflow(t38, t40, &r41);
        if (o42) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 272, 15);
        uint64_t t43 = r41;
        uint64_t t44 = GM_LD(uint64_t, gm_c6p4chcutca);
        unsigned __int128 n45 = (unsigned __int128)t44;
        if (n45 >= 64) gm_trap_shift((uint64_t)n45, "u64", gm_srcq7ohh8h77bn7, 272, 14);
        uint64_t t46 = (uint64_t)(t43 >> (int)n45);
        uint64_t t47 = gm_f_us_cuv01fp(t46);
        gm_l4 = t47;
    }
    {
        uint64_t t48 = gm_l4;
        uint64_t t49 = (uintptr_t)t48;
        void * t50 = gm_l0;
        uint64_t t51 = GM_LD(uintptr_t, ((((uint8_t *)t50) + 0) + 0) + 8);
        if (t49 >= (uintptr_t)(t51)) gm_trap_index(t49, t51, gm_srcq7ohh8h77bn7, 273, 12);
        void * t52 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t50) + 0) + 0)) + t49 * 32);
        gm_l5 = t52;
    }
    {
        void * t53 = gm_l5;
        void * t54 = gm_l5;
        uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)t54) + 28));
        uint32_t r56; bool o57 = __builtin_add_overflow(t55, ((uint32_t)1ULL), &r56);
        if (o57) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 274, 5);
        uint32_t t58 = r56;
        GM_ST(uint32_t, (((uint8_t *)t53) + 28), t58);
    }
    {
        uint32_t t59;
        void * t61 = gm_l5;
        uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)t61) + 16));
        uint64_t t63 = GM_LD(uint64_t, gm_c2t30gdg5i0);
        uint8_t t64 = ((t62 == t63));
        if (!t64) goto ie60;
        uint32_t t66 = ((uint32_t)1ULL);
        t59 = t66;
        goto ix60;
ie60:;
        uint64_t t68 = GM_LD(uint64_t, gm_ckasdtpni1c);
        void * t69 = gm_l5;
        uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)t69) + 16));
        unsigned __int128 n71 = (unsigned __int128)t70;
        if (n71 >= 64) gm_trap_shift((uint64_t)n71, "u64", gm_srcq7ohh8h77bn7, 275, 48);
        uint64_t t72 = (uint64_t)(t68 >> (int)n71);
        uint32_t t73 = (uint32_t)t72;
        uint32_t t74 = t73;
        t59 = t74;
ix60:;
        gm_l6 = t59;
    }
    {
        void * t76 = gm_l5;
        uint32_t t77 = GM_LD(uint32_t, (((uint8_t *)t76) + 28));
        uint32_t t78 = gm_l6;
        uint8_t t79 = ((t77 == t78));
        if (!t79) goto ie75;
        {
            void * t81 = gm_l0;
            void * t82 = (void *)(((uint8_t *)t81) + 120);
            uint64_t t83 = gm_l4;
            gm_tftb1odc739t5 t84 = gm_f_push_ucgmlo2(t82, t83);
            gm_tftb1odc739t5 t85 = t84;
            if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 1, gm_srcq7ohh8h77bn7, 278, 9);
            gm_b0_1 t86 = GM_LD(gm_b0_1, (((uint8_t *)&t85) + 1));
        }
        {
            void * t87 = gm_l0;
            void * t88 = gm_l0;
            uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)t88) + 168));
            uint32_t t90 = gm_l6;
            uint64_t t91 = (uint64_t)t90;
            uint64_t r92; bool o93 = __builtin_add_overflow(t89, t91, &r92);
            if (o93) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 279, 9);
            uint64_t t94 = r92;
            GM_ST(uint64_t, (((uint8_t *)t87) + 168), t94);
        }
        {
            for (;;) {
                void * t96 = gm_l0;
                uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)t96) + 168));
                uint64_t t98 = GM_LD(uint64_t, gm_c2ftoibr66a);
                uint8_t t99 = (t97 > t98);
                if (!t99) goto lb95;
                {
                    void * t102 = gm_l0;
                    gm_b0_1 t103 = gm_f_drop_oldest_o9uaetp((void *)NULL, cx, NULL, t102);
                    if (gm_unw.kind) {
                        goto gm_ll100;
                    }
                }
lc95:;
            }
gm_ll100:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb95; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc95; }
            }
            goto gm_lad;
lb95:;
        }
        goto ix75;
ie75:;
ix75:;
    }
    {
        uint8_t t104 = 0;
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
__attribute__((aligned(4))) void gm_f_locate_ciotffk(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_locate_ciotffk F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    void * gm_l2; /* pg */
    void * gm_l3; /* ss */
    uint64_t gm_l4; /* i */
    void * gm_l5; /* s */
    uint64_t gm_l6; /* base */
    GM_STACK_CHECK(&F, 480u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        void * t4 = gm_f_page_of_artfs32(t2, t3);
        void * t5 = t4;
        void * t6;
        if ((GM_LD(void *, ((uint8_t *)&t5)) != 0)) {
            void * t7 = GM_LD(void *, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_taordiq61jj46 t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        void * t10 = gm_l2;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        void * t12 = t11;
        void * t13;
        if ((GM_LD(void *, ((uint8_t *)&t12)) != 0)) {
            void * t14 = GM_LD(void *, (((uint8_t *)&t12) + 0));
            t13 = t14;
        } else {
            gm_taordiq61jj46 t15;
            memset(&t15, 0, sizeof t15);
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        gm_l3 = t13;
    }
    {
        uint64_t t17 = gm_l1;
        void * t18 = gm_l2;
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 8));
        uint64_t r20; bool o21 = __builtin_sub_overflow(t17, t19, &r20);
        if (o21) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 187, 11);
        uint64_t t22 = r20;
        void * t23 = gm_l2;
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 16));
        unsigned __int128 n25 = (unsigned __int128)t24;
        if (n25 >= 64) gm_trap_shift((uint64_t)n25, "u64", gm_srcq7ohh8h77bn7, 187, 10);
        uint64_t t26 = (uint64_t)(t22 >> (int)n25);
        gm_l4 = t26;
    }
    {
        void * t27 = gm_l3;
        void * t28 = t27;
        uint64_t t29 = gm_l4;
        uint64_t t30 = gm_f_us_cuv01fp(t29);
        uint64_t t31 = (uintptr_t)t30;
        void * t32 = (void *)((uint8_t *)t28 + t31 * 24);
        gm_l5 = t32;
    }
    {
        void * t33 = gm_l2;
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 8));
        uint64_t t35 = gm_l4;
        void * t36 = gm_l2;
        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)t36) + 16));
        unsigned __int128 n38 = (unsigned __int128)t37;
        if (n38 >= 64) gm_trap_shift((uint64_t)n38, "u64", gm_srcq7ohh8h77bn7, 189, 24);
        uint64_t r39 = (uint64_t)((uint64_t)t35 << (int)n38);
        bool o40 = (uint64_t)(r39 >> (int)n38) != t35;
        if (o40) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 189, 24);
        uint64_t t41 = r39;
        uint64_t r42; bool o43 = __builtin_add_overflow(t34, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 189, 13);
        uint64_t t44 = r42;
        gm_l6 = t44;
    }
    {
        uint8_t t46 = 0;
        void * t47 = gm_l5;
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t47) + 16));
        uint8_t t49 = (uint8_t)!t48;
        if (!t49) {
            uint64_t t50 = gm_l1;
            uint64_t t51 = gm_l6;
            uint64_t r52; bool o53 = __builtin_sub_overflow(t50, t51, &r52);
            if (o53) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 190, 22);
            uint64_t t54 = r52;
            void * t55 = gm_l5;
            uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 8));
            uint8_t t57 = (t54 > t56);
            t46 = t57;
        } else t46 = 1;
        if (!t46) goto ie45;
        {
            gm_taordiq61jj46 t59;
            memset(&t59, 0, sizeof t59);
            uint8_t t60 = 0;
            F.ret = t59;
            goto gm_fret;
        }
        goto ix45;
ie45:;
ix45:;
    }
    {
        void * t61 = gm_l5;
        uint64_t t62 = gm_l6;
        gm_ttf6i6v1da8cm t63;
        GM_ST(void *, ((uint8_t *)&t63 + 0), t61);
        GM_ST(uint64_t, ((uint8_t *)&t63 + 8), t62);
        gm_taordiq61jj46 t64;
        GM_ST(uint8_t, (uint8_t *)&t64 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t64 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t64 + 20, 0);
        GM_ST(gm_ttf6i6v1da8cm, ((uint8_t *)&t64), t63);
        ((uint8_t *)&t64)[16] = 1;
        uint8_t t65 = 0;
        F.ret = t64;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_taordiq61jj46 gm_f_locate_ciotffk_c(void * a0, uint64_t a1) { gm_taordiq61jj46 gm_r; gm_f_locate_ciotffk(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mem_deinit_0088958(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_mem_deinit_0088958 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* pi */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 0) + 0) + 8);
            gm_l1 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l2;
                uint64_t t7 = gm_l1;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l2;
                    uint64_t t12 = t11;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l3 = t15;
                    {
                        {
                            void * t18 = gm_l0;
                            uint64_t t19 = gm_l3;
                            uint64_t t20 = gm_f_release_page_66inn9t((void *)NULL, cx, NULL, t18, t19);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 327, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t22 = gm_l2;
                    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                    uint64_t t25 = r23;
                    gm_l2 = t25;
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
        void * t26 = gm_l0;
        void * t27 = (void *)(((uint8_t *)t26) + 0);
        gm_f_deinit_u4v62un(t27);
    }
    {
        void * t28 = gm_l0;
        void * t29 = (void *)(((uint8_t *)t28) + 120);
        gm_f_deinit_d5p9g61(t29);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_mem_message_e0go901 F;
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* addr */
    gm_t9qkh0rsooc8d gm_l4; /* kind */
    gm_b16_8 gm_l5; /* what */
    gm_t66j1im9ip128 gm_l6; /* x */
    uint64_t gm_l7; /* addr */
    uint64_t gm_l8; /* align */
    uint64_t gm_l9; /* addr */
    uint64_t gm_l10; /* size */
    gm_t9qkh0rsooc8d gm_l11; /* kind */
    GM_STACK_CHECK(&F, 1323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t12) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; default: goto ma11; }
ma3:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t14 != ((uint8_t)0ULL)) goto mn13;
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_suklu1i9clb5l));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(47));
    t1 = t15;
    goto me2;
mn13:;
ma4:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t17 != ((uint8_t)1ULL)) goto mn16;
    uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 0));
    gm_l1 = t18;
    uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    gm_l2 = t19;
    uint64_t t20 = gm_l2;
    uint64_t t21 = gm_l1;
    gm_t2lqu8jsj511d t22;
    GM_ST(uint64_t, ((uint8_t *)&t22 + 0), t20);
    GM_ST(uint64_t, ((uint8_t *)&t22 + 8), t21);
    gm_tabb710d9mq2e t23;
    gm_f_format_bovj9ap((void *)&t23, (void *)NULL, cx, NULL, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t24 = t23;
    if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_srcq7ohh8h77bn7, 78, 29);
    gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
    gm_b16_8 t26 = t25;
    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
    t1 = t27;
    goto me2;
mn16:;
ma5:;
    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t29 != ((uint8_t)2ULL)) goto mn28;
    uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 0));
    gm_l3 = t30;
    gm_t9qkh0rsooc8d t31 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)a0) + 8) + 8));
    gm_l4 = t31;
    {
        gm_b16_8 t33;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
        switch (t39) { case ((uint8_t)0ULL): goto ma35; case ((uint8_t)1ULL): goto ma35; case ((uint8_t)2ULL): goto ma36; case ((uint8_t)3ULL): goto ma37; case ((uint8_t)4ULL): goto ma37; case ((uint8_t)5ULL): goto ma37; default: goto ma37; }
ma35:;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
        if (t43 != ((uint8_t)0ULL)) goto pa42;
        goto po41;
pa42:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
        if (t45 != ((uint8_t)1ULL)) goto pa44;
        goto po41;
pa44:;
        goto mn40;
po41:;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_skt7g4v99o4j0));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(41));
        t33 = t46;
        goto me34;
mn40:;
ma36:;
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
        if (t48 != ((uint8_t)2ULL)) goto mn47;
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sagd3osht35og));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(12));
        t33 = t49;
        goto me34;
mn47:;
ma37:;
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sacs352aeakr8));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(17));
        t33 = t51;
        goto me34;
mn50:;
ma38:;
        gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 80, 17);
me34:;
        gm_l5 = t33;
    }
    gm_tf1bjg81u0glv t52;
    uint64_t t53 = gm_l3;
    GM_ST(uint64_t, (uint8_t *)&t52 + 0, t53);
    gm_b16_8 t54 = gm_l5;
    GM_ST(gm_b16_8, (uint8_t *)&t52 + 8, t54);
    gm_tabb710d9mq2e t55;
    gm_f_format_q07lmui((void *)&t55, (void *)NULL, cx, NULL, &t52);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t56 = t55;
    if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_srcq7ohh8h77bn7, 85, 9);
    gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
    gm_b16_8 t58 = t57;
    gm_b16_8 t59 = t58;
    gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
    t1 = t60;
    goto me2;
mn28:;
ma6:;
    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t62 != ((uint8_t)3ULL)) goto mn61;
    gm_t66j1im9ip128 t63 = GM_LD(gm_t66j1im9ip128, (((uint8_t *)a0) + 8));
    gm_l6 = t63;
    gm_t66j1im9ip128 t64;
    uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
    GM_ST(uint64_t, (uint8_t *)&t64 + 0, t65);
    uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
    GM_ST(uint64_t, (uint8_t *)&t64 + 8, t66);
    uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
    GM_ST(uint64_t, (uint8_t *)&t64 + 16, t67);
    uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 24));
    GM_ST(uint64_t, (uint8_t *)&t64 + 24, t68);
    gm_tabb710d9mq2e t69;
    gm_f_format_173gk69((void *)&t69, (void *)NULL, cx, NULL, &t64);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t70 = t69;
    if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 8, gm_srcq7ohh8h77bn7, 87, 24);
    gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
    gm_b16_8 t72 = t71;
    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
    t1 = t73;
    goto me2;
mn61:;
ma7:;
    uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t75 != ((uint8_t)4ULL)) goto mn74;
    uint64_t t76 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 0));
    gm_l7 = t76;
    uint64_t t77 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    gm_l8 = t77;
    uint64_t t78 = gm_l7;
    uint64_t t79 = gm_l8;
    gm_t2lqu8jsj511d t80;
    GM_ST(uint64_t, ((uint8_t *)&t80 + 0), t78);
    GM_ST(uint64_t, ((uint8_t *)&t80 + 8), t79);
    gm_tabb710d9mq2e t81;
    gm_f_format_hvaqp8b((void *)&t81, (void *)NULL, cx, NULL, t80);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t82 = t81;
    if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 8, gm_srcq7ohh8h77bn7, 88, 33);
    gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 8));
    gm_b16_8 t84 = t83;
    gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
    t1 = t85;
    goto me2;
mn74:;
ma8:;
    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t87 != ((uint8_t)5ULL)) goto mn86;
    uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
    gm_l9 = t88;
    uint64_t t89 = gm_l9;
    gm_t4ce0jd3tdro4 t90;
    GM_ST(uint64_t, ((uint8_t *)&t90 + 0), t89);
    gm_tabb710d9mq2e t91;
    gm_f_format_n0787fu((void *)&t91, (void *)NULL, cx, NULL, t90);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t92 = t91;
    if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 8, gm_srcq7ohh8h77bn7, 89, 24);
    gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 8));
    gm_b16_8 t94 = t93;
    gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
    t1 = t95;
    goto me2;
mn86:;
ma9:;
    uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t97 != ((uint8_t)6ULL)) goto mn96;
    gm_b16_8 t98;
    GM_ST(void *, (uint8_t *)&t98, (void *)(gm_sdvkaj73n9goh));
    GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(23));
    t1 = t98;
    goto me2;
mn96:;
ma10:;
    uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t100 != ((uint8_t)7ULL)) goto mn99;
    uint64_t t101 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 0));
    gm_l10 = t101;
    gm_t9qkh0rsooc8d t102 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)a0) + 8) + 8));
    gm_l11 = t102;
    gm_b16_8 t103;
    gm_t9qkh0rsooc8d t105 = gm_l11;
    gm_t9qkh0rsooc8d t106;
    GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t107;
    GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)1ULL));
    gm_t0am46iohlpol t108;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t108 + 0), t106);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t108 + 1), t107);
    gm_t0am46iohlpol t109 = t108;
    uint8_t t110 = 0;
    gm_t9qkh0rsooc8d t111 = t105;
    if (gm_eqv4kh4qul78(((uint8_t *)&t109) + 0, ((uint8_t *)&t111))) t110 = 1;
    if (gm_eqv4kh4qul78(((uint8_t *)&t109) + 1, ((uint8_t *)&t111))) t110 = 1;
    uint8_t t112 = (uint8_t)(t110);
    if (!t112) goto ie104;
    uint64_t t114 = gm_l10;
    gm_t4ce0jd3tdro4 t115;
    GM_ST(uint64_t, ((uint8_t *)&t115 + 0), t114);
    gm_tabb710d9mq2e t116;
    gm_f_format_itetl3f((void *)&t116, (void *)NULL, cx, NULL, t115);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t117 = t116;
    if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 8, gm_srcq7ohh8h77bn7, 91, 77);
    gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
    gm_b16_8 t119 = t118;
    gm_b16_8 t120 = t119;
    gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
    t103 = t121;
    goto ix104;
ie104:;
    uint64_t t123 = gm_l10;
    gm_t4ce0jd3tdro4 t124;
    GM_ST(uint64_t, ((uint8_t *)&t124 + 0), t123);
    gm_tabb710d9mq2e t125;
    gm_f_format_o05ssh3((void *)&t125, (void *)NULL, cx, NULL, t124);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t126 = t125;
    if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 8, gm_srcq7ohh8h77bn7, 91, 156);
    gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&t126) + 8));
    gm_b16_8 t128 = t127;
    gm_b16_8 t129 = t128;
    gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
    t103 = t130;
ix104:;
    t1 = t103;
    goto me2;
mn99:;
ma11:;
    gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 76, 57);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_memory_dqom95e(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint64_t a1) {
    struct gm_fr_memory_dqom95e F;
    uint64_t gm_l0; /* data_base */
    uint64_t gm_l1; /* ptr_bytes */
    uint64_t gm_l2; /* limit */
    uint64_t gm_l3; /* base */
    GM_STACK_CHECK(&F, 321u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2;
        uint64_t t4 = gm_l1;
        uint8_t t5 = (t4 >= ((uint64_t)8ULL));
        if (!t5) goto ie3;
        uint64_t t7 = (uint64_t)~((uint64_t)0ULL);
        uint64_t t8 = t7;
        t2 = t8;
        goto ix3;
ie3:;
        uint64_t t10 = gm_l1;
        uint64_t r11; bool o12 = __builtin_mul_overflow(t10, ((uint64_t)8ULL), &r11);
        if (o12) gm_trap("integer overflow in `*`", gm_srcq7ohh8h77bn7, 111, 62);
        uint64_t t13 = r11;
        unsigned __int128 n14 = (unsigned __int128)t13;
        if (n14 >= 64) gm_trap_shift((uint64_t)n14, "u64", gm_srcq7ohh8h77bn7, 111, 51);
        uint64_t r15 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n14);
        bool o16 = (uint64_t)(r15 >> (int)n14) != ((uint64_t)1ULL);
        if (o16) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 111, 51);
        uint64_t t17 = r15;
        uint64_t t18 = t17;
        t2 = t18;
ix3:;
        gm_l2 = t2;
    }
    {
        uint64_t t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, gm_ckasdtpni1c);
        uint64_t r21; bool o22 = __builtin_add_overflow(t19, t20, &r21);
        if (o22) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 112, 14);
        uint64_t t23 = r21;
        uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 112, 14);
        uint64_t t26 = r24;
        uint64_t t27 = GM_LD(uint64_t, gm_ckasdtpni1c);
        if (t27 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 112, 13);
        uint64_t t28 = (uint64_t)(t26 / t27);
        uint64_t t29 = GM_LD(uint64_t, gm_ckasdtpni1c);
        uint64_t r30; bool o31 = __builtin_mul_overflow(t28, t29, &r30);
        if (o31) gm_trap("integer overflow in `*`", gm_srcq7ohh8h77bn7, 112, 13);
        uint64_t t32 = r30;
        gm_l3 = t32;
    }
    {
        gm_trqfvmjtt4sij t33;
        gm_f_list_bcj49nd((void *)&t33, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t34 = gm_l3;
        uint64_t t35 = gm_l2;
        gm_tl4e4vqe807ge t36;
        for (uint64_t i = 0; i < 8; i++) GM_ST(uint64_t, (uint8_t *)&t36 + i * 8, ((uint64_t)0ULL));
        gm_trqfvmjtt4sij t37;
        gm_f_list_5dhgg4i((void *)&t37, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 0, t33);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 40, t34);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 48, t35);
        GM_ST(gm_tl4e4vqe807ge, (uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 56, t36);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 120, t37);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 160, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 168, ((uint64_t)0ULL));
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_pages_p5tqfl1(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_pages_p5tqfl1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* addr */
    GM_STACK_CHECK(&F, 219u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 40));
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
        uint64_t t6 = (uint64_t)t5;
        uint64_t t7 = GM_LD(uint64_t, gm_ckasdtpni1c);
        uint64_t r8; bool o9 = __builtin_mul_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `*`", gm_srcq7ohh8h77bn7, 118, 22);
        uint64_t t10 = r8;
        uint64_t r11; bool o12 = __builtin_add_overflow(t3, t10, &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 118, 13);
        uint64_t t13 = r11;
        gm_l2 = t13;
    }
    {
        uint64_t t15 = gm_l1;
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 48));
        uint64_t t18 = gm_l2;
        uint64_t r19; bool o20 = __builtin_sub_overflow(t17, t18, &r19);
        if (o20) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 119, 13);
        uint64_t t21 = r19;
        uint64_t t22 = GM_LD(uint64_t, gm_ckasdtpni1c);
        if (t22 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 119, 12);
        uint64_t t23 = (uint64_t)(t21 / t22);
        uint8_t t24 = (t15 > t23);
        if (!t24) goto ie14;
        {
            gm_tf5srh4mp4b3i t26;
            memset(&t26, 0, sizeof t26);
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        uint64_t t28 = gm_l2;
        gm_tf5srh4mp4b3i t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t29), t28);
        ((uint8_t *)&t29)[8] = 1;
        uint8_t t30 = 0;
        F.ret = t29;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void * gm_f_page_of_artfs32(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_t934k1j7u70ct gm_f_read_u6r4vns_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3) { gm_t934k1j7u70ct gm_r; gm_f_read_u6r4vns(&gm_r, a0, a1, a2, a3); return gm_r; }
__attribute__((aligned(4))) uint64_t gm_f_release_page_66inn9t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_release_page_66inn9t F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* pi */
    void * gm_l2; /* pg */
    void * gm_l3; /* ss */
    void * gm_l4; /* s */
    uint64_t gm_l5; /* k */
    uint64_t gm_l6; /* per */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    void * gm_l10; /* s */
    GM_STACK_CHECK(&F, 997u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
        if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcq7ohh8h77bn7, 302, 12);
        void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 32);
        gm_l2 = t6;
    }
    {
        void * t7 = gm_l2;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = t8;
        void * t10;
        if ((GM_LD(void *, ((uint8_t *)&t9)) != 0)) {
            void * t11 = GM_LD(void *, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            uint8_t t12 = 0;
            F.ret = ((uint64_t)0ULL);
            goto gm_fret;
        }
        gm_l3 = t10;
    }
    {
        void * t14 = gm_l2;
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)t14) + 16));
        uint64_t t16 = GM_LD(uint64_t, gm_c2t30gdg5i0);
        uint8_t t17 = ((t15 == t16));
        if (!t17) goto ie13;
        {
            uint64_t t19 = (uintptr_t)((uint64_t)0ULL);
            void * t20 = gm_l3;
            void * t21 = (void *)((uint8_t *)t20 + t19 * 24);
            gm_l4 = t21;
        }
        {
            void * t23 = gm_l4;
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t23) + 17));
            if (!t24) goto ie22;
            void * t25 = gm_l4;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 8));
            uint8_t t27 = (t26 > ((uint64_t)0ULL));
            if (!t27) goto ie22;
            {
                void * t29 = gm_l4;
                void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
                void * t31 = ((uint8_t *)t30);
                uint64_t t32 = (uintptr_t)((uint64_t)0ULL);
                void * t33 = gm_l4;
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 8));
                uint64_t t35 = gm_f_us_cuv01fp(t34);
                uint64_t t36 = (uintptr_t)t35;
                gm_b16_8 t37;
                GM_ST(void *, (uint8_t *)&t37, (void *)(((uint8_t *)t31 + t32 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(t36 - t32));
                gm_b16_8 t38 = t37;
                gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                gm_b0_1 t40 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t39, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0ddgrnig17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix22;
ie22:;
ix22:;
        }
        {
            uint64_t t41 = gm_l1;
            gm_l5 = t41;
        }
        {
            for (;;) {
                uint64_t t43 = gm_l5;
                void * t44 = gm_l0;
                uint64_t t45 = GM_LD(uintptr_t, ((((uint8_t *)t44) + 0) + 0) + 8);
                uint8_t t46 = (t43 < t45);
                if (!t46) goto lb42;
                uint64_t t47 = gm_l5;
                uint64_t t48 = (uintptr_t)t47;
                void * t49 = gm_l0;
                uint64_t t50 = GM_LD(uintptr_t, ((((uint8_t *)t49) + 0) + 0) + 8);
                if (t48 >= (uintptr_t)(t50)) gm_trap_index(t48, t50, gm_srcq7ohh8h77bn7, 308, 41);
                uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t49) + 0) + 0)) + t48 * 32) + 16));
                uint64_t t52 = GM_LD(uint64_t, gm_c2t30gdg5i0);
                uint8_t t53 = ((t51 == t52));
                if (!t53) goto lb42;
                uint64_t t54 = gm_l5;
                uint64_t t55 = (uintptr_t)t54;
                void * t56 = gm_l0;
                uint64_t t57 = GM_LD(uintptr_t, ((((uint8_t *)t56) + 0) + 0) + 8);
                if (t55 >= (uintptr_t)(t57)) gm_trap_index(t55, t57, gm_srcq7ohh8h77bn7, 308, 77);
                uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t56) + 0) + 0)) + t55 * 32) + 8));
                void * t59 = gm_l2;
                uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)t59) + 8));
                uint8_t t61 = ((t58 == t60));
                if (!t61) goto lb42;
                uint64_t t62 = gm_l5;
                uint64_t t63 = (uintptr_t)t62;
                void * t64 = gm_l0;
                uint64_t t65 = GM_LD(uintptr_t, ((((uint8_t *)t64) + 0) + 0) + 8);
                if (t63 >= (uintptr_t)(t65)) gm_trap_index(t63, t65, gm_srcq7ohh8h77bn7, 308, 114);
                void * t66 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t64) + 0) + 0)) + t63 * 32) + 0));
                void * t67 = t66;
                uint8_t t68;
                if ((GM_LD(void *, ((uint8_t *)&t67)) != 0)) goto mn70;
                t68 = ((uint8_t)0);
                goto me69;
mn70:;
                t68 = ((uint8_t)1);
                goto me69;
mn71:;
                gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 308, 114);
me69:;
                if (!t68) goto lb42;
                {
                    uint64_t t74 = gm_l5;
                    uint64_t t75 = (uintptr_t)t74;
                    void * t76 = gm_l0;
                    uint64_t t77 = GM_LD(uintptr_t, ((((uint8_t *)t76) + 0) + 0) + 8);
                    if (t75 >= (uintptr_t)(t77)) gm_trap_index(t75, t77, gm_srcq7ohh8h77bn7, 309, 13);
                    GM_ST(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t76) + 0) + 0)) + t75 * 32) + 0), ((void *)0));
                }
                {
                    uint64_t t78 = gm_l5;
                    uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                    if (o80) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 310, 13);
                    uint64_t t81 = r79;
                    gm_l5 = t81;
                }
lc42:;
            }
lb42:;
        }
        {
            void * t82 = gm_l4;
            gm_b0_1 t83 = gm_f_destroy_l0inh8t((void *)NULL, cx, NULL, t82, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kgubkej3sov)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t84 = 0;
            F.ret = ((uint64_t)1ULL);
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        uint64_t t85 = GM_LD(uint64_t, gm_ckasdtpni1c);
        void * t86 = gm_l2;
        uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)t86) + 16));
        unsigned __int128 n88 = (unsigned __int128)t87;
        if (n88 >= 64) gm_trap_shift((uint64_t)n88, "u64", gm_srcq7ohh8h77bn7, 315, 15);
        uint64_t t89 = (uint64_t)(t85 >> (int)n88);
        uint64_t t90 = gm_f_us_cuv01fp(t89);
        gm_l6 = t90;
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t92 = gm_l6;
            gm_l7 = t92;
        }
        {
            for (;;) {
                uint64_t t94 = gm_l8;
                uint64_t t95 = gm_l7;
                uint8_t t96 = (t94 < t95);
                if (!t96) goto lb93;
                {
                    uint64_t t99 = gm_l8;
                    uint64_t t100 = t99;
                    uint64_t t103 = GM_LD(uint64_t, ((uint8_t *)&t100));
                    gm_l9 = t103;
                    {
                        uint64_t t105 = gm_l9;
                        uint64_t t106 = (uintptr_t)t105;
                        void * t107 = gm_l3;
                        void * t108 = (void *)((uint8_t *)t107 + t106 * 24);
                        gm_l10 = t108;
                    }
                    {
                        void * t110 = gm_l10;
                        uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)t110) + 17));
                        if (!t111) goto ie109;
                        void * t112 = gm_l10;
                        uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)t112) + 8));
                        uint8_t t114 = (t113 > ((uint64_t)0ULL));
                        if (!t114) goto ie109;
                        {
                            void * t116 = gm_l10;
                            void * t117 = GM_LD(void *, (((uint8_t *)t116) + 0));
                            void * t118 = ((uint8_t *)t117);
                            uint64_t t119 = (uintptr_t)((uint64_t)0ULL);
                            void * t120 = gm_l10;
                            uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)t120) + 8));
                            uint64_t t122 = gm_f_us_cuv01fp(t121);
                            uint64_t t123 = (uintptr_t)t122;
                            gm_b16_8 t124;
                            GM_ST(void *, (uint8_t *)&t124, (void *)(((uint8_t *)t118 + t119 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(t123 - t119));
                            gm_b16_8 t125 = t124;
                            gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                            gm_b0_1 t127 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t126, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2afmplo64l)));
                            if (gm_unw.kind) {
                                goto gm_ll97;
                            }
                        }
                        goto ix109;
ie109:;
ix109:;
                    }
                    goto me101;
mn102:;
                    gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 316, 5);
me101:;
                }
lc93:;
                {
                    uint64_t t129 = gm_l8;
                    uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                    uint64_t t132 = r130;
                    gm_l8 = t132;
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
    {
        void * t133 = gm_l3;
        void * t134 = ((uint8_t *)t133);
        uint64_t t135 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t136 = gm_l6;
        uint64_t t137 = (uintptr_t)t136;
        gm_b16_8 t138;
        GM_ST(void *, (uint8_t *)&t138, (void *)(((uint8_t *)t134 + t135 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(t137 - t135));
        gm_b16_8 t139 = t138;
        gm_b16_8 t140 = GM_LD(gm_b16_8, ((uint8_t *)&t139));
        gm_b0_1 t141 = gm_f_free_oobi9fu((void *)NULL, cx, NULL, t140, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ke9p7o91039)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t142 = gm_l2;
        GM_ST(void *, (((uint8_t *)t142) + 0), ((void *)0));
    }
    {
        uint64_t t143 = gm_l6;
        uint8_t t144 = 0;
        F.ret = t143;
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
extern __attribute__((aligned(4))) void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
inline __attribute__((aligned(4))) void gm_f_write_5gub0ul(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_5gub0ul F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    gm_b16_8 gm_l2; /* data */
    uint64_t gm_l3; /* align */
    gm_b16_8 gm_l4; /* d */
    GM_STACK_CHECK(&F, 424u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            void * t3 = gm_l0;
            uint64_t t4 = gm_l1;
            uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t t6 = (uint64_t)t5;
            uint64_t t7 = gm_l3;
            gm_t934k1j7u70ct t8;
            gm_f_write_view_pebit6h((void *)&t8, t3, t4, t6, t7);
            gm_t934k1j7u70ct t9 = t8;
            if (((((uint8_t *)&t9))[0] != 0)) {
                gm_tpekhfs32op0f t10 = GM_LD(gm_tpekhfs32op0f, (((uint8_t *)&t9) + 8));
                gm_tgfdnufetdnfc t11;
                GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
                ((uint8_t *)&t11)[0] = 1;
                GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t11 + 8), t10);
                F.ret = t11;
                goto gm_fret;
            }
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
            gm_l4 = t12;
        }
        {
            gm_b16_8 t13 = gm_l4;
            gm_b16_8 t14 = gm_l2;
            gm_f_copy_from_f7j6p05(t13, t14);
        }
    }
    gm_b0_1 t15;
    gm_tgfdnufetdnfc t16;
    memset(&t16, 0, sizeof t16);
    ((uint8_t *)&t16)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t16 + 8), t15);
    gm_tgfdnufetdnfc t17 = t16;
    F.ret = t17;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tgfdnufetdnfc gm_f_write_5gub0ul_c(void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3) { gm_tgfdnufetdnfc gm_r; gm_f_write_5gub0ul(&gm_r, a0, a1, a2, a3); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_t934k1j7u70ct gm_f_write_view_pebit6h_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3) { gm_t934k1j7u70ct gm_r; gm_f_write_view_pebit6h(&gm_r, a0, a1, a2, a3); return gm_r; }
