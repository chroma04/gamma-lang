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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_cskjv6t053tkje { uint32_t f0; uint64_t f1; } gm_cskjv6t053tkje;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; } gm_tb1msgo1fu1n3;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint32_t f4; uint32_t f5; uint32_t f6; uint32_t f7; } gm_tu4r32bi6mhcq;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_cskjv6t053tkje) == 16 && _Alignof(gm_cskjv6t053tkje) == 8 && offsetof(gm_cskjv6t053tkje, f0) == 0 && offsetof(gm_cskjv6t053tkje, f1) == 8, "layout of `Char`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_tl874t2atnlh3 f1; } gm_t2eb59dvglnle;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_t3jim1gvsdr87;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tqs5p4n2tq19v f0; gm_tqs5p4n2tq19v f1; } gm_t5r6k0fdcfsb3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; } gm_t6ofeqo2h242p;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; } gm_t724v60om7sh5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb1msgo1fu1n3) == 5 && _Alignof(gm_tb1msgo1fu1n3) == 1 && offsetof(gm_tb1msgo1fu1n3, f0) == 0 && offsetof(gm_tb1msgo1fu1n3, f1) == 1 && offsetof(gm_tb1msgo1fu1n3, f2) == 2 && offsetof(gm_tb1msgo1fu1n3, f3) == 3 && offsetof(gm_tb1msgo1fu1n3, f4) == 4, "layout of `(u8, u8, u8, u8, u8)`");
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; uint64_t f1; } gm_te0mgnjdphs16;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; gm_t9qkh0rsooc8d f7; gm_t9qkh0rsooc8d f8; gm_t9qkh0rsooc8d f9; } gm_tfh78knfg7urq;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; } gm_tk22flr9mh3ql;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tu4r32bi6mhcq) == 32 && _Alignof(gm_tu4r32bi6mhcq) == 4 && offsetof(gm_tu4r32bi6mhcq, f0) == 0 && offsetof(gm_tu4r32bi6mhcq, f1) == 4 && offsetof(gm_tu4r32bi6mhcq, f2) == 8 && offsetof(gm_tu4r32bi6mhcq, f3) == 12 && offsetof(gm_tu4r32bi6mhcq, f4) == 16 && offsetof(gm_tu4r32bi6mhcq, f5) == 20 && offsetof(gm_tu4r32bi6mhcq, f6) == 24 && offsetof(gm_tu4r32bi6mhcq, f7) == 28, "layout of `(u32, u32, u32, u32, u32, u32, u32, u32)`");
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t2792tsg5eg72;
_Static_assert(sizeof(gm_t2eb59dvglnle) == 16 && _Alignof(gm_t2eb59dvglnle) == 4 && offsetof(gm_t2eb59dvglnle, f0) == 0 && offsetof(gm_t2eb59dvglnle, f1) == 4, "layout of `Bracket`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
_Static_assert(sizeof(gm_t3jim1gvsdr87) == 40 && _Alignof(gm_t3jim1gvsdr87) == 8 && offsetof(gm_t3jim1gvsdr87, f0) == 0 && offsetof(gm_t3jim1gvsdr87, f1) == 16 && offsetof(gm_t3jim1gvsdr87, f2) == 32, "layout of `DocComment`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5r6k0fdcfsb3) == 4 && _Alignof(gm_t5r6k0fdcfsb3) == 1 && offsetof(gm_t5r6k0fdcfsb3, f0) == 0 && offsetof(gm_t5r6k0fdcfsb3, f1) == 2, "layout of `(?u8, ?u8)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t6ofeqo2h242p) == 7 && _Alignof(gm_t6ofeqo2h242p) == 1 && offsetof(gm_t6ofeqo2h242p, f0) == 0 && offsetof(gm_t6ofeqo2h242p, f1) == 1 && offsetof(gm_t6ofeqo2h242p, f2) == 2 && offsetof(gm_t6ofeqo2h242p, f3) == 3 && offsetof(gm_t6ofeqo2h242p, f4) == 4 && offsetof(gm_t6ofeqo2h242p, f5) == 5 && offsetof(gm_t6ofeqo2h242p, f6) == 6, "layout of `(Command, Command, Command, Command, Command, Command, Command)`");
_Static_assert(sizeof(gm_t724v60om7sh5) == 5 && _Alignof(gm_t724v60om7sh5) == 1 && offsetof(gm_t724v60om7sh5, f0) == 0 && offsetof(gm_t724v60om7sh5, f1) == 1 && offsetof(gm_t724v60om7sh5, f2) == 2 && offsetof(gm_t724v60om7sh5, f3) == 3 && offsetof(gm_t724v60om7sh5, f4) == 4, "layout of `(Keyword, Keyword, Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_te0mgnjdphs16) == 16 && _Alignof(gm_te0mgnjdphs16) == 8 && offsetof(gm_te0mgnjdphs16, f0) == 0 && offsetof(gm_te0mgnjdphs16, f1) == 8, "layout of `Chunk`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tfh78knfg7urq) == 10 && _Alignof(gm_tfh78knfg7urq) == 1 && offsetof(gm_tfh78knfg7urq, f0) == 0 && offsetof(gm_tfh78knfg7urq, f1) == 1 && offsetof(gm_tfh78knfg7urq, f2) == 2 && offsetof(gm_tfh78knfg7urq, f3) == 3 && offsetof(gm_tfh78knfg7urq, f4) == 4 && offsetof(gm_tfh78knfg7urq, f5) == 5 && offsetof(gm_tfh78knfg7urq, f6) == 6 && offsetof(gm_tfh78knfg7urq, f7) == 7 && offsetof(gm_tfh78knfg7urq, f8) == 8 && offsetof(gm_tfh78knfg7urq, f9) == 9, "layout of `(Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tk22flr9mh3ql) == 3 && _Alignof(gm_tk22flr9mh3ql) == 1 && offsetof(gm_tk22flr9mh3ql, f0) == 0 && offsetof(gm_tk22flr9mh3ql, f1) == 1 && offsetof(gm_tk22flr9mh3ql, f2) == 2, "layout of `(Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint64_t f2; void * f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_tc0nbpdhofi37 f7; gm_trqfvmjtt4sij f8; gm_toceffb697p0u f9; gm_trqfvmjtt4sij f10; uint8_t f11; uint8_t pe[7]; } gm_trd6bg8is1v4n;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t2eb59dvglnle f0; uint8_t f1; uint8_t pe[3]; } gm_tu1ap1uj3psaf;
_Static_assert(sizeof(gm_t2792tsg5eg72) == 64 && _Alignof(gm_t2792tsg5eg72) == 8 && offsetof(gm_t2792tsg5eg72, f0) == 0 && offsetof(gm_t2792tsg5eg72, f1) == 40 && offsetof(gm_t2792tsg5eg72, f2) == 48 && offsetof(gm_t2792tsg5eg72, f3) == 56, "layout of `Diagnostics`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_trd6bg8is1v4n) == 288 && _Alignof(gm_trd6bg8is1v4n) == 8 && offsetof(gm_trd6bg8is1v4n, f0) == 0 && offsetof(gm_trd6bg8is1v4n, f1) == 8 && offsetof(gm_trd6bg8is1v4n, f2) == 24 && offsetof(gm_trd6bg8is1v4n, f3) == 32 && offsetof(gm_trd6bg8is1v4n, f4) == 40 && offsetof(gm_trd6bg8is1v4n, f5) == 80 && offsetof(gm_trd6bg8is1v4n, f6) == 120 && offsetof(gm_trd6bg8is1v4n, f7) == 160 && offsetof(gm_trd6bg8is1v4n, f8) == 184 && offsetof(gm_trd6bg8is1v4n, f9) == 224 && offsetof(gm_trd6bg8is1v4n, f10) == 240 && offsetof(gm_trd6bg8is1v4n, f11) == 280, "layout of `Lexer`");
_Static_assert(sizeof(gm_tu1ap1uj3psaf) == 20 && _Alignof(gm_tu1ap1uj3psaf) == 4 && offsetof(gm_tu1ap1uj3psaf, f0) == 0 && offsetof(gm_tu1ap1uj3psaf, f1) == 16, "layout of `?Bracket`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_backtick_run_l95fb09 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_block_comment_fvce7qt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_byte_b4v8vul {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_c_equality_u566nc4 {
    gm_frame h;
    gm_t4gut2c9fj1kt l10; /* d */
    gm_b0_1 ret;
};
struct gm_fr_clear_4qp22g2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_close_all_brackets_bql4j88 {
    gm_frame h;
    gm_t4gut2c9fj1kt l6; /* d */
    gm_b0_1 ret;
};
struct gm_fr_close_bracket_tbjprpe {
    gm_frame h;
    gm_t4gut2c9fj1kt l11; /* d */
    gm_b0_1 ret;
};
struct gm_fr_closer_fus0off {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_closer_slot_f4sb6v0 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_closing_na3a1ur {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_current_char_ctktsl2 {
    gm_frame h;
    gm_tb31k5l794t7p ret;
};
struct gm_fr_declaration_starts_at_mp730g1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_diagnostics_n3kkhf4 {
    gm_frame h;
    gm_t2792tsg5eg72 ret;
};
struct gm_fr_emit_ekvv703 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_emit_semicolon_7da5b93 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3pd55t2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hc7thq6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qbupbr6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_p5r0d56 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_flavors_qsu4oq0 {
    gm_frame h;
    gm_t6ofeqo2h242p ret;
};
struct gm_fr_flush_newline_9023ml8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_inline_comment_end_8i5h1eq {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_inline_raw_string_end_6lenf33 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_is_alnum_n81ervr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_alpha_12215ga {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ascii_space_t2ltvst {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_digit_2hoggnj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ident_cont_317fpdb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ident_start_rjefpfm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_lower_4ebokoh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_punct_fdeminj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unicode_space_b2m9rku {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_upper_iekrftp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_lex_dc8mbmf {
    gm_frame h;
    gm_trd6bg8is1v4n l3; /* lx */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_lex_recovering_obo2892 {
    gm_frame h;
    gm_t2792tsg5eg72 l3; /* first */
    gm_trd6bg8is1v4n l4; /* lx */
    gm_tr8dvlu9ik4gj l5; /* out */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_trd6bg8is1v4n l9; /* again */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_lex_token_h8g44s4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_lex_word_9li16te {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_line_binds_jm0pnul {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_line_comment_s71u8hq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_2iogcnb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5o6u5a9 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_lpokg1m {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lone_cr_ktb0jvm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_new_lexer_dm12g12 {
    gm_frame h;
    gm_trd6bg8is1v4n ret;
};
struct gm_fr_note_newline_cchbr7g {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_one_or_two_lava5tu {
    gm_frame h;
    gm_te0mgnjdphs16 ret;
};
struct gm_fr_open_bracket_3kcj4k8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_opener_8hqhoc3 {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_peek_72bostn {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_peek_at_74p8lkq {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_pop_1q0s3gu {
    gm_frame h;
    gm_tu1ap1uj3psaf ret;
};
struct gm_fr_pop_j4rpvm5 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_newline_ke0tojq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_push_o6f0em6 {
    gm_frame h;
    gm_t3jim1gvsdr87 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pnf14h1 {
    gm_frame h;
    gm_tm4pc0emhrv1g l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vs3f95a {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_13c41om {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_bnsp8p9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ci9rduq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_run_v26is74 {
    gm_frame h;
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_scan_ident_h1b2n4u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_scan_punct_ecrh51c {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_second_word_end_dpnj7g8 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_skip_trivia_014s7ai {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_slot_4oe07nr {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_span_64igq5o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_tg7cev6 {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_starts_continuation_n3333i7 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_suppresses_newline_after_o08ord2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_truncate_h4ov51t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unexpected_char_r3t8d4c {
    gm_frame h;
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqgj9fu5tboe(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqq6nrh55uon(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint64_t gm_f_backtick_run_l95fb09(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_block_comment_fvce7qt(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_byte_b4v8vul(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_c_equality_u566nc4(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_clear_4qp22g2(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_close_all_brackets_bql4j88(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_close_bracket_tbjprpe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_closer_fus0off(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) uint64_t gm_f_closer_slot_f4sb6v0(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_closing_na3a1ur(const void * a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_closing_na3a1ur_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_current_char_ctktsl2(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_declaration_starts_at_mp730g1(void * a0, uint64_t a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_declaration_starts_at_mp730g1_c(void * a0, uint64_t a1, gm_t3fcbuehh3bs2 a2);
__attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_decode_jrk6b03(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_ekvv703(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_semicolon_7da5b93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3pd55t2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hc7thq6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qbupbr6(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_p5r0d56(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t6ofeqo2h242p gm_f_flavors_qsu4oq0(uint8_t a0);
__attribute__((aligned(4))) void gm_f_flush_newline_9023ml8(void * a0, const void * a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_flush_newline_9023ml8_c(void * a0, gm_t3fcbuehh3bs2 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_format_7a5f58b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d7osfot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_daod35i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fuvjr35(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_g637ghi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_i801868(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_pscs6r9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t32jdqr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t4ivgd1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_inline_comment_end_8i5h1eq(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_inline_raw_string_end_6lenf33(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ascii_space_t2ltvst(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_start_rjefpfm(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj(const void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_is_unicode_space_b2m9rku(uint32_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_keyword_from_1ofmh1u(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_char_4qk4vjp(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_number_9j9sdfi(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_raw_string_uf6bd4g(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_lex_recovering_obo2892(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_string_bcdbdjm(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_token_h8g44s4(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_word_9li16te(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_line_binds_jm0pnul(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_line_comment_s71u8hq(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_2iogcnb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5o6u5a9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_lpokg1m(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lone_cr_ktb0jvm(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_note_newline_cchbr7g(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_te0mgnjdphs16 gm_f_one_or_two_lava5tu(uint8_t a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) void gm_f_open_bracket_3kcj4k8(void * a0, gm_t9qkh0rsooc8d a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_opener_8hqhoc3(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_72bostn(void * a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_at_74p8lkq(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_1q0s3gu(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_punct_text_ov4r40u(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_newline_ke0tojq(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o6f0em6(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pnf14h1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vs3f95a(void * a0, gm_t2eb59dvglnle a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_13c41om(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bnsp8p9(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ci9rduq(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_i65s8oj(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_scan_ident_h1b2n4u(void * a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_scan_punct_ecrh51c(void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_second_word_end_dpnj7g8(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_skip_trivia_014s7ai(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint64_t gm_f_slot_4oe07nr(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_tg7cev6(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_starts_continuation_n3333i7(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_continuation_n3333i7_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_suppresses_newline_after_o08ord2(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_suppresses_newline_after_o08ord2_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) void gm_f_truncate_h4ov51t(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unexpected_char_r3t8d4c(void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_c644mb9o5bq[16] __attribute__((aligned(8)));
extern const uint8_t gm_s1c4sfff8ac39[7];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s5t1b3eks758h[27];
extern const uint8_t gm_s9r64t1166q1f[5];
extern const uint8_t gm_sb54ui2amic32[2];
extern const uint8_t gm_scvhpklutvpjg[4];
extern const uint8_t gm_sgpdmvc9s0d2p[3];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sphr6fu31dtrl[3];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src41vqrhgekac9[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcvmmksi0829pa[];
extern const uint8_t gm_srfagufvviad7[6];
extern const uint8_t gm_srh29phg8b30r[4];
extern const uint8_t gm_ss67f0gmfen1m[3];
extern const uint8_t gm_stnlf4pvbut1c[3];
extern const uint8_t gm_svbhcvlahd4n2[3];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqgj9fu5tboe(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqq6nrh55uon(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqgj9fu5tboe(a, b);
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 40);
        gm_tbgd14esf0jvi t4;
        memset((uint8_t *)&t4 + 12, 0, 4);
        gm_tl874t2atnlh3 t5 = gm_l1;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tftb1odc739t5 t7 = gm_f_push_kiclmcn(t3, &t4);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcgfglnv8p059r, 92, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_byte_b4v8vul(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_byte_b4v8vul F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    void * t3 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t4 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    uint64_t t5 = gm_l1;
    uint64_t t6 = (uintptr_t)t5;
    gm_tqs5p4n2tq19v t7;
    if (t6 < t4) {
    uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)t3 + t6 * 1));
    gm_tqs5p4n2tq19v t9;
    GM_ST(uint8_t, ((uint8_t *)&t9), t8);
    ((uint8_t *)&t9)[1] = 1;
        t7 = t9;
    } else {
    gm_tqs5p4n2tq19v t10;
    memset(&t10, 0, sizeof t10);
        t7 = t10;
    }
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_clear_4qp22g2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_4qp22g2 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        void * t5 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t3) + 0)));
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 140, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_current_char_ctktsl2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_current_char_ctktsl2 F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 24));
    gm_tb31k5l794t7p t5 = GM_CONV(gm_tb31k5l794t7p, gm_f_decode_jrk6b03(t2, t4));
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_cskjv6t053tkje, F.ret);
gm_fprop:
    return GM_CONV(gm_cskjv6t053tkje, F.ret);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_semicolon_7da5b93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_emit_semicolon_7da5b93 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t3fcbuehh3bs2 t3;
        GM_ST(uint8_t, (uint8_t *)&t3 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t3 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t3 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t3 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t3 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t3 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)9ULL));
        uint64_t t4 = gm_l1;
        gm_f_flush_newline_9023ml8(t2, &t3, t4);
    }
    {
        void * t5 = gm_l0;
        void * t6 = gm_l0;
        uint64_t t7 = gm_l1;
        uint64_t t8 = gm_l1;
        uint64_t r9; bool o10 = __builtin_add_overflow(t8, ((uint64_t)1ULL), &r9);
        if (o10) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 656, 33);
        uint64_t t11 = r9;
        gm_tl874t2atnlh3 t12 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t6, t7, t11));
        gm_f_push_newline_ke0tojq(t5, GM_CONV(gm_cseahvg8v7p0vs, t12));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1th396a F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 184)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3pd55t2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3pd55t2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hc7thq6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hc7thq6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jbu9fo3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qbupbr6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qbupbr6 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_p5r0d56(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3) {
    struct gm_fr_error_p5r0d56 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    gm_b16_8 gm_l3; /* message */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 32));
        void * t4 = gm_l0;
        uint64_t t5 = gm_l1;
        uint64_t t6 = gm_l2;
        gm_tl874t2atnlh3 t7 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t4, t5, t6));
        gm_b16_8 t8 = gm_l3;
        gm_b0_1 t9 = gm_f_report_eardf07((void *)NULL, cx, NULL, t3, t7, t8);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_alnum_n81ervr F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_alpha_12215ga(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_digit_2hoggnj(t4);
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_alpha_12215ga F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_upper_iekrftp(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_lower_4ebokoh(t4);
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_ascii_space_t2ltvst(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ascii_space_t2ltvst F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    gm_tb1msgo1fu1n3 t2;
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)32ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 1), ((uint8_t)9ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 2), ((uint8_t)10ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 3), ((uint8_t)12ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 4), ((uint8_t)13ULL));
    uint8_t t3 = 0;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 0, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 1, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 2, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 3, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 4, ((uint8_t *)&t1))) t3 = 1;
    uint8_t t4 = (uint8_t)(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_digit_2hoggnj F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)48ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)57ULL));
        t1 = t5;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ident_cont_317fpdb F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_alnum_n81ervr(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = ((t4 == ((uint8_t)95ULL)));
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_start_rjefpfm(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ident_start_rjefpfm F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_alpha_12215ga(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = ((t4 == ((uint8_t)95ULL)));
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_lower_4ebokoh F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)97ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)122ULL));
        t1 = t5;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj(const void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_punct_fdeminj F;
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
    gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 308, 51);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_upper_iekrftp F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)65ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)90ULL));
        t1 = t5;
    } else t1 = 0;
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
inline __attribute__((aligned(4))) void gm_f_list_2iogcnb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_2iogcnb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3pd55t2();
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
inline __attribute__((aligned(4))) void gm_f_list_5o6u5a9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5o6u5a9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_hc7thq6();
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
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_go2qg0b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_d914i5u();
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
inline __attribute__((aligned(4))) void gm_f_list_lpokg1m(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_lpokg1m F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_qbupbr6();
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lone_cr_ktb0jvm(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lone_cr_ktb0jvm F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 24));
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
        uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 198, 22);
        uint64_t t9 = r7;
        gm_b16_8 t10 = GM_LD(gm_b16_8, gm_c644mb9o5bq);
        gm_b0_1 t11 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t2, t4, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 24));
        uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 199, 5);
        uint64_t t17 = r15;
        GM_ST(uint64_t, (((uint8_t *)t12) + 24), t17);
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
inline __attribute__((aligned(4))) void gm_f_note_newline_cchbr7g(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_note_newline_cchbr7g F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint8_t t4;
        if ((((((uint8_t *)t3) + 224))[12] != 0)) goto mn6;
        t4 = ((uint8_t)1);
        goto me5;
mn6:;
        t4 = ((uint8_t)0);
        goto me5;
mn7:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 203, 8);
me5:;
        if (!t4) goto ie2;
        {
            void * t9 = gm_l0;
            uint8_t *t10 = (((uint8_t *)t9) + 224);
            void * t11 = gm_l0;
            uint64_t t12 = gm_l1;
            uint64_t t13 = gm_l2;
            gm_tl874t2atnlh3 t14 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t11, t12, t13));
            gm_toceffb697p0u t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[12] = 1;
            GM_ST(gm_toceffb697p0u, t10, t15);
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
inline __attribute__((aligned(4))) gm_te0mgnjdphs16 gm_f_one_or_two_lava5tu(uint8_t a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_one_or_two_lava5tu F;
    uint8_t gm_l0; /* two */
    gm_t9qkh0rsooc8d gm_l1; /* long */
    gm_t9qkh0rsooc8d gm_l2; /* short */
    GM_STACK_CHECK_SHALLOW(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_te0mgnjdphs16 t1;
    uint8_t t3 = gm_l0;
    if (!t3) goto ie2;
    gm_t9qkh0rsooc8d t5 = gm_l1;
    gm_te0mgnjdphs16 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t6 + 0), t5);
    GM_ST(uint64_t, ((uint8_t *)&t6 + 8), ((uint64_t)2ULL));
    gm_te0mgnjdphs16 t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    gm_t9qkh0rsooc8d t9 = gm_l2;
    gm_te0mgnjdphs16 t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 0), t9);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 8), ((uint64_t)1ULL));
    gm_te0mgnjdphs16 t11 = t10;
    t1 = t11;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_72bostn(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_peek_72bostn F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    void * t3 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t4 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
    uint64_t t7 = (uintptr_t)t6;
    gm_tqs5p4n2tq19v t8;
    if (t7 < t4) {
    uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)t3 + t7 * 1));
    gm_tqs5p4n2tq19v t10;
    GM_ST(uint8_t, ((uint8_t *)&t10), t9);
    ((uint8_t *)&t10)[1] = 1;
        t8 = t10;
    } else {
    gm_tqs5p4n2tq19v t11;
    memset(&t11, 0, sizeof t11);
        t8 = t11;
    }
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_at_74p8lkq(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_peek_at_74p8lkq F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    void * t3 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t4 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
    uint64_t t7 = gm_l1;
    uint64_t r8; bool o9 = __builtin_add_overflow(t6, t7, &r8);
    if (o9) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 153, 64);
    uint64_t t10 = r8;
    uint64_t t11 = (uintptr_t)t10;
    gm_tqs5p4n2tq19v t12;
    if (t11 < t4) {
    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)t3 + t11 * 1));
    gm_tqs5p4n2tq19v t14;
    GM_ST(uint8_t, ((uint8_t *)&t14), t13);
    ((uint8_t *)&t14)[1] = 1;
        t12 = t14;
    } else {
    gm_tqs5p4n2tq19v t15;
    memset(&t15, 0, sizeof t15);
        t12 = t15;
    }
    F.ret = t12;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_1q0s3gu(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_1q0s3gu F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t2eb59dvglnle gm_l2; /* x */
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
            gm_tu1ap1uj3psaf t8;
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
        gm_t2eb59dvglnle t17 = GM_LD(gm_t2eb59dvglnle, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 16));
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
        gm_t2eb59dvglnle t29 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tu1ap1uj3psaf *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tu1ap1uj3psaf *)gm_out) + 18, 0);
        ((uint8_t *)&(*(gm_tu1ap1uj3psaf *)gm_out))[16] = 1;
        GM_ST(gm_t2eb59dvglnle, (uint8_t *)&(*(gm_tu1ap1uj3psaf *)gm_out) + 0, t29);
        goto gm_fdone;
    }
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_im9d4ro F;
    void * gm_l0; /* l */
    uint32_t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_d914i5u();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_2onqfca(t10, ((uint64_t)1ULL), t12);
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
        uint32_t t21 = gm_l1;
        GM_ST(uint32_t, ((uint8_t *)t20 + t18 * 4), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 4)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kiclmcn F;
    void * gm_l0; /* l */
    gm_tbgd14esf0jvi gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_jbu9fo3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_8skjmj4(t10, ((uint64_t)1ULL), t12);
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
        gm_tbgd14esf0jvi t21 = GM_LD(gm_tbgd14esf0jvi, ((uint8_t *)a1));
        GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)t20 + t18 * 32), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 32)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o6f0em6(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o6f0em6 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_hc7thq6();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_13c41om(t10, ((uint64_t)1ULL), t12);
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
        gm_t3jim1gvsdr87 t21 = GM_LD(gm_t3jim1gvsdr87, ((uint8_t *)a1));
        GM_ST(gm_t3jim1gvsdr87, ((uint8_t *)t20 + t18 * 40), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pnf14h1(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pnf14h1 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_qbupbr6();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_bnsp8p9(t10, ((uint64_t)1ULL), t12);
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
        gm_tm4pc0emhrv1g t21 = GM_LD(gm_tm4pc0emhrv1g, ((uint8_t *)a1));
        GM_ST(gm_tm4pc0emhrv1g, ((uint8_t *)t20 + t18 * 48), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 48)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vs3f95a(void * a0, gm_t2eb59dvglnle a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vs3f95a F;
    void * gm_l0; /* l */
    gm_t2eb59dvglnle gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_3pd55t2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_ci9rduq(t10, ((uint64_t)1ULL), t12);
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
        gm_t2eb59dvglnle t21 = gm_l1;
        GM_ST(gm_t2eb59dvglnle, ((uint8_t *)t20 + t18 * 16), t21);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_13c41om(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_13c41om F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2onqfca F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)4ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8skjmj4 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bnsp8p9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_bnsp8p9 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ci9rduq(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ci9rduq F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) uint64_t gm_f_slot_4oe07nr(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_slot_4oe07nr F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 30u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma5; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    t1 = ((uint64_t)0ULL);
    goto me2;
mn8:;
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t11 != ((uint8_t)1ULL)) goto mn10;
    t1 = ((uint64_t)1ULL);
    goto me2;
mn10:;
ma5:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t15 != ((uint8_t)2ULL)) goto pa14;
    goto po13;
pa14:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t17 != ((uint8_t)3ULL)) goto pa16;
    goto po13;
pa16:;
    goto mn12;
po13:;
    t1 = ((uint64_t)2ULL);
    goto me2;
mn12:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 60, 40);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_64igq5o F;
    uint32_t gm_l0; /* file */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t t1 = gm_l0;
    uint64_t t2 = gm_l1;
    uint32_t t3 = (uint32_t)t2;
    uint64_t t4 = gm_l2;
    uint32_t t5 = (uint32_t)t4;
    gm_tl874t2atnlh3 t6;
    GM_ST(uint32_t, ((uint8_t *)&t6 + 0), t1);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 4), t3);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 8), t5);
    F.ret = t6;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_tg7cev6(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_tg7cev6 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)t1) + 0));
    uint64_t t3 = gm_l1;
    uint64_t t4 = gm_l2;
    gm_tl874t2atnlh3 t5 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_64igq5o(t2, t3, t4));
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
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
inline __attribute__((aligned(4))) void gm_f_truncate_h4ov51t(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_h4ov51t F;
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
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
inline __attribute__((aligned(4))) uint64_t gm_f_backtick_run_l95fb09(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_backtick_run_l95fb09 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 140u);
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
            uint64_t t4 = gm_l2;
            uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t6 = (t4 < t5);
            if (!t6) goto lb3;
            uint64_t t7 = gm_l2;
            uint64_t t8 = (uintptr_t)t7;
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srcvmmksi0829pa, 734, 25);
            uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 1));
            uint8_t t11 = ((t10 == ((uint8_t)96ULL)));
            if (!t11) goto lb3;
            {
                uint64_t t14 = gm_l2;
                uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
                if (o16) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 734, 39);
                uint64_t t17 = r15;
                gm_l2 = t17;
            }
lc3:;
        }
lb3:;
    }
    {
        uint64_t t18 = gm_l2;
        uint64_t t19 = gm_l1;
        uint64_t r20; bool o21 = __builtin_sub_overflow(t18, t19, &r20);
        if (o21) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 735, 12);
        uint64_t t22 = r20;
        uint8_t t23 = 0;
        F.ret = t22;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_block_comment_fvce7qt(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_block_comment_fvce7qt F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* i */
    int32_t gm_l3; /* depth */
    uint8_t gm_l4; /* b */
    gm_tqs5p4n2tq19v gm_l5; /* next */
    GM_STACK_CHECK(&F, 427u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        uint64_t t4 = gm_l1;
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)2ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 235, 10);
        uint64_t t7 = r5;
        gm_l2 = t7;
    }
    {
        gm_l3 = ((int32_t)1LL);
    }
    {
        for (;;) {
            int32_t t9 = gm_l3;
            uint8_t t10 = (t9 > ((int32_t)0LL));
            if (!t10) goto lb8;
            {
                void * t13 = gm_l0;
                uint64_t t14 = gm_l2;
                gm_tqs5p4n2tq19v t15 = gm_f_byte_b4v8vul(t13, t14);
                uint8_t t16;
                if (((((uint8_t *)&t15))[1] != 0)) {
                    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
                    t16 = t17;
                } else {
                    {
                        void * t19 = gm_l0;
                        uint64_t t20 = gm_l1;
                        uint64_t t21 = gm_l1;
                        uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)2ULL), &r22);
                        if (o23) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 239, 26);
                        uint64_t t24 = r22;
                        gm_b16_8 t25;
                        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s5t1b3eks758h));
                        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(26));
                        gm_b0_1 t26 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t19, t20, t24, t25);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                    }
                    goto lb8;
                }
                gm_l4 = t16;
            }
            {
                void * t27 = gm_l0;
                uint64_t t28 = gm_l2;
                uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                if (o30) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 242, 25);
                uint64_t t31 = r29;
                gm_tqs5p4n2tq19v t32 = gm_f_byte_b4v8vul(t27, t31);
                gm_l5 = t32;
            }
            {
                uint8_t t34 = gm_l4;
                uint8_t t35 = ((t34 == ((uint8_t)47ULL)));
                if (!t35) goto ie33;
                gm_tqs5p4n2tq19v t36 = gm_l5;
                gm_tqs5p4n2tq19v t37;
                GM_ST(uint8_t, ((uint8_t *)&t37), ((uint8_t)42ULL));
                ((uint8_t *)&t37)[1] = 1;
                uint8_t t38 = (gm_eqb44cb3cf1n(((uint8_t *)&t36), ((uint8_t *)&t37)));
                if (!t38) goto ie33;
                {
                    int32_t t40 = gm_l3;
                    int32_t r41; bool o42 = __builtin_add_overflow(t40, ((int32_t)1LL), &r41);
                    if (o42) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 244, 13);
                    int32_t t43 = r41;
                    gm_l3 = t43;
                }
                {
                    uint64_t t44 = gm_l2;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)2ULL), &r45);
                    if (o46) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 245, 13);
                    uint64_t t47 = r45;
                    gm_l2 = t47;
                }
                goto ix33;
ie33:;
                uint8_t t49 = gm_l4;
                uint8_t t50 = ((t49 == ((uint8_t)42ULL)));
                if (!t50) goto ie48;
                gm_tqs5p4n2tq19v t51 = gm_l5;
                gm_tqs5p4n2tq19v t52;
                GM_ST(uint8_t, ((uint8_t *)&t52), ((uint8_t)47ULL));
                ((uint8_t *)&t52)[1] = 1;
                uint8_t t53 = (gm_eqb44cb3cf1n(((uint8_t *)&t51), ((uint8_t *)&t52)));
                if (!t53) goto ie48;
                {
                    int32_t t55 = gm_l3;
                    int32_t r56; bool o57 = __builtin_sub_overflow(t55, ((int32_t)1LL), &r56);
                    if (o57) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 247, 13);
                    int32_t t58 = r56;
                    gm_l3 = t58;
                }
                {
                    uint64_t t59 = gm_l2;
                    uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)2ULL), &r60);
                    if (o61) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 248, 13);
                    uint64_t t62 = r60;
                    gm_l2 = t62;
                }
                goto ix48;
ie48:;
                uint8_t t64 = gm_l4;
                uint8_t t65 = ((t64 == ((uint8_t)10ULL)));
                if (!t65) goto ie63;
                {
                    void * t67 = gm_l0;
                    uint64_t t68 = gm_l2;
                    uint64_t t69 = gm_l2;
                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                    if (o71) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 250, 32);
                    uint64_t t72 = r70;
                    gm_f_note_newline_cchbr7g(t67, t68, t72);
                }
                {
                    uint64_t t73 = gm_l2;
                    uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                    if (o75) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 251, 13);
                    uint64_t t76 = r74;
                    gm_l2 = t76;
                }
                goto ix63;
ie63:;
                uint8_t t78 = gm_l4;
                uint8_t t79 = ((t78 == ((uint8_t)13ULL)));
                if (!t79) goto ie77;
                gm_tqs5p4n2tq19v t80 = gm_l5;
                gm_tqs5p4n2tq19v t81;
                GM_ST(uint8_t, ((uint8_t *)&t81), ((uint8_t)10ULL));
                ((uint8_t *)&t81)[1] = 1;
                uint8_t t82 = (gm_eqb44cb3cf1n(((uint8_t *)&t80), ((uint8_t *)&t81)));
                if (!t82) goto ie77;
                {
                    void * t84 = gm_l0;
                    uint64_t t85 = gm_l2;
                    uint64_t t86 = gm_l2;
                    uint64_t r87; bool o88 = __builtin_add_overflow(t86, ((uint64_t)2ULL), &r87);
                    if (o88) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 253, 32);
                    uint64_t t89 = r87;
                    gm_f_note_newline_cchbr7g(t84, t85, t89);
                }
                {
                    uint64_t t90 = gm_l2;
                    uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)2ULL), &r91);
                    if (o92) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 254, 13);
                    uint64_t t93 = r91;
                    gm_l2 = t93;
                }
                goto ix77;
ie77:;
                uint8_t t95 = gm_l4;
                uint8_t t96 = ((t95 == ((uint8_t)13ULL)));
                if (!t96) goto ie94;
                {
                    void * t98 = gm_l0;
                    uint64_t t99 = gm_l2;
                    uint64_t t100 = gm_l2;
                    uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                    if (o102) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 256, 25);
                    uint64_t t103 = r101;
                    gm_b16_8 t104 = GM_LD(gm_b16_8, gm_c644mb9o5bq);
                    gm_b0_1 t105 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t98, t99, t103, t104);
                    if (gm_unw.kind) {
                        goto gm_ll11;
                    }
                }
                {
                    uint64_t t106 = gm_l2;
                    uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                    if (o108) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 257, 13);
                    uint64_t t109 = r107;
                    gm_l2 = t109;
                }
                goto ix94;
ie94:;
                {
                    uint64_t t111 = gm_l2;
                    uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                    if (o113) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 259, 13);
                    uint64_t t114 = r112;
                    gm_l2 = t114;
                }
ix94:;
ix77:;
ix63:;
ix48:;
ix33:;
            }
lc8:;
        }
gm_ll11:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
        }
        goto gm_lad;
lb8:;
    }
    {
        void * t115 = gm_l0;
        uint64_t t116 = gm_l2;
        GM_ST(uint64_t, (((uint8_t *)t115) + 24), t116);
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
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_byte_b4v8vul(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_c_equality_u566nc4(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_c_equality_u566nc4 F;
    void * gm_l0; /* lx */
    uint8_t gm_l1; /* b */
    uint64_t gm_l2; /* lo */
    uint8_t gm_l3; /* eq */
    gm_b16_8 gm_l4; /* op */
    gm_b16_8 gm_l5; /* written */
    gm_t3fcbuehh3bs2 gm_l6; /* kind */
    gm_b16_8 gm_l7; /* before */
    gm_b16_8 gm_l8; /* after */
    uint8_t gm_l9; /* c */
    GM_STACK_CHECK(&F, 1172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
        uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)2ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 329, 5);
        uint64_t t9 = r7;
        GM_ST(uint64_t, (((uint8_t *)t4) + 24), t9);
    }
    {
        uint8_t t10 = gm_l1;
        uint8_t t11 = ((t10 == ((uint8_t)61ULL)));
        gm_l3 = t11;
    }
    {
        gm_b16_8 t12;
        uint8_t t14 = gm_l3;
        if (!t14) goto ie13;
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sgpdmvc9s0d2p));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(2));
        gm_b16_8 t17 = t16;
        t12 = t17;
        goto ix13;
ie13:;
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1c4sfff8ac39));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(6));
        gm_b16_8 t20 = t19;
        t12 = t20;
ix13:;
        gm_l4 = t12;
    }
    {
        gm_b16_8 t21;
        uint8_t t23 = gm_l3;
        if (!t23) goto ie22;
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sphr6fu31dtrl));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(2));
        gm_b16_8 t26 = t25;
        t21 = t26;
        goto ix22;
ie22:;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_stnlf4pvbut1c));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(2));
        gm_b16_8 t29 = t28;
        t21 = t29;
ix22:;
        gm_l5 = t21;
    }
    {
        gm_t3fcbuehh3bs2 t30;
        uint8_t t32 = gm_l3;
        if (!t32) goto ie31;
        gm_t9qkh0rsooc8d t34;
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)16ULL));
        gm_t3fcbuehh3bs2 t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t35 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
        gm_t3fcbuehh3bs2 t36 = t35;
        t30 = t36;
        goto ix31;
ie31:;
        gm_t9qkh0rsooc8d t38;
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)67ULL));
        gm_t3fcbuehh3bs2 t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t39 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t39 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t39 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t39 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t39 + 8), t38);
        gm_t3fcbuehh3bs2 t40 = t39;
        t30 = t40;
ix31:;
        gm_l6 = t30;
    }
    {
        gm_b16_8 t41;
        uint64_t t43 = gm_l2;
        uint8_t t44 = (t43 > ((uint64_t)0ULL));
        if (!t44) goto ie42;
        uint64_t t45 = gm_l2;
        uint64_t r46; bool o47 = __builtin_sub_overflow(t45, ((uint64_t)1ULL), &r46);
        if (o47) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 335, 55);
        uint64_t t48 = r46;
        uint64_t t49 = (uintptr_t)t48;
        void * t50 = gm_l0;
        uint64_t t51 = GM_LD(uintptr_t, (((uint8_t *)t50) + 8) + 8);
        if (t49 >= (uintptr_t)(t51)) gm_trap_index(t49, t51, gm_srcvmmksi0829pa, 335, 48);
        uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t50) + 8)) + t49 * 1));
        uint8_t t53 = gm_f_is_ascii_space_t2ltvst(t52);
        uint8_t t54 = (uint8_t)!t53;
        if (!t54) goto ie42;
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(1));
        gm_b16_8 t57 = t56;
        t41 = t57;
        goto ix42;
ie42:;
        gm_b16_8 t59;
        GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(0));
        gm_b16_8 t60 = t59;
        t41 = t60;
ix42:;
        gm_l7 = t41;
    }
    {
        gm_b16_8 t61;
        GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(0));
        gm_l8 = t61;
    }
    {
        void * t63 = gm_l0;
        gm_tqs5p4n2tq19v t64 = gm_f_peek_72bostn(t63);
        if (!((((uint8_t *)&t64))[1] != 0)) goto ie62;
        uint8_t t65 = GM_LD(uint8_t, ((uint8_t *)&t64));
        gm_l9 = t65;
        {
            uint8_t t68 = gm_l9;
            uint8_t t69 = gm_f_is_ascii_space_t2ltvst(t68);
            uint8_t t70 = (uint8_t)!t69;
            if (!t70) goto ie67;
            {
                gm_b16_8 t72;
                GM_ST(void *, (uint8_t *)&t72, (void *)(gm_s3p0rbs9hc3lg));
                GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(1));
                gm_l8 = t72;
            }
            goto ix67;
ie67:;
ix67:;
        }
        goto ix62;
ie62:;
ix62:;
    }
    {
        void * t73 = gm_l0;
        uint64_t t74 = gm_l2;
        void * t75 = gm_l0;
        uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)t75) + 24));
        gm_tl874t2atnlh3 t77 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t73, t74, t76));
        gm_t6j6smj7qoctu t78;
        gm_b16_8 t79 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t78 + 0, t79);
        gm_b16_8 t80 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t78 + 16, t80);
        gm_tabb710d9mq2e t81;
        gm_f_format_g637ghi((void *)&t81, (void *)NULL, cx, NULL, &t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 8, gm_srcvmmksi0829pa, 340, 42);
        gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&t81) + 8));
        gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
        gm_f_error_s6hn871((void *)&F.l10, (void *)NULL, cx, NULL, t77, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t84 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t85 = gm_l4;
        gm_tnpifv20irnph t86;
        GM_ST(gm_b16_8, ((uint8_t *)&t86 + 0), t85);
        gm_tabb710d9mq2e t87;
        gm_f_format_ooibcq2((void *)&t87, (void *)NULL, cx, NULL, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 8, gm_srcvmmksi0829pa, 341, 15);
        gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&t87) + 8));
        gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
        void * t90 = gm_l0;
        uint64_t t91 = gm_l2;
        void * t92 = gm_l0;
        uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)t92) + 24));
        gm_tl874t2atnlh3 t94 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t90, t91, t93));
        gm_tfeab279c16rr t95;
        gm_b16_8 t96 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t95 + 0, t96);
        gm_b16_8 t97 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t95 + 16, t97);
        gm_b16_8 t98 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t95 + 32, t98);
        gm_tabb710d9mq2e t99;
        gm_f_format_dsta2ql((void *)&t99, (void *)NULL, cx, NULL, &t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t99))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t99) + 8, gm_srcvmmksi0829pa, 341, 67);
        gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&t99) + 8));
        gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
        gm_b0_1 t102 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t84, t89, t94, t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t103 = gm_l0;
        void * t104 = GM_LD(void *, (((uint8_t *)t103) + 32));
        gm_f_push_o4ckdr1(t104, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l10)));
    }
    {
        void * t105 = gm_l0;
        uint64_t t106 = gm_l2;
        void * t107 = gm_l0;
        uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)t107) + 24));
        gm_b0_1 t109 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t105, &(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l6)), t106, t108);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_all_brackets_bql4j88(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_close_all_brackets_bql4j88 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    gm_tl874t2atnlh3 gm_l2; /* here */
    gm_t2eb59dvglnle gm_l3; /* b */
    void * gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* open */
    GM_STACK_CHECK(&F, 635u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_toceffb697p0u t3;
        memset(&t3, 0, sizeof t3);
        GM_ST(gm_toceffb697p0u, (((uint8_t *)t2) + 224), t3);
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = gm_l1;
        uint64_t t6 = gm_l1;
        gm_tl874t2atnlh3 t7 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t4, t5, t6));
        gm_l2 = t7;
    }
    {
        for (;;) {
            void * t9 = gm_l0;
            void * t10 = (void *)(((uint8_t *)t9) + 120);
            gm_tu1ap1uj3psaf t11;
            gm_f_pop_1q0s3gu((void *)&t11, t10);
            if (!((((uint8_t *)&t11))[16] != 0)) goto lb8;
            gm_t2eb59dvglnle t12 = GM_LD(gm_t2eb59dvglnle, ((uint8_t *)&t11));
            gm_l3 = t12;
            {
                {
                    void * t16 = gm_l0;
                    uint8_t *t17 = (uint8_t *)((((uint8_t *)t16) + 160));
                    gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 0));
                    uint64_t t19 = gm_f_slot_4oe07nr(t18);
                    uint64_t t20 = (uintptr_t)t19;
                    if (t20 >= (uintptr_t)(3)) gm_trap_index(t20, 3, gm_srcvmmksi0829pa, 614, 9);
                    void * t21 = (void *)(t17 + t20 * 8);
                    gm_l4 = t21;
                }
                {
                    void * t22 = gm_l4;
                    void * t23 = gm_l4;
                    uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)t23));
                    uint64_t r25; bool o26 = __builtin_sub_overflow(t24, ((uint64_t)1ULL), &r25);
                    if (o26) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 614, 9);
                    uint64_t t27 = r25;
                    GM_ST(uint64_t, ((uint8_t *)t22), t27);
                }
            }
            {
                gm_t9qkh0rsooc8d t28 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 0));
                gm_t9qkh0rsooc8d t29 = gm_f_opener_8hqhoc3(t28);
                gm_b16_8 t30 = gm_f_punct_text_ov4r40u(t29);
                gm_l5 = t30;
            }
            {
                gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 4));
                gm_b16_8 t32 = gm_l5;
                gm_tnpifv20irnph t33;
                GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
                gm_tabb710d9mq2e t34;
                gm_f_format_t32jdqr((void *)&t34, (void *)NULL, cx, NULL, t33);
                if (gm_unw.kind) {
                    goto gm_ll13;
                }
                if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srcvmmksi0829pa, 616, 33);
                gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
                gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
                gm_f_error_s6hn871((void *)&F.l6, (void *)NULL, cx, NULL, t31, t36);
                if (gm_unw.kind) {
                    goto gm_ll13;
                }
            }
            {
                void * t37 = (void *)((uint8_t *)&F.l6);
                gm_tl874t2atnlh3 t38 = gm_l2;
                gm_b16_8 t39 = gm_l5;
                gm_tnpifv20irnph t40;
                GM_ST(gm_b16_8, ((uint8_t *)&t40 + 0), t39);
                gm_tabb710d9mq2e t41;
                gm_f_format_d7osfot((void *)&t41, (void *)NULL, cx, NULL, t40);
                if (gm_unw.kind) {
                    goto gm_ll13;
                }
                if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 8, gm_srcvmmksi0829pa, 617, 27);
                gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&t41) + 8));
                gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                gm_f_add_label_50fj7vv(t37, GM_CONV(gm_cseahvg8v7p0vs, t38), t43);
            }
            {
                void * t44 = gm_l0;
                void * t45 = GM_LD(void *, (((uint8_t *)t44) + 32));
                gm_f_push_o4ckdr1(t45, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l6)));
            }
            {
                void * t46 = gm_l0;
                void * t47 = (void *)(((uint8_t *)t46) + 40);
                gm_tm4pc0emhrv1g t48;
                memset((uint8_t *)&t48 + 44, 0, 4);
                gm_t9qkh0rsooc8d t49 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 0));
                gm_t9qkh0rsooc8d t50 = gm_f_closer_fus0off(t49);
                gm_t3fcbuehh3bs2 t51;
                GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t51 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t51 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t51 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t51 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t51 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t51 + 8), t50);
                GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t48 + 0, t51);
                gm_tl874t2atnlh3 t52 = gm_l2;
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t48 + 32, t52);
                gm_tftb1odc739t5 t53 = gm_f_push_pnf14h1(t47, &t48);
                if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_srcvmmksi0829pa, 619, 9);
                gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
            }
lc8:;
        }
gm_ll13:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
        }
        goto gm_lad;
lb8:;
    }
    {
        void * t55 = gm_l0;
        void * t56 = (void *)(((uint8_t *)t55) + 184);
        gm_f_clear_4qp22g2(t56);
    }
    {
        void * t57 = gm_l0;
        gm_tl874t2atnlh3 t58 = gm_l2;
        gm_f_push_newline_ke0tojq(t57, GM_CONV(gm_cseahvg8v7p0vs, t58));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_close_bracket_tbjprpe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_close_bracket_tbjprpe F;
    void * gm_l0; /* lx */
    gm_t9qkh0rsooc8d gm_l1; /* c */
    gm_tl874t2atnlh3 gm_l2; /* at */
    gm_b16_8 gm_l3; /* items */
    uint64_t gm_l4; /* i */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* k */
    gm_t2eb59dvglnle gm_l8; /* skipped */
    void * gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* open */
    void * gm_l12; /* <tmp> */
    uint64_t gm_l13; /* depth */
    uint64_t gm_l14; /* n */
    GM_STACK_CHECK(&F, 891u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 120) + 0));
        gm_l3 = t3;
    }
    {
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t r5; bool o6 = __builtin_sub_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 634, 10);
        uint64_t t7 = r5;
        gm_l4 = t7;
    }
    {
        for (;;) {
            uint64_t t9 = gm_l4;
            uint8_t t10 = (t9 > ((uint64_t)0ULL));
            if (!t10) goto lb8;
            uint64_t t11 = gm_l4;
            uint64_t t12 = (uintptr_t)t11;
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_srcvmmksi0829pa, 635, 28);
            gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 16) + 0));
            gm_t9qkh0rsooc8d t15 = gm_f_closer_fus0off(t14);
            gm_t9qkh0rsooc8d t16 = gm_l1;
            uint8_t t17 = (!gm_eqv3tj1pu43g(((uint8_t *)&t15), ((uint8_t *)&t16)));
            if (!t17) goto lb8;
            {
                uint64_t t20 = gm_l4;
                uint64_t r21; bool o22 = __builtin_sub_overflow(t20, ((uint64_t)1ULL), &r21);
                if (o22) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 635, 54);
                uint64_t t23 = r21;
                gm_l4 = t23;
            }
lc8:;
        }
lb8:;
    }
    {
        {
            uint64_t t25 = gm_l4;
            uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
            if (o27) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 636, 14);
            uint64_t t28 = r26;
            gm_l6 = t28;
        }
        {
            uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            gm_l5 = t29;
        }
        {
            for (;;) {
                uint64_t t31 = gm_l6;
                uint64_t t32 = gm_l5;
                uint8_t t33 = (t31 < t32);
                if (!t33) goto lb30;
                {
                    uint64_t t36 = gm_l6;
                    uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&t36));
                    gm_l7 = t39;
                    {
                        uint64_t t41 = gm_l7;
                        uint64_t t42 = (uintptr_t)t41;
                        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t42 >= (uintptr_t)(t43)) gm_trap_index(t42, t43, gm_srcvmmksi0829pa, 637, 20);
                        gm_t2eb59dvglnle t44 = GM_LD(gm_t2eb59dvglnle, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t42 * 16));
                        gm_l8 = t44;
                    }
                    {
                        {
                            void * t46 = gm_l0;
                            uint8_t *t47 = (uint8_t *)((((uint8_t *)t46) + 160));
                            gm_t9qkh0rsooc8d t48 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l8) + 0));
                            uint64_t t49 = gm_f_slot_4oe07nr(t48);
                            uint64_t t50 = (uintptr_t)t49;
                            if (t50 >= (uintptr_t)(3)) gm_trap_index(t50, 3, gm_srcvmmksi0829pa, 638, 9);
                            void * t51 = (void *)(t47 + t50 * 8);
                            gm_l9 = t51;
                        }
                        {
                            void * t52 = gm_l9;
                            void * t53 = gm_l9;
                            uint64_t t54 = GM_LD(uint64_t, ((uint8_t *)t53));
                            uint64_t r55; bool o56 = __builtin_sub_overflow(t54, ((uint64_t)1ULL), &r55);
                            if (o56) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 638, 9);
                            uint64_t t57 = r55;
                            GM_ST(uint64_t, ((uint8_t *)t52), t57);
                        }
                    }
                    {
                        gm_t9qkh0rsooc8d t58 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l8) + 0));
                        gm_t9qkh0rsooc8d t59 = gm_f_opener_8hqhoc3(t58);
                        gm_b16_8 t60 = gm_f_punct_text_ov4r40u(t59);
                        gm_l10 = t60;
                    }
                    {
                        gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l8) + 4));
                        gm_b16_8 t62 = gm_l10;
                        gm_tnpifv20irnph t63;
                        GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t62);
                        gm_tabb710d9mq2e t64;
                        gm_f_format_t32jdqr((void *)&t64, (void *)NULL, cx, NULL, t63);
                        if (gm_unw.kind) {
                            goto gm_ll34;
                        }
                        if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 8, gm_srcvmmksi0829pa, 640, 39);
                        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 8));
                        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                        gm_f_error_s6hn871((void *)&F.l11, (void *)NULL, cx, NULL, t61, t66);
                        if (gm_unw.kind) {
                            goto gm_ll34;
                        }
                    }
                    {
                        void * t67 = (void *)((uint8_t *)&F.l11);
                        gm_tl874t2atnlh3 t68 = gm_l2;
                        gm_t6j6smj7qoctu t69;
                        gm_t9qkh0rsooc8d t70 = gm_l1;
                        gm_b16_8 t71 = gm_f_punct_text_ov4r40u(t70);
                        GM_ST(gm_b16_8, (uint8_t *)&t69 + 0, t71);
                        gm_b16_8 t72 = gm_l10;
                        GM_ST(gm_b16_8, (uint8_t *)&t69 + 16, t72);
                        gm_tabb710d9mq2e t73;
                        gm_f_format_7a5f58b((void *)&t73, (void *)NULL, cx, NULL, &t69);
                        if (gm_unw.kind) {
                            goto gm_ll34;
                        }
                        if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 8, gm_srcvmmksi0829pa, 641, 25);
                        gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
                        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                        gm_f_add_label_50fj7vv(t67, GM_CONV(gm_cseahvg8v7p0vs, t68), t75);
                    }
                    {
                        void * t76 = gm_l0;
                        void * t77 = GM_LD(void *, (((uint8_t *)t76) + 32));
                        gm_f_push_o4ckdr1(t77, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l11)));
                    }
                    goto me37;
mn38:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 636, 5);
me37:;
                }
lc30:;
                {
                    uint64_t t79 = gm_l6;
                    uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                    uint64_t t82 = r80;
                    gm_l6 = t82;
                }
            }
gm_ll34:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb30; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc30; }
            }
            goto gm_lad;
lb30:;
        }
    }
    {
        {
            void * t84 = gm_l0;
            uint8_t *t85 = (uint8_t *)((((uint8_t *)t84) + 160));
            gm_t9qkh0rsooc8d t86 = gm_l1;
            uint64_t t87 = gm_f_closer_slot_f4sb6v0(t86);
            uint64_t t88 = (uintptr_t)t87;
            if (t88 >= (uintptr_t)(3)) gm_trap_index(t88, 3, gm_srcvmmksi0829pa, 644, 5);
            void * t89 = (void *)(t85 + t88 * 8);
            gm_l12 = t89;
        }
        {
            void * t90 = gm_l12;
            void * t91 = gm_l12;
            uint64_t t92 = GM_LD(uint64_t, ((uint8_t *)t91));
            uint64_t r93; bool o94 = __builtin_sub_overflow(t92, ((uint64_t)1ULL), &r93);
            if (o94) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 644, 5);
            uint64_t t95 = r93;
            GM_ST(uint64_t, ((uint8_t *)t90), t95);
        }
    }
    {
        void * t96 = gm_l0;
        void * t97 = (void *)(((uint8_t *)t96) + 120);
        uint64_t t98 = gm_l4;
        gm_f_truncate_h4ov51t(t97, t98);
    }
    {
        uint64_t t99 = gm_l4;
        gm_l13 = t99;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb100;
            {
                void * t103 = gm_l0;
                uint64_t t104 = GM_LD(uintptr_t, ((((uint8_t *)t103) + 184) + 0) + 8);
                gm_l14 = t104;
            }
            {
                uint8_t t106 = 0;
                uint64_t t107 = gm_l14;
                uint8_t t108 = ((t107 == ((uint64_t)0ULL)));
                if (!t108) {
                    uint64_t t109 = gm_l14;
                    uint64_t r110; bool o111 = __builtin_sub_overflow(t109, ((uint64_t)1ULL), &r110);
                    if (o111) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 649, 47);
                    uint64_t t112 = r110;
                    uint64_t t113 = (uintptr_t)t112;
                    void * t114 = gm_l0;
                    uint64_t t115 = GM_LD(uintptr_t, ((((uint8_t *)t114) + 184) + 0) + 8);
                    if (t113 >= (uintptr_t)(t115)) gm_trap_index(t113, t115, gm_srcvmmksi0829pa, 649, 22);
                    uint64_t t116 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t114) + 184) + 0)) + t113 * 8));
                    uint64_t t117 = gm_l13;
                    uint8_t t118 = (t116 <= t117);
                    t106 = t118;
                } else t106 = 1;
                if (!t106) goto ie105;
                {
                    goto lb100;
                }
                goto ix105;
ie105:;
ix105:;
            }
            {
                {
                    void * t121 = gm_l0;
                    void * t122 = (void *)(((uint8_t *)t121) + 184);
                    gm_tf5srh4mp4b3i t123 = gm_f_pop_j4rpvm5(t122);
                }
            }
lc100:;
        }
lb100:;
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_closer_fus0off(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_closer_fus0off F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma5; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)62ULL));
    t1 = t10;
    goto me2;
mn8:;
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)1ULL)) goto mn11;
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)64ULL));
    t1 = t13;
    goto me2;
mn11:;
ma5:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t17 != ((uint8_t)2ULL)) goto pa16;
    goto po15;
pa16:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)3ULL)) goto pa18;
    goto po15;
pa18:;
    goto mn14;
po15:;
    gm_t9qkh0rsooc8d t20;
    GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)66ULL));
    t1 = t20;
    goto me2;
mn14:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 53, 42);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_closer_slot_f4sb6v0(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_closer_slot_f4sb6v0 F;
    gm_t9qkh0rsooc8d gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma5; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma5; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma5; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma5; case ((uint8_t)12ULL): goto ma5; case ((uint8_t)13ULL): goto ma5; case ((uint8_t)14ULL): goto ma5; case ((uint8_t)15ULL): goto ma5; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma5; case ((uint8_t)18ULL): goto ma5; case ((uint8_t)19ULL): goto ma5; case ((uint8_t)20ULL): goto ma5; case ((uint8_t)21ULL): goto ma5; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma5; case ((uint8_t)24ULL): goto ma5; case ((uint8_t)25ULL): goto ma5; case ((uint8_t)26ULL): goto ma5; case ((uint8_t)27ULL): goto ma5; case ((uint8_t)28ULL): goto ma5; case ((uint8_t)29ULL): goto ma5; case ((uint8_t)30ULL): goto ma5; case ((uint8_t)31ULL): goto ma5; case ((uint8_t)32ULL): goto ma5; case ((uint8_t)33ULL): goto ma5; case ((uint8_t)34ULL): goto ma5; case ((uint8_t)35ULL): goto ma5; case ((uint8_t)36ULL): goto ma5; case ((uint8_t)37ULL): goto ma5; case ((uint8_t)38ULL): goto ma5; case ((uint8_t)39ULL): goto ma5; case ((uint8_t)40ULL): goto ma5; case ((uint8_t)41ULL): goto ma5; case ((uint8_t)42ULL): goto ma5; case ((uint8_t)43ULL): goto ma5; case ((uint8_t)44ULL): goto ma5; case ((uint8_t)45ULL): goto ma5; case ((uint8_t)46ULL): goto ma5; case ((uint8_t)47ULL): goto ma5; case ((uint8_t)48ULL): goto ma5; case ((uint8_t)49ULL): goto ma5; case ((uint8_t)50ULL): goto ma5; case ((uint8_t)51ULL): goto ma5; case ((uint8_t)52ULL): goto ma5; case ((uint8_t)53ULL): goto ma5; case ((uint8_t)54ULL): goto ma5; case ((uint8_t)55ULL): goto ma5; case ((uint8_t)56ULL): goto ma5; case ((uint8_t)57ULL): goto ma5; case ((uint8_t)58ULL): goto ma5; case ((uint8_t)59ULL): goto ma5; case ((uint8_t)60ULL): goto ma5; case ((uint8_t)61ULL): goto ma5; case ((uint8_t)62ULL): goto ma3; case ((uint8_t)63ULL): goto ma5; case ((uint8_t)64ULL): goto ma4; case ((uint8_t)65ULL): goto ma5; case ((uint8_t)66ULL): goto ma5; case ((uint8_t)67ULL): goto ma5; case ((uint8_t)68ULL): goto ma5; default: goto ma5; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)62ULL)) goto mn8;
    t1 = ((uint64_t)0ULL);
    goto me2;
mn8:;
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t11 != ((uint8_t)64ULL)) goto mn10;
    t1 = ((uint64_t)1ULL);
    goto me2;
mn10:;
ma5:;
    t1 = ((uint64_t)2ULL);
    goto me2;
mn12:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 67, 41);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_closing_na3a1ur(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_closing_na3a1ur F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t3fcbuehh3bs2 t1 = GM_LD(gm_t3fcbuehh3bs2, ((uint8_t *)a0));
    gm_taari1s0m51a7 t2;
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t1) + 0));
    if (t5 != ((uint8_t)8ULL)) goto mn4;
    gm_t9qkh0rsooc8d t6 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t1) + 8));
    gm_l1 = t6;
    gm_t9qkh0rsooc8d t7 = gm_l1;
    gm_t9qkh0rsooc8d t8;
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)62ULL));
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)64ULL));
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)66ULL));
    gm_tk22flr9mh3ql t11;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 0), t8);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 1), t9);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 2), t10);
    uint8_t t12 = 0;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t11) + 0, ((uint8_t *)&t7))) t12 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t11) + 1, ((uint8_t *)&t7))) t12 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t11) + 2, ((uint8_t *)&t7))) t12 = 1;
    uint8_t t13 = (uint8_t)(t12);
    if (!t13) goto mn4;
    gm_t9qkh0rsooc8d t14 = gm_l1;
    gm_taari1s0m51a7 t15;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15), t14);
    ((uint8_t *)&t15)[1] = 1;
    t2 = t15;
    goto me3;
mn4:;
    gm_taari1s0m51a7 t17;
    memset(&t17, 0, sizeof t17);
    t2 = t17;
    goto me3;
mn16:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 74, 42);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_closing_na3a1ur_c(gm_t3fcbuehh3bs2 a0) { return gm_f_closing_na3a1ur(&a0); }
extern __attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_current_char_ctktsl2(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_declaration_starts_at_mp730g1(void * a0, uint64_t a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_declaration_starts_at_mp730g1 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    gm_t3fcbuehh3bs2 gm_l2; /* kind */
    uint64_t gm_l3; /* i */
    gm_b16_8 gm_l4; /* word */
    gm_b16_8 gm_l5; /* after */
    gm_t9qkh0rsooc8d gm_l6; /* k */
    GM_STACK_CHECK(&F, 641u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        uint8_t t4 = (!(t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = gm_l1;
        uint64_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint64_t)1ULL), &r6);
        if (o7) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 592, 31);
        uint64_t t8 = r6;
        uint64_t t9 = (uintptr_t)t8;
        void * t10 = gm_l0;
        uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 8) + 8);
        if (t9 >= (uintptr_t)(t11)) gm_trap_index(t9, t11, gm_srcvmmksi0829pa, 592, 24);
        uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t10) + 8)) + t9 * 1));
        uint8_t t13 = (!(t12 == ((uint8_t)10ULL)));
        if (!t13) goto ie2;
        {
            uint8_t t15 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t16 = gm_l1;
        gm_l3 = t16;
    }
    {
        for (;;) {
            uint64_t t18 = gm_l3;
            void * t19 = gm_l0;
            uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 8) + 8);
            uint8_t t21 = (t18 < t20);
            if (!t21) goto lb17;
            uint64_t t22 = gm_l3;
            uint64_t t23 = (uintptr_t)t22;
            void * t24 = gm_l0;
            uint64_t t25 = GM_LD(uintptr_t, (((uint8_t *)t24) + 8) + 8);
            if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_srcvmmksi0829pa, 594, 51);
            uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t24) + 8)) + t23 * 1));
            uint8_t t27 = gm_f_is_ident_cont_317fpdb(t26);
            if (!t27) goto lb17;
            {
                uint64_t t30 = gm_l3;
                uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                if (o32) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 594, 64);
                uint64_t t33 = r31;
                gm_l3 = t33;
            }
lc17:;
        }
lb17:;
    }
    {
        void * t34 = gm_l0;
        uint64_t t35 = GM_LD(uintptr_t, (((uint8_t *)t34) + 8) + 8);
        void * t36 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t34) + 8)));
        uint64_t t37 = gm_l1;
        uint64_t t38 = (uintptr_t)t37;
        uint64_t t39 = gm_l3;
        uint64_t t40 = (uintptr_t)t39;
        if (t38 > t40 || t40 > (uintptr_t)t35) gm_trap_slice(t38, t40, t35, gm_srcvmmksi0829pa, 595, 13);
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t36 + t38 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t38));
        gm_l4 = t41;
    }
    {
        for (;;) {
            uint64_t t43 = gm_l3;
            void * t44 = gm_l0;
            uint64_t t45 = GM_LD(uintptr_t, (((uint8_t *)t44) + 8) + 8);
            uint8_t t46 = (t43 < t45);
            if (!t46) goto lb42;
            uint64_t t47 = gm_l3;
            uint64_t t48 = (uintptr_t)t47;
            void * t49 = gm_l0;
            uint64_t t50 = GM_LD(uintptr_t, (((uint8_t *)t49) + 8) + 8);
            if (t48 >= (uintptr_t)(t50)) gm_trap_index(t48, t50, gm_srcvmmksi0829pa, 596, 30);
            uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t49) + 8)) + t48 * 1));
            gm_tqs5p4n2tq19v t52;
            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)32ULL));
            GM_ST(uint8_t, ((uint8_t *)&t52 + 1), ((uint8_t)9ULL));
            uint8_t t53 = 0;
            if (gm_eqjmccqdb90f(((uint8_t *)&t52) + 0, ((uint8_t *)&t51))) t53 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t52) + 1, ((uint8_t *)&t51))) t53 = 1;
            uint8_t t54 = (uint8_t)(t53);
            if (!t54) goto lb42;
            {
                uint64_t t57 = gm_l3;
                uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                if (o59) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 596, 57);
                uint64_t t60 = r58;
                gm_l3 = t60;
            }
lc42:;
        }
lb42:;
    }
    {
        void * t61 = gm_l0;
        uint64_t t62 = GM_LD(uintptr_t, (((uint8_t *)t61) + 8) + 8);
        void * t63 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t61) + 8)));
        uint64_t t64 = gm_l3;
        uint64_t t65 = (uintptr_t)t64;
        if (t65 > (uintptr_t)t62 || (uintptr_t)t62 > (uintptr_t)t62) gm_trap_slice(t65, (uintptr_t)t62, t62, gm_srcvmmksi0829pa, 597, 14);
        gm_b16_8 t66;
        GM_ST(void *, (uint8_t *)&t66, (void *)(((uint8_t *)t63 + t65 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)((uintptr_t)t62 - t65));
        gm_l5 = t66;
    }
    {
        uint8_t t67;
        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        switch (t74) { case ((uint8_t)0ULL): goto ma71; case ((uint8_t)1ULL): goto ma72; case ((uint8_t)2ULL): goto ma70; case ((uint8_t)3ULL): goto ma72; case ((uint8_t)4ULL): goto ma72; case ((uint8_t)5ULL): goto ma72; case ((uint8_t)6ULL): goto ma72; case ((uint8_t)7ULL): goto ma69; case ((uint8_t)8ULL): goto ma72; case ((uint8_t)9ULL): goto ma72; case ((uint8_t)10ULL): goto ma72; default: goto ma72; }
ma69:;
        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t76 != ((uint8_t)7ULL)) goto mn75;
        t67 = ((uint8_t)1);
        goto me68;
mn75:;
ma70:;
        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t78 != ((uint8_t)2ULL)) goto mn77;
        gm_t9qkh0rsooc8d t79 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a2) + 8));
        gm_l6 = t79;
        uint8_t t80 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l6) + 0) == ((uint8_t)21ULL)));
        t67 = t80;
        goto me68;
mn77:;
ma71:;
        uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t82 != ((uint8_t)0ULL)) goto mn81;
        uint8_t t84 = 0;
        uint8_t t85 = 0;
        gm_b16_8 t86 = gm_l5;
        gm_b16_8 t87;
        GM_ST(void *, (uint8_t *)&t87, (void *)(gm_ss67f0gmfen1m));
        GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(2));
        uint8_t t88 = gm_f_starts_with_6vvt01e(t86, t87);
        if (!t88) {
            uint8_t t89 = 0;
            gm_b16_8 t90 = gm_l4;
            gm_t6j6smj7qoctu t91;
            gm_b16_8 t92;
            GM_ST(void *, (uint8_t *)&t92, (void *)(gm_s9r64t1166q1f));
            GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t91 + 0, t92);
            gm_b16_8 t93;
            GM_ST(void *, (uint8_t *)&t93, (void *)(gm_srfagufvviad7));
            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(5));
            GM_ST(gm_b16_8, (uint8_t *)&t91 + 16, t93);
            uint8_t t94 = 0;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t91) + 0, ((uint8_t *)&t90))) t94 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t91) + 16, ((uint8_t *)&t90))) t94 = 1;
            uint8_t t95 = (uint8_t)(t94);
            if (t95) {
                gm_b16_8 t96 = gm_l5;
                gm_b16_8 t97;
                GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sb54ui2amic32));
                GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(1));
                uint8_t t98 = gm_f_starts_with_6vvt01e(t96, t97);
                t89 = t98;
            } else t89 = 0;
            t85 = t89;
        } else t85 = 1;
        if (!t85) {
            uint8_t t99 = 0;
            uint8_t t100 = 0;
            uint8_t t101 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_sic5vsema0r36, 7) == 0));
            if (t101) {
                uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t103 = (t102 > ((uint64_t)0ULL));
                t100 = t103;
            } else t100 = 0;
            if (t100) {
                uint8_t t104 = 0;
                uint64_t t105 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                if (t105 >= (uintptr_t)(t106)) gm_trap_index(t105, t106, gm_srcvmmksi0829pa, 603, 62);
                uint8_t t107 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t105 * 1));
                uint8_t t108 = ((t107 == ((uint8_t)40ULL)));
                if (!t108) {
                    uint64_t t109 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    if (t109 >= (uintptr_t)(t110)) gm_trap_index(t109, t110, gm_srcvmmksi0829pa, 603, 103);
                    uint8_t t111 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t109 * 1));
                    uint8_t t112 = gm_f_is_ident_start_rjefpfm(t111);
                    t104 = t112;
                } else t104 = 1;
                t99 = t104;
            } else t99 = 0;
            t84 = t99;
        } else t84 = 1;
        uint8_t t113 = t84;
        t67 = t113;
        goto me68;
mn81:;
ma72:;
        t67 = ((uint8_t)0);
        goto me68;
mn114:;
ma73:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 598, 12);
me68:;
        uint8_t t115 = 0;
        F.ret = t67;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_declaration_starts_at_mp730g1_c(void * a0, uint64_t a1, gm_t3fcbuehh3bs2 a2) { return gm_f_declaration_starts_at_mp730g1(a0, a1, &a2); }
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_ekvv703(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2, uint64_t a3) {
    struct gm_fr_emit_ekvv703 F;
    void * gm_l0; /* lx */
    gm_t3fcbuehh3bs2 gm_l1; /* kind */
    uint64_t gm_l2; /* lo */
    uint64_t gm_l3; /* hi */
    gm_tl874t2atnlh3 gm_l4; /* here */
    gm_t9qkh0rsooc8d gm_l5; /* c */
    uint64_t gm_l6; /* depth */
    uint64_t gm_l7; /* n */
    uint8_t gm_l8; /* is_match_body */
    gm_t9qkh0rsooc8d gm_l9; /* p */
    GM_STACK_CHECK(&F, 743u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l2;
        uint64_t t4 = gm_l3;
        gm_tl874t2atnlh3 t5 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t2, t3, t4));
        gm_l4 = t5;
    }
    {
        gm_taari1s0m51a7 t7 = gm_f_closing_na3a1ur(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l1)));
        if (!((((uint8_t *)&t7))[1] != 0)) goto ie6;
        gm_t9qkh0rsooc8d t8 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&t7));
        gm_l5 = t8;
        {
            void * t11 = gm_l0;
            uint8_t *t12 = (uint8_t *)((((uint8_t *)t11) + 160));
            gm_t9qkh0rsooc8d t13 = gm_l5;
            uint64_t t14 = gm_f_closer_slot_f4sb6v0(t13);
            uint64_t t15 = (uintptr_t)t14;
            if (t15 >= (uintptr_t)(3)) gm_trap_index(t15, 3, gm_srcvmmksi0829pa, 563, 12);
            uint64_t t16 = GM_LD(uint64_t, (t12 + t15 * 8));
            uint8_t t17 = ((t16 == ((uint64_t)0ULL)));
            if (!t17) goto ie10;
            {
                void * t19 = gm_l0;
                uint64_t t20 = gm_l2;
                uint64_t t21 = gm_l3;
                gm_t9qkh0rsooc8d t22 = gm_l5;
                gm_b16_8 t23 = gm_f_punct_text_ov4r40u(t22);
                gm_tnpifv20irnph t24;
                GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t23);
                gm_tabb710d9mq2e t25;
                gm_f_format_daod35i((void *)&t25, (void *)NULL, cx, NULL, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_srcvmmksi0829pa, 564, 30);
                gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
                gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
                gm_b0_1 t28 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t19, t20, t21, t27);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t29;
                uint8_t t30 = 0;
                F.ret = t29;
                goto gm_fret;
            }
            goto ix10;
ie10:;
ix10:;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t32 = gm_l0;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)t32) + 280));
        if (!t33) goto ie31;
        void * t34 = gm_l0;
        uint64_t t35 = GM_LD(uintptr_t, ((((uint8_t *)t34) + 120) + 0) + 8);
        uint8_t t36 = (t35 > ((uint64_t)0ULL));
        if (!t36) goto ie31;
        void * t37 = gm_l0;
        uint64_t t38 = gm_l2;
        uint8_t t39 = gm_f_declaration_starts_at_mp730g1(t37, t38, &(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l1)));
        if (!t39) goto ie31;
        {
            void * t41 = gm_l0;
            uint64_t t42 = gm_l2;
            gm_b0_1 t43 = gm_f_close_all_brackets_bql4j88((void *)NULL, cx, NULL, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix31;
ie31:;
ix31:;
    }
    {
        void * t45 = gm_l0;
        uint8_t t46;
        if ((((((uint8_t *)t45) + 224))[12] != 0)) goto mn48;
        t46 = ((uint8_t)0);
        goto me47;
mn48:;
        t46 = ((uint8_t)1);
        goto me47;
mn49:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 571, 8);
me47:;
        if (!t46) goto ie44;
        {
            void * t51 = gm_l0;
            uint64_t t52 = gm_l3;
            gm_f_flush_newline_9023ml8(t51, &(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l1)), t52);
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        gm_t3fcbuehh3bs2 t53 = gm_l1;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
        switch (t62) { case ((uint8_t)0ULL): goto ma60; case ((uint8_t)1ULL): goto ma60; case ((uint8_t)2ULL): goto ma59; case ((uint8_t)3ULL): goto ma60; case ((uint8_t)4ULL): goto ma60; case ((uint8_t)5ULL): goto ma60; case ((uint8_t)6ULL): goto ma60; case ((uint8_t)7ULL): goto ma60; case ((uint8_t)8ULL): goto ma55; case ((uint8_t)9ULL): goto ma60; case ((uint8_t)10ULL): goto ma60; default: goto ma60; }
ma55:;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
        if (t64 != ((uint8_t)8ULL)) goto mn63;
        uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)&t53) + 8) + 0));
        if (t65 != ((uint8_t)61ULL)) goto mn63;
        void * t66 = gm_l0;
        gm_t9qkh0rsooc8d t67;
        GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)0ULL));
        gm_tl874t2atnlh3 t68 = gm_l4;
        gm_f_open_bracket_3kcj4k8(t66, t67, GM_CONV(gm_cseahvg8v7p0vs, t68));
        goto me54;
mn63:;
ma56:;
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
        if (t70 != ((uint8_t)8ULL)) goto mn69;
        uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)&t53) + 8) + 0));
        if (t71 != ((uint8_t)63ULL)) goto mn69;
        void * t72 = gm_l0;
        gm_t9qkh0rsooc8d t73;
        GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)1ULL));
        gm_tl874t2atnlh3 t74 = gm_l4;
        gm_f_open_bracket_3kcj4k8(t72, t73, GM_CONV(gm_cseahvg8v7p0vs, t74));
        goto me54;
mn69:;
ma57:;
        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
        if (t76 != ((uint8_t)8ULL)) goto mn75;
        uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)&t53) + 8) + 0));
        if (t77 != ((uint8_t)65ULL)) goto mn75;
        {
            void * t79 = gm_l0;
            uint64_t t80 = GM_LD(uintptr_t, ((((uint8_t *)t79) + 120) + 0) + 8);
            gm_l6 = t80;
        }
        {
            void * t81 = gm_l0;
            uint64_t t82 = GM_LD(uintptr_t, ((((uint8_t *)t81) + 184) + 0) + 8);
            gm_l7 = t82;
        }
        {
            uint8_t t83 = 0;
            uint64_t t84 = gm_l7;
            uint8_t t85 = (t84 > ((uint64_t)0ULL));
            if (t85) {
                uint64_t t86 = gm_l7;
                uint64_t r87; bool o88 = __builtin_sub_overflow(t86, ((uint64_t)1ULL), &r87);
                if (o88) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 578, 65);
                uint64_t t89 = r87;
                uint64_t t90 = (uintptr_t)t89;
                void * t91 = gm_l0;
                uint64_t t92 = GM_LD(uintptr_t, ((((uint8_t *)t91) + 184) + 0) + 8);
                if (t90 >= (uintptr_t)(t92)) gm_trap_index(t90, t92, gm_srcvmmksi0829pa, 578, 40);
                uint64_t t93 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t91) + 184) + 0)) + t90 * 8));
                uint64_t t94 = gm_l6;
                uint8_t t95 = ((t93 == t94));
                t83 = t95;
            } else t83 = 0;
            gm_l8 = t83;
        }
        {
            uint8_t t97 = gm_l8;
            if (!t97) goto ie96;
            {
                {
                    void * t100 = gm_l0;
                    void * t101 = (void *)(((uint8_t *)t100) + 184);
                    gm_tf5srh4mp4b3i t102 = gm_f_pop_j4rpvm5(t101);
                }
            }
            goto ix96;
ie96:;
ix96:;
        }
        {
            void * t103 = gm_l0;
            gm_t9qkh0rsooc8d t104;
            uint8_t t106 = gm_l8;
            if (!t106) goto ie105;
            gm_t9qkh0rsooc8d t108;
            GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)3ULL));
            gm_t9qkh0rsooc8d t109 = t108;
            t104 = t109;
            goto ix105;
ie105:;
            gm_t9qkh0rsooc8d t111;
            GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)2ULL));
            gm_t9qkh0rsooc8d t112 = t111;
            t104 = t112;
ix105:;
            gm_tl874t2atnlh3 t113 = gm_l4;
            gm_f_open_bracket_3kcj4k8(t103, t104, GM_CONV(gm_cseahvg8v7p0vs, t113));
        }
        goto me54;
mn75:;
ma58:;
        uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
        if (t115 != ((uint8_t)8ULL)) goto mn114;
        gm_t9qkh0rsooc8d t116 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t53) + 8));
        gm_l9 = t116;
        gm_t9qkh0rsooc8d t117 = gm_l9;
        gm_t9qkh0rsooc8d t118;
        GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)62ULL));
        gm_t9qkh0rsooc8d t119;
        GM_ST(uint8_t, ((uint8_t *)&t119 + 0), ((uint8_t)64ULL));
        gm_t9qkh0rsooc8d t120;
        GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)66ULL));
        gm_tk22flr9mh3ql t121;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t121 + 0), t118);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t121 + 1), t119);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t121 + 2), t120);
        uint8_t t122 = 0;
        if (gm_eqv3tj1pu43g(((uint8_t *)&t121) + 0, ((uint8_t *)&t117))) t122 = 1;
        if (gm_eqv3tj1pu43g(((uint8_t *)&t121) + 1, ((uint8_t *)&t117))) t122 = 1;
        if (gm_eqv3tj1pu43g(((uint8_t *)&t121) + 2, ((uint8_t *)&t117))) t122 = 1;
        uint8_t t123 = (uint8_t)(t122);
        if (!t123) goto mn114;
        void * t124 = gm_l0;
        gm_t9qkh0rsooc8d t125 = gm_l9;
        gm_tl874t2atnlh3 t126 = gm_l4;
        gm_b0_1 t127 = gm_f_close_bracket_tbjprpe((void *)NULL, cx, NULL, t124, t125, t126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me54;
mn114:;
ma59:;
        uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
        if (t129 != ((uint8_t)2ULL)) goto mn128;
        uint8_t t130 = GM_LD(uint8_t, ((((uint8_t *)&t53) + 8) + 0));
        if (t130 != ((uint8_t)17ULL)) goto mn128;
        void * t131 = gm_l0;
        void * t132 = (void *)(((uint8_t *)t131) + 184);
        void * t133 = gm_l0;
        uint64_t t134 = GM_LD(uintptr_t, ((((uint8_t *)t133) + 120) + 0) + 8);
        gm_tftb1odc739t5 t135 = gm_f_push_ucgmlo2(t132, t134);
        if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_srcvmmksi0829pa, 583, 29);
        gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
        goto me54;
mn128:;
ma60:;
        goto me54;
mn137:;
ma61:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 572, 5);
me54:;
    }
    {
        void * t139 = gm_l0;
        void * t140 = (void *)(((uint8_t *)t139) + 40);
        gm_tm4pc0emhrv1g t141;
        memset((uint8_t *)&t141 + 44, 0, 4);
        memcpy((uint8_t *)&t141 + 0, ((uint8_t *)&gm_l1), 32);
        gm_tl874t2atnlh3 t142 = gm_l4;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t141 + 32, t142);
        gm_tftb1odc739t5 t143 = gm_f_push_pnf14h1(t140, &t141);
        if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 1, gm_srcvmmksi0829pa, 586, 5);
        gm_b0_1 t144 = GM_LD(gm_b0_1, (((uint8_t *)&t143) + 1));
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_emit_semicolon_7da5b93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_error_p5r0d56(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_t6ofeqo2h242p gm_f_flavors_qsu4oq0(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_flavors_qsu4oq0 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    gm_t6ofeqo2h242p t2;
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)&t1));
    if (!(t5 == ((uint8_t)43ULL))) goto mn4;
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)23ULL));
    gm_t9qkh0rsooc8d t8;
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)10ULL));
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)14ULL));
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)18ULL));
    gm_t9qkh0rsooc8d t11;
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)33ULL));
    gm_t9qkh0rsooc8d t12;
    GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)37ULL));
    gm_t6ofeqo2h242p t13;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 0), t6);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t7);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 2), t8);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 3), t9);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 4), t10);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 5), t11);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 6), t12);
    t2 = t13;
    goto me3;
mn4:;
    uint8_t t15 = GM_LD(uint8_t, ((uint8_t *)&t1));
    if (!(t15 == ((uint8_t)45ULL))) goto mn14;
    gm_t9qkh0rsooc8d t16;
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t17;
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)24ULL));
    gm_t9qkh0rsooc8d t18;
    GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)11ULL));
    gm_t9qkh0rsooc8d t19;
    GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)15ULL));
    gm_t9qkh0rsooc8d t20;
    GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)19ULL));
    gm_t9qkh0rsooc8d t21;
    GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)34ULL));
    gm_t9qkh0rsooc8d t22;
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)38ULL));
    gm_t6ofeqo2h242p t23;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 0), t16);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 1), t17);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 2), t18);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 3), t19);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 4), t20);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 5), t21);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 6), t22);
    t2 = t23;
    goto me3;
mn14:;
    gm_t9qkh0rsooc8d t25;
    GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)2ULL));
    gm_t9qkh0rsooc8d t26;
    GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)25ULL));
    gm_t9qkh0rsooc8d t27;
    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)12ULL));
    gm_t9qkh0rsooc8d t28;
    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)16ULL));
    gm_t9qkh0rsooc8d t29;
    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)20ULL));
    gm_t9qkh0rsooc8d t30;
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)35ULL));
    gm_t9qkh0rsooc8d t31;
    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)39ULL));
    gm_t6ofeqo2h242p t32;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 0), t25);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 1), t26);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 2), t27);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 3), t28);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 4), t29);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 5), t30);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 6), t31);
    t2 = t32;
    goto me3;
mn24:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 542, 36);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_flush_newline_9023ml8(void * a0, const void * a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_flush_newline_9023ml8 F;
    void * gm_l0; /* lx */
    gm_t3fcbuehh3bs2 gm_l1; /* next */
    uint64_t gm_l2; /* next_hi */
    gm_tl874t2atnlh3 gm_l3; /* at */
    uint64_t gm_l4; /* depth */
    gm_taari1s0m51a7 gm_l5; /* innermost */
    uint64_t gm_l6; /* n */
    uint8_t gm_l7; /* dot_name */
    GM_STACK_CHECK(&F, 483u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_toceffb697p0u t3 = GM_LD(gm_toceffb697p0u, (((uint8_t *)t2) + 224));
        gm_tl874t2atnlh3 t4;
        if (((((uint8_t *)&t3))[12] != 0)) {
            gm_tl874t2atnlh3 t5 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_b0_1 t6;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l3 = t4;
    }
    {
        void * t8 = gm_l0;
        gm_toceffb697p0u t9;
        memset(&t9, 0, sizeof t9);
        GM_ST(gm_toceffb697p0u, (((uint8_t *)t8) + 224), t9);
    }
    {
        void * t10 = gm_l0;
        uint64_t t11 = GM_LD(uintptr_t, ((((uint8_t *)t10) + 120) + 0) + 8);
        gm_l4 = t11;
    }
    {
        gm_taari1s0m51a7 t12;
        uint64_t t14 = gm_l4;
        uint8_t t15 = (t14 > ((uint64_t)0ULL));
        if (!t15) goto ie13;
        uint64_t t17 = gm_l4;
        uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
        if (o19) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 673, 64);
        uint64_t t20 = r18;
        uint64_t t21 = (uintptr_t)t20;
        void * t22 = gm_l0;
        uint64_t t23 = GM_LD(uintptr_t, ((((uint8_t *)t22) + 120) + 0) + 8);
        if (t21 >= (uintptr_t)(t23)) gm_trap_index(t21, t23, gm_srcvmmksi0829pa, 673, 46);
        gm_t9qkh0rsooc8d t24 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t22) + 120) + 0)) + t21 * 16) + 0));
        gm_t9qkh0rsooc8d t25 = t24;
        gm_taari1s0m51a7 t26;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t26), t25);
        ((uint8_t *)&t26)[1] = 1;
        t12 = t26;
        goto ix13;
ie13:;
        gm_taari1s0m51a7 t28;
        memset(&t28, 0, sizeof t28);
        gm_taari1s0m51a7 t29 = t28;
        t12 = t29;
ix13:;
        gm_l5 = t12;
    }
    {
        uint8_t t31 = 0;
        gm_taari1s0m51a7 t32 = gm_l5;
        gm_t9qkh0rsooc8d t33;
        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)0ULL));
        gm_taari1s0m51a7 t34;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34), t33);
        ((uint8_t *)&t34)[1] = 1;
        uint8_t t35 = (gm_eqq6nrh55uon(((uint8_t *)&t32), ((uint8_t *)&t34)));
        if (!t35) {
            gm_taari1s0m51a7 t36 = gm_l5;
            gm_t9qkh0rsooc8d t37;
            GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)1ULL));
            gm_taari1s0m51a7 t38;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38), t37);
            ((uint8_t *)&t38)[1] = 1;
            uint8_t t39 = (gm_eqq6nrh55uon(((uint8_t *)&t36), ((uint8_t *)&t38)));
            t31 = t39;
        } else t31 = 1;
        if (!t31) goto ie30;
        {
            gm_b0_1 t41;
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t43 = gm_l0;
        uint64_t t44 = GM_LD(uintptr_t, ((((uint8_t *)t43) + 40) + 0) + 8);
        gm_l6 = t44;
    }
    {
        uint64_t t46 = gm_l6;
        uint8_t t47 = ((t46 == ((uint64_t)0ULL)));
        if (!t47) goto ie45;
        {
            gm_b0_1 t49;
            uint8_t t50 = 0;
            F.ret = t49;
            goto gm_fret;
        }
        goto ix45;
ie45:;
ix45:;
    }
    {
        uint64_t t52 = gm_l6;
        uint64_t r53; bool o54 = __builtin_sub_overflow(t52, ((uint64_t)1ULL), &r53);
        if (o54) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 677, 49);
        uint64_t t55 = r53;
        uint64_t t56 = (uintptr_t)t55;
        void * t57 = gm_l0;
        uint64_t t58 = GM_LD(uintptr_t, ((((uint8_t *)t57) + 40) + 0) + 8);
        if (t56 >= (uintptr_t)(t58)) gm_trap_index(t56, t58, gm_srcvmmksi0829pa, 677, 33);
        uint8_t t59 = gm_f_suppresses_newline_after_o08ord2(&(*(gm_t3fcbuehh3bs2 *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t57) + 40) + 0)) + t56 * 48) + 0)));
        if (!t59) goto ie51;
        {
            void * t61 = gm_l0;
            void * t62 = (void *)(((uint8_t *)t61) + 240);
            uint32_t t63 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            gm_tftb1odc739t5 t64 = gm_f_push_im9d4ro(t62, t63);
            if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_srcvmmksi0829pa, 678, 9);
            gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
        }
        {
            gm_b0_1 t66;
            uint8_t t67 = 0;
            F.ret = t66;
            goto gm_fret;
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        gm_taari1s0m51a7 t69 = gm_l5;
        gm_t9qkh0rsooc8d t70;
        GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)3ULL));
        gm_taari1s0m51a7 t71;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71), t70);
        ((uint8_t *)&t71)[1] = 1;
        uint8_t t72 = (!gm_eqq6nrh55uon(((uint8_t *)&t69), ((uint8_t *)&t71)));
        if (!t72) goto ie68;
        {
            uint8_t t74 = 0;
            uint8_t t75 = 0;
            uint8_t t76 = 0;
            gm_t9qkh0rsooc8d t77;
            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)58ULL));
            uint8_t t78 = gm_f_is_punct_fdeminj(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l1)), t77);
            if (t78) {
                uint64_t t79 = gm_l2;
                void * t80 = gm_l0;
                uint64_t t81 = GM_LD(uintptr_t, (((uint8_t *)t80) + 8) + 8);
                uint8_t t82 = (t79 < t81);
                t76 = t82;
            } else t76 = 0;
            if (t76) {
                uint64_t t83 = gm_l2;
                uint64_t t84 = (uintptr_t)t83;
                void * t85 = gm_l0;
                uint64_t t86 = GM_LD(uintptr_t, (((uint8_t *)t85) + 8) + 8);
                if (t84 >= (uintptr_t)(t86)) gm_trap_index(t84, t86, gm_srcvmmksi0829pa, 683, 92);
                uint8_t t87 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t85) + 8)) + t84 * 1));
                uint8_t t88 = gm_f_is_ident_start_rjefpfm(t87);
                t75 = t88;
            } else t75 = 0;
            if (t75) {
                void * t89 = gm_l0;
                uint64_t t90 = GM_LD(uintptr_t, (((uint8_t *)t89) + 8) + 8);
                void * t91 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t89) + 8)));
                uint64_t t92 = gm_l2;
                uint64_t t93 = (uintptr_t)t92;
                if (t93 > (uintptr_t)t90 || (uintptr_t)t90 > (uintptr_t)t90) gm_trap_slice(t93, (uintptr_t)t90, t90, gm_srcvmmksi0829pa, 684, 32);
                gm_b16_8 t94;
                GM_ST(void *, (uint8_t *)&t94, (void *)(((uint8_t *)t91 + t93 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)((uintptr_t)t90 - t93));
                uint8_t t95 = gm_f_line_binds_jm0pnul(t94);
                uint8_t t96 = (uint8_t)!t95;
                t74 = t96;
            } else t74 = 0;
            gm_l7 = t74;
        }
        {
            uint8_t t98 = 0;
            uint8_t t99 = gm_l7;
            if (!t99) {
                uint8_t t100 = gm_f_starts_continuation_n3333i7(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l1)));
                t98 = t100;
            } else t98 = 1;
            if (!t98) goto ie97;
            {
                gm_b0_1 t102;
                uint8_t t103 = 0;
                F.ret = t102;
                goto gm_fret;
            }
            goto ix97;
ie97:;
ix97:;
        }
        goto ix68;
ie68:;
ix68:;
    }
    {
        void * t104 = gm_l0;
        gm_tl874t2atnlh3 t105 = gm_l3;
        gm_f_push_newline_ke0tojq(t104, GM_CONV(gm_cseahvg8v7p0vs, t105));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_flush_newline_9023ml8_c(void * a0, gm_t3fcbuehh3bs2 a1, uint64_t a2) { gm_f_flush_newline_9023ml8(a0, &a1, a2); }
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_inline_comment_end_8i5h1eq(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_inline_comment_end_8i5h1eq F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* i */
    int32_t gm_l3; /* depth */
    uint8_t gm_l4; /* b */
    gm_tqs5p4n2tq19v gm_l5; /* next */
    GM_STACK_CHECK_SHALLOW(&F, 354u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t r3; bool o4 = __builtin_add_overflow(t2, ((uint64_t)2ULL), &r3);
        if (o4) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 711, 10);
        uint64_t t5 = r3;
        gm_l2 = t5;
    }
    {
        gm_l3 = ((int32_t)1LL);
    }
    {
        for (;;) {
            int32_t t7 = gm_l3;
            uint8_t t8 = (t7 > ((int32_t)0LL));
            if (!t8) goto lb6;
            {
                gm_b16_8 t11 = gm_l0;
                void * t12 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t11)));
                uint64_t t13 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t11) + 8);
                uint64_t t14 = gm_l2;
                uint64_t t15 = (uintptr_t)t14;
                gm_tqs5p4n2tq19v t16;
                if (t15 < t13) {
                uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)t12 + t15 * 1));
                gm_tqs5p4n2tq19v t18;
                GM_ST(uint8_t, ((uint8_t *)&t18), t17);
                ((uint8_t *)&t18)[1] = 1;
                    t16 = t18;
                } else {
                gm_tqs5p4n2tq19v t19;
                memset(&t19, 0, sizeof t19);
                    t16 = t19;
                }
                uint8_t t20;
                if (((((uint8_t *)&t16))[1] != 0)) {
                    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
                    t20 = t21;
                } else {
                    gm_tf5srh4mp4b3i t22;
                    memset(&t22, 0, sizeof t22);
                    uint8_t t23 = 0;
                    F.ret = t22;
                    goto gm_fret;
                }
                gm_l4 = t20;
            }
            {
                gm_b16_8 t24 = gm_l0;
                void * t25 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t24)));
                uint64_t t26 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t24) + 8);
                uint64_t t27 = gm_l2;
                uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 715, 23);
                uint64_t t30 = r28;
                uint64_t t31 = (uintptr_t)t30;
                gm_tqs5p4n2tq19v t32;
                if (t31 < t26) {
                uint8_t t33 = GM_LD(uint8_t, ((uint8_t *)t25 + t31 * 1));
                gm_tqs5p4n2tq19v t34;
                GM_ST(uint8_t, ((uint8_t *)&t34), t33);
                ((uint8_t *)&t34)[1] = 1;
                    t32 = t34;
                } else {
                gm_tqs5p4n2tq19v t35;
                memset(&t35, 0, sizeof t35);
                    t32 = t35;
                }
                gm_l5 = t32;
            }
            {
                uint8_t t37 = gm_l4;
                uint8_t t38 = ((t37 == ((uint8_t)47ULL)));
                if (!t38) goto ie36;
                gm_tqs5p4n2tq19v t39 = gm_l5;
                gm_tqs5p4n2tq19v t40;
                GM_ST(uint8_t, ((uint8_t *)&t40), ((uint8_t)42ULL));
                ((uint8_t *)&t40)[1] = 1;
                uint8_t t41 = (gm_eqb44cb3cf1n(((uint8_t *)&t39), ((uint8_t *)&t40)));
                if (!t41) goto ie36;
                {
                    int32_t t43 = gm_l3;
                    int32_t r44; bool o45 = __builtin_add_overflow(t43, ((int32_t)1LL), &r44);
                    if (o45) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 717, 13);
                    int32_t t46 = r44;
                    gm_l3 = t46;
                }
                {
                    uint64_t t47 = gm_l2;
                    uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)2ULL), &r48);
                    if (o49) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 718, 13);
                    uint64_t t50 = r48;
                    gm_l2 = t50;
                }
                goto ix36;
ie36:;
                uint8_t t52 = gm_l4;
                uint8_t t53 = ((t52 == ((uint8_t)42ULL)));
                if (!t53) goto ie51;
                gm_tqs5p4n2tq19v t54 = gm_l5;
                gm_tqs5p4n2tq19v t55;
                GM_ST(uint8_t, ((uint8_t *)&t55), ((uint8_t)47ULL));
                ((uint8_t *)&t55)[1] = 1;
                uint8_t t56 = (gm_eqb44cb3cf1n(((uint8_t *)&t54), ((uint8_t *)&t55)));
                if (!t56) goto ie51;
                {
                    int32_t t58 = gm_l3;
                    int32_t r59; bool o60 = __builtin_sub_overflow(t58, ((int32_t)1LL), &r59);
                    if (o60) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 720, 13);
                    int32_t t61 = r59;
                    gm_l3 = t61;
                }
                {
                    uint64_t t62 = gm_l2;
                    uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)2ULL), &r63);
                    if (o64) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 721, 13);
                    uint64_t t65 = r63;
                    gm_l2 = t65;
                }
                goto ix51;
ie51:;
                uint8_t t67 = gm_l4;
                gm_tqs5p4n2tq19v t68;
                GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)10ULL));
                GM_ST(uint8_t, ((uint8_t *)&t68 + 1), ((uint8_t)13ULL));
                uint8_t t69 = 0;
                if (gm_eqjmccqdb90f(((uint8_t *)&t68) + 0, ((uint8_t *)&t67))) t69 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t68) + 1, ((uint8_t *)&t67))) t69 = 1;
                uint8_t t70 = (uint8_t)(t69);
                if (!t70) goto ie66;
                {
                    gm_tf5srh4mp4b3i t72;
                    memset(&t72, 0, sizeof t72);
                    uint8_t t73 = 0;
                    F.ret = t72;
                    goto gm_fret;
                }
                goto ix66;
ie66:;
                {
                    uint64_t t75 = gm_l2;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    if (o77) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 725, 13);
                    uint64_t t78 = r76;
                    gm_l2 = t78;
                }
ix66:;
ix51:;
ix36:;
            }
lc6:;
        }
lb6:;
    }
    {
        uint64_t t79 = gm_l2;
        gm_tf5srh4mp4b3i t80;
        GM_ST(uint8_t, (uint8_t *)&t80 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t80 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t80 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t80), t79);
        ((uint8_t *)&t80)[8] = 1;
        uint8_t t81 = 0;
        F.ret = t80;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_inline_raw_string_end_6lenf33(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_inline_raw_string_end_6lenf33 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* delim */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* b */
    uint64_t gm_l5; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 349u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        uint64_t t3 = gm_l1;
        uint64_t t4 = gm_f_backtick_run_l95fb09(t2, t3);
        gm_l2 = t4;
    }
    {
        uint64_t t5 = gm_l1;
        uint64_t t6 = gm_l2;
        uint64_t r7; bool o8 = __builtin_add_overflow(t5, t6, &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 742, 10);
        uint64_t t9 = r7;
        gm_l3 = t9;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb10;
            {
                gm_b16_8 t13 = gm_l0;
                void * t14 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t13)));
                uint64_t t15 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t13) + 8);
                uint64_t t16 = gm_l3;
                uint64_t t17 = (uintptr_t)t16;
                gm_tqs5p4n2tq19v t18;
                if (t17 < t15) {
                uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)t14 + t17 * 1));
                gm_tqs5p4n2tq19v t20;
                GM_ST(uint8_t, ((uint8_t *)&t20), t19);
                ((uint8_t *)&t20)[1] = 1;
                    t18 = t20;
                } else {
                gm_tqs5p4n2tq19v t21;
                memset(&t21, 0, sizeof t21);
                    t18 = t21;
                }
                uint8_t t22;
                if (((((uint8_t *)&t18))[1] != 0)) {
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t18) + 0));
                    t22 = t23;
                } else {
                    gm_tf5srh4mp4b3i t24;
                    memset(&t24, 0, sizeof t24);
                    uint8_t t25 = 0;
                    F.ret = t24;
                    goto gm_fret;
                }
                gm_l4 = t22;
            }
            {
                uint8_t t27 = gm_l4;
                uint8_t t28 = ((t27 == ((uint8_t)96ULL)));
                if (!t28) goto ie26;
                {
                    gm_b16_8 t30 = gm_l0;
                    uint64_t t31 = gm_l3;
                    uint64_t t32 = gm_f_backtick_run_l95fb09(t30, t31);
                    gm_l5 = t32;
                }
                {
                    uint64_t t33 = gm_l3;
                    uint64_t t34 = gm_l5;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t33, t34, &r35);
                    if (o36) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 747, 13);
                    uint64_t t37 = r35;
                    gm_l3 = t37;
                }
                {
                    uint64_t t39 = gm_l5;
                    uint64_t t40 = gm_l2;
                    uint8_t t41 = ((t39 == t40));
                    if (!t41) goto ie38;
                    {
                        uint64_t t43 = gm_l3;
                        gm_tf5srh4mp4b3i t44;
                        GM_ST(uint8_t, (uint8_t *)&t44 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t44 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t44 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t44), t43);
                        ((uint8_t *)&t44)[8] = 1;
                        uint8_t t45 = 0;
                        F.ret = t44;
                        goto gm_fret;
                    }
                    goto ix38;
ie38:;
ix38:;
                }
                goto ix26;
ie26:;
                uint8_t t47 = gm_l4;
                gm_tqs5p4n2tq19v t48;
                GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)10ULL));
                GM_ST(uint8_t, ((uint8_t *)&t48 + 1), ((uint8_t)13ULL));
                uint8_t t49 = 0;
                if (gm_eqjmccqdb90f(((uint8_t *)&t48) + 0, ((uint8_t *)&t47))) t49 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t48) + 1, ((uint8_t *)&t47))) t49 = 1;
                uint8_t t50 = (uint8_t)(t49);
                if (!t50) goto ie46;
                {
                    gm_tf5srh4mp4b3i t52;
                    memset(&t52, 0, sizeof t52);
                    uint8_t t53 = 0;
                    F.ret = t52;
                    goto gm_fret;
                }
                goto ix46;
ie46:;
                {
                    uint64_t t55 = gm_l3;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    if (o57) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 752, 13);
                    uint64_t t58 = r56;
                    gm_l3 = t58;
                }
ix46:;
ix26:;
            }
lc10:;
        }
lb10:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_ascii_space_t2ltvst(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unicode_space_b2m9rku(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unicode_space_b2m9rku F;
    uint32_t gm_l0; /* c */
    uint32_t gm_l1; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2 = 0;
        uint32_t t3 = gm_l0;
        gm_tu4r32bi6mhcq t4;
        GM_ST(uint32_t, (uint8_t *)&t4 + 0, ((uint32_t)133ULL));
        GM_ST(uint32_t, (uint8_t *)&t4 + 4, ((uint32_t)160ULL));
        GM_ST(uint32_t, (uint8_t *)&t4 + 8, ((uint32_t)5760ULL));
        GM_ST(uint32_t, (uint8_t *)&t4 + 12, ((uint32_t)8232ULL));
        GM_ST(uint32_t, (uint8_t *)&t4 + 16, ((uint32_t)8233ULL));
        GM_ST(uint32_t, (uint8_t *)&t4 + 20, ((uint32_t)8239ULL));
        GM_ST(uint32_t, (uint8_t *)&t4 + 24, ((uint32_t)8287ULL));
        GM_ST(uint32_t, (uint8_t *)&t4 + 28, ((uint32_t)12288ULL));
        uint8_t t5 = 0;
        if (gm_eq467d5cm1d5(((uint8_t *)&t4) + 0, ((uint8_t *)&t3))) t5 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t4) + 4, ((uint8_t *)&t3))) t5 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t4) + 8, ((uint8_t *)&t3))) t5 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t4) + 12, ((uint8_t *)&t3))) t5 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t4) + 16, ((uint8_t *)&t3))) t5 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t4) + 20, ((uint8_t *)&t3))) t5 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t4) + 24, ((uint8_t *)&t3))) t5 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t4) + 28, ((uint8_t *)&t3))) t5 = 1;
        uint8_t t6 = (uint8_t)(t5);
        if (!t6) {
            {
                uint32_t t8 = gm_l0;
                gm_l1 = t8;
            }
            uint8_t t9 = 0;
            uint32_t t10 = gm_l1;
            uint8_t t11 = (((uint32_t)8192ULL) <= t10);
            if (t11) {
                uint32_t t12 = gm_l1;
                uint8_t t13 = (t12 <= ((uint32_t)8202ULL));
                t9 = t13;
            } else t9 = 0;
            uint8_t t14 = t9;
            t2 = t14;
        } else t2 = 1;
        uint8_t t15 = 0;
        F.ret = t2;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_lex_recovering_obo2892(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_lex_recovering_obo2892 F;
    uint32_t gm_l0; /* file */
    gm_b16_8 gm_l1; /* src */
    void * gm_l2; /* diags */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 399u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_diagnostics_n3kkhf4((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        void * t4 = (void *)((uint8_t *)&F.l3);
        gm_f_new_lexer_dm12g12((void *)&F.l4, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l4);
        gm_f_run_v26is74((void *)&F.l5, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = (void *)((uint8_t *)&F.l3);
        uint8_t t8 = gm_f_has_errors_if7ai0m(t7);
        uint8_t t9 = (uint8_t)!t8;
        if (!t9) goto ie6;
        {
            {
                gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 0) + 0));
                gm_l6 = t12;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t14 = gm_l7;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t16 = (t14 < t15);
                    if (!t16) goto lb13;
                    {
                        uint64_t t19 = gm_l7;
                        uint64_t t20 = (uintptr_t)t19;
                        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        gm_t4gut2c9fj1kt t24 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t20 * 184));
                        F.l8 = t24;
                        {
                            void * t26 = gm_l2;
                            gm_f_push_o4ckdr1(t26, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l8)));
                        }
                        goto me22;
mn23:;
                        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 30, 9);
me22:;
                    }
lc13:;
                    {
                        uint64_t t28 = gm_l7;
                        uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                        uint64_t t31 = r29;
                        gm_l7 = t31;
                    }
                }
lb13:;
            }
        }
        {
            memmove(gm_out, ((uint8_t *)&F.l5), 120);
            goto gm_fdone;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        uint32_t t32 = gm_l0;
        gm_b16_8 t33 = gm_l1;
        void * t34 = gm_l2;
        gm_f_new_lexer_dm12g12((void *)&F.l9, (void *)NULL, cx, NULL, t32, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        GM_ST(uint8_t, (((uint8_t *)&F.l9) + 280), ((uint8_t)1));
    }
    {
        void * t35 = (void *)((uint8_t *)&F.l9);
        gm_f_run_v26is74((void *)&(*(gm_tr8dvlu9ik4gj *)gm_out), (void *)NULL, cx, NULL, t35);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_token_h8g44s4(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_lex_token_h8g44s4 F;
    void * gm_l0; /* lx */
    uint8_t gm_l1; /* b */
    uint64_t gm_l2; /* lo */
    uint8_t gm_l3; /* next */
    gm_b16_8 gm_l4; /* name */
    gm_t3fcbuehh3bs2 gm_l5; /* kind */
    gm_t9qkh0rsooc8d gm_l6; /* p */
    GM_STACK_CHECK(&F, 666u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l2 = t3;
    }
    {
        uint8_t t4 = gm_l1;
        uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)&t4));
        if ((t9 < ((uint8_t)97ULL)) || (t9 > ((uint8_t)122ULL))) goto pa8;
        goto po7;
pa8:;
        uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)&t4));
        if ((t11 < ((uint8_t)65ULL)) || (t11 > ((uint8_t)90ULL))) goto pa10;
        goto po7;
pa10:;
        uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)&t4));
        if (!(t13 == ((uint8_t)95ULL))) goto pa12;
        goto po7;
pa12:;
        goto mn6;
po7:;
        void * t14 = gm_l0;
        gm_b0_1 t15 = gm_f_lex_word_9li16te((void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn6:;
        uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t4));
        if ((t17 < ((uint8_t)48ULL)) || (t17 > ((uint8_t)57ULL))) goto mn16;
        void * t18 = gm_l0;
        gm_b0_1 t19 = gm_f_lex_number_9j9sdfi((void *)NULL, cx, NULL, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn16:;
        uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)&t4));
        if (!(t21 == ((uint8_t)34ULL))) goto mn20;
        void * t22 = gm_l0;
        uint64_t t23 = gm_l2;
        gm_t9qkh0rsooc8d t24;
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
        gm_b0_1 t25 = gm_f_lex_string_bcdbdjm((void *)NULL, cx, NULL, t22, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn20:;
        uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)&t4));
        if (!(t27 == ((uint8_t)96ULL))) goto mn26;
        void * t28 = gm_l0;
        gm_b0_1 t29 = gm_f_lex_raw_string_uf6bd4g((void *)NULL, cx, NULL, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn26:;
        uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)&t4));
        if (!(t31 == ((uint8_t)39ULL))) goto mn30;
        void * t32 = gm_l0;
        gm_b0_1 t33 = gm_f_lex_char_4qk4vjp((void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn30:;
        uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)&t4));
        if (!(t35 == ((uint8_t)59ULL))) goto mn34;
        {
            void * t37 = gm_l0;
            void * t38 = gm_l0;
            uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 24));
            uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
            if (o41) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 299, 13);
            uint64_t t42 = r40;
            GM_ST(uint64_t, (((uint8_t *)t37) + 24), t42);
        }
        {
            void * t43 = gm_l0;
            uint64_t t44 = gm_l2;
            gm_b0_1 t45 = gm_f_emit_semicolon_7da5b93((void *)NULL, cx, NULL, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me5;
mn34:;
        uint8_t t49 = GM_LD(uint8_t, ((uint8_t *)&t4));
        if (!(t49 == ((uint8_t)64ULL))) goto pa48;
        goto po47;
pa48:;
        uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)&t4));
        if (!(t51 == ((uint8_t)35ULL))) goto pa50;
        goto po47;
pa50:;
        goto mn46;
po47:;
        {
            void * t53 = gm_l0;
            void * t54 = gm_l0;
            uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t54) + 24));
            uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
            if (o57) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 303, 13);
            uint64_t t58 = r56;
            GM_ST(uint64_t, (((uint8_t *)t53) + 24), t58);
        }
        {
            void * t59 = gm_l0;
            gm_tqs5p4n2tq19v t60 = gm_f_peek_72bostn(t59);
            uint8_t t61;
            if (((((uint8_t *)&t60))[1] != 0)) {
                uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
                t61 = t62;
            } else {
                t61 = ((uint8_t)0ULL);
            }
            gm_l3 = t61;
        }
        {
            uint8_t t64 = gm_l3;
            uint8_t t65 = gm_f_is_ident_start_rjefpfm(t64);
            if (!t65) goto ie63;
            {
                void * t67 = gm_l0;
                gm_b16_8 t68 = gm_f_scan_ident_h1b2n4u(t67);
                gm_l4 = t68;
            }
            {
                gm_t3fcbuehh3bs2 t69;
                uint8_t t71 = gm_l1;
                uint8_t t72 = ((t71 == ((uint8_t)64ULL)));
                if (!t72) goto ie70;
                gm_b16_8 t74 = gm_l4;
                gm_t3fcbuehh3bs2 t75;
                GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t75 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)6ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t75 + 8), t74);
                gm_t3fcbuehh3bs2 t76 = t75;
                t69 = t76;
                goto ix70;
ie70:;
                gm_b16_8 t78 = gm_l4;
                gm_t3fcbuehh3bs2 t79;
                GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t79 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)7ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t79 + 8), t78);
                gm_t3fcbuehh3bs2 t80 = t79;
                t69 = t80;
ix70:;
                gm_l5 = t69;
            }
            {
                void * t81 = gm_l0;
                uint64_t t82 = gm_l2;
                void * t83 = gm_l0;
                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)t83) + 24));
                gm_b0_1 t85 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t81, &(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l5)), t82, t84);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix63;
ie63:;
            {
                void * t87 = gm_l0;
                gm_t9qkh0rsooc8d t88;
                uint8_t t90 = gm_l1;
                uint8_t t91 = ((t90 == ((uint8_t)64ULL)));
                if (!t91) goto ie89;
                gm_t9qkh0rsooc8d t93;
                GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)48ULL));
                gm_t9qkh0rsooc8d t94 = t93;
                t88 = t94;
                goto ix89;
ie89:;
                gm_t9qkh0rsooc8d t96;
                GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)49ULL));
                gm_t9qkh0rsooc8d t97 = t96;
                t88 = t97;
ix89:;
                gm_t3fcbuehh3bs2 t98;
                GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t98 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t98 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t98 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t98 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t98 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t98 + 8), t88);
                uint64_t t99 = gm_l2;
                void * t100 = gm_l0;
                uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)t100) + 24));
                gm_b0_1 t102 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t87, &t98, t99, t101);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix63:;
        }
        goto me5;
mn46:;
        uint8_t t106 = GM_LD(uint8_t, ((uint8_t *)&t4));
        if (!(t106 == ((uint8_t)61ULL))) goto pa105;
        goto po104;
pa105:;
        uint8_t t108 = GM_LD(uint8_t, ((uint8_t *)&t4));
        if (!(t108 == ((uint8_t)33ULL))) goto pa107;
        goto po104;
pa107:;
        goto mn103;
po104:;
        void * t109 = gm_l0;
        gm_tqs5p4n2tq19v t110 = gm_f_peek_at_74p8lkq(t109, ((uint64_t)1ULL));
        gm_tqs5p4n2tq19v t111;
        GM_ST(uint8_t, ((uint8_t *)&t111), ((uint8_t)61ULL));
        ((uint8_t *)&t111)[1] = 1;
        uint8_t t112 = (gm_eqb44cb3cf1n(((uint8_t *)&t110), ((uint8_t *)&t111)));
        if (!t112) goto mn103;
        void * t113 = gm_l0;
        uint8_t t114 = gm_l1;
        gm_b0_1 t115 = gm_f_c_equality_u566nc4((void *)NULL, cx, NULL, t113, t114);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn103:;
        {
            void * t119 = gm_l0;
            gm_taari1s0m51a7 t120 = gm_f_scan_punct_ecrh51c(t119);
            if (!((((uint8_t *)&t120))[1] != 0)) goto ie118;
            gm_t9qkh0rsooc8d t121 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&t120));
            gm_l6 = t121;
            {
                void * t123 = gm_l0;
                gm_t9qkh0rsooc8d t124 = gm_l6;
                gm_t3fcbuehh3bs2 t125;
                GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t125 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t125 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t125 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t125 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t125 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t125 + 8), t124);
                uint64_t t126 = gm_l2;
                void * t127 = gm_l0;
                uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)t127) + 24));
                gm_b0_1 t129 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t123, &t125, t126, t128);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix118;
ie118:;
            {
                void * t131 = gm_l0;
                gm_b0_1 t132 = gm_f_unexpected_char_r3t8d4c((void *)NULL, cx, NULL, t131);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix118:;
        }
        goto me5;
mn116:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 292, 5);
me5:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_word_9li16te(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lex_word_9li16te F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    gm_b16_8 gm_l2; /* text */
    gm_t9qkh0rsooc8d gm_l3; /* kw */
    gm_taari1s0m51a7 gm_l4; /* two_word */
    uint64_t gm_l5; /* end */
    uint64_t gm_l6; /* e */
    uint64_t gm_l7; /* e */
    gm_t9qkh0rsooc8d gm_l8; /* p */
    GM_STACK_CHECK(&F, 635u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_f_scan_ident_h1b2n4u(t4);
        gm_l2 = t5;
    }
    {
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t8 = ((t7 == ((uint64_t)1ULL)));
        if (!t8) goto ie6;
        {
            uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t11 >= (uintptr_t)(t12)) gm_trap_index(t11, t12, gm_srcvmmksi0829pa, 365, 12);
            uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 1));
            uint8_t t14 = ((t13 == ((uint8_t)99ULL)));
            if (!t14) goto ie10;
            void * t15 = gm_l0;
            gm_tqs5p4n2tq19v t16 = gm_f_peek_72bostn(t15);
            gm_tqs5p4n2tq19v t17;
            GM_ST(uint8_t, ((uint8_t *)&t17), ((uint8_t)34ULL));
            ((uint8_t *)&t17)[1] = 1;
            uint8_t t18 = (gm_eqb44cb3cf1n(((uint8_t *)&t16), ((uint8_t *)&t17)));
            if (!t18) goto ie10;
            {
                void * t20 = gm_l0;
                uint64_t t21 = gm_l1;
                gm_t9qkh0rsooc8d t22;
                GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)2ULL));
                gm_b0_1 t23 = gm_f_lex_string_bcdbdjm((void *)NULL, cx, NULL, t20, t21, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t24;
                uint8_t t25 = 0;
                F.ret = t24;
                goto gm_fret;
            }
            goto ix10;
ie10:;
ix10:;
        }
        {
            uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_srcvmmksi0829pa, 369, 12);
            uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t27 * 1));
            uint8_t t30 = ((t29 == ((uint8_t)95ULL)));
            if (!t30) goto ie26;
            {
                void * t32 = gm_l0;
                gm_t3fcbuehh3bs2 t33;
                GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t33 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t33 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)1ULL));
                uint64_t t34 = gm_l1;
                void * t35 = gm_l0;
                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 24));
                gm_b0_1 t37 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t32, &t33, t34, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t38;
                uint8_t t39 = 0;
                F.ret = t38;
                goto gm_fret;
            }
            goto ix26;
ie26:;
ix26:;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        gm_b16_8 t40 = gm_l2;
        gm_taari1s0m51a7 t41 = gm_f_keyword_from_1ofmh1u(t40);
        gm_t9qkh0rsooc8d t42;
        if (((((uint8_t *)&t41))[1] != 0)) {
            gm_t9qkh0rsooc8d t43 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t41) + 0));
            t42 = t43;
        } else {
            {
                void * t45 = gm_l0;
                gm_b16_8 t46 = gm_l2;
                gm_t3fcbuehh3bs2 t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t47 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t47 + 8), t46);
                uint64_t t48 = gm_l1;
                void * t49 = gm_l0;
                uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)t49) + 24));
                gm_b0_1 t51 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t45, &t47, t48, t50);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t52;
            uint8_t t53 = 0;
            F.ret = t52;
            goto gm_fret;
        }
        gm_l3 = t42;
    }
    {
        gm_taari1s0m51a7 t54;
        memset(&t54, 0, sizeof t54);
        gm_l4 = t54;
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        uint8_t t56 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l3) + 0) == ((uint8_t)16ULL)));
        if (!t56) goto ie55;
        {
            void * t59 = gm_l0;
            void * t60 = gm_l0;
            uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)t60) + 24));
            gm_b16_8 t62;
            GM_ST(void *, (uint8_t *)&t62, (void *)(gm_scvhpklutvpjg));
            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(3));
            gm_tf5srh4mp4b3i t63 = gm_f_second_word_end_dpnj7g8(t59, t61, t62);
            if (!((((uint8_t *)&t63))[8] != 0)) goto ie58;
            uint64_t t64 = GM_LD(uint64_t, ((uint8_t *)&t63));
            gm_l6 = t64;
            {
                gm_t9qkh0rsooc8d t66;
                GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)67ULL));
                gm_taari1s0m51a7 t67;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t67), t66);
                ((uint8_t *)&t67)[1] = 1;
                gm_l4 = t67;
            }
            {
                uint64_t t68 = gm_l6;
                gm_l5 = t68;
            }
            goto ix58;
ie58:;
ix58:;
        }
        goto ix55;
ie55:;
        uint8_t t70 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l3) + 0) == ((uint8_t)18ULL)));
        if (!t70) goto ie69;
        {
            void * t73 = gm_l0;
            void * t74 = gm_l0;
            uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)t74) + 24));
            gm_b16_8 t76;
            GM_ST(void *, (uint8_t *)&t76, (void *)(gm_svbhcvlahd4n2));
            GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(2));
            gm_tf5srh4mp4b3i t77 = gm_f_second_word_end_dpnj7g8(t73, t75, t76);
            if (!((((uint8_t *)&t77))[8] != 0)) goto ie72;
            uint64_t t78 = GM_LD(uint64_t, ((uint8_t *)&t77));
            gm_l7 = t78;
            {
                gm_t9qkh0rsooc8d t80;
                GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)68ULL));
                gm_taari1s0m51a7 t81;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t81), t80);
                ((uint8_t *)&t81)[1] = 1;
                gm_l4 = t81;
            }
            {
                uint64_t t82 = gm_l7;
                gm_l5 = t82;
            }
            goto ix72;
ie72:;
ix72:;
        }
        goto ix69;
ie69:;
ix69:;
ix55:;
    }
    {
        if (!((((uint8_t *)&gm_l4))[1] != 0)) goto ie83;
        gm_t9qkh0rsooc8d t84 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l4));
        gm_l8 = t84;
        {
            void * t86 = gm_l0;
            uint64_t t87 = gm_l5;
            GM_ST(uint64_t, (((uint8_t *)t86) + 24), t87);
        }
        {
            void * t88 = gm_l0;
            gm_t9qkh0rsooc8d t89 = gm_l8;
            gm_t3fcbuehh3bs2 t90;
            GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t90 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t90 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t90 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t90 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t90 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t90 + 8), t89);
            uint64_t t91 = gm_l1;
            uint64_t t92 = gm_l5;
            gm_b0_1 t93 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t88, &t90, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix83;
ie83:;
        {
            void * t95 = gm_l0;
            gm_t9qkh0rsooc8d t96 = gm_l3;
            gm_t3fcbuehh3bs2 t97;
            GM_ST(uint8_t, (uint8_t *)&t97 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t97 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t97 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t97 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t97 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t97 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t97 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t97 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t97 + 8), t96);
            uint64_t t98 = gm_l1;
            void * t99 = gm_l0;
            uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)t99) + 24));
            gm_b0_1 t101 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t95, &t97, t98, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix83:;
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
__attribute__((aligned(4))) uint8_t gm_f_line_binds_jm0pnul(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_line_binds_jm0pnul F;
    gm_b16_8 gm_l0; /* rest */
    int32_t gm_l1; /* depth */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 599u);
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
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srcvmmksi0829pa, 764, 14);
                uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 1));
                gm_l3 = t11;
            }
            {
                uint8_t t12 = gm_l3;
                uint8_t t15 = GM_LD(uint8_t, ((uint8_t *)&t12));
                if (!(t15 == ((uint8_t)10ULL))) goto mn14;
                uint8_t t16 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
                goto me13;
mn14:;
                uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)&t12));
                if (!(t20 == ((uint8_t)40ULL))) goto pa19;
                goto po18;
pa19:;
                uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)&t12));
                if (!(t22 == ((uint8_t)91ULL))) goto pa21;
                goto po18;
pa21:;
                uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)&t12));
                if (!(t24 == ((uint8_t)123ULL))) goto pa23;
                goto po18;
pa23:;
                goto mn17;
po18:;
                {
                    int32_t t26 = gm_l1;
                    int32_t r27; bool o28 = __builtin_add_overflow(t26, ((int32_t)1LL), &r27);
                    if (o28) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 767, 32);
                    int32_t t29 = r27;
                    gm_l1 = t29;
                }
                goto me13;
mn17:;
                uint8_t t33 = GM_LD(uint8_t, ((uint8_t *)&t12));
                if (!(t33 == ((uint8_t)41ULL))) goto pa32;
                goto po31;
pa32:;
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)&t12));
                if (!(t35 == ((uint8_t)93ULL))) goto pa34;
                goto po31;
pa34:;
                uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)&t12));
                if (!(t37 == ((uint8_t)125ULL))) goto pa36;
                goto po31;
pa36:;
                goto mn30;
po31:;
                {
                    int32_t t39 = gm_l1;
                    int32_t r40; bool o41 = __builtin_sub_overflow(t39, ((int32_t)1LL), &r40);
                    if (o41) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 769, 17);
                    int32_t t42 = r40;
                    gm_l1 = t42;
                }
                {
                    int32_t t44 = gm_l1;
                    uint8_t t45 = (t44 < ((int32_t)0LL));
                    if (!t45) goto ie43;
                    {
                        uint8_t t47 = 0;
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                    goto ix43;
ie43:;
ix43:;
                }
                goto me13;
mn30:;
                uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)&t12));
                if (!(t51 == ((uint8_t)34ULL))) goto pa50;
                goto po49;
pa50:;
                uint8_t t53 = GM_LD(uint8_t, ((uint8_t *)&t12));
                if (!(t53 == ((uint8_t)39ULL))) goto pa52;
                goto po49;
pa52:;
                goto mn48;
po49:;
                {
                    uint64_t t55 = gm_l2;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    if (o57) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 773, 17);
                    uint64_t t58 = r56;
                    gm_l2 = t58;
                }
                {
                    for (;;) {
                        uint64_t t60 = gm_l2;
                        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t62 = (t60 < t61);
                        if (!t62) goto lb59;
                        uint64_t t63 = gm_l2;
                        uint64_t t64 = (uintptr_t)t63;
                        uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_srcvmmksi0829pa, 774, 40);
                        uint8_t t66 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t64 * 1));
                        uint8_t t67 = gm_l3;
                        uint8_t t68 = (!(t66 == t67));
                        if (!t68) goto lb59;
                        uint64_t t69 = gm_l2;
                        uint64_t t70 = (uintptr_t)t69;
                        uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t70 >= (uintptr_t)(t71)) gm_trap_index(t70, t71, gm_srcvmmksi0829pa, 774, 61);
                        uint8_t t72 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t70 * 1));
                        uint8_t t73 = (!(t72 == ((uint8_t)10ULL)));
                        if (!t73) goto lb59;
                        {
                            uint64_t t76 = gm_l2;
                            uint64_t t77;
                            uint64_t t79 = gm_l2;
                            uint64_t t80 = (uintptr_t)t79;
                            uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t80 >= (uintptr_t)(t81)) gm_trap_index(t80, t81, gm_srcvmmksi0829pa, 775, 29);
                            uint8_t t82 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t80 * 1));
                            uint8_t t83 = ((t82 == ((uint8_t)92ULL)));
                            if (!t83) goto ie78;
                            uint64_t t85 = ((uint64_t)2ULL);
                            t77 = t85;
                            goto ix78;
ie78:;
                            uint64_t t87 = ((uint64_t)1ULL);
                            t77 = t87;
ix78:;
                            uint64_t r88; bool o89 = __builtin_add_overflow(t76, t77, &r88);
                            if (o89) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 775, 21);
                            uint64_t t90 = r88;
                            gm_l2 = t90;
                        }
lc59:;
                    }
lb59:;
                }
                goto me13;
mn48:;
                uint8_t t92 = GM_LD(uint8_t, ((uint8_t *)&t12));
                if (!(t92 == ((uint8_t)47ULL))) goto mn91;
                gm_b16_8 t93 = gm_l0;
                void * t94 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t93)));
                uint64_t t95 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t93) + 8);
                uint64_t t96 = gm_l2;
                uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                if (o98) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 778, 29);
                uint64_t t99 = r97;
                uint64_t t100 = (uintptr_t)t99;
                gm_tqs5p4n2tq19v t101;
                if (t100 < t95) {
                uint8_t t102 = GM_LD(uint8_t, ((uint8_t *)t94 + t100 * 1));
                gm_tqs5p4n2tq19v t103;
                GM_ST(uint8_t, ((uint8_t *)&t103), t102);
                ((uint8_t *)&t103)[1] = 1;
                    t101 = t103;
                } else {
                gm_tqs5p4n2tq19v t104;
                memset(&t104, 0, sizeof t104);
                    t101 = t104;
                }
                gm_tqs5p4n2tq19v t105;
                GM_ST(uint8_t, ((uint8_t *)&t105), ((uint8_t)47ULL));
                ((uint8_t *)&t105)[1] = 1;
                uint8_t t106 = (gm_eqb44cb3cf1n(((uint8_t *)&t101), ((uint8_t *)&t105)));
                if (!t106) goto mn91;
                uint8_t t107 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
                goto me13;
mn91:;
                uint8_t t109 = GM_LD(uint8_t, ((uint8_t *)&t12));
                if (!(t109 == ((uint8_t)47ULL))) goto mn108;
                gm_b16_8 t110 = gm_l0;
                void * t111 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t110)));
                uint64_t t112 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t110) + 8);
                uint64_t t113 = gm_l2;
                uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                if (o115) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 779, 29);
                uint64_t t116 = r114;
                uint64_t t117 = (uintptr_t)t116;
                gm_tqs5p4n2tq19v t118;
                if (t117 < t112) {
                uint8_t t119 = GM_LD(uint8_t, ((uint8_t *)t111 + t117 * 1));
                gm_tqs5p4n2tq19v t120;
                GM_ST(uint8_t, ((uint8_t *)&t120), t119);
                ((uint8_t *)&t120)[1] = 1;
                    t118 = t120;
                } else {
                gm_tqs5p4n2tq19v t121;
                memset(&t121, 0, sizeof t121);
                    t118 = t121;
                }
                gm_tqs5p4n2tq19v t122;
                GM_ST(uint8_t, ((uint8_t *)&t122), ((uint8_t)42ULL));
                ((uint8_t *)&t122)[1] = 1;
                uint8_t t123 = (gm_eqb44cb3cf1n(((uint8_t *)&t118), ((uint8_t *)&t122)));
                if (!t123) goto mn108;
                {
                    gm_b16_8 t125 = gm_l0;
                    uint64_t t126 = gm_l2;
                    gm_tf5srh4mp4b3i t127 = gm_f_inline_comment_end_8i5h1eq(t125, t126);
                    uint64_t t128;
                    if (((((uint8_t *)&t127))[8] != 0)) {
                        uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&t127) + 0));
                        t128 = t129;
                    } else {
                        uint8_t t130 = 0;
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                    gm_l2 = t128;
                }
                {
                    goto lc2;
                }
                goto me13;
mn108:;
                uint8_t t132 = GM_LD(uint8_t, ((uint8_t *)&t12));
                if (!(t132 == ((uint8_t)96ULL))) goto mn131;
                {
                    gm_b16_8 t134 = gm_l0;
                    uint64_t t135 = gm_l2;
                    gm_tf5srh4mp4b3i t136 = gm_f_inline_raw_string_end_6lenf33(t134, t135);
                    uint64_t t137;
                    if (((((uint8_t *)&t136))[8] != 0)) {
                        uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&t136) + 0));
                        t137 = t138;
                    } else {
                        uint8_t t139 = 0;
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                    gm_l2 = t137;
                }
                {
                    goto lc2;
                }
                goto me13;
mn131:;
                uint8_t t141 = GM_LD(uint8_t, ((uint8_t *)&t12));
                if (!(t141 == ((uint8_t)58ULL))) goto mn140;
                uint8_t t142 = 0;
                int32_t t143 = gm_l1;
                uint8_t t144 = ((t143 == ((int32_t)0LL)));
                if (t144) {
                    gm_b16_8 t145 = gm_l0;
                    void * t146 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t145)));
                    uint64_t t147 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t145) + 8);
                    uint64_t t148 = gm_l2;
                    uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
                    if (o150) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 787, 44);
                    uint64_t t151 = r149;
                    uint64_t t152 = (uintptr_t)t151;
                    gm_tqs5p4n2tq19v t153;
                    if (t152 < t147) {
                    uint8_t t154 = GM_LD(uint8_t, ((uint8_t *)t146 + t152 * 1));
                    gm_tqs5p4n2tq19v t155;
                    GM_ST(uint8_t, ((uint8_t *)&t155), t154);
                    ((uint8_t *)&t155)[1] = 1;
                        t153 = t155;
                    } else {
                    gm_tqs5p4n2tq19v t156;
                    memset(&t156, 0, sizeof t156);
                        t153 = t156;
                    }
                    gm_tqs5p4n2tq19v t157;
                    GM_ST(uint8_t, ((uint8_t *)&t157), ((uint8_t)61ULL));
                    ((uint8_t *)&t157)[1] = 1;
                    uint8_t t158 = (gm_eqb44cb3cf1n(((uint8_t *)&t153), ((uint8_t *)&t157)));
                    t142 = t158;
                } else t142 = 0;
                if (!t142) goto mn140;
                uint8_t t159 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
                goto me13;
mn140:;
                goto me13;
mn160:;
                gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 765, 9);
me13:;
            }
            {
                uint64_t t162 = gm_l2;
                uint64_t r163; bool o164 = __builtin_add_overflow(t162, ((uint64_t)1ULL), &r163);
                if (o164) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 790, 9);
                uint64_t t165 = r163;
                gm_l2 = t165;
            }
lc2:;
        }
lb2:;
    }
    {
        uint8_t t166 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_line_comment_s71u8hq(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_line_comment_s71u8hq F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint8_t gm_l2; /* is_doc */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* b */
    gm_b16_8 gm_l5; /* text */
    uint64_t gm_l6; /* next */
    GM_STACK_CHECK(&F, 643u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        uint8_t t4 = 0;
        void * t5 = gm_l0;
        gm_tqs5p4n2tq19v t6 = gm_f_peek_at_74p8lkq(t5, ((uint64_t)2ULL));
        gm_tqs5p4n2tq19v t7;
        GM_ST(uint8_t, ((uint8_t *)&t7), ((uint8_t)47ULL));
        ((uint8_t *)&t7)[1] = 1;
        uint8_t t8 = (gm_eqb44cb3cf1n(((uint8_t *)&t6), ((uint8_t *)&t7)));
        if (t8) {
            void * t9 = gm_l0;
            gm_tqs5p4n2tq19v t10 = gm_f_peek_at_74p8lkq(t9, ((uint64_t)3ULL));
            gm_tqs5p4n2tq19v t11;
            GM_ST(uint8_t, ((uint8_t *)&t11), ((uint8_t)47ULL));
            ((uint8_t *)&t11)[1] = 1;
            uint8_t t12 = (!gm_eqb44cb3cf1n(((uint8_t *)&t10), ((uint8_t *)&t11)));
            t4 = t12;
        } else t4 = 0;
        gm_l2 = t4;
    }
    {
        uint64_t t13 = gm_l1;
        uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)2ULL), &r14);
        if (o15) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 209, 10);
        uint64_t t16 = r14;
        gm_l3 = t16;
    }
    {
        for (;;) {
            void * t18 = gm_l0;
            uint64_t t19 = gm_l3;
            gm_tqs5p4n2tq19v t20 = gm_f_byte_b4v8vul(t18, t19);
            if (!((((uint8_t *)&t20))[1] != 0)) goto lb17;
            uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)&t20));
            gm_l4 = t21;
            {
                uint8_t t25 = gm_l4;
                uint8_t t26 = ((t25 == ((uint8_t)10ULL)));
                if (!t26) goto ie24;
                {
                    goto lb17;
                }
                goto ix24;
ie24:;
ix24:;
            }
            {
                uint8_t t29 = gm_l4;
                uint8_t t30 = ((t29 == ((uint8_t)13ULL)));
                if (!t30) goto ie28;
                {
                    void * t33 = gm_l0;
                    uint64_t t34 = gm_l3;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    if (o36) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 213, 24);
                    uint64_t t37 = r35;
                    gm_tqs5p4n2tq19v t38 = gm_f_byte_b4v8vul(t33, t37);
                    gm_tqs5p4n2tq19v t39;
                    GM_ST(uint8_t, ((uint8_t *)&t39), ((uint8_t)10ULL));
                    ((uint8_t *)&t39)[1] = 1;
                    uint8_t t40 = (gm_eqb44cb3cf1n(((uint8_t *)&t38), ((uint8_t *)&t39)));
                    if (!t40) goto ie32;
                    {
                        goto lb17;
                    }
                    goto ix32;
ie32:;
ix32:;
                }
                {
                    void * t42 = gm_l0;
                    uint64_t t43 = gm_l3;
                    uint64_t t44 = gm_l3;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    if (o46) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 214, 25);
                    uint64_t t47 = r45;
                    gm_b16_8 t48 = GM_LD(gm_b16_8, gm_c644mb9o5bq);
                    gm_b0_1 t49 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t42, t43, t47, t48);
                    if (gm_unw.kind) {
                        goto gm_ll22;
                    }
                }
                goto ix28;
ie28:;
ix28:;
            }
            {
                uint64_t t50 = gm_l3;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 216, 9);
                uint64_t t53 = r51;
                gm_l3 = t53;
            }
lc17:;
        }
gm_ll22:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
        }
        goto gm_lad;
lb17:;
    }
    {
        uint8_t t55 = gm_l2;
        if (!t55) goto ie54;
        {
            void * t57 = gm_l0;
            uint64_t t58 = GM_LD(uintptr_t, (((uint8_t *)t57) + 8) + 8);
            void * t59 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t57) + 8)));
            uint64_t t60 = gm_l1;
            uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)3ULL), &r61);
            if (o62) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 219, 24);
            uint64_t t63 = r61;
            uint64_t t64 = (uintptr_t)t63;
            uint64_t t65 = gm_l3;
            uint64_t t66 = (uintptr_t)t65;
            if (t64 > t66 || t66 > (uintptr_t)t58) gm_trap_slice(t64, t66, t58, gm_srcvmmksi0829pa, 219, 17);
            gm_b16_8 t67;
            GM_ST(void *, (uint8_t *)&t67, (void *)(((uint8_t *)t59 + t64 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(t66 - t64));
            gm_l5 = t67;
        }
        {
            gm_b16_8 t69 = gm_l5;
            gm_b16_8 t70;
            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_s3p0rbs9hc3lg));
            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(1));
            uint8_t t71 = gm_f_starts_with_6vvt01e(t69, t70);
            if (!t71) goto ie68;
            {
                uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                void * t74 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)));
                uint64_t t75 = (uintptr_t)((uint64_t)1ULL);
                if (t75 > (uintptr_t)t73 || (uintptr_t)t73 > (uintptr_t)t73) gm_trap_slice(t75, (uintptr_t)t73, t73, gm_srcvmmksi0829pa, 220, 43);
                gm_b16_8 t76;
                GM_ST(void *, (uint8_t *)&t76, (void *)(((uint8_t *)t74 + t75 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)((uintptr_t)t73 - t75));
                gm_l5 = t76;
            }
            goto ix68;
ie68:;
ix68:;
        }
        {
            uint64_t t77 = gm_l3;
            gm_l6 = t77;
        }
        {
            void * t79 = gm_l0;
            uint64_t t80 = gm_l6;
            gm_tqs5p4n2tq19v t81 = gm_f_byte_b4v8vul(t79, t80);
            gm_tqs5p4n2tq19v t82;
            GM_ST(uint8_t, ((uint8_t *)&t82), ((uint8_t)13ULL));
            ((uint8_t *)&t82)[1] = 1;
            uint8_t t83 = (gm_eqb44cb3cf1n(((uint8_t *)&t81), ((uint8_t *)&t82)));
            if (!t83) goto ie78;
            {
                uint64_t t85 = gm_l6;
                uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
                if (o87) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 222, 36);
                uint64_t t88 = r86;
                gm_l6 = t88;
            }
            goto ix78;
ie78:;
ix78:;
        }
        {
            void * t90 = gm_l0;
            uint64_t t91 = gm_l6;
            gm_tqs5p4n2tq19v t92 = gm_f_byte_b4v8vul(t90, t91);
            gm_tqs5p4n2tq19v t93;
            GM_ST(uint8_t, ((uint8_t *)&t93), ((uint8_t)10ULL));
            ((uint8_t *)&t93)[1] = 1;
            uint8_t t94 = (gm_eqb44cb3cf1n(((uint8_t *)&t92), ((uint8_t *)&t93)));
            if (!t94) goto ie89;
            {
                uint64_t t96 = gm_l6;
                uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                if (o98) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 224, 13);
                uint64_t t99 = r97;
                gm_l6 = t99;
            }
            {
                for (;;) {
                    void * t101 = gm_l0;
                    uint64_t t102 = gm_l6;
                    gm_tqs5p4n2tq19v t103 = gm_f_byte_b4v8vul(t101, t102);
                    gm_tqs5p4n2tq19v t104;
                    GM_ST(uint8_t, ((uint8_t *)&t104), ((uint8_t)32ULL));
                    ((uint8_t *)&t104)[1] = 1;
                    gm_tqs5p4n2tq19v t105;
                    GM_ST(uint8_t, ((uint8_t *)&t105), ((uint8_t)9ULL));
                    ((uint8_t *)&t105)[1] = 1;
                    gm_t5r6k0fdcfsb3 t106;
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t106 + 0), t104);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t106 + 2), t105);
                    uint8_t t107 = 0;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t106) + 0, ((uint8_t *)&t103))) t107 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t106) + 2, ((uint8_t *)&t103))) t107 = 1;
                    uint8_t t108 = (uint8_t)(t107);
                    if (!t108) goto lb100;
                    {
                        uint64_t t111 = gm_l6;
                        uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                        if (o113) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 225, 50);
                        uint64_t t114 = r112;
                        gm_l6 = t114;
                    }
lc100:;
                }
lb100:;
            }
            goto ix89;
ie89:;
ix89:;
        }
        {
            void * t115 = gm_l0;
            void * t116 = (void *)(((uint8_t *)t115) + 80);
            gm_t3jim1gvsdr87 t117;
            memset((uint8_t *)&t117 + 12, 0, 4);
            memset((uint8_t *)&t117 + 36, 0, 4);
            void * t118 = gm_l0;
            uint64_t t119 = gm_l1;
            uint64_t t120 = gm_l3;
            gm_tl874t2atnlh3 t121 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t118, t119, t120));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t117 + 0, t121);
            gm_b16_8 t122 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t117 + 16, t122);
            uint64_t t123 = gm_l6;
            uint32_t t124 = (uint32_t)t123;
            GM_ST(uint32_t, (uint8_t *)&t117 + 32, t124);
            gm_tftb1odc739t5 t125 = gm_f_push_o6f0em6(t116, &t117);
            if (((((uint8_t *)&t125))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t125) + 1, gm_srcvmmksi0829pa, 227, 9);
            gm_b0_1 t126 = GM_LD(gm_b0_1, (((uint8_t *)&t125) + 1));
        }
        goto ix54;
ie54:;
ix54:;
    }
    {
        void * t127 = gm_l0;
        uint64_t t128 = gm_l3;
        GM_ST(uint64_t, (((uint8_t *)t127) + 24), t128);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_lone_cr_ktb0jvm(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_new_lexer_dm12g12 F;
    uint32_t gm_l0; /* file */
    gm_b16_8 gm_l1; /* src */
    void * gm_l2; /* diags */
    GM_STACK_CHECK(&F, 296u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        memset((uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 4, 0, 4);
        memset((uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 281, 0, 7);
        uint32_t t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        void * t4 = gm_l2;
        gm_trqfvmjtt4sij t5;
        gm_f_list_lpokg1m((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t6;
        gm_f_list_5o6u5a9((void *)&t6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t7;
        gm_f_list_2iogcnb((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tc0nbpdhofi37 t8;
        for (uint64_t i = 0; i < 3; i++) GM_ST(uint64_t, (uint8_t *)&t8 + i * 8, ((uint64_t)0ULL));
        gm_trqfvmjtt4sij t9;
        gm_f_list_5dhgg4i((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_toceffb697p0u t10;
        memset(&t10, 0, sizeof t10);
        gm_trqfvmjtt4sij t11;
        gm_f_list_go2qg0b((void *)&t11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint32_t, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 0, t2);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 8, t3);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(void *, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 32, t4);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 40, t5);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 80, t6);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 120, t7);
        GM_ST(gm_tc0nbpdhofi37, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 160, t8);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 184, t9);
        GM_ST(gm_toceffb697p0u, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 224, t10);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 240, t11);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 280, ((uint8_t)0));
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
extern __attribute__((aligned(4))) void gm_f_note_newline_cchbr7g(void * a0, uint64_t a1, uint64_t a2);
extern __attribute__((aligned(4))) gm_te0mgnjdphs16 gm_f_one_or_two_lava5tu(uint8_t a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) void gm_f_open_bracket_3kcj4k8(void * a0, gm_t9qkh0rsooc8d a1, gm_cseahvg8v7p0vs a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_open_bracket_3kcj4k8 F;
    void * gm_l0; /* lx */
    gm_t9qkh0rsooc8d gm_l1; /* kind */
    gm_tl874t2atnlh3 gm_l2; /* at */
    void * gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_tl874t2atnlh3, a2);
    {
        {
            void * t3 = gm_l0;
            uint8_t *t4 = (uint8_t *)((((uint8_t *)t3) + 160));
            gm_t9qkh0rsooc8d t5 = gm_l1;
            uint64_t t6 = gm_f_slot_4oe07nr(t5);
            uint64_t t7 = (uintptr_t)t6;
            if (t7 >= (uintptr_t)(3)) gm_trap_index(t7, 3, gm_srcvmmksi0829pa, 626, 5);
            void * t8 = (void *)(t4 + t7 * 8);
            gm_l3 = t8;
        }
        {
            void * t9 = gm_l3;
            void * t10 = gm_l3;
            uint64_t t11 = GM_LD(uint64_t, ((uint8_t *)t10));
            uint64_t r12; bool o13 = __builtin_add_overflow(t11, ((uint64_t)1ULL), &r12);
            if (o13) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 626, 5);
            uint64_t t14 = r12;
            GM_ST(uint64_t, ((uint8_t *)t9), t14);
        }
    }
    {
        void * t15 = gm_l0;
        void * t16 = (void *)(((uint8_t *)t15) + 120);
        gm_t9qkh0rsooc8d t17 = gm_l1;
        gm_tl874t2atnlh3 t18 = gm_l2;
        gm_t2eb59dvglnle t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 0), t17);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t19 + 4), t18);
        gm_tftb1odc739t5 t20 = gm_f_push_vs3f95a(t16, t19);
        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcvmmksi0829pa, 627, 5);
        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_opener_8hqhoc3(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_opener_8hqhoc3 F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma5; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)61ULL));
    t1 = t10;
    goto me2;
mn8:;
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)1ULL)) goto mn11;
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)63ULL));
    t1 = t13;
    goto me2;
mn11:;
ma5:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t17 != ((uint8_t)2ULL)) goto pa16;
    goto po15;
pa16:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)3ULL)) goto pa18;
    goto po15;
pa18:;
    goto mn14;
po15:;
    gm_t9qkh0rsooc8d t20;
    GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)65ULL));
    t1 = t20;
    goto me2;
mn14:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 47, 42);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_72bostn(void * a0);
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_at_74p8lkq(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_push_newline_ke0tojq(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_newline_ke0tojq F;
    void * gm_l0; /* lx */
    gm_tl874t2atnlh3 gm_l1; /* at */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 40) + 0) + 8);
        gm_l2 = t3;
    }
    {
        uint64_t t5 = gm_l2;
        uint8_t t6 = (t5 > ((uint64_t)0ULL));
        if (!t6) goto ie4;
        uint64_t t7 = gm_l2;
        uint64_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint64_t)1ULL), &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 662, 34);
        uint64_t t10 = r8;
        uint64_t t11 = (uintptr_t)t10;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uintptr_t, ((((uint8_t *)t12) + 40) + 0) + 8);
        if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srcvmmksi0829pa, 662, 18);
        uint8_t t14 = (!(GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t12) + 40) + 0)) + t11 * 48) + 0) + 0) == ((uint8_t)9ULL)));
        if (!t14) goto ie4;
        {
            void * t16 = gm_l0;
            void * t17 = (void *)(((uint8_t *)t16) + 40);
            gm_tm4pc0emhrv1g t18;
            memset((uint8_t *)&t18 + 44, 0, 4);
            gm_t3fcbuehh3bs2 t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t19 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t19 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t19 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)9ULL));
            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t18 + 0, t19);
            gm_tl874t2atnlh3 t20 = gm_l1;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t18 + 32, t20);
            gm_tftb1odc739t5 t21 = gm_f_push_pnf14h1(t17, &t18);
            if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srcvmmksi0829pa, 663, 9);
            gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_run_v26is74 F;
    void * gm_l0; /* lx */
    uint8_t gm_l1; /* b */
    uint64_t gm_l2; /* end */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t2eb59dvglnle gm_l5; /* b */
    GM_STACK_CHECK(&F, 742u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 8));
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_srh29phg8b30r));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(3));
        uint8_t t6 = gm_f_starts_with_6vvt01e(t4, t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            GM_ST(uint64_t, (((uint8_t *)t8) + 24), ((uint64_t)3ULL));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 40);
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 8) + 8);
        if (((uint64_t)4ULL) == 0) gm_trap("division by zero", gm_srcvmmksi0829pa, 128, 23);
        uint64_t t13 = (uint64_t)(t12 / ((uint64_t)4ULL));
        uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)16ULL), &r14);
        if (o15) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 128, 23);
        uint64_t t16 = r14;
        gm_tftb1odc739t5 t17 = gm_f_reserve_i65s8oj(t10, t16);
        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srcvmmksi0829pa, 128, 5);
        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb19;
            {
                void * t22 = gm_l0;
                gm_b0_1 t23 = gm_f_skip_trivia_014s7ai((void *)NULL, cx, NULL, t22);
                if (gm_unw.kind) {
                    goto gm_ll20;
                }
            }
            {
                void * t24 = gm_l0;
                gm_tqs5p4n2tq19v t25 = gm_f_peek_72bostn(t24);
                uint8_t t26;
                if (((((uint8_t *)&t25))[1] != 0)) {
                    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
                    t26 = t27;
                } else {
                    goto lb19;
                }
                gm_l1 = t26;
            }
            {
                void * t28 = gm_l0;
                uint8_t t29 = gm_l1;
                gm_b0_1 t30 = gm_f_lex_token_h8g44s4((void *)NULL, cx, NULL, t28, t29);
                if (gm_unw.kind) {
                    goto gm_ll20;
                }
            }
lc19:;
        }
gm_ll20:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb19; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc19; }
        }
        goto gm_lad;
lb19:;
    }
    {
        void * t31 = gm_l0;
        uint64_t t32 = GM_LD(uintptr_t, (((uint8_t *)t31) + 8) + 8);
        gm_l2 = t32;
    }
    {
        void * t33 = gm_l0;
        gm_t3fcbuehh3bs2 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t34 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t34 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t34 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)10ULL));
        uint64_t t35 = gm_l2;
        gm_f_flush_newline_9023ml8(t33, &t34, t35);
    }
    {
        {
            void * t37 = gm_l0;
            gm_b16_8 t38 = GM_LD(gm_b16_8, ((((uint8_t *)t37) + 120) + 0));
            gm_l3 = t38;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t40 = gm_l4;
                uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t42 = (t40 < t41);
                if (!t42) goto lb39;
                {
                    uint64_t t45 = gm_l4;
                    uint64_t t46 = (uintptr_t)t45;
                    uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t2eb59dvglnle t50 = GM_LD(gm_t2eb59dvglnle, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t46 * 16));
                    gm_l5 = t50;
                    {
                        void * t52 = gm_l0;
                        void * t53 = GM_LD(void *, (((uint8_t *)t52) + 32));
                        gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 4));
                        gm_t9qkh0rsooc8d t55 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l5) + 0));
                        gm_t9qkh0rsooc8d t56 = gm_f_opener_8hqhoc3(t55);
                        gm_b16_8 t57 = gm_f_punct_text_ov4r40u(t56);
                        gm_tnpifv20irnph t58;
                        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t57);
                        gm_tabb710d9mq2e t59;
                        gm_f_format_t32jdqr((void *)&t59, (void *)NULL, cx, NULL, t58);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 8, gm_srcvmmksi0829pa, 137, 33);
                        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t59) + 8));
                        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                        gm_b0_1 t62 = gm_f_report_eardf07((void *)NULL, cx, NULL, t53, t54, t61);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                    }
                    goto me48;
mn49:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 136, 5);
me48:;
                }
lc39:;
                {
                    uint64_t t64 = gm_l4;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                    uint64_t t67 = r65;
                    gm_l4 = t67;
                }
            }
gm_ll43:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb39; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc39; }
            }
            goto gm_lad;
lb39:;
        }
    }
    {
        void * t68 = gm_l0;
        void * t69 = (void *)(((uint8_t *)t68) + 40);
        gm_tm4pc0emhrv1g t70;
        memset((uint8_t *)&t70 + 44, 0, 4);
        gm_t3fcbuehh3bs2 t71;
        GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t71 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t71 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t71 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)10ULL));
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t70 + 0, t71);
        void * t72 = gm_l0;
        uint64_t t73 = gm_l2;
        uint64_t t74 = gm_l2;
        gm_tl874t2atnlh3 t75 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t72, t73, t74));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t70 + 32, t75);
        gm_tftb1odc739t5 t76 = gm_f_push_pnf14h1(t69, &t70);
        if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcvmmksi0829pa, 139, 5);
        gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
    }
    {
        void * t78 = gm_l0;
        gm_trqfvmjtt4sij t79 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t78) + 40));
        void * t80 = gm_l0;
        gm_trqfvmjtt4sij t81 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t80) + 80));
        void * t82 = gm_l0;
        gm_trqfvmjtt4sij t83 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t82) + 240));
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tr8dvlu9ik4gj *)gm_out) + 0, t79);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tr8dvlu9ik4gj *)gm_out) + 40, t81);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tr8dvlu9ik4gj *)gm_out) + 80, t83);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_scan_ident_h1b2n4u(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scan_ident_h1b2n4u F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* start */
    uint8_t gm_l2; /* c */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        for (;;) {
            void * t5 = gm_l0;
            gm_tqs5p4n2tq19v t6 = gm_f_peek_72bostn(t5);
            if (!((((uint8_t *)&t6))[1] != 0)) goto lb4;
            uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)&t6));
            gm_l2 = t7;
            {
                uint8_t t11 = gm_l2;
                uint8_t t12 = gm_f_is_ident_cont_317fpdb(t11);
                uint8_t t13 = (uint8_t)!t12;
                if (!t13) goto ie10;
                {
                    goto lb4;
                }
                goto ix10;
ie10:;
ix10:;
            }
            {
                void * t15 = gm_l0;
                void * t16 = gm_l0;
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 24));
                uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
                if (o19) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 355, 9);
                uint64_t t20 = r18;
                GM_ST(uint64_t, (((uint8_t *)t15) + 24), t20);
            }
lc4:;
        }
lb4:;
    }
    {
        void * t21 = gm_l0;
        uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)t21) + 8) + 8);
        void * t23 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t21) + 8)));
        uint64_t t24 = gm_l1;
        uint64_t t25 = (uintptr_t)t24;
        void * t26 = gm_l0;
        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t26) + 24));
        uint64_t t28 = (uintptr_t)t27;
        if (t25 > t28 || t28 > (uintptr_t)t22) gm_trap_slice(t25, t28, t22, gm_srcvmmksi0829pa, 357, 12);
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t23 + t25 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(t28 - t25));
        uint8_t t30 = 0;
        F.ret = t29;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_scan_punct_ecrh51c(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scan_punct_ecrh51c F;
    void * gm_l0; /* lx */
    uint8_t gm_l1; /* first */
    uint8_t gm_l2; /* a1 */
    uint8_t gm_l3; /* a2 */
    uint8_t gm_l4; /* a3 */
    gm_t9qkh0rsooc8d gm_l5; /* p */
    uint64_t gm_l6; /* len */
    gm_t6ofeqo2h242p gm_l7; /* fl */
    GM_STACK_CHECK_SHALLOW(&F, 1280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)t5) + 8) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcvmmksi0829pa, 429, 14);
        uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t5) + 8)) + t4 * 1));
        gm_l1 = t7;
    }
    {
        void * t8 = gm_l0;
        gm_tqs5p4n2tq19v t9 = gm_f_peek_at_74p8lkq(t8, ((uint64_t)1ULL));
        uint8_t t10;
        if (((((uint8_t *)&t9))[1] != 0)) {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            t10 = ((uint8_t)0ULL);
        }
        gm_l2 = t10;
    }
    {
        void * t12 = gm_l0;
        gm_tqs5p4n2tq19v t13 = gm_f_peek_at_74p8lkq(t12, ((uint64_t)2ULL));
        uint8_t t14;
        if (((((uint8_t *)&t13))[1] != 0)) {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            t14 = ((uint8_t)0ULL);
        }
        gm_l3 = t14;
    }
    {
        void * t16 = gm_l0;
        gm_tqs5p4n2tq19v t17 = gm_f_peek_at_74p8lkq(t16, ((uint64_t)3ULL));
        uint8_t t18;
        if (((((uint8_t *)&t17))[1] != 0)) {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
            t18 = t19;
        } else {
            t18 = ((uint8_t)0ULL);
        }
        gm_l4 = t18;
    }
    {
        gm_t9qkh0rsooc8d t20;
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0ULL));
        gm_l5 = t20;
    }
    {
        gm_l6 = ((uint64_t)1ULL);
    }
    {
        uint8_t t21 = gm_l1;
        uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t26 == ((uint8_t)43ULL))) goto pa25;
        goto po24;
pa25:;
        uint8_t t28 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t28 == ((uint8_t)45ULL))) goto pa27;
        goto po24;
pa27:;
        uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t30 == ((uint8_t)42ULL))) goto pa29;
        goto po24;
pa29:;
        goto mn23;
po24:;
        {
            uint8_t t32 = gm_l1;
            gm_t6ofeqo2h242p t33 = gm_f_flavors_qsu4oq0(t32);
            gm_l7 = t33;
        }
        {
            uint8_t t35 = gm_l1;
            uint8_t t36 = ((t35 == ((uint8_t)45ULL)));
            if (!t36) goto ie34;
            uint8_t t37 = gm_l2;
            uint8_t t38 = ((t37 == ((uint8_t)45ULL)));
            if (!t38) goto ie34;
            uint8_t t39 = gm_l3;
            uint8_t t40 = ((t39 == ((uint8_t)45ULL)));
            if (!t40) goto ie34;
            {
                gm_t9qkh0rsooc8d t42;
                GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)53ULL));
                gm_te0mgnjdphs16 t43;
                GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t43 + 0), t42);
                GM_ST(uint64_t, ((uint8_t *)&t43 + 8), ((uint64_t)3ULL));
                gm_te0mgnjdphs16 t44 = t43;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t44 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t44 + 8));
            }
            goto ix34;
ie34:;
            uint8_t t46 = gm_l1;
            uint8_t t47 = ((t46 == ((uint8_t)45ULL)));
            if (!t47) goto ie45;
            uint8_t t48 = gm_l2;
            uint8_t t49 = ((t48 == ((uint8_t)62ULL)));
            if (!t49) goto ie45;
            {
                gm_t9qkh0rsooc8d t51;
                GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)60ULL));
                gm_te0mgnjdphs16 t52;
                GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t52 + 0), t51);
                GM_ST(uint64_t, ((uint8_t *)&t52 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t53 = t52;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t53 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t53 + 8));
            }
            goto ix45;
ie45:;
            uint8_t t55 = gm_l2;
            uint8_t t56 = ((t55 == ((uint8_t)61ULL)));
            if (!t56) goto ie54;
            {
                gm_t9qkh0rsooc8d t58 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 1));
                gm_te0mgnjdphs16 t59;
                GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 0), t58);
                GM_ST(uint64_t, ((uint8_t *)&t59 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t60 = t59;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t60 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t60 + 8));
            }
            goto ix54;
ie54:;
            uint8_t t62 = gm_l2;
            uint8_t t63 = ((t62 == ((uint8_t)37ULL)));
            if (!t63) goto ie61;
            uint8_t t64 = gm_l3;
            uint8_t t65 = ((t64 == ((uint8_t)61ULL)));
            if (!t65) goto ie61;
            {
                gm_t9qkh0rsooc8d t67 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 5));
                gm_te0mgnjdphs16 t68;
                GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t68 + 0), t67);
                GM_ST(uint64_t, ((uint8_t *)&t68 + 8), ((uint64_t)3ULL));
                gm_te0mgnjdphs16 t69 = t68;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t69 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t69 + 8));
            }
            goto ix61;
ie61:;
            uint8_t t71 = gm_l2;
            uint8_t t72 = ((t71 == ((uint8_t)37ULL)));
            if (!t72) goto ie70;
            {
                gm_t9qkh0rsooc8d t74 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 2));
                gm_te0mgnjdphs16 t75;
                GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t75 + 0), t74);
                GM_ST(uint64_t, ((uint8_t *)&t75 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t76 = t75;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t76 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t76 + 8));
            }
            goto ix70;
ie70:;
            uint8_t t78 = gm_l2;
            uint8_t t79 = ((t78 == ((uint8_t)124ULL)));
            if (!t79) goto ie77;
            uint8_t t80 = gm_l3;
            uint8_t t81 = ((t80 == ((uint8_t)61ULL)));
            if (!t81) goto ie77;
            {
                gm_t9qkh0rsooc8d t83 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 6));
                gm_te0mgnjdphs16 t84;
                GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t84 + 0), t83);
                GM_ST(uint64_t, ((uint8_t *)&t84 + 8), ((uint64_t)3ULL));
                gm_te0mgnjdphs16 t85 = t84;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t85 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t85 + 8));
            }
            goto ix77;
ie77:;
            uint8_t t87 = gm_l2;
            uint8_t t88 = ((t87 == ((uint8_t)124ULL)));
            if (!t88) goto ie86;
            {
                gm_t9qkh0rsooc8d t90 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 3));
                gm_te0mgnjdphs16 t91;
                GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t91 + 0), t90);
                GM_ST(uint64_t, ((uint8_t *)&t91 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t92 = t91;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t92 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t92 + 8));
            }
            goto ix86;
ie86:;
            uint8_t t94 = gm_l2;
            uint8_t t95 = ((t94 == ((uint8_t)63ULL)));
            if (!t95) goto ie93;
            {
                gm_t9qkh0rsooc8d t97 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 4));
                gm_te0mgnjdphs16 t98;
                GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t98 + 0), t97);
                GM_ST(uint64_t, ((uint8_t *)&t98 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t99 = t98;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t99 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t99 + 8));
            }
            goto ix93;
ie93:;
            {
                gm_t9qkh0rsooc8d t101 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 0));
                gm_l5 = t101;
            }
ix93:;
ix86:;
ix77:;
ix70:;
ix61:;
ix54:;
ix45:;
ix34:;
        }
        goto me22;
mn23:;
        uint8_t t103 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t103 == ((uint8_t)47ULL))) goto mn102;
        {
            uint8_t t105 = gm_l2;
            uint8_t t106 = ((t105 == ((uint8_t)61ULL)));
            gm_t9qkh0rsooc8d t107;
            GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)26ULL));
            gm_t9qkh0rsooc8d t108;
            GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)3ULL));
            gm_te0mgnjdphs16 t109 = gm_f_one_or_two_lava5tu(t106, t107, t108);
            gm_te0mgnjdphs16 t110 = t109;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t110 + 0));
            GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t110 + 8));
        }
        goto me22;
mn102:;
        uint8_t t112 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t112 == ((uint8_t)37ULL))) goto mn111;
        {
            uint8_t t114 = gm_l2;
            uint8_t t115 = ((t114 == ((uint8_t)61ULL)));
            gm_t9qkh0rsooc8d t116;
            GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)27ULL));
            gm_t9qkh0rsooc8d t117;
            GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)4ULL));
            gm_te0mgnjdphs16 t118 = gm_f_one_or_two_lava5tu(t115, t116, t117);
            gm_te0mgnjdphs16 t119 = t118;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t119 + 0));
            GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t119 + 8));
        }
        goto me22;
mn111:;
        uint8_t t121 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t121 == ((uint8_t)60ULL))) goto mn120;
        {
            uint8_t t124 = gm_l2;
            uint8_t t125 = ((t124 == ((uint8_t)60ULL)));
            if (!t125) goto ie123;
            {
                uint8_t t128 = gm_l3;
                uint8_t t129 = ((t128 == ((uint8_t)37ULL)));
                if (!t129) goto ie127;
                uint8_t t130 = gm_l4;
                uint8_t t131 = ((t130 == ((uint8_t)61ULL)));
                if (!t131) goto ie127;
                {
                    gm_t9qkh0rsooc8d t133;
                    GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)36ULL));
                    gm_te0mgnjdphs16 t134;
                    GM_ST(uint8_t, (uint8_t *)&t134 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t134 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t134 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t134 + 0), t133);
                    GM_ST(uint64_t, ((uint8_t *)&t134 + 8), ((uint64_t)4ULL));
                    gm_te0mgnjdphs16 t135 = t134;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t135 + 0));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t135 + 8));
                }
                goto ix127;
ie127:;
                uint8_t t137 = gm_l3;
                uint8_t t138 = ((t137 == ((uint8_t)124ULL)));
                if (!t138) goto ie136;
                uint8_t t139 = gm_l4;
                uint8_t t140 = ((t139 == ((uint8_t)61ULL)));
                if (!t140) goto ie136;
                {
                    gm_t9qkh0rsooc8d t142;
                    GM_ST(uint8_t, ((uint8_t *)&t142 + 0), ((uint8_t)40ULL));
                    gm_te0mgnjdphs16 t143;
                    GM_ST(uint8_t, (uint8_t *)&t143 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t143 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t143 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t143 + 0), t142);
                    GM_ST(uint64_t, ((uint8_t *)&t143 + 8), ((uint64_t)4ULL));
                    gm_te0mgnjdphs16 t144 = t143;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t144 + 0));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t144 + 8));
                }
                goto ix136;
ie136:;
                uint8_t t146 = gm_l3;
                uint8_t t147 = ((t146 == ((uint8_t)37ULL)));
                if (!t147) goto ie145;
                {
                    gm_t9qkh0rsooc8d t149;
                    GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)13ULL));
                    gm_te0mgnjdphs16 t150;
                    GM_ST(uint8_t, (uint8_t *)&t150 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t150 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t150 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t150 + 0), t149);
                    GM_ST(uint64_t, ((uint8_t *)&t150 + 8), ((uint64_t)3ULL));
                    gm_te0mgnjdphs16 t151 = t150;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t151 + 0));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t151 + 8));
                }
                goto ix145;
ie145:;
                uint8_t t153 = gm_l3;
                uint8_t t154 = ((t153 == ((uint8_t)124ULL)));
                if (!t154) goto ie152;
                {
                    gm_t9qkh0rsooc8d t156;
                    GM_ST(uint8_t, ((uint8_t *)&t156 + 0), ((uint8_t)17ULL));
                    gm_te0mgnjdphs16 t157;
                    GM_ST(uint8_t, (uint8_t *)&t157 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t157 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t157 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t157 + 0), t156);
                    GM_ST(uint64_t, ((uint8_t *)&t157 + 8), ((uint64_t)3ULL));
                    gm_te0mgnjdphs16 t158 = t157;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t158 + 0));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t158 + 8));
                }
                goto ix152;
ie152:;
                uint8_t t160 = gm_l3;
                uint8_t t161 = ((t160 == ((uint8_t)63ULL)));
                if (!t161) goto ie159;
                {
                    gm_t9qkh0rsooc8d t163;
                    GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)21ULL));
                    gm_te0mgnjdphs16 t164;
                    GM_ST(uint8_t, (uint8_t *)&t164 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t164 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t164 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t164 + 0), t163);
                    GM_ST(uint64_t, ((uint8_t *)&t164 + 8), ((uint64_t)3ULL));
                    gm_te0mgnjdphs16 t165 = t164;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t165 + 0));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t165 + 8));
                }
                goto ix159;
ie159:;
                uint8_t t167 = gm_l3;
                uint8_t t168 = ((t167 == ((uint8_t)61ULL)));
                if (!t168) goto ie166;
                {
                    gm_t9qkh0rsooc8d t170;
                    GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)28ULL));
                    gm_te0mgnjdphs16 t171;
                    GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t171 + 0), t170);
                    GM_ST(uint64_t, ((uint8_t *)&t171 + 8), ((uint64_t)3ULL));
                    gm_te0mgnjdphs16 t172 = t171;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t172 + 0));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t172 + 8));
                }
                goto ix166;
ie166:;
                {
                    gm_t9qkh0rsooc8d t174;
                    GM_ST(uint8_t, ((uint8_t *)&t174 + 0), ((uint8_t)5ULL));
                    gm_te0mgnjdphs16 t175;
                    GM_ST(uint8_t, (uint8_t *)&t175 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t175 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t175 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t175 + 0), t174);
                    GM_ST(uint64_t, ((uint8_t *)&t175 + 8), ((uint64_t)2ULL));
                    gm_te0mgnjdphs16 t176 = t175;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t176 + 0));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t176 + 8));
                }
ix166:;
ix159:;
ix152:;
ix145:;
ix136:;
ix127:;
            }
            goto ix123;
ie123:;
            uint8_t t178 = gm_l2;
            uint8_t t179 = ((t178 == ((uint8_t)61ULL)));
            if (!t179) goto ie177;
            {
                gm_t9qkh0rsooc8d t181;
                GM_ST(uint8_t, ((uint8_t *)&t181 + 0), ((uint8_t)42ULL));
                gm_te0mgnjdphs16 t182;
                GM_ST(uint8_t, (uint8_t *)&t182 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t182 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t182 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t182 + 0), t181);
                GM_ST(uint64_t, ((uint8_t *)&t182 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t183 = t182;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t183 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t183 + 8));
            }
            goto ix177;
ie177:;
            {
                gm_t9qkh0rsooc8d t185;
                GM_ST(uint8_t, ((uint8_t *)&t185 + 0), ((uint8_t)41ULL));
                gm_l5 = t185;
            }
ix177:;
ix123:;
        }
        goto me22;
mn120:;
        uint8_t t187 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t187 == ((uint8_t)62ULL))) goto mn186;
        {
            uint8_t t190 = gm_l2;
            uint8_t t191 = ((t190 == ((uint8_t)62ULL)));
            if (!t191) goto ie189;
            uint8_t t192 = gm_l3;
            uint8_t t193 = ((t192 == ((uint8_t)61ULL)));
            if (!t193) goto ie189;
            {
                gm_t9qkh0rsooc8d t195;
                GM_ST(uint8_t, ((uint8_t *)&t195 + 0), ((uint8_t)29ULL));
                gm_te0mgnjdphs16 t196;
                GM_ST(uint8_t, (uint8_t *)&t196 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t196 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t196 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t196 + 0), t195);
                GM_ST(uint64_t, ((uint8_t *)&t196 + 8), ((uint64_t)3ULL));
                gm_te0mgnjdphs16 t197 = t196;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t197 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t197 + 8));
            }
            goto ix189;
ie189:;
            uint8_t t199 = gm_l2;
            uint8_t t200 = ((t199 == ((uint8_t)62ULL)));
            if (!t200) goto ie198;
            {
                gm_t9qkh0rsooc8d t202;
                GM_ST(uint8_t, ((uint8_t *)&t202 + 0), ((uint8_t)6ULL));
                gm_te0mgnjdphs16 t203;
                GM_ST(uint8_t, (uint8_t *)&t203 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t203 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t203 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t203 + 0), t202);
                GM_ST(uint64_t, ((uint8_t *)&t203 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t204 = t203;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t204 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t204 + 8));
            }
            goto ix198;
ie198:;
            uint8_t t206 = gm_l2;
            uint8_t t207 = ((t206 == ((uint8_t)61ULL)));
            if (!t207) goto ie205;
            {
                gm_t9qkh0rsooc8d t209;
                GM_ST(uint8_t, ((uint8_t *)&t209 + 0), ((uint8_t)44ULL));
                gm_te0mgnjdphs16 t210;
                GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t210 + 0), t209);
                GM_ST(uint64_t, ((uint8_t *)&t210 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t211 = t210;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t211 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t211 + 8));
            }
            goto ix205;
ie205:;
            {
                gm_t9qkh0rsooc8d t213;
                GM_ST(uint8_t, ((uint8_t *)&t213 + 0), ((uint8_t)43ULL));
                gm_l5 = t213;
            }
ix205:;
ix198:;
ix189:;
        }
        goto me22;
mn186:;
        uint8_t t215 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t215 == ((uint8_t)38ULL))) goto mn214;
        {
            uint8_t t217 = gm_l2;
            uint8_t t218 = ((t217 == ((uint8_t)61ULL)));
            gm_t9qkh0rsooc8d t219;
            GM_ST(uint8_t, ((uint8_t *)&t219 + 0), ((uint8_t)30ULL));
            gm_t9qkh0rsooc8d t220;
            GM_ST(uint8_t, ((uint8_t *)&t220 + 0), ((uint8_t)7ULL));
            gm_te0mgnjdphs16 t221 = gm_f_one_or_two_lava5tu(t218, t219, t220);
            gm_te0mgnjdphs16 t222 = t221;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t222 + 0));
            GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t222 + 8));
        }
        goto me22;
mn214:;
        uint8_t t224 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t224 == ((uint8_t)124ULL))) goto mn223;
        {
            uint8_t t226 = gm_l2;
            uint8_t t227 = ((t226 == ((uint8_t)61ULL)));
            gm_t9qkh0rsooc8d t228;
            GM_ST(uint8_t, ((uint8_t *)&t228 + 0), ((uint8_t)31ULL));
            gm_t9qkh0rsooc8d t229;
            GM_ST(uint8_t, ((uint8_t *)&t229 + 0), ((uint8_t)8ULL));
            gm_te0mgnjdphs16 t230 = gm_f_one_or_two_lava5tu(t227, t228, t229);
            gm_te0mgnjdphs16 t231 = t230;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t231 + 0));
            GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t231 + 8));
        }
        goto me22;
mn223:;
        uint8_t t233 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t233 == ((uint8_t)126ULL))) goto mn232;
        {
            uint8_t t235 = gm_l2;
            uint8_t t236 = ((t235 == ((uint8_t)61ULL)));
            gm_t9qkh0rsooc8d t237;
            GM_ST(uint8_t, ((uint8_t *)&t237 + 0), ((uint8_t)32ULL));
            gm_t9qkh0rsooc8d t238;
            GM_ST(uint8_t, ((uint8_t *)&t238 + 0), ((uint8_t)9ULL));
            gm_te0mgnjdphs16 t239 = gm_f_one_or_two_lava5tu(t236, t237, t238);
            gm_te0mgnjdphs16 t240 = t239;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t240 + 0));
            GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t240 + 8));
        }
        goto me22;
mn232:;
        uint8_t t242 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t242 == ((uint8_t)61ULL))) goto mn241;
        {
            uint8_t t244 = gm_l2;
            uint8_t t245 = ((t244 == ((uint8_t)62ULL)));
            gm_t9qkh0rsooc8d t246;
            GM_ST(uint8_t, ((uint8_t *)&t246 + 0), ((uint8_t)59ULL));
            gm_t9qkh0rsooc8d t247;
            GM_ST(uint8_t, ((uint8_t *)&t247 + 0), ((uint8_t)22ULL));
            gm_te0mgnjdphs16 t248 = gm_f_one_or_two_lava5tu(t245, t246, t247);
            gm_te0mgnjdphs16 t249 = t248;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t249 + 0));
            GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t249 + 8));
        }
        goto me22;
mn241:;
        uint8_t t251 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t251 == ((uint8_t)46ULL))) goto mn250;
        {
            uint8_t t254 = gm_l2;
            uint8_t t255 = ((t254 == ((uint8_t)46ULL)));
            if (!t255) goto ie253;
            uint8_t t256 = gm_l3;
            uint8_t t257 = ((t256 == ((uint8_t)46ULL)));
            if (!t257) goto ie253;
            {
                gm_t9qkh0rsooc8d t259;
                GM_ST(uint8_t, ((uint8_t *)&t259 + 0), ((uint8_t)52ULL));
                gm_te0mgnjdphs16 t260;
                GM_ST(uint8_t, (uint8_t *)&t260 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t260 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t260 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t260 + 0), t259);
                GM_ST(uint64_t, ((uint8_t *)&t260 + 8), ((uint64_t)3ULL));
                gm_te0mgnjdphs16 t261 = t260;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t261 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t261 + 8));
            }
            goto ix253;
ie253:;
            uint8_t t263 = gm_l2;
            uint8_t t264 = ((t263 == ((uint8_t)46ULL)));
            if (!t264) goto ie262;
            uint8_t t265 = gm_l3;
            uint8_t t266 = ((t265 == ((uint8_t)61ULL)));
            if (!t266) goto ie262;
            {
                gm_t9qkh0rsooc8d t268;
                GM_ST(uint8_t, ((uint8_t *)&t268 + 0), ((uint8_t)51ULL));
                gm_te0mgnjdphs16 t269;
                GM_ST(uint8_t, (uint8_t *)&t269 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t269 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t269 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t269 + 0), t268);
                GM_ST(uint64_t, ((uint8_t *)&t269 + 8), ((uint64_t)3ULL));
                gm_te0mgnjdphs16 t270 = t269;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t270 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t270 + 8));
            }
            goto ix262;
ie262:;
            uint8_t t272 = gm_l2;
            uint8_t t273 = ((t272 == ((uint8_t)46ULL)));
            if (!t273) goto ie271;
            {
                gm_t9qkh0rsooc8d t275;
                GM_ST(uint8_t, ((uint8_t *)&t275 + 0), ((uint8_t)50ULL));
                gm_te0mgnjdphs16 t276;
                GM_ST(uint8_t, (uint8_t *)&t276 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t276 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t276 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t276 + 0), t275);
                GM_ST(uint64_t, ((uint8_t *)&t276 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t277 = t276;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t277 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t277 + 8));
            }
            goto ix271;
ie271:;
            {
                gm_t9qkh0rsooc8d t279;
                GM_ST(uint8_t, ((uint8_t *)&t279 + 0), ((uint8_t)58ULL));
                gm_l5 = t279;
            }
ix271:;
ix262:;
ix253:;
        }
        goto me22;
mn250:;
        uint8_t t281 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t281 == ((uint8_t)58ULL))) goto mn280;
        {
            uint8_t t284 = gm_l2;
            uint8_t t285 = ((t284 == ((uint8_t)58ULL)));
            if (!t285) goto ie283;
            {
                gm_t9qkh0rsooc8d t287;
                GM_ST(uint8_t, ((uint8_t *)&t287 + 0), ((uint8_t)54ULL));
                gm_te0mgnjdphs16 t288;
                GM_ST(uint8_t, (uint8_t *)&t288 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t288 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t288 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t288 + 0), t287);
                GM_ST(uint64_t, ((uint8_t *)&t288 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t289 = t288;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t289 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t289 + 8));
            }
            goto ix283;
ie283:;
            uint8_t t291 = gm_l2;
            uint8_t t292 = ((t291 == ((uint8_t)61ULL)));
            if (!t292) goto ie290;
            {
                gm_t9qkh0rsooc8d t294;
                GM_ST(uint8_t, ((uint8_t *)&t294 + 0), ((uint8_t)55ULL));
                gm_te0mgnjdphs16 t295;
                GM_ST(uint8_t, (uint8_t *)&t295 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t295 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t295 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t295 + 0), t294);
                GM_ST(uint64_t, ((uint8_t *)&t295 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t296 = t295;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t296 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t296 + 8));
            }
            goto ix290;
ie290:;
            {
                gm_t9qkh0rsooc8d t298;
                GM_ST(uint8_t, ((uint8_t *)&t298 + 0), ((uint8_t)56ULL));
                gm_l5 = t298;
            }
ix290:;
ix283:;
        }
        goto me22;
mn280:;
        uint8_t t300 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t300 == ((uint8_t)63ULL))) goto mn299;
        {
            gm_t9qkh0rsooc8d t302;
            GM_ST(uint8_t, ((uint8_t *)&t302 + 0), ((uint8_t)45ULL));
            gm_l5 = t302;
        }
        goto me22;
mn299:;
        uint8_t t304 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t304 == ((uint8_t)33ULL))) goto mn303;
        {
            gm_t9qkh0rsooc8d t306;
            GM_ST(uint8_t, ((uint8_t *)&t306 + 0), ((uint8_t)46ULL));
            gm_l5 = t306;
        }
        goto me22;
mn303:;
        uint8_t t308 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t308 == ((uint8_t)94ULL))) goto mn307;
        {
            gm_t9qkh0rsooc8d t310;
            GM_ST(uint8_t, ((uint8_t *)&t310 + 0), ((uint8_t)47ULL));
            gm_l5 = t310;
        }
        goto me22;
mn307:;
        uint8_t t312 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t312 == ((uint8_t)44ULL))) goto mn311;
        {
            gm_t9qkh0rsooc8d t314;
            GM_ST(uint8_t, ((uint8_t *)&t314 + 0), ((uint8_t)57ULL));
            gm_l5 = t314;
        }
        goto me22;
mn311:;
        uint8_t t316 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t316 == ((uint8_t)40ULL))) goto mn315;
        {
            gm_t9qkh0rsooc8d t318;
            GM_ST(uint8_t, ((uint8_t *)&t318 + 0), ((uint8_t)61ULL));
            gm_l5 = t318;
        }
        goto me22;
mn315:;
        uint8_t t320 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t320 == ((uint8_t)41ULL))) goto mn319;
        {
            gm_t9qkh0rsooc8d t322;
            GM_ST(uint8_t, ((uint8_t *)&t322 + 0), ((uint8_t)62ULL));
            gm_l5 = t322;
        }
        goto me22;
mn319:;
        uint8_t t324 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t324 == ((uint8_t)91ULL))) goto mn323;
        {
            gm_t9qkh0rsooc8d t326;
            GM_ST(uint8_t, ((uint8_t *)&t326 + 0), ((uint8_t)63ULL));
            gm_l5 = t326;
        }
        goto me22;
mn323:;
        uint8_t t328 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t328 == ((uint8_t)93ULL))) goto mn327;
        {
            gm_t9qkh0rsooc8d t330;
            GM_ST(uint8_t, ((uint8_t *)&t330 + 0), ((uint8_t)64ULL));
            gm_l5 = t330;
        }
        goto me22;
mn327:;
        uint8_t t332 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t332 == ((uint8_t)123ULL))) goto mn331;
        {
            gm_t9qkh0rsooc8d t334;
            GM_ST(uint8_t, ((uint8_t *)&t334 + 0), ((uint8_t)65ULL));
            gm_l5 = t334;
        }
        goto me22;
mn331:;
        uint8_t t336 = GM_LD(uint8_t, ((uint8_t *)&t21));
        if (!(t336 == ((uint8_t)125ULL))) goto mn335;
        {
            gm_t9qkh0rsooc8d t338;
            GM_ST(uint8_t, ((uint8_t *)&t338 + 0), ((uint8_t)66ULL));
            gm_l5 = t338;
        }
        goto me22;
mn335:;
        gm_taari1s0m51a7 t340;
        memset(&t340, 0, sizeof t340);
        uint8_t t341 = 0;
        F.ret = t340;
        goto gm_fret;
        goto me22;
mn339:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 435, 5);
me22:;
    }
    {
        void * t342 = gm_l0;
        void * t343 = gm_l0;
        uint64_t t344 = GM_LD(uint64_t, (((uint8_t *)t343) + 24));
        uint64_t t345 = gm_l6;
        uint64_t r346; bool o347 = __builtin_add_overflow(t344, t345, &r346);
        if (o347) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 530, 5);
        uint64_t t348 = r346;
        GM_ST(uint64_t, (((uint8_t *)t342) + 24), t348);
    }
    {
        gm_t9qkh0rsooc8d t349 = gm_l5;
        gm_taari1s0m51a7 t350;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t350), t349);
        ((uint8_t *)&t350)[1] = 1;
        uint8_t t351 = 0;
        F.ret = t350;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_second_word_end_dpnj7g8(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_second_word_end_dpnj7g8 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* from */
    gm_b16_8 gm_l2; /* word */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* b */
    uint64_t gm_l5; /* end */
    uint8_t gm_l6; /* c */
    GM_STACK_CHECK(&F, 421u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l1;
        gm_l3 = t2;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb3;
            {
                void * t6 = gm_l0;
                uint64_t t7 = gm_l3;
                gm_tqs5p4n2tq19v t8 = gm_f_byte_b4v8vul(t6, t7);
                uint8_t t9;
                if (((((uint8_t *)&t8))[1] != 0)) {
                    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
                    t9 = t10;
                } else {
                    goto lb3;
                }
                gm_l4 = t9;
            }
            {
                uint8_t t12 = gm_l4;
                gm_tqs5p4n2tq19v t13;
                GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)32ULL));
                GM_ST(uint8_t, ((uint8_t *)&t13 + 1), ((uint8_t)9ULL));
                uint8_t t14 = 0;
                if (gm_eqjmccqdb90f(((uint8_t *)&t13) + 0, ((uint8_t *)&t12))) t14 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t13) + 1, ((uint8_t *)&t12))) t14 = 1;
                uint8_t t15 = (uint8_t)(t14);
                if (!t15) goto ie11;
                {
                    uint64_t t17 = gm_l3;
                    uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
                    if (o19) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 273, 13);
                    uint64_t t20 = r18;
                    gm_l3 = t20;
                }
                goto ix11;
ie11:;
                uint8_t t22 = gm_l4;
                uint8_t t23 = ((t22 == ((uint8_t)47ULL)));
                if (!t23) goto ie21;
                void * t24 = gm_l0;
                uint64_t t25 = gm_l3;
                uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                if (o27) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 274, 40);
                uint64_t t28 = r26;
                gm_tqs5p4n2tq19v t29 = gm_f_byte_b4v8vul(t24, t28);
                gm_tqs5p4n2tq19v t30;
                GM_ST(uint8_t, ((uint8_t *)&t30), ((uint8_t)42ULL));
                ((uint8_t *)&t30)[1] = 1;
                uint8_t t31 = (gm_eqb44cb3cf1n(((uint8_t *)&t29), ((uint8_t *)&t30)));
                if (!t31) goto ie21;
                {
                    void * t33 = gm_l0;
                    gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t33) + 8));
                    uint64_t t35 = gm_l3;
                    gm_tf5srh4mp4b3i t36 = gm_f_inline_comment_end_8i5h1eq(t34, t35);
                    uint64_t t37;
                    if (((((uint8_t *)&t36))[8] != 0)) {
                        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&t36) + 0));
                        t37 = t38;
                    } else {
                        gm_tf5srh4mp4b3i t39;
                        memset(&t39, 0, sizeof t39);
                        uint8_t t40 = 0;
                        F.ret = t39;
                        goto gm_fret;
                    }
                    gm_l3 = t37;
                }
                goto ix21;
ie21:;
                {
                    goto lb3;
                }
ix21:;
ix11:;
            }
lc3:;
        }
lb3:;
    }
    {
        uint64_t t42 = gm_l3;
        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint64_t r44; bool o45 = __builtin_add_overflow(t42, t43, &r44);
        if (o45) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 280, 12);
        uint64_t t46 = r44;
        gm_l5 = t46;
    }
    {
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 8) + 8);
        void * t50 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 8)));
        uint64_t t51 = gm_l3;
        uint64_t t52 = (uintptr_t)t51;
        if (t52 > (uintptr_t)t49 || (uintptr_t)t49 > (uintptr_t)t49) gm_trap_slice(t52, (uintptr_t)t49, t49, gm_srcvmmksi0829pa, 281, 12);
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(((uint8_t *)t50 + t52 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)((uintptr_t)t49 - t52));
        gm_b16_8 t54 = gm_l2;
        uint8_t t55 = gm_f_starts_with_6vvt01e(t53, t54);
        uint8_t t56 = (uint8_t)!t55;
        if (!t56) goto ie47;
        {
            gm_tf5srh4mp4b3i t58;
            memset(&t58, 0, sizeof t58);
            uint8_t t59 = 0;
            F.ret = t58;
            goto gm_fret;
        }
        goto ix47;
ie47:;
ix47:;
    }
    {
        void * t61 = gm_l0;
        uint64_t t62 = gm_l5;
        gm_tqs5p4n2tq19v t63 = gm_f_byte_b4v8vul(t61, t62);
        if (!((((uint8_t *)&t63))[1] != 0)) goto ie60;
        uint8_t t64 = GM_LD(uint8_t, ((uint8_t *)&t63));
        gm_l6 = t64;
        {
            uint8_t t67 = gm_l6;
            uint8_t t68 = gm_f_is_ident_cont_317fpdb(t67);
            if (!t68) goto ie66;
            {
                gm_tf5srh4mp4b3i t70;
                memset(&t70, 0, sizeof t70);
                uint8_t t71 = 0;
                F.ret = t70;
                goto gm_fret;
            }
            goto ix66;
ie66:;
ix66:;
        }
        goto ix60;
ie60:;
ix60:;
    }
    {
        uint64_t t72 = gm_l5;
        gm_tf5srh4mp4b3i t73;
        GM_ST(uint8_t, (uint8_t *)&t73 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t73 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t73 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t73), t72);
        ((uint8_t *)&t73)[8] = 1;
        uint8_t t74 = 0;
        F.ret = t73;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_skip_trivia_014s7ai(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_skip_trivia_014s7ai F;
    void * gm_l0; /* lx */
    uint8_t gm_l1; /* b */
    gm_tqs5p4n2tq19v gm_l2; /* next */
    GM_STACK_CHECK(&F, 279u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t3 = gm_l0;
            gm_tqs5p4n2tq19v t4 = gm_f_peek_72bostn(t3);
            if (!((((uint8_t *)&t4))[1] != 0)) goto lb2;
            uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)&t4));
            gm_l1 = t5;
            {
                uint8_t t8 = gm_l1;
                uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)&t8));
                if (!(t13 == ((uint8_t)32ULL))) goto pa12;
                goto po11;
pa12:;
                uint8_t t15 = GM_LD(uint8_t, ((uint8_t *)&t8));
                if (!(t15 == ((uint8_t)9ULL))) goto pa14;
                goto po11;
pa14:;
                goto mn10;
po11:;
                {
                    void * t17 = gm_l0;
                    void * t18 = gm_l0;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 24));
                    uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
                    if (o21) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 166, 28);
                    uint64_t t22 = r20;
                    GM_ST(uint64_t, (((uint8_t *)t17) + 24), t22);
                }
                goto me9;
mn10:;
                uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)&t8));
                if (!(t24 == ((uint8_t)10ULL))) goto mn23;
                {
                    void * t26 = gm_l0;
                    void * t27 = gm_l0;
                    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)t27) + 24));
                    void * t29 = gm_l0;
                    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)t29) + 24));
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    if (o32) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 168, 41);
                    uint64_t t33 = r31;
                    gm_f_note_newline_cchbr7g(t26, t28, t33);
                }
                {
                    void * t34 = gm_l0;
                    void * t35 = gm_l0;
                    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 24));
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    if (o38) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 169, 17);
                    uint64_t t39 = r37;
                    GM_ST(uint64_t, (((uint8_t *)t34) + 24), t39);
                }
                goto me9;
mn23:;
                uint8_t t41 = GM_LD(uint8_t, ((uint8_t *)&t8));
                if (!(t41 == ((uint8_t)13ULL))) goto mn40;
                {
                    void * t44 = gm_l0;
                    gm_tqs5p4n2tq19v t45 = gm_f_peek_at_74p8lkq(t44, ((uint64_t)1ULL));
                    gm_tqs5p4n2tq19v t46;
                    GM_ST(uint8_t, ((uint8_t *)&t46), ((uint8_t)10ULL));
                    ((uint8_t *)&t46)[1] = 1;
                    uint8_t t47 = (gm_eqb44cb3cf1n(((uint8_t *)&t45), ((uint8_t *)&t46)));
                    if (!t47) goto ie43;
                    {
                        void * t49 = gm_l0;
                        void * t50 = gm_l0;
                        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 24));
                        void * t52 = gm_l0;
                        uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)t52) + 24));
                        uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)2ULL), &r54);
                        if (o55) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 173, 45);
                        uint64_t t56 = r54;
                        gm_f_note_newline_cchbr7g(t49, t51, t56);
                    }
                    {
                        void * t57 = gm_l0;
                        void * t58 = gm_l0;
                        uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)t58) + 24));
                        uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)2ULL), &r60);
                        if (o61) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 174, 21);
                        uint64_t t62 = r60;
                        GM_ST(uint64_t, (((uint8_t *)t57) + 24), t62);
                    }
                    goto ix43;
ie43:;
                    {
                        void * t64 = gm_l0;
                        gm_b0_1 t65 = gm_f_lone_cr_ktb0jvm((void *)NULL, cx, NULL, t64);
                        if (gm_unw.kind) {
                            goto gm_ll6;
                        }
                    }
ix43:;
                }
                goto me9;
mn40:;
                uint8_t t67 = GM_LD(uint8_t, ((uint8_t *)&t8));
                if (!(t67 == ((uint8_t)47ULL))) goto mn66;
                {
                    void * t69 = gm_l0;
                    gm_tqs5p4n2tq19v t70 = gm_f_peek_at_74p8lkq(t69, ((uint64_t)1ULL));
                    gm_l2 = t70;
                }
                {
                    gm_tqs5p4n2tq19v t72 = gm_l2;
                    gm_tqs5p4n2tq19v t73;
                    GM_ST(uint8_t, ((uint8_t *)&t73), ((uint8_t)47ULL));
                    ((uint8_t *)&t73)[1] = 1;
                    uint8_t t74 = (gm_eqb44cb3cf1n(((uint8_t *)&t72), ((uint8_t *)&t73)));
                    if (!t74) goto ie71;
                    {
                        void * t76 = gm_l0;
                        gm_b0_1 t77 = gm_f_line_comment_s71u8hq((void *)NULL, cx, NULL, t76);
                        if (gm_unw.kind) {
                            goto gm_ll6;
                        }
                    }
                    goto ix71;
ie71:;
                    gm_tqs5p4n2tq19v t79 = gm_l2;
                    gm_tqs5p4n2tq19v t80;
                    GM_ST(uint8_t, ((uint8_t *)&t80), ((uint8_t)42ULL));
                    ((uint8_t *)&t80)[1] = 1;
                    uint8_t t81 = (gm_eqb44cb3cf1n(((uint8_t *)&t79), ((uint8_t *)&t80)));
                    if (!t81) goto ie78;
                    {
                        void * t83 = gm_l0;
                        gm_b0_1 t84 = gm_f_block_comment_fvce7qt((void *)NULL, cx, NULL, t83);
                        if (gm_unw.kind) {
                            goto gm_ll6;
                        }
                    }
                    goto ix78;
ie78:;
                    {
                        gm_b0_1 t86;
                        uint8_t t87 = 0;
                        F.ret = t86;
                        goto gm_fret;
                    }
ix78:;
ix71:;
                }
                goto me9;
mn66:;
                gm_b0_1 t89;
                uint8_t t90 = 0;
                F.ret = t89;
                goto gm_fret;
                goto me9;
mn88:;
                gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 165, 9);
me9:;
            }
lc2:;
        }
gm_ll6:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
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
extern __attribute__((aligned(4))) uint64_t gm_f_slot_4oe07nr(gm_t9qkh0rsooc8d a0);
extern __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_tg7cev6(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_continuation_n3333i7(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_continuation_n3333i7 F;
    gm_t3fcbuehh3bs2 gm_l0; /* kind */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)2ULL)) goto mn3;
    gm_t9qkh0rsooc8d t5 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    gm_t9qkh0rsooc8d t6 = gm_l1;
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)7ULL));
    gm_t9qkh0rsooc8d t8;
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)20ULL));
    gm_tk22flr9mh3ql t10;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 0), t7);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 1), t8);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 2), t9);
    uint8_t t11 = 0;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t10) + 0, ((uint8_t *)&t6))) t11 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t10) + 1, ((uint8_t *)&t6))) t11 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t10) + 2, ((uint8_t *)&t6))) t11 = 1;
    uint8_t t12 = (uint8_t)(t11);
    t1 = t12;
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn13:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 703, 55);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_starts_continuation_n3333i7_c(gm_t3fcbuehh3bs2 a0) { return gm_f_starts_continuation_n3333i7(&a0); }
inline __attribute__((aligned(4))) uint8_t gm_f_suppresses_newline_after_o08ord2(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_suppresses_newline_after_o08ord2 F;
    gm_t3fcbuehh3bs2 gm_l0; /* kind */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    gm_t9qkh0rsooc8d gm_l2; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 78u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma5; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma5; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma4; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma5; default: goto ma5; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t9 != ((uint8_t)2ULL)) goto mn8;
    gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l1 = t10;
    gm_t9qkh0rsooc8d t11 = gm_l1;
    gm_t9qkh0rsooc8d t12;
    GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)16ULL));
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)15ULL));
    gm_t9qkh0rsooc8d t14;
    GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t15;
    GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)20ULL));
    gm_t9qkh0rsooc8d t16;
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)7ULL));
    gm_t724v60om7sh5 t17;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 0), t12);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t13);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 2), t14);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 3), t15);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 4), t16);
    uint8_t t18 = 0;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t17) + 0, ((uint8_t *)&t11))) t18 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t17) + 1, ((uint8_t *)&t11))) t18 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t17) + 2, ((uint8_t *)&t11))) t18 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t17) + 3, ((uint8_t *)&t11))) t18 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t17) + 4, ((uint8_t *)&t11))) t18 = 1;
    uint8_t t19 = (uint8_t)(t18);
    t1 = t19;
    goto me2;
mn8:;
ma4:;
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t21 != ((uint8_t)8ULL)) goto mn20;
    gm_t9qkh0rsooc8d t22 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l2 = t22;
    gm_t9qkh0rsooc8d t23 = gm_l2;
    gm_t9qkh0rsooc8d t24;
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)45ULL));
    gm_t9qkh0rsooc8d t25;
    GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)46ULL));
    gm_t9qkh0rsooc8d t26;
    GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)47ULL));
    gm_t9qkh0rsooc8d t27;
    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)48ULL));
    gm_t9qkh0rsooc8d t28;
    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)49ULL));
    gm_t9qkh0rsooc8d t29;
    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)52ULL));
    gm_t9qkh0rsooc8d t30;
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)53ULL));
    gm_t9qkh0rsooc8d t31;
    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)62ULL));
    gm_t9qkh0rsooc8d t32;
    GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)64ULL));
    gm_t9qkh0rsooc8d t33;
    GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)66ULL));
    gm_tfh78knfg7urq t34;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 0), t24);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 1), t25);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 2), t26);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 3), t27);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 4), t28);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 5), t29);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 6), t30);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 7), t31);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 8), t32);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 9), t33);
    uint8_t t35 = 0;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t34) + 0, ((uint8_t *)&t23))) t35 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t34) + 1, ((uint8_t *)&t23))) t35 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t34) + 2, ((uint8_t *)&t23))) t35 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t34) + 3, ((uint8_t *)&t23))) t35 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t34) + 4, ((uint8_t *)&t23))) t35 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t34) + 5, ((uint8_t *)&t23))) t35 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t34) + 6, ((uint8_t *)&t23))) t35 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t34) + 7, ((uint8_t *)&t23))) t35 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t34) + 8, ((uint8_t *)&t23))) t35 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t34) + 9, ((uint8_t *)&t23))) t35 = 1;
    uint8_t t36 = (uint8_t)(!t35);
    t1 = t36;
    goto me2;
mn20:;
ma5:;
    t1 = ((uint8_t)0);
    goto me2;
mn37:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 691, 60);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_suppresses_newline_after_o08ord2_c(gm_t3fcbuehh3bs2 a0) { return gm_f_suppresses_newline_after_o08ord2(&a0); }
__attribute__((aligned(4))) gm_b0_1 gm_f_unexpected_char_r3t8d4c(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unexpected_char_r3t8d4c F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    gm_tb31k5l794t7p gm_l2; /* c */
    gm_b16_8 gm_l3; /* message */
    uint32_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 671u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_tb31k5l794t7p t5 = GM_CONV(gm_tb31k5l794t7p, gm_f_current_char_ctktsl2(t4));
        gm_l2 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 24));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t r10; bool o11 = __builtin_add_overflow(t8, t9, &r10);
        if (o11) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 403, 5);
        uint64_t t12 = r10;
        GM_ST(uint64_t, (((uint8_t *)t6) + 24), t12);
    }
    {
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(0));
        gm_l3 = t13;
    }
    {
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        uint8_t t16 = (t15 >= ((uint32_t)128ULL));
        if (!t16) goto ie14;
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        uint8_t t18 = gm_f_is_unicode_space_b2m9rku(t17);
        if (!t18) goto ie14;
        {
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
            gm_t88rcucg1b7uk t21;
            GM_ST(uint32_t, ((uint8_t *)&t21 + 0), t20);
            gm_tabb710d9mq2e t22;
            gm_f_format_t4ivgd1((void *)&t22, (void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_srcvmmksi0829pa, 406, 19);
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
            gm_l3 = t24;
        }
        goto ix14;
ie14:;
        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        uint8_t t27 = (t26 >= ((uint32_t)128ULL));
        if (!t27) goto ie25;
        {
            gm_tommf4j9upomc t29;
            memset((uint8_t *)&t29 + 20, 0, 4);
            void * t30 = gm_l0;
            uint64_t t31 = GM_LD(uintptr_t, (((uint8_t *)t30) + 8) + 8);
            void * t32 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t30) + 8)));
            uint64_t t33 = gm_l1;
            uint64_t t34 = (uintptr_t)t33;
            void * t35 = gm_l0;
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 24));
            uint64_t t37 = (uintptr_t)t36;
            if (t34 > t37 || t37 > (uintptr_t)t31) gm_trap_slice(t34, t37, t31, gm_srcvmmksi0829pa, 410, 13);
            gm_b16_8 t38;
            GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t32 + t34 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t34));
            GM_ST(gm_b16_8, (uint8_t *)&t29 + 0, t38);
            uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
            GM_ST(uint32_t, (uint8_t *)&t29 + 16, t39);
            gm_tabb710d9mq2e t40;
            gm_f_format_pscs6r9((void *)&t40, (void *)NULL, cx, NULL, &t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_srcvmmksi0829pa, 408, 19);
            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_l3 = t42;
        }
        goto ix25;
ie25:;
        {
            uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
            gm_l4 = t45;
        }
        uint8_t t46 = 0;
        uint32_t t47 = gm_l4;
        uint8_t t48 = (((uint32_t)33ULL) <= t47);
        if (t48) {
            uint32_t t49 = gm_l4;
            uint8_t t50 = (t49 <= ((uint32_t)126ULL));
            t46 = t50;
        } else t46 = 0;
        uint8_t t51 = t46;
        if (!t51) goto ie43;
        {
            void * t53 = gm_l0;
            uint64_t t54 = GM_LD(uintptr_t, (((uint8_t *)t53) + 8) + 8);
            void * t55 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t53) + 8)));
            uint64_t t56 = gm_l1;
            uint64_t t57 = (uintptr_t)t56;
            void * t58 = gm_l0;
            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)t58) + 24));
            uint64_t t60 = (uintptr_t)t59;
            if (t57 > t60 || t60 > (uintptr_t)t54) gm_trap_slice(t57, t60, t54, gm_srcvmmksi0829pa, 414, 59);
            gm_b16_8 t61;
            GM_ST(void *, (uint8_t *)&t61, (void *)(((uint8_t *)t55 + t57 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(t60 - t57));
            gm_tnpifv20irnph t62;
            GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t61);
            gm_tabb710d9mq2e t63;
            gm_f_format_fuvjr35((void *)&t63, (void *)NULL, cx, NULL, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srcvmmksi0829pa, 414, 19);
            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
            gm_l3 = t65;
        }
        goto ix43;
ie43:;
        {
            uint32_t t67 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
            gm_t88rcucg1b7uk t68;
            GM_ST(uint32_t, ((uint8_t *)&t68 + 0), t67);
            gm_tabb710d9mq2e t69;
            gm_f_format_i801868((void *)&t69, (void *)NULL, cx, NULL, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 8, gm_srcvmmksi0829pa, 416, 19);
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 8));
            gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
            gm_l3 = t71;
        }
ix43:;
ix25:;
ix14:;
    }
    {
        void * t72 = gm_l0;
        uint64_t t73 = gm_l1;
        void * t74 = gm_l0;
        uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)t74) + 24));
        gm_b16_8 t76 = gm_l3;
        gm_b0_1 t77 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t72, t73, t75, t76);
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
