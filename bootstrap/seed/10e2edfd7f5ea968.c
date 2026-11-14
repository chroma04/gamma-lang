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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; uint64_t v1; uint64_t v2; } f1; } gm_tu0a56t4bfvqh;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t p3[6]; gm_b16_8 f3; void * f4; void * f5; gm_tl874t2atnlh3 f6; uint8_t pe[4]; } gm_t24301fas1b2a;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; } gm_t2p74ki8sl4an;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_tb59ptmmofnb5;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_tn68el8d14vku v1; } f1; } gm_tsncjtib3j1l1;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t pe[3]; } gm_tt7dbdq4n1egc;
_Static_assert(sizeof(gm_tu0a56t4bfvqh) == 16 && _Alignof(gm_tu0a56t4bfvqh) == 8 && offsetof(gm_tu0a56t4bfvqh, f0) == 0 && offsetof(gm_tu0a56t4bfvqh, f1) == 8, "layout of `Base`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_tsncjtib3j1l1 f0; uint8_t f1; uint8_t pe[7]; } gm_t1335qftqvd8f;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t24301fas1b2a) == 72 && _Alignof(gm_t24301fas1b2a) == 8 && offsetof(gm_t24301fas1b2a, f0) == 0 && offsetof(gm_t24301fas1b2a, f1) == 16 && offsetof(gm_t24301fas1b2a, f2) == 17 && offsetof(gm_t24301fas1b2a, f3) == 24 && offsetof(gm_t24301fas1b2a, f4) == 40 && offsetof(gm_t24301fas1b2a, f5) == 48 && offsetof(gm_t24301fas1b2a, f6) == 56, "layout of `FieldDecl`");
_Static_assert(sizeof(gm_t2p74ki8sl4an) == 24 && _Alignof(gm_t2p74ki8sl4an) == 4 && offsetof(gm_t2p74ki8sl4an, f0) == 0 && offsetof(gm_t2p74ki8sl4an, f1) == 12, "layout of `(Span, Span)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; } gm_t6ok5tvcjc200;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_tf1bjg81u0glv f0; uint8_t f1; uint8_t pe[7]; } gm_t8sds1gdern44;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; void * v1; } f1; } gm_tapdr2ag17fh7;
_Static_assert(sizeof(gm_tb59ptmmofnb5) == 24 && _Alignof(gm_tb59ptmmofnb5) == 8 && offsetof(gm_tb59ptmmofnb5, f0) == 0 && offsetof(gm_tb59ptmmofnb5, f1) == 16 && offsetof(gm_tb59ptmmofnb5, f2) == 20, "layout of `LocalFact`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
typedef struct { gm_trqfvmjtt4sij f0; uint32_t f1; uint8_t pe[4]; } gm_tfqiukge1b9q0;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { gm_tt7dbdq4n1egc f0; uint8_t f1; uint8_t pe[3]; } gm_tkol5reu9t1u2;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_th2lqbo77pvsc f3; } gm_tl5cuhjfnc4cr;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; void * f2; gm_th2lqbo77pvsc f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tlivgedjs5ps3;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tnljbma3s3h16;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_tkn42u7na3t81 f0; uint8_t f1; uint8_t pe[3]; } gm_tpk1phrp285m3;
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; } gm_ts686ckv8ehm2;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsncjtib3j1l1) == 24 && _Alignof(gm_tsncjtib3j1l1) == 8 && offsetof(gm_tsncjtib3j1l1, f0) == 0 && offsetof(gm_tsncjtib3j1l1, f1) == 8, "layout of `DeclRef`");
_Static_assert(sizeof(gm_tt7dbdq4n1egc) == 28 && _Alignof(gm_tt7dbdq4n1egc) == 4 && offsetof(gm_tt7dbdq4n1egc, f0) == 0 && offsetof(gm_tt7dbdq4n1egc, f1) == 12 && offsetof(gm_tt7dbdq4n1egc, f2) == 24, "layout of `(span: Span, operand: Span, same: bool)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
typedef struct { gm_tb59ptmmofnb5 f0; uint8_t f1; uint8_t pe[7]; } gm_tvsjp38cd9njc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnljbma3s3h16 v0; gm_t7bjbmukv26e7 v1; } f1; } gm_t03a3imodqi32;
_Static_assert(sizeof(gm_t1335qftqvd8f) == 32 && _Alignof(gm_t1335qftqvd8f) == 8 && offsetof(gm_t1335qftqvd8f, f0) == 0 && offsetof(gm_t1335qftqvd8f, f1) == 24, "layout of `?DeclRef`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
typedef struct { gm_tfqiukge1b9q0 f0; uint8_t f1; uint8_t pe[7]; } gm_t3jh0etmvddmg;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { gm_t1s75i08qtptd f0; gm_t08olq97rscn3 f1; gm_t79rbdv4rafe5 f2; } gm_t43d6nbdfap6p;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t6ok5tvcjc200) == 40 && _Alignof(gm_t6ok5tvcjc200) == 8 && offsetof(gm_t6ok5tvcjc200, f0) == 0 && offsetof(gm_t6ok5tvcjc200, f1) == 16, "layout of `Key`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_t6ok5tvcjc200 f1; } gm_t70bah4iduq3d;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8sds1gdern44) == 32 && _Alignof(gm_t8sds1gdern44) == 8 && offsetof(gm_t8sds1gdern44, f0) == 0 && offsetof(gm_t8sds1gdern44, f1) == 24, "layout of `?(usize, string)`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_tapdr2ag17fh7) == 48 && _Alignof(gm_tapdr2ag17fh7) == 8 && offsetof(gm_tapdr2ag17fh7, f0) == 0 && offsetof(gm_tapdr2ag17fh7, f1) == 8, "layout of `WhenElse`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t24301fas1b2a v0; gm_t7bjbmukv26e7 v1; } f1; } gm_tfc3oljlofib6;
typedef struct { gm_tl5cuhjfnc4cr f0; gm_tl874t2atnlh3 f1; uint32_t f2; uint32_t f3; uint8_t pe[4]; } gm_tfgiovingnnea;
_Static_assert(sizeof(gm_tfqiukge1b9q0) == 48 && _Alignof(gm_tfqiukge1b9q0) == 8 && offsetof(gm_tfqiukge1b9q0, f0) == 0 && offsetof(gm_tfqiukge1b9q0, f1) == 40, "layout of `(q: List(u32), r: u32)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tkol5reu9t1u2) == 32 && _Alignof(gm_tkol5reu9t1u2) == 4 && offsetof(gm_tkol5reu9t1u2, f0) == 0 && offsetof(gm_tkol5reu9t1u2, f1) == 28, "layout of `?Found`");
_Static_assert(sizeof(gm_tl5cuhjfnc4cr) == 64 && _Alignof(gm_tl5cuhjfnc4cr) == 8 && offsetof(gm_tl5cuhjfnc4cr, f0) == 0 && offsetof(gm_tl5cuhjfnc4cr, f1) == 16 && offsetof(gm_tl5cuhjfnc4cr, f2) == 24 && offsetof(gm_tl5cuhjfnc4cr, f3) == 40, "layout of `Item`");
_Static_assert(sizeof(gm_tlivgedjs5ps3) == 72 && _Alignof(gm_tlivgedjs5ps3) == 8 && offsetof(gm_tlivgedjs5ps3, f0) == 0 && offsetof(gm_tlivgedjs5ps3, f1) == 16 && offsetof(gm_tlivgedjs5ps3, f2) == 24 && offsetof(gm_tlivgedjs5ps3, f3) == 32 && offsetof(gm_tlivgedjs5ps3, f4) == 56, "layout of `FnSig`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tnljbma3s3h16) == 64 && _Alignof(gm_tnljbma3s3h16) == 8 && offsetof(gm_tnljbma3s3h16, f0) == 0 && offsetof(gm_tnljbma3s3h16, f1) == 32 && offsetof(gm_tnljbma3s3h16, f2) == 40 && offsetof(gm_tnljbma3s3h16, f3) == 48, "layout of `Variant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpk1phrp285m3) == 12 && _Alignof(gm_tpk1phrp285m3) == 4 && offsetof(gm_tpk1phrp285m3, f0) == 0 && offsetof(gm_tpk1phrp285m3, f1) == 8, "layout of `?TypeFact`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts686ckv8ehm2) == 40 && _Alignof(gm_ts686ckv8ehm2) == 8 && offsetof(gm_ts686ckv8ehm2, f0) == 0 && offsetof(gm_ts686ckv8ehm2, f1) == 32, "layout of `(name: Ident, ty: ^Expr)`");
typedef struct { void * f0; void * f1; void * f2; uint64_t f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_t4c7ipjj9s3fp f6; gm_t4c7ipjj9s3fp f7; gm_t4c7ipjj9s3fp f8; gm_t1335qftqvd8f f9; } gm_tt5bocau6bbvj;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; gm_ts686ckv8ehm2 v1; } f1; } gm_ttja7ii61nila;
_Static_assert(sizeof(gm_tvsjp38cd9njc) == 32 && _Alignof(gm_tvsjp38cd9njc) == 8 && offsetof(gm_tvsjp38cd9njc, f0) == 0 && offsetof(gm_tvsjp38cd9njc, f1) == 24, "layout of `?LocalFact`");
_Static_assert(sizeof(gm_t03a3imodqi32) == 72 && _Alignof(gm_t03a3imodqi32) == 8 && offsetof(gm_t03a3imodqi32, f0) == 0 && offsetof(gm_t03a3imodqi32, f1) == 8, "layout of `UnionMember`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t3jh0etmvddmg) == 56 && _Alignof(gm_t3jh0etmvddmg) == 8 && offsetof(gm_t3jh0etmvddmg, f0) == 0 && offsetof(gm_t3jh0etmvddmg, f1) == 48, "layout of `?Scope`");
_Static_assert(sizeof(gm_t43d6nbdfap6p) == 112 && _Alignof(gm_t43d6nbdfap6p) == 8 && offsetof(gm_t43d6nbdfap6p, f0) == 0 && offsetof(gm_t43d6nbdfap6p, f1) == 40 && offsetof(gm_t43d6nbdfap6p, f2) == 72, "layout of `WhileStmt`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t70bah4iduq3d) == 48 && _Alignof(gm_t70bah4iduq3d) == 8 && offsetof(gm_t70bah4iduq3d, f0) == 0 && offsetof(gm_t70bah4iduq3d, f1) == 8, "layout of `(kind: Kind, hover: Hover)`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tfc3oljlofib6) == 80 && _Alignof(gm_tfc3oljlofib6) == 8 && offsetof(gm_tfc3oljlofib6, f0) == 0 && offsetof(gm_tfc3oljlofib6, f1) == 8, "layout of `StructMember`");
_Static_assert(sizeof(gm_tfgiovingnnea) == 88 && _Alignof(gm_tfgiovingnnea) == 8 && offsetof(gm_tfgiovingnnea, f0) == 0 && offsetof(gm_tfgiovingnnea, f1) == 64 && offsetof(gm_tfgiovingnnea, f2) == 76 && offsetof(gm_tfgiovingnnea, f3) == 80, "layout of `LocalScope`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
_Static_assert(sizeof(gm_tt5bocau6bbvj) == 312 && _Alignof(gm_tt5bocau6bbvj) == 8 && offsetof(gm_tt5bocau6bbvj, f0) == 0 && offsetof(gm_tt5bocau6bbvj, f1) == 8 && offsetof(gm_tt5bocau6bbvj, f2) == 16 && offsetof(gm_tt5bocau6bbvj, f3) == 24 && offsetof(gm_tt5bocau6bbvj, f4) == 32 && offsetof(gm_tt5bocau6bbvj, f5) == 72 && offsetof(gm_tt5bocau6bbvj, f6) == 112 && offsetof(gm_tt5bocau6bbvj, f7) == 168 && offsetof(gm_tt5bocau6bbvj, f8) == 224 && offsetof(gm_tt5bocau6bbvj, f9) == 280, "layout of `Walker`");
_Static_assert(sizeof(gm_ttja7ii61nila) == 48 && _Alignof(gm_ttja7ii61nila) == 8 && offsetof(gm_ttja7ii61nila, f0) == 0 && offsetof(gm_ttja7ii61nila, f1) == 8, "layout of `IfElse`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
typedef struct { gm_t6qooom8qa2do f0; uint8_t f1; uint8_t pe[7]; } gm_tlabdfckddbfd;
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_tlabdfckddbfd) == 216 && _Alignof(gm_tlabdfckddbfd) == 8 && offsetof(gm_tlabdfckddbfd, f0) == 0 && offsetof(gm_tlabdfckddbfd, f1) == 208, "layout of `?DeclValue`");
struct gm_fr_add_ref_tgbe5vc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_arg_amoajjv {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* e */
    gm_tc3oht4ksth9d l3; /* e */
    gm_tc3oht4ksth9d l4; /* e */
    gm_b0_1 ret;
};
struct gm_fr_base_of_9tbrpvr {
    gm_frame h;
    gm_t6qooom8qa2do l3; /* v */
    gm_tu0a56t4bfvqh ret;
};
struct gm_fr_block_jfodv9u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_call_8vfr61j {
    gm_frame h;
    gm_t5v0c8ia6a5mo l26; /* p */
    gm_b0_1 ret;
};
struct gm_fr_classify_pdaj2gb {
    gm_frame h;
    gm_tu0a56t4bfvqh ret;
};
struct gm_fr_cond_d94lif5 {
    gm_frame h;
    gm_t0d02142k4tu8 l5; /* pat */
    gm_b0_1 ret;
};
struct gm_fr_contains_2dnkft5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_475f687 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_cra391b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_decl_name_span_ht2nhb5 {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_declare_feg3ue0 {
    gm_frame h;
    gm_tl5cuhjfnc4cr l10; /* item */
    gm_b0_1 ret;
};
struct gm_fr_def_once_pqgvduh {
    gm_frame h;
    gm_t6ok5tvcjc200 l2; /* h */
    gm_b0_1 ret;
};
struct gm_fr_effect_lit_26tkk47 {
    gm_frame h;
    gm_tv2uf4leil5hq l3; /* b */
    void * ret;
};
struct gm_fr_empty_slice_1ivdmti {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_537clvp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6ld1tun {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8td41o0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_cln8ptp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jdnmbru {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lv6i4jh_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n5p0uct {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ensure_hover_3epdjmk {
    gm_frame h;
    gm_t70bah4iduq3d l5; /* info */
    gm_t70bah4iduq3d l8; /* info */
    gm_b0_1 ret;
};
struct gm_fr_expr_kih3eo8 {
    gm_frame h;
    void * l0; /* w */
    gm_t1s75i08qtptd l2; /* label */
    gm_tioi3jh9v22g5 l5; /* x */
    gm_tlivgedjs5ps3 l18; /* sg */
    gm_t79rbdv4rafe5 l28; /* b */
    gm_tchr52g0ema53 l29; /* x */
    gm_b0_1 ret;
};
struct gm_fr_field_7p9pmjo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_field_use_838f8e8 {
    gm_frame h;
    gm_tpautr4br7h3l l9; /* def */
    uint8_t ret;
};
struct gm_fr_find_field_l0p9325 {
    gm_frame h;
    gm_tfc3oljlofib6 l5; /* m */
    gm_t24301fas1b2a l6; /* f */
    gm_t7bjbmukv26e7 l10; /* wh */
    gm_b0_1 ret;
};
struct gm_fr_find_payload_field_i16vrf1 {
    gm_frame h;
    gm_t03a3imodqi32 l6; /* m */
    gm_tnljbma3s3h16 l7; /* v */
    gm_tv1o1j44ii4db l12; /* el */
    gm_t7bjbmukv26e7 l14; /* wh */
    gm_b0_1 ret;
};
struct gm_fr_find_variant_kifk1mm {
    gm_frame h;
    gm_t03a3imodqi32 l5; /* m */
    gm_tnljbma3s3h16 l6; /* v */
    gm_t7bjbmukv26e7 l7; /* wh */
    gm_b0_1 ret;
};
struct gm_fr_fn_rqpk5og {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_use_l994th9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_expr_f16jm2b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_map_7pa51u7 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_oh3lgki {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_s7cd6p5 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_if_expr_6oqub48 {
    gm_frame h;
    gm_ttja7ii61nila l6; /* el */
    gm_t79rbdv4rafe5 l7; /* b */
    gm_b0_1 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_label_use_9lcr9t0 {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_b0_1 ret;
};
struct gm_fr_list_nrf6taq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rk274m7_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ubba23m_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uc1atd0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_binding_25t1g13 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_lookup_decl_10pnl6c {
    gm_frame h;
    gm_t1335qftqvd8f ret;
};
struct gm_fr_lookup_local_tdg7skl {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_match_expr_j2bvhsh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_member_def_9f4kiah {
    gm_frame h;
    gm_tv2uf4leil5hq l5; /* b */
    gm_toceffb697p0u ret;
};
struct gm_fr_name_use_73pph7t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_note_receiver_6mb4kc5 {
    gm_frame h;
    gm_t6qooom8qa2do l6; /* v */
    gm_trqfvmjtt4sij l9; /* items */
    gm_b0_1 ret;
};
struct gm_fr_operation_use_ue1or58 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_path_type_flfaob1 {
    gm_frame h;
    gm_t6qooom8qa2do l5; /* v */
    gm_t6qooom8qa2do l9; /* v */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_pattern_jo09k6f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_payload_field_3o532rn {
    gm_frame h;
    gm_tv2uf4leil5hq l5; /* b */
    gm_b0_1 ret;
};
struct gm_fr_pop_amp5ob6 {
    gm_frame h;
    gm_t1s75i08qtptd ret;
};
struct gm_fr_pop_g2usrvd_2 {
    gm_frame h;
    gm_tfqiukge1b9q0 l2; /* x */
    gm_t3jh0etmvddmg ret;
};
struct gm_fr_pop_label_e8oj3h3 {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_b0_1 ret;
};
struct gm_fr_pop_scope_vgka5ae {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_bb3f68j {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_label_gg2j5c3 {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_b0_1 ret;
};
struct gm_fr_push_pcqb56b_2 {
    gm_frame h;
    gm_tfqiukge1b9q0 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rb04a8h {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_scope_u4jfguj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_push_sige3eu {
    gm_frame h;
    gm_tfgiovingnnea l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_ref_key_vam5ppg {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_regrow_8skjmj4_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_b5mmd35 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_cvgrcrj_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_q6son6o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_rgq4vgc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_sig_89f514c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_stmt_ald930b {
    gm_frame h;
    gm_tv2uf4leil5hq l2; /* binding */
    gm_t43d6nbdfap6p l12; /* x */
    gm_t0d02142k4tu8 l13; /* pat */
    gm_t79rbdv4rafe5 l17; /* b */
    gm_b0_1 ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_type_literal_nnom1cv {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b0_1 ret;
};
struct gm_fr_type_of_tiqb21n {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_use_decl_sfao4j5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_variant_at_9i78oa0 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* tys */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_variant_payload_3id0koh {
    gm_frame h;
    gm_tckdobunaulee l10; /* v */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_variant_use_29ooua3 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_walker_8qtbjaj {
    gm_frame h;
    gm_tt5bocau6bbvj l4; /* w */
    gm_t6qooom8qa2do l8; /* v */
    gm_tt5bocau6bbvj ret;
};
struct gm_fr_when_stmt_r5g5fsm {
    gm_frame h;
    gm_tapdr2ag17fh7 l2; /* el */
    gm_t79rbdv4rafe5 l3; /* b */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_ref_tgbe5vc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_adt_decl_of_2bflsov(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_arg_amoajjv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tu0a56t4bfvqh gm_f_base_of_9tbrpvr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_block_jfodv9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_call_8vfr61j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tu0a56t4bfvqh gm_f_classify_pdaj2gb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_cond_d94lif5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_2dnkft5(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_475f687(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_cra391b(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_decl_done_mk70ol9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_decl_info_ahk1bni(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_decl_name_span_ht2nhb5(void * a0, const void * a1);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_decl_name_span_ht2nhb5_c(void * a0, gm_tsncjtib3j1l1 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_feg3ue0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2, uint32_t a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_def_once_pqgvduh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void * gm_f_effect_lit_26tkk47(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1ivdmti(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_537clvp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6ld1tun(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8td41o0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cln8ptp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jdnmbru(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lv6i4jh_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5p0uct(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_ensure_hover_3epdjmk(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_expr_kih3eo8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_field_7p9pmjo(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, const void * a3);
__attribute__((aligned(4))) uint8_t gm_f_field_use_838f8e8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_1ff6h79(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_bhdmq1t(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_find_field_l0p9325(gm_b16_8 a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_krs565o(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_find_payload_field_i16vrf1(gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) void gm_f_find_variant_kifk1mm(gm_b16_8 a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_literal_sodo3l0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rqpk5og(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_use_l994th9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_for_each_child_tkrfoqu(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_expr_f16jm2b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_ahgk48j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ekcps7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_1ji4too(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_b2m4si4(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tpk1phrp285m3 gm_f_get_o4mipv6(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_get_o6c8ntf(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_rlnefnq(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_get_shqk64n(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_handler_literal_bbdui98(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_7pa51u7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_oh3lgki(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_s7cd6p5(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_if_expr_6oqub48(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_label_use_9lcr9t0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_nrf6taq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rk274m7_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ubba23m_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uc1atd0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_local_binding_25t1g13(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_lookup_decl_10pnl6c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_lookup_local_tdg7skl(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_match_expr_j2bvhsh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_member_def_9f4kiah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_module_members_26b4s91(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_name_use_73pph7t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_receiver_6mb4kc5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_op_info_4a09t36(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_operation_members_62l824c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_operation_use_ue1or58(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_params_ufsghpd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_path_type_flfaob1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_pattern_jo09k6f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, uint32_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_payload_field_3o532rn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_amp5ob6(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_g2usrvd_2(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_pop_label_e8oj3h3(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_pop_label_e8oj3h3_c(void * a0, gm_t1s75i08qtptd a1);
inline __attribute__((aligned(4))) void gm_f_pop_scope_vgka5ae(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bb3f68j(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_label_gg2j5c3(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pcqb56b_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rb04a8h(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_u4jfguj(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_sige3eu(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_1rbrft2(void * a0, uint64_t a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_hku1qp6(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_rt5d34n(void * a0, gm_cseahvg8v7p0vs a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_sokl06f(void * a0, uint32_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_ss3ma85(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint64_t gm_f_ref_key_vam5ppg(const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_ref_key_vam5ppg_c(gm_tsncjtib3j1l1 a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_b5mmd35(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_cvgrcrj_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q6son6o(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_rgq4vgc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_sig_89f514c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_sig_rest_gkcl8d2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_fjcc76p(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_stmt_ald930b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_type_lit_lm0h7k6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_type_literal_nnom1cv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_tiqb21n(void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_use_decl_sfao4j5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_variant_at_9i78oa0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_variant_members_nesi2hv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_variant_payload_3id0koh(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_variant_use_29ooua3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) void gm_f_walker_8qtbjaj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_when_stmt_r5g5fsm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s4g2k7ups3ui6[6];
extern const uint8_t gm_ses7tdrdt8a6u[6];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcb2hdor4f6g0q[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcrgc9bkquf6nr[];
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_2dnkft5(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_2dnkft5 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* k */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_bhdmq1t(t2, t3);
        uint8_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) goto mn7;
        t5 = ((uint8_t)0);
        goto me6;
mn7:;
        t5 = ((uint8_t)1);
        goto me6;
mn8:;
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 153, 12);
me6:;
        uint8_t t9 = 0;
        F.ret = t5;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_475f687(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_475f687 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* k */
    GM_STACK_CHECK(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_krs565o(t2, t3);
        uint8_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) goto mn7;
        t5 = ((uint8_t)0);
        goto me6;
mn7:;
        t5 = ((uint8_t)1);
        goto me6;
mn8:;
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 153, 12);
me6:;
        uint8_t t9 = 0;
        F.ret = t5;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_cra391b(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_cra391b F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* k */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_1ff6h79(t2, t3);
        uint8_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) goto mn7;
        t5 = ((uint8_t)0);
        goto me6;
mn7:;
        t5 = ((uint8_t)1);
        goto me6;
mn8:;
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 153, 12);
me6:;
        uint8_t t9 = 0;
        F.ret = t5;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_def_once_pqgvduh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_def_once_pqgvduh F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 16));
        void * t5 = (void *)(((uint8_t *)t4) + 40);
        gm_tl874t2atnlh3 t6 = gm_l1;
        uint8_t t7 = gm_f_contains_cra391b(t5, GM_CONV(gm_cseahvg8v7p0vs, t6));
        uint8_t t8 = (uint8_t)!t7;
        if (!t8) goto ie2;
        {
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 16));
            void * t12 = (void *)(((uint8_t *)t11) + 40);
            gm_tl874t2atnlh3 t13 = gm_l1;
            gm_tftb1odc739t5 t14 = gm_f_put_rt5d34n(t12, GM_CONV(gm_cseahvg8v7p0vs, t13), &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a2)));
            if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_srcb2hdor4f6g0q, 149, 40);
            gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1ivdmti(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1ivdmti F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_537clvp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_537clvp F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6ld1tun(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6ld1tun F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8td41o0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8td41o0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cln8ptp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_cln8ptp F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jbu9fo3_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jdnmbru(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jdnmbru F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lv6i4jh_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lv6i4jh_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5p0uct(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n5p0uct F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rqpk5og(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_rqpk5og F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_expr_kih3eo8 *)((&F.h)->up))->l0));
        void * t3 = gm_l0;
        gm_b0_1 t4 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_7pa51u7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_7pa51u7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_cln8ptp();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_oh3lgki(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_oh3lgki F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_n5p0uct();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_s7cd6p5(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_s7cd6p5 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_1ivdmti();
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
inline __attribute__((aligned(4))) void gm_f_list_nrf6taq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nrf6taq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_jdnmbru();
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
inline __attribute__((aligned(4))) void gm_f_list_rk274m7_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rk274m7_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_jbu9fo3_2();
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
inline __attribute__((aligned(4))) void gm_f_list_ubba23m_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ubba23m_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lv6i4jh_2();
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
inline __attribute__((aligned(4))) void gm_f_list_uc1atd0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uc1atd0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_6ld1tun();
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_59juqoo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t1 = gm_l1;
    uint64_t t2 = (uintptr_t)t1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 200) + 0) + 8);
    if (t2 >= (uintptr_t)(t4)) gm_trap_index(t2, t4, gm_srcc2qutubarto1, 547, 78);
    void * t5 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t3) + 200) + 0)) + t2 * 184) + 56);
    gm_b16_8 t6 = gm_l2;
    gm_tf5srh4mp4b3i t7 = gm_f_get_espvf96(t5, t6);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_amp5ob6(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_amp5ob6 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t08olq97rscn3 gm_l2; /* x */
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
            gm_t1s75i08qtptd t8;
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
        gm_t08olq97rscn3 t17 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 32));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        const uint8_t *t29 = ((uint8_t *)&gm_l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)gm_out) + 36, 0);
        ((uint8_t *)&(*(gm_t1s75i08qtptd *)gm_out))[32] = 1;
        memmove((uint8_t *)&(*(gm_t1s75i08qtptd *)gm_out) + 0, t29, 32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_g2usrvd_2(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_g2usrvd_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
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
            gm_t3jh0etmvddmg t8;
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
        gm_tfqiukge1b9q0 t17 = GM_LD(gm_tfqiukge1b9q0, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 48));
        F.l2 = t17;
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        const uint8_t *t29 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3jh0etmvddmg *)gm_out) + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3jh0etmvddmg *)gm_out) + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3jh0etmvddmg *)gm_out) + 52, 0);
        ((uint8_t *)&(*(gm_t3jh0etmvddmg *)gm_out))[48] = 1;
        memmove((uint8_t *)&(*(gm_t3jh0etmvddmg *)gm_out) + 0, t29, 48);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_pop_label_e8oj3h3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_label_e8oj3h3 F;
    void * gm_l0; /* w */
    GM_STACK_CHECK_SHALLOW(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        uint8_t t3;
        if (((((uint8_t *)&F.l1))[32] != 0)) goto mn5;
        t3 = ((uint8_t)0);
        goto me4;
mn5:;
        t3 = ((uint8_t)1);
        goto me4;
mn6:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 732, 8);
me4:;
        if (!t3) goto ie2;
        {
            {
                void * t9 = gm_l0;
                void * t10 = (void *)(((uint8_t *)t9) + 72);
                gm_t1s75i08qtptd t11;
                gm_f_pop_amp5ob6((void *)&t11, t10);
            }
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
inline __attribute__((aligned(4))) void gm_f_pop_scope_vgka5ae(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_scope_vgka5ae F;
    void * gm_l0; /* w */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            void * t4 = (void *)(((uint8_t *)t3) + 32);
            gm_t3jh0etmvddmg t5;
            gm_f_pop_g2usrvd_2((void *)&t5, t4);
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bb3f68j(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bb3f68j F;
    void * gm_l0; /* l */
    gm_t08olq97rscn3 gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_jdnmbru();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_rgq4vgc(t10, ((uint64_t)1ULL), t12);
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
        gm_t08olq97rscn3 t21 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a1));
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)t20 + t18 * 32), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kiclmcn_2 F;
    void * gm_l0; /* l */
    gm_t08olq97rscn3 gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_jbu9fo3_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_8skjmj4_2(t10, ((uint64_t)1ULL), t12);
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
        gm_t08olq97rscn3 t21 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a1));
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)t20 + t18 * 32), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pcqb56b_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pcqb56b_2 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_lv6i4jh_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_cvgrcrj_2(t10, ((uint64_t)1ULL), t12);
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
        gm_tfqiukge1b9q0 t21 = GM_LD(gm_tfqiukge1b9q0, ((uint8_t *)a1));
        GM_ST(gm_tfqiukge1b9q0, ((uint8_t *)t20 + t18 * 48), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rb04a8h(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rb04a8h F;
    void * gm_l0; /* l */
    gm_t2p74ki8sl4an gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_8td41o0();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_q6son6o(t10, ((uint64_t)1ULL), t12);
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
        gm_t2p74ki8sl4an t21 = GM_LD(gm_t2p74ki8sl4an, ((uint8_t *)a1));
        GM_ST(gm_t2p74ki8sl4an, ((uint8_t *)t20 + t18 * 24), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 24)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_u4jfguj(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_push_scope_u4jfguj F;
    void * gm_l0; /* w */
    uint32_t gm_l1; /* end */
    GM_STACK_CHECK(&F, 82u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 32);
        gm_tfqiukge1b9q0 t4;
        memset((uint8_t *)&t4 + 44, 0, 4);
        gm_f_list_nrf6taq((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t4 + 0)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t5 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t4 + 40, t5);
        gm_tftb1odc739t5 t6 = gm_f_push_pcqb56b_2(t3, &t4);
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srcb2hdor4f6g0q, 126, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_sige3eu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_sige3eu F;
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
            gm_b16_8 t11 = gm_f_empty_slice_537clvp();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_b5mmd35(t10, ((uint64_t)1ULL), t12);
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
        gm_tfgiovingnnea t21 = GM_LD(gm_tfgiovingnnea, ((uint8_t *)a1));
        GM_ST(gm_tfgiovingnnea, ((uint8_t *)t20 + t18 * 88), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 88)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8skjmj4_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_b5mmd35(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_b5mmd35 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)88ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_cvgrcrj_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_cvgrcrj_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q6son6o(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_q6son6o F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_rgq4vgc(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_rgq4vgc F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_kmjeh8s F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 129u);
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
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        uint8_t t9 = 0;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_sig_89f514c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_sig_89f514c F;
    void * gm_l0; /* w */
    void * gm_l1; /* sg */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        uint32_t t4 = GM_LD(uint32_t, ((((uint8_t *)t3) + 56) + 8));
        gm_b0_1 t5 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t2, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l1;
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 0));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_b0_1 t10 = gm_f_params_ufsghpd((void *)NULL, cx, NULL, t6, t9, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t11 = gm_l0;
        void * t12 = gm_l1;
        gm_b0_1 t13 = gm_f_sig_rest_gkcl8d2((void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t14 = gm_l0;
        gm_f_pop_scope_vgka5ae(t14);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_type_literal_nnom1cv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_type_literal_nnom1cv F;
    void * gm_l0; /* w */
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_uc1atd0((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        void * t4 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t5 = gm_f_type_lit_lm0h7k6((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_tiqb21n(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_type_of_tiqb21n F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tkn42u7na3t81 gm_l2; /* t */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        void * t5 = (void *)(((uint8_t *)t4) + 0);
        gm_tl874t2atnlh3 t6 = gm_l1;
        gm_tpk1phrp285m3 t7 = gm_f_get_o4mipv6(t5, GM_CONV(gm_cseahvg8v7p0vs, t6));
        if (!((((uint8_t *)&t7))[8] != 0)) goto ie2;
        gm_tkn42u7na3t81 t8 = GM_LD(gm_tkn42u7na3t81, ((uint8_t *)&t7));
        gm_l2 = t8;
        {
            gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l2) + 0));
            gm_tkn42u7na3t81 t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t11), t10);
            ((uint8_t *)&t11)[4] = 1;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_tkn42u7na3t81 t13;
        memset(&t13, 0, sizeof t13);
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_use_decl_sfao4j5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_use_decl_sfao4j5 F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* from */
    gm_tsncjtib3j1l1 gm_l2; /* d */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        void * t4 = gm_l0;
        gm_tl874t2atnlh3 t5 = GM_CONV(gm_tl874t2atnlh3, gm_f_decl_name_span_ht2nhb5(t4, &(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l2))));
        gm_b0_1 t6 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t2, t3, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = gm_l0;
        gm_b0_1 t8 = gm_f_ensure_hover_3epdjmk((void *)NULL, cx, NULL, t7, &(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t9 = gm_l0;
        gm_tl874t2atnlh3 t10 = gm_l1;
        gm_b0_1 t11 = gm_f_note_receiver_6mb4kc5((void *)NULL, cx, NULL, t9, t10, &(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l2)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_ref_tgbe5vc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_add_ref_tgbe5vc F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* from */
    gm_tl874t2atnlh3 gm_l2; /* to */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t5 = (!gm_eqff85ac10o3(((uint8_t *)&t3), ((uint8_t *)&t4)));
        if (!t5) goto ie2;
        gm_tl874t2atnlh3 t6 = gm_l2;
        gm_tl874t2atnlh3 t7 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t8 = (!gm_eqff85ac10o3(((uint8_t *)&t6), ((uint8_t *)&t7)));
        if (!t8) goto ie2;
        {
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 16));
            void * t12 = (void *)(((uint8_t *)t11) + 0);
            gm_t2p74ki8sl4an t13;
            gm_tl874t2atnlh3 t14 = gm_l1;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t13 + 0, t14);
            gm_tl874t2atnlh3 t15 = gm_l2;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t13 + 12, t15);
            gm_tftb1odc739t5 t16 = gm_f_push_rb04a8h(t12, &t13);
            if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 1, gm_srcb2hdor4f6g0q, 173, 62);
            gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_arg_amoajjv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_arg_amoajjv F;
    void * gm_l0; /* w */
    void * gm_l1; /* a */
    GM_STACK_CHECK(&F, 340u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; default: goto ma7; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t10 != ((uint8_t)0ULL)) goto mn9;
        gm_tc3oht4ksth9d t11 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)t2) + 0) + 8));
        F.l2 = t11;
        void * t12 = gm_l0;
        void * t13 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t14 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn9:;
ma5:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t16 != ((uint8_t)1ULL)) goto mn15;
        gm_tc3oht4ksth9d t17 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)t2) + 0) + 8) + 32));
        F.l3 = t17;
        void * t18 = gm_l0;
        void * t19 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t20 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn15:;
ma6:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t22 != ((uint8_t)2ULL)) goto mn21;
        gm_tc3oht4ksth9d t23 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)t2) + 0) + 8));
        F.l4 = t23;
        void * t24 = gm_l0;
        void * t25 = (void *)((uint8_t *)&F.l4);
        gm_b0_1 t26 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn21:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 489, 5);
me3:;
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
inline __attribute__((aligned(4))) gm_tu0a56t4bfvqh gm_f_base_of_9tbrpvr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_base_of_9tbrpvr F;
    void * gm_l0; /* w */
    gm_tsncjtib3j1l1 gm_l1; /* d */
    uint64_t gm_l2; /* x */
    uint64_t gm_l4; /* m */
    GM_STACK_CHECK(&F, 689u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t3 != ((uint8_t)0ULL)) goto ie2;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_l2 = t4;
        {
            void * t6 = gm_l0;
            void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
            uint64_t t8 = gm_l2;
            gm_tlabdfckddbfd t9;
            gm_f_decl_done_mk70ol9((void *)&t9, (void *)NULL, cx, NULL, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6qooom8qa2do t10;
            if (((((uint8_t *)&t9))[208] != 0)) {
                t10 = (*(gm_t6qooom8qa2do *)(((uint8_t *)&t9) + 0));
            } else {
                gm_tu0a56t4bfvqh t11;
                GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t11 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)3ULL));
                uint8_t t12 = 0;
                F.ret = t11;
                goto gm_fret;
            }
            F.l3 = t10;
        }
        {
            gm_tu0a56t4bfvqh t13;
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            switch (t20) { case ((uint8_t)0ULL): goto ma16; case ((uint8_t)1ULL): goto ma16; case ((uint8_t)2ULL): goto ma17; case ((uint8_t)3ULL): goto ma18; case ((uint8_t)4ULL): goto ma18; case ((uint8_t)5ULL): goto ma18; case ((uint8_t)6ULL): goto ma18; case ((uint8_t)7ULL): goto ma18; case ((uint8_t)8ULL): goto ma18; case ((uint8_t)9ULL): goto ma15; case ((uint8_t)10ULL): goto ma18; case ((uint8_t)11ULL): goto ma18; default: goto ma18; }
ma15:;
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t22 != ((uint8_t)9ULL)) goto mn21;
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
            gm_l4 = t23;
            uint64_t t24 = gm_l4;
            gm_tu0a56t4bfvqh t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t25 + 8), t24);
            t13 = t25;
            goto me14;
mn21:;
ma16:;
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t29 != ((uint8_t)0ULL)) goto pa28;
            goto po27;
pa28:;
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t31 != ((uint8_t)1ULL)) goto pa30;
            goto po27;
pa30:;
            goto mn26;
po27:;
            uint64_t t32 = gm_l2;
            gm_tu0a56t4bfvqh t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)1ULL));
            GM_ST(uint64_t, ((uint8_t *)&t33 + 8), t32);
            t13 = t33;
            goto me14;
mn26:;
ma17:;
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t35 != ((uint8_t)2ULL)) goto mn34;
            uint64_t t36 = gm_l2;
            gm_tu0a56t4bfvqh t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)2ULL));
            GM_ST(uint64_t, ((uint8_t *)&t37 + 8), t36);
            t13 = t37;
            goto me14;
mn34:;
ma18:;
            gm_tu0a56t4bfvqh t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t39 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)3ULL));
            t13 = t39;
            goto me14;
mn38:;
ma19:;
            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 272, 12);
me14:;
            uint8_t t40 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_tu0a56t4bfvqh t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t42 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)3ULL));
            uint8_t t43 = 0;
            F.ret = t42;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_block_jfodv9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_block_jfodv9u F;
    void * gm_l0; /* w */
    void * gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 181u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        uint32_t t4 = GM_LD(uint32_t, ((((uint8_t *)t3) + 24) + 8));
        gm_b0_1 t5 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t2, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t7 = gm_l1;
            uint64_t t8 = GM_LD(uintptr_t, (((uint8_t *)t7) + 8) + 8);
            gm_l2 = t8;
        }
        {
            for (;;) {
                uint64_t t10 = gm_l3;
                uint64_t t11 = gm_l2;
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l3;
                    uint64_t t18 = GM_LD(uint64_t, ((uint8_t *)&t15));
                    gm_l4 = t18;
                    {
                        void * t20 = gm_l0;
                        uint64_t t21 = gm_l4;
                        uint64_t t22 = (uintptr_t)t21;
                        void * t23 = gm_l1;
                        uint64_t t24 = GM_LD(uintptr_t, (((uint8_t *)t23) + 8) + 8);
                        if (t22 >= (uintptr_t)(t24)) gm_trap_index(t22, t24, gm_srcb2hdor4f6g0q, 604, 39);
                        void * t25 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t23) + 8)) + t22 * 144);
                        gm_b0_1 t26 = gm_f_stmt_ald930b((void *)NULL, cx, NULL, t20, t25);
                        if (gm_unw.kind) {
                            goto gm_ll13;
                        }
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 604, 5);
me16:;
                }
lc9:;
                {
                    uint64_t t28 = gm_l3;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l3 = t31;
                }
            }
gm_ll13:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb9; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc9; }
            }
            goto gm_lad;
lb9:;
        }
    }
    {
        void * t32 = gm_l0;
        gm_f_pop_scope_vgka5ae(t32);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_call_8vfr61j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_call_8vfr61j F;
    void * gm_l0; /* w */
    void * gm_l1; /* e */
    void * gm_l2; /* callee */
    gm_b16_8 gm_l3; /* args */
    gm_t9805qtonbsag gm_l4; /* target */
    gm_t8sds1gdern44 gm_l5; /* variant */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    gm_tu0a56t4bfvqh gm_l8; /* cls */
    uint8_t gm_l9; /* is_field */
    gm_t88rcucg1b7uk gm_l10; /* t */
    uint32_t gm_l11; /* f */
    uint64_t gm_l12; /* d */
    gm_t08olq97rscn3 gm_l13; /* id */
    uint64_t gm_l14; /* d */
    gm_tf5srh4mp4b3i gm_l15; /* struct_decl */
    gm_t88rcucg1b7uk gm_l16; /* t */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    void * gm_l20; /* a */
    gm_t08olq97rscn3 gm_l21; /* n */
    gm_tf1bjg81u0glv gm_l22; /* v */
    uint32_t gm_l23; /* f */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l27; /* d */
    gm_tl874t2atnlh3 gm_l28; /* to */
    GM_STACK_CHECK(&F, 1699u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
        void * t4 = (void *)(((uint8_t *)t3) + 112);
        void * t5 = gm_l1;
        gm_tl874t2atnlh3 t6 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t5) + 72));
        gm_t9805qtonbsag t7 = gm_f_get_rlnefnq(t4, GM_CONV(gm_cseahvg8v7p0vs, t6));
        gm_l4 = t7;
    }
    {
        gm_t8sds1gdern44 t8;
        memset(&t8, 0, sizeof t8);
        gm_l5 = t8;
    }
    {
        void * t9 = gm_l2;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma13; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma13; case ((uint8_t)25ULL): goto ma13; case ((uint8_t)26ULL): goto ma13; case ((uint8_t)27ULL): goto ma13; case ((uint8_t)28ULL): goto ma13; case ((uint8_t)29ULL): goto ma13; case ((uint8_t)30ULL): goto ma13; case ((uint8_t)31ULL): goto ma13; case ((uint8_t)32ULL): goto ma13; case ((uint8_t)33ULL): goto ma13; case ((uint8_t)34ULL): goto ma13; case ((uint8_t)35ULL): goto ma13; case ((uint8_t)36ULL): goto ma13; case ((uint8_t)37ULL): goto ma13; case ((uint8_t)38ULL): goto ma13; case ((uint8_t)39ULL): goto ma13; case ((uint8_t)40ULL): goto ma13; case ((uint8_t)41ULL): goto ma13; case ((uint8_t)42ULL): goto ma13; case ((uint8_t)43ULL): goto ma13; case ((uint8_t)44ULL): goto ma13; case ((uint8_t)45ULL): goto ma13; case ((uint8_t)46ULL): goto ma13; case ((uint8_t)47ULL): goto ma13; case ((uint8_t)48ULL): goto ma13; case ((uint8_t)49ULL): goto ma13; case ((uint8_t)50ULL): goto ma13; default: goto ma13; }
ma11:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t17 != ((uint8_t)22ULL)) goto mn16;
        void * t18 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 0));
        gm_l6 = t18;
        gm_t08olq97rscn3 t19 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t9) + 8) + 8) + 8));
        gm_l7 = t19;
        {
            void * t21 = gm_l0;
            void * t22 = gm_l6;
            gm_tu0a56t4bfvqh t23 = gm_f_classify_pdaj2gb((void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t23;
        }
        {
            uint8_t t25 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l8) + 0) == ((uint8_t)3ULL)));
            if (!t25) goto ie24;
            {
                void * t27 = gm_l0;
                void * t28 = gm_l6;
                gm_b0_1 t29 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t27, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_l9 = ((uint8_t)0);
            }
            {
                void * t31 = gm_l0;
                void * t32 = gm_l6;
                gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 72));
                gm_tkn42u7na3t81 t34 = gm_f_type_of_tiqb21n(t31, GM_CONV(gm_cseahvg8v7p0vs, t33));
                if (!((((uint8_t *)&t34))[4] != 0)) goto ie30;
                gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t34));
                gm_l10 = t35;
                {
                    void * t37 = gm_l0;
                    gm_t88rcucg1b7uk t38 = gm_l10;
                    uint8_t t39 = gm_f_field_use_838f8e8((void *)NULL, cx, NULL, t37, t38, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l9 = t39;
                }
                goto ix30;
ie30:;
ix30:;
            }
            {
                uint8_t t41 = gm_l9;
                uint8_t t42 = (uint8_t)!t41;
                if (!t42) goto ie40;
                {
                    if (!((((uint8_t *)&gm_l4))[4] != 0)) goto ie44;
                    uint32_t t45 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                    gm_l11 = t45;
                    {
                        void * t47 = gm_l0;
                        gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 16));
                        uint32_t t49 = gm_l11;
                        gm_b0_1 t50 = gm_f_fn_use_l994th9((void *)NULL, cx, NULL, t47, t48, t49);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix44;
ie44:;
ix44:;
                }
                goto ix40;
ie40:;
ix40:;
            }
            goto ix24;
ie24:;
            {
                uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                if (t53 != ((uint8_t)1ULL)) goto ie52;
                uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                gm_l12 = t54;
                {
                    gm_tf1bjg81u0glv t56;
                    uint64_t t57 = gm_l12;
                    GM_ST(uint64_t, (uint8_t *)&t56 + 0, t57);
                    gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t56 + 8, t58);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t8sds1gdern44 *)((uint8_t *)&gm_l5)) + 25, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t8sds1gdern44 *)((uint8_t *)&gm_l5)) + 26, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t8sds1gdern44 *)((uint8_t *)&gm_l5)) + 28, 0);
                    ((uint8_t *)&(*(gm_t8sds1gdern44 *)((uint8_t *)&gm_l5)))[24] = 1;
                    GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_t8sds1gdern44 *)((uint8_t *)&gm_l5)) + 0, t56);
                }
                goto ix52;
ie52:;
ix52:;
            }
            {
                void * t59 = gm_l0;
                void * t60 = gm_l2;
                gm_b0_1 t61 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t59, t60);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix24:;
        }
        goto me10;
mn16:;
ma12:;
        uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t63 != ((uint8_t)8ULL)) goto mn62;
        gm_t08olq97rscn3 t64 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t9) + 8) + 8));
        gm_l13 = t64;
        {
            void * t67 = gm_l0;
            void * t68 = gm_l1;
            gm_tl874t2atnlh3 t69 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t68) + 72));
            gm_tf5srh4mp4b3i t70 = gm_f_variant_at_9i78oa0((void *)NULL, cx, NULL, t67, t69, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l13)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t70))[8] != 0)) goto ie66;
            uint64_t t71 = GM_LD(uint64_t, ((uint8_t *)&t70));
            gm_l14 = t71;
            {
                gm_tf1bjg81u0glv t73;
                uint64_t t74 = gm_l14;
                GM_ST(uint64_t, (uint8_t *)&t73 + 0, t74);
                gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t73 + 8, t75);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t8sds1gdern44 *)((uint8_t *)&gm_l5)) + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t8sds1gdern44 *)((uint8_t *)&gm_l5)) + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t8sds1gdern44 *)((uint8_t *)&gm_l5)) + 28, 0);
                ((uint8_t *)&(*(gm_t8sds1gdern44 *)((uint8_t *)&gm_l5)))[24] = 1;
                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_t8sds1gdern44 *)((uint8_t *)&gm_l5)) + 0, t73);
            }
            goto ix66;
ie66:;
ix66:;
        }
        goto me10;
mn62:;
ma13:;
        void * t77 = gm_l0;
        void * t78 = gm_l2;
        gm_b0_1 t79 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t77, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me10;
mn76:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 543, 5);
me10:;
    }
    {
        gm_tf5srh4mp4b3i t80;
        memset(&t80, 0, sizeof t80);
        gm_l15 = t80;
    }
    {
        uint8_t t82;
        if (((((uint8_t *)&gm_l4))[4] != 0)) goto mn84;
        t82 = ((uint8_t)1);
        goto me83;
mn84:;
        t82 = ((uint8_t)0);
        goto me83;
mn85:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 565, 8);
me83:;
        if (!t82) goto ie81;
        uint8_t t86;
        if (((((uint8_t *)&gm_l5))[24] != 0)) goto mn88;
        t86 = ((uint8_t)1);
        goto me87;
mn88:;
        t86 = ((uint8_t)0);
        goto me87;
mn89:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 565, 27);
me87:;
        if (!t86) goto ie81;
        {
            void * t92 = gm_l0;
            void * t93 = gm_l1;
            gm_tl874t2atnlh3 t94 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t93) + 72));
            gm_tkn42u7na3t81 t95 = gm_f_type_of_tiqb21n(t92, GM_CONV(gm_cseahvg8v7p0vs, t94));
            if (!((((uint8_t *)&t95))[4] != 0)) goto ie91;
            gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t95));
            gm_l16 = t96;
            {
                void * t98 = gm_l0;
                void * t99 = GM_LD(void *, (((uint8_t *)t98) + 0));
                gm_t88rcucg1b7uk t100 = gm_l16;
                gm_tf5srh4mp4b3i t101 = gm_f_adt_decl_of_2bflsov(t99, GM_CONV(gm_csguankko4lmhd, t100));
                gm_l15 = t101;
            }
            goto ix91;
ie91:;
ix91:;
        }
        goto ix81;
ie81:;
ix81:;
    }
    {
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            gm_l17 = t103;
        }
        {
            for (;;) {
                uint64_t t105 = gm_l18;
                uint64_t t106 = gm_l17;
                uint8_t t107 = (t105 < t106);
                if (!t107) goto lb104;
                {
                    uint64_t t110 = gm_l18;
                    uint64_t t113 = GM_LD(uint64_t, ((uint8_t *)&t110));
                    gm_l19 = t113;
                    {
                        uint64_t t115 = gm_l19;
                        uint64_t t116 = (uintptr_t)t115;
                        uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t116 >= (uintptr_t)(t117)) gm_trap_index(t116, t117, gm_srcb2hdor4f6g0q, 569, 15);
                        void * t118 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t116 * 144);
                        gm_l20 = t118;
                    }
                    {
                        void * t120 = gm_l20;
                        uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)t120) + 0) + 0));
                        if (t121 != ((uint8_t)1ULL)) goto ie119;
                        gm_t08olq97rscn3 t122 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t120) + 0) + 8) + 0));
                        gm_l21 = t122;
                        {
                            if (!((((uint8_t *)&gm_l5))[24] != 0)) goto ie124;
                            gm_tf1bjg81u0glv t125 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)&gm_l5));
                            gm_l22 = t125;
                            {
                                void * t127 = gm_l0;
                                uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 0));
                                gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                                gm_b0_1 t130 = gm_f_payload_field_3o532rn((void *)NULL, cx, NULL, t127, t128, t129, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l21)));
                                if (gm_unw.kind) {
                                    goto gm_ll108;
                                }
                            }
                            goto ix124;
ie124:;
                            if (!((((uint8_t *)&gm_l4))[4] != 0)) goto ie131;
                            uint32_t t132 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                            gm_l23 = t132;
                            {
                                {
                                    uint32_t t135 = gm_l23;
                                    uint64_t t136 = (uint64_t)t135;
                                    uint64_t t137 = (uintptr_t)t136;
                                    void * t138 = gm_l0;
                                    void * t139 = GM_LD(void *, (((uint8_t *)t138) + 0));
                                    uint64_t t140 = GM_LD(uintptr_t, ((((uint8_t *)t139) + 344) + 0) + 8);
                                    if (t137 >= (uintptr_t)(t140)) gm_trap_index(t137, t140, gm_srcb2hdor4f6g0q, 574, 26);
                                    gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t139) + 344) + 0)) + t137 * 136) + 40));
                                    gm_l24 = t141;
                                }
                                {
                                    gm_l25 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t143 = gm_l25;
                                        uint64_t t144 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                        uint8_t t145 = (t143 < t144);
                                        if (!t145) goto lb142;
                                        {
                                            uint64_t t148 = gm_l25;
                                            uint64_t t149 = (uintptr_t)t148;
                                            uint64_t t150 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                            gm_t5v0c8ia6a5mo t153 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t149 * 56));
                                            F.l26 = t153;
                                            {
                                                gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
                                                gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                                                uint8_t t158 = (gm_eq0iqiubfm7e(((uint8_t *)&t156), ((uint8_t *)&t157)));
                                                if (!t158) goto ie155;
                                                {
                                                    void * t160 = gm_l0;
                                                    gm_tl874t2atnlh3 t161 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l21) + 16));
                                                    gm_tl874t2atnlh3 t162 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 20));
                                                    gm_b0_1 t163 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t160, t161, t162);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll146;
                                                    }
                                                }
                                                {
                                                    void * t164 = gm_l0;
                                                    gm_tl874t2atnlh3 t165 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 20));
                                                    gm_t6ok5tvcjc200 t166;
                                                    gm_t6j6smj7qoctu t167;
                                                    gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
                                                    GM_ST(gm_b16_8, (uint8_t *)&t167 + 0, t168);
                                                    void * t169 = gm_l0;
                                                    void * t170 = GM_LD(void *, (((uint8_t *)t169) + 0));
                                                    gm_t88rcucg1b7uk t171 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l26) + 16));
                                                    gm_b16_8 t172 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t170, t171);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll146;
                                                    }
                                                    GM_ST(gm_b16_8, (uint8_t *)&t167 + 16, t172);
                                                    gm_tabb710d9mq2e t173;
                                                    gm_f_format_f6rro2t((void *)&t173, (void *)NULL, cx, NULL, &t167);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll146;
                                                    }
                                                    if (((((uint8_t *)&t173))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t173) + 8, gm_srcb2hdor4f6g0q, 577, 56);
                                                    gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&t173) + 8));
                                                    gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&t174));
                                                    GM_ST(gm_b16_8, (uint8_t *)&t166 + 0, t175);
                                                    gm_th2lqbo77pvsc t176;
                                                    memset(&t176, 0, sizeof t176);
                                                    GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t166 + 16, t176);
                                                    gm_b0_1 t177 = gm_f_def_once_pqgvduh((void *)NULL, cx, NULL, t164, t165, &t166);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll146;
                                                    }
                                                }
                                                {
                                                    goto lb142;
                                                }
                                                goto ix155;
ie155:;
ix155:;
                                            }
                                            goto me151;
mn152:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 574, 17);
me151:;
                                        }
lc142:;
                                        {
                                            uint64_t t179 = gm_l25;
                                            uint64_t r180; bool o181 = __builtin_add_overflow(t179, ((uint64_t)1ULL), &r180);
                                            uint64_t t182 = r180;
                                            gm_l25 = t182;
                                        }
                                    }
gm_ll146:;
                                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb142; }
                                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc142; }
                                    }
                                    goto gm_ll108;
lb142:;
                                }
                            }
                            goto ix131;
ie131:;
                            if (!((((uint8_t *)&gm_l15))[8] != 0)) goto ie183;
                            uint64_t t184 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                            gm_l27 = t184;
                            {
                                void * t187 = gm_l0;
                                uint64_t t188 = gm_l27;
                                gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                                gm_toceffb697p0u t190 = gm_f_member_def_9f4kiah((void *)NULL, cx, NULL, t187, t188, t189);
                                if (gm_unw.kind) {
                                    goto gm_ll108;
                                }
                                if (!((((uint8_t *)&t190))[12] != 0)) goto ie186;
                                gm_tl874t2atnlh3 t191 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&t190));
                                gm_l28 = t191;
                                {
                                    void * t193 = gm_l0;
                                    gm_tl874t2atnlh3 t194 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l21) + 16));
                                    gm_tl874t2atnlh3 t195 = gm_l28;
                                    gm_b0_1 t196 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t193, t194, t195);
                                    if (gm_unw.kind) {
                                        goto gm_ll108;
                                    }
                                }
                                goto ix186;
ie186:;
ix186:;
                            }
                            goto ix183;
ie183:;
ix183:;
ix131:;
ix124:;
                        }
                        goto ix119;
ie119:;
ix119:;
                    }
                    {
                        void * t197 = gm_l0;
                        void * t198 = gm_l20;
                        gm_b0_1 t199 = gm_f_arg_amoajjv((void *)NULL, cx, NULL, t197, t198);
                        if (gm_unw.kind) {
                            goto gm_ll108;
                        }
                    }
                    goto me111;
mn112:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 568, 5);
me111:;
                }
lc104:;
                {
                    uint64_t t201 = gm_l18;
                    uint64_t r202; bool o203 = __builtin_add_overflow(t201, ((uint64_t)1ULL), &r202);
                    uint64_t t204 = r202;
                    gm_l18 = t204;
                }
            }
gm_ll108:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb104; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc104; }
            }
            goto gm_lad;
lb104:;
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
__attribute__((aligned(4))) gm_tu0a56t4bfvqh gm_f_classify_pdaj2gb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_classify_pdaj2gb F;
    void * gm_l0; /* w */
    void * gm_l1; /* e */
    void * gm_l2; /* inner */
    gm_t08olq97rscn3 gm_l3; /* id */
    gm_tsncjtib3j1l1 gm_l4; /* d */
    void * gm_l5; /* base */
    gm_t08olq97rscn3 gm_l6; /* name */
    uint64_t gm_l7; /* m */
    uint64_t gm_l8; /* d */
    void * gm_l9; /* callee */
    uint64_t gm_l10; /* d */
    GM_STACK_CHECK(&F, 761u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma4; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; default: goto ma8; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)9ULL)) goto mn11;
        void * t13 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t13;
        void * t14 = gm_l0;
        void * t15 = gm_l2;
        gm_tu0a56t4bfvqh t16 = gm_f_classify_pdaj2gb((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
        goto me3;
mn11:;
ma5:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t19 != ((uint8_t)6ULL)) goto mn18;
        gm_t08olq97rscn3 t20 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t20;
        {
            void * t23 = gm_l0;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_toceffb697p0u t25 = gm_f_lookup_local_tdg7skl(t23, t24);
            uint8_t t26;
            if (((((uint8_t *)&t25))[12] != 0)) goto mn28;
            t26 = ((uint8_t)0);
            goto me27;
mn28:;
            t26 = ((uint8_t)1);
            goto me27;
mn29:;
            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 250, 16);
me27:;
            if (!t26) goto ie22;
            {
                gm_tu0a56t4bfvqh t31;
                GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t31 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)3ULL));
                uint8_t t32 = 0;
                F.ret = t31;
                goto gm_fret;
            }
            goto ix22;
ie22:;
ix22:;
        }
        {
            void * t34 = gm_l0;
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_t1335qftqvd8f t36;
            gm_f_lookup_decl_10pnl6c((void *)&t36, (void *)NULL, cx, NULL, t34, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t36))[24] != 0)) goto ie33;
            gm_l4 = (*(gm_tsncjtib3j1l1 *)(((uint8_t *)&t36) + 0));
            {
                void * t38 = gm_l0;
                gm_tu0a56t4bfvqh t39 = gm_f_base_of_9tbrpvr((void *)NULL, cx, NULL, t38, &(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t40 = 0;
                F.ret = t39;
                goto gm_fret;
            }
            goto ix33;
ie33:;
ix33:;
        }
        {
            gm_tu0a56t4bfvqh t41;
            GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t41 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)3ULL));
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto me3;
mn18:;
ma6:;
        uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t44 != ((uint8_t)22ULL)) goto mn43;
        void * t45 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l5 = t45;
        gm_t08olq97rscn3 t46 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l6 = t46;
        {
            void * t49 = gm_l0;
            void * t50 = gm_l5;
            gm_tu0a56t4bfvqh t51 = gm_f_classify_pdaj2gb((void *)NULL, cx, NULL, t49, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&t51) + 0));
            if (t52 != ((uint8_t)0ULL)) goto ie48;
            uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&t51) + 8));
            gm_l7 = t53;
            {
                void * t56 = gm_l0;
                void * t57 = GM_LD(void *, (((uint8_t *)t56) + 0));
                void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
                uint64_t t59 = gm_l7;
                gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
                gm_tf5srh4mp4b3i t61 = gm_f_lookup_59juqoo(t58, t59, t60);
                if (!((((uint8_t *)&t61))[8] != 0)) goto ie55;
                uint64_t t62 = GM_LD(uint64_t, ((uint8_t *)&t61));
                gm_l8 = t62;
                {
                    void * t64 = gm_l0;
                    uint64_t t65 = gm_l8;
                    gm_tsncjtib3j1l1 t66;
                    GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t66 + 16, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)0ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t66 + 8), t65);
                    gm_tu0a56t4bfvqh t67 = gm_f_base_of_9tbrpvr((void *)NULL, cx, NULL, t64, &t66);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t68 = 0;
                    F.ret = t67;
                    goto gm_fret;
                }
                goto ix55;
ie55:;
ix55:;
            }
            goto ix48;
ie48:;
ix48:;
        }
        {
            gm_tu0a56t4bfvqh t69;
            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t69 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)3ULL));
            uint8_t t70 = 0;
            F.ret = t69;
            goto gm_fret;
        }
        goto me3;
mn43:;
ma7:;
        uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t72 != ((uint8_t)19ULL)) goto mn71;
        void * t73 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l9 = t73;
        {
            void * t76 = gm_l0;
            void * t77 = gm_l9;
            gm_tu0a56t4bfvqh t78 = gm_f_classify_pdaj2gb((void *)NULL, cx, NULL, t76, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&t78) + 0));
            if (t79 != ((uint8_t)1ULL)) goto ie75;
            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&t78) + 8));
            gm_l10 = t80;
            {
                uint64_t t82 = gm_l10;
                gm_tu0a56t4bfvqh t83;
                GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)1ULL));
                GM_ST(uint64_t, ((uint8_t *)&t83 + 8), t82);
                uint8_t t84 = 0;
                F.ret = t83;
                goto gm_fret;
            }
            goto ix75;
ie75:;
ix75:;
        }
        {
            gm_tu0a56t4bfvqh t85;
            GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t85 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)3ULL));
            uint8_t t86 = 0;
            F.ret = t85;
            goto gm_fret;
        }
        goto me3;
mn71:;
ma8:;
        gm_tu0a56t4bfvqh t88;
        GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t88 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)3ULL));
        uint8_t t89 = 0;
        F.ret = t88;
        goto gm_fret;
        goto me3;
mn87:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 247, 5);
me3:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_cond_d94lif5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_cond_d94lif5 F;
    void * gm_l0; /* w */
    void * gm_l1; /* c */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l6; /* value */
    gm_tkn42u7na3t81 gm_l7; /* ty */
    void * gm_l8; /* e */
    GM_STACK_CHECK(&F, 339u);
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
            void * t3 = gm_l1;
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l4 = t14;
                    {
                        uint64_t t16 = gm_l4;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l1;
                        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcb2hdor4f6g0q, 668, 15);
                        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)) + t17 * 80) + 0));
                        if (t22 != ((uint8_t)0ULL)) goto mn21;
                        gm_t0d02142k4tu8 t23 = GM_LD(gm_t0d02142k4tu8, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)) + t17 * 80) + 8) + 0));
                        F.l5 = t23;
                        void * t24 = GM_LD(void *, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)) + t17 * 80) + 8) + 64));
                        gm_l6 = t24;
                        {
                            void * t26 = gm_l0;
                            void * t27 = gm_l6;
                            gm_b0_1 t28 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t26, t27);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t29 = gm_l0;
                            void * t30 = gm_l6;
                            gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t30) + 72));
                            gm_tkn42u7na3t81 t32 = gm_f_type_of_tiqb21n(t29, GM_CONV(gm_cseahvg8v7p0vs, t31));
                            gm_l7 = t32;
                        }
                        {
                            void * t33 = gm_l0;
                            void * t34 = (void *)((uint8_t *)&F.l5);
                            gm_tkn42u7na3t81 t35 = gm_l7;
                            uint32_t t36 = GM_LD(uint32_t, ((((uint8_t *)&F.l5) + 48) + 8));
                            gm_b0_1 t37 = gm_f_pattern_jo09k6f((void *)NULL, cx, NULL, t33, t34, t35, t36);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me20;
mn21:;
                        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)) + t17 * 80) + 0));
                        if (t39 != ((uint8_t)1ULL)) goto mn38;
                        void * t40 = GM_LD(void *, (((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)) + t17 * 80) + 8));
                        gm_l8 = t40;
                        void * t41 = gm_l0;
                        void * t42 = gm_l8;
                        gm_b0_1 t43 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t41, t42);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        goto me20;
mn38:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 668, 9);
me20:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 667, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t45 = gm_l3;
                    uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                    uint64_t t48 = r46;
                    gm_l3 = t48;
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
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_decl_name_span_ht2nhb5(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_name_span_ht2nhb5 F;
    void * gm_l0; /* w */
    gm_tsncjtib3j1l1 gm_l1; /* d */
    uint64_t gm_l2; /* x */
    uint64_t gm_l3; /* x */
    uint32_t gm_l4; /* i */
    void * gm_l5; /* lit */
    GM_STACK_CHECK_SHALLOW(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t4 != ((uint8_t)0ULL)) goto mn3;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        gm_l2 = t5;
        uint64_t t6 = gm_l2;
        uint64_t t7 = (uintptr_t)t6;
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        uint64_t t11 = GM_LD(uintptr_t, ((((uint8_t *)t10) + 240) + 0) + 8);
        if (t7 >= (uintptr_t)(t11)) gm_trap_index(t7, t11, gm_srcb2hdor4f6g0q, 179, 28);
        gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t10) + 240) + 0)) + t7 * 216) + 0) + 16));
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
        goto me2;
mn3:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t15 != ((uint8_t)1ULL)) goto mn14;
        uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 0));
        gm_l3 = t16;
        uint32_t t17 = GM_LD(uint32_t, ((((uint8_t *)a1) + 8) + 8));
        gm_l4 = t17;
        {
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
            uint64_t t22 = gm_l3;
            void * t23 = gm_f_effect_lit_26tkk47(t21, t22);
            if (!(GM_LD(void *, ((uint8_t *)&t23)) != 0)) goto ie19;
            void * t24 = GM_LD(void *, ((uint8_t *)&t23));
            gm_l5 = t24;
            {
                uint32_t t26 = gm_l4;
                uint64_t t27 = (uint64_t)t26;
                uint64_t t28 = (uintptr_t)t27;
                void * t29 = gm_l5;
                uint64_t t30 = GM_LD(uintptr_t, (((uint8_t *)t29) + 24) + 8);
                if (t28 >= (uintptr_t)(t30)) gm_trap_index(t28, t30, gm_srcb2hdor4f6g0q, 181, 51);
                gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t29) + 24)) + t28 * 80) + 0) + 16));
                uint8_t t32 = 0;
                F.ret = t31;
                goto gm_fret;
            }
            goto ix19;
ie19:;
ix19:;
        }
        {
            gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
            uint8_t t34 = 0;
            F.ret = t33;
            goto gm_fret;
        }
        goto me2;
mn14:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 178, 5);
me2:;
    }
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_decl_name_span_ht2nhb5_c(void * a0, gm_tsncjtib3j1l1 a1) { return gm_f_decl_name_span_ht2nhb5(a0, &a1); }
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_feg3ue0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2, uint32_t a3) {
    struct gm_fr_declare_feg3ue0 F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l1; /* id */
    gm_t9qkh0rsooc8d gm_l2; /* kind */
    uint32_t gm_l3; /* from */
    void * gm_l4; /* last */
    uint32_t gm_l5; /* end */
    gm_b16_8 gm_l6; /* code */
    gm_b16_8 gm_l7; /* detail */
    gm_tb59ptmmofnb5 gm_l8; /* f */
    gm_b16_8 gm_l9; /* shown */
    GM_STACK_CHECK(&F, 858u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t3 = 0;
        uint8_t t4 = (((GM_LD(uintptr_t, (((uint8_t *)a1) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)a1) + 0)), gm_sfdbfrv46lihd, 1) == 0));
        if (!t4) {
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 32) + 0) + 8);
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            t3 = t7;
        } else t3 = 1;
        if (!t3) goto ie2;
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
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 32) + 0) + 8);
        uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
        if (o14) gm_trap("integer overflow in `-`", gm_srcb2hdor4f6g0q, 155, 29);
        uint64_t t15 = r13;
        uint64_t t16 = (uintptr_t)t15;
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 32) + 0) + 8);
        if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcb2hdor4f6g0q, 155, 14);
        void * t19 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 32) + 0)) + t16 * 48);
        gm_l4 = t19;
    }
    {
        void * t20 = gm_l4;
        uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)t20) + 40));
        gm_l5 = t21;
    }
    {
        void * t22 = gm_l4;
        void * t23 = (void *)(((uint8_t *)t22) + 0);
        gm_t08olq97rscn3 t24;
        memset((uint8_t *)&t24 + 28, 0, 4);
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t24 + 0, t25);
        gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t24 + 16, t26);
        gm_tftb1odc739t5 t27 = gm_f_push_bb3f68j(t23, &t24);
        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srcb2hdor4f6g0q, 157, 5);
        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
    }
    {
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        gm_l6 = t29;
    }
    {
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(0));
        gm_l7 = t30;
    }
    {
        void * t32 = gm_l0;
        void * t33 = GM_LD(void *, (((uint8_t *)t32) + 8));
        void * t34 = (void *)(((uint8_t *)t33) + 168);
        gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        gm_tvsjp38cd9njc t36;
        gm_f_get_shqk64n((void *)&t36, t34, GM_CONV(gm_cseahvg8v7p0vs, t35));
        if (!((((uint8_t *)&t36))[24] != 0)) goto ie31;
        gm_l8 = (*(gm_tb59ptmmofnb5 *)(((uint8_t *)&t36) + 0));
        {
            void * t38 = gm_l0;
            void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
            gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l8) + 16));
            gm_b16_8 t41 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t41;
        }
        {
            gm_t6j6smj7qoctu t42;
            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t42 + 0, t43);
            gm_b16_8 t44 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t42 + 16, t44);
            gm_tabb710d9mq2e t45;
            gm_f_format_f6rro2t((void *)&t45, (void *)NULL, cx, NULL, &t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_srcb2hdor4f6g0q, 162, 16);
            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
            gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
            gm_l6 = t47;
        }
        {
            gm_b16_8 t48 = gm_l9;
            gm_l7 = t48;
        }
        goto ix31;
ie31:;
ix31:;
    }
    {
        void * t49 = gm_l0;
        gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        gm_t6ok5tvcjc200 t51;
        gm_b16_8 t52 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t51 + 0, t52);
        gm_th2lqbo77pvsc t53;
        memset(&t53, 0, sizeof t53);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t51 + 16, t53);
        gm_b0_1 t54 = gm_f_def_once_pqgvduh((void *)NULL, cx, NULL, t49, t50, &t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memset((uint8_t *)&F.l10 + 17, 0, 7);
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 0, t55);
        gm_t9qkh0rsooc8d t56 = gm_l2;
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l10 + 16, t56);
        gm_b16_8 t57 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 24, t57);
        gm_th2lqbo77pvsc t58;
        memset(&t58, 0, sizeof t58);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l10 + 40, t58);
    }
    {
        void * t59 = gm_l0;
        void * t60 = GM_LD(void *, (((uint8_t *)t59) + 16));
        void * t61 = (void *)(((uint8_t *)t60) + 136);
        gm_tfgiovingnnea t62;
        memset((uint8_t *)&t62 + 84, 0, 4);
        memcpy((uint8_t *)&t62 + 0, ((uint8_t *)&F.l10), 64);
        gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t62 + 64, t63);
        uint32_t t64 = gm_l3;
        GM_ST(uint32_t, (uint8_t *)&t62 + 76, t64);
        uint32_t t65 = gm_l5;
        GM_ST(uint32_t, (uint8_t *)&t62 + 80, t65);
        gm_tftb1odc739t5 t66 = gm_f_push_sige3eu(t61, &t62);
        if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srcb2hdor4f6g0q, 167, 5);
        gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_def_once_pqgvduh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
inline __attribute__((aligned(4))) void * gm_f_effect_lit_26tkk47(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_lit_26tkk47 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    void * gm_l2; /* decl */
    void * gm_l4; /* lit */
    GM_STACK_CHECK_SHALLOW(&F, 197u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srcb2hdor4f6g0q, 74, 14);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t3 * 216);
        gm_l2 = t7;
    }
    {
        void * t9 = gm_l2;
        uint8_t t10 = GM_LD(uint8_t, (((((uint8_t *)t9) + 40) + 64) + 0));
        if (t10 != ((uint8_t)0ULL)) goto ie8;
        gm_tv2uf4leil5hq t11 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t9) + 40) + 64) + 8));
        F.l3 = t11;
        {
            void * t14 = GM_LD(void *, (((uint8_t *)&F.l3) + 40));
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            if (t15 != ((uint8_t)34ULL)) goto ie13;
            void * t16 = GM_LD(void *, ((((uint8_t *)t14) + 8) + 8));
            gm_l4 = t16;
            {
                void * t18 = gm_l4;
                uint8_t t19 = 0;
                F.ret = t18;
                goto gm_fret;
            }
            goto ix13;
ie13:;
            {
                uint8_t t21 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
ix13:;
        }
        goto ix8;
ie8:;
        {
            uint8_t t23 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
ix8:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_ensure_hover_3epdjmk(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_ensure_hover_3epdjmk F;
    void * gm_l0; /* w */
    gm_tsncjtib3j1l1 gm_l1; /* d */
    uint64_t gm_l2; /* key */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint64_t gm_l4; /* x */
    uint64_t gm_l6; /* x */
    uint32_t gm_l7; /* i */
    GM_STACK_CHECK(&F, 292u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        uint64_t t2 = gm_f_ref_key_vam5ppg(&(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l1)));
        gm_l2 = t2;
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 112);
        uint64_t t6 = gm_l2;
        uint8_t t7 = gm_f_contains_475f687(t5, t6);
        if (!t7) goto ie3;
        {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 112);
        uint64_t t13 = gm_l2;
        gm_tftb1odc739t5 t14 = gm_f_put_1rbrft2(t12, t13, ((uint8_t)1));
        if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_srcb2hdor4f6g0q, 190, 5);
        gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
    }
    {
        void * t16 = gm_l0;
        gm_tl874t2atnlh3 t17 = GM_CONV(gm_tl874t2atnlh3, gm_f_decl_name_span_ht2nhb5(t16, &(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l1))));
        gm_l3 = t17;
    }
    {
        gm_tl874t2atnlh3 t19 = gm_l3;
        gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t21 = (gm_eqff85ac10o3(((uint8_t *)&t19), ((uint8_t *)&t20)));
        if (!t21) goto ie18;
        {
            gm_b0_1 t23;
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t27 != ((uint8_t)0ULL)) goto mn26;
        uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_l4 = t28;
        {
            void * t30 = gm_l0;
            void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
            uint64_t t32 = gm_l4;
            gm_f_decl_info_ahk1bni((void *)&F.l5, (void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t33 = gm_l0;
            gm_tl874t2atnlh3 t34 = gm_l3;
            gm_b0_1 t35 = gm_f_def_once_pqgvduh((void *)NULL, cx, NULL, t33, t34, &(*(gm_t6ok5tvcjc200 *)(((uint8_t *)&F.l5) + 8)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me25;
mn26:;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t37 != ((uint8_t)1ULL)) goto mn36;
        uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)&gm_l1) + 8) + 0));
        gm_l6 = t38;
        uint32_t t39 = GM_LD(uint32_t, ((((uint8_t *)&gm_l1) + 8) + 8));
        gm_l7 = t39;
        {
            void * t41 = gm_l0;
            void * t42 = GM_LD(void *, (((uint8_t *)t41) + 0));
            uint64_t t43 = gm_l6;
            uint32_t t44 = gm_l7;
            gm_f_op_info_4a09t36((void *)&F.l8, (void *)NULL, cx, NULL, t42, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t45 = gm_l0;
            gm_tl874t2atnlh3 t46 = gm_l3;
            gm_b0_1 t47 = gm_f_def_once_pqgvduh((void *)NULL, cx, NULL, t45, t46, &(*(gm_t6ok5tvcjc200 *)(((uint8_t *)&F.l8) + 8)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me25;
mn36:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 193, 5);
me25:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_expr_kih3eo8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_expr_kih3eo8 F;
    void * gm_l1; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    gm_t08olq97rscn3 gm_l4; /* id */
    gm_t08olq97rscn3 gm_l6; /* c */
    void * gm_l7; /* base */
    gm_t08olq97rscn3 gm_l8; /* name */
    void * gm_l9; /* callee */
    gm_b16_8 gm_l10; /* args */
    void * gm_l11; /* sg */
    void * gm_l12; /* lit */
    void * gm_l13; /* h */
    void * gm_l14; /* f */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    void * gm_l19; /* ty */
    void * gm_l20; /* i */
    void * gm_l21; /* m */
    void * gm_l22; /* x */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* i */
    void * gm_l26; /* on */
    void * gm_l27; /* f */
    void * gm_l30; /* v */
    GM_STACK_CHECK(&F, 1637u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t27) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma4; case ((uint8_t)4ULL): goto ma4; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma4; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma12; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma13; case ((uint8_t)33ULL): goto ma14; case ((uint8_t)34ULL): goto ma15; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma16; case ((uint8_t)37ULL): goto ma17; case ((uint8_t)38ULL): goto ma18; case ((uint8_t)39ULL): goto ma19; case ((uint8_t)40ULL): goto ma20; case ((uint8_t)41ULL): goto ma21; case ((uint8_t)42ULL): goto ma22; case ((uint8_t)43ULL): goto ma23; case ((uint8_t)44ULL): goto ma24; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma25; case ((uint8_t)47ULL): goto ma5; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma4; default: goto ma26; }
ma4:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t31 != ((uint8_t)0ULL)) goto pa30;
        goto po29;
pa30:;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t33 != ((uint8_t)1ULL)) goto pa32;
        goto po29;
pa32:;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t35 != ((uint8_t)2ULL)) goto pa34;
        goto po29;
pa34:;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t37 != ((uint8_t)3ULL)) goto pa36;
        goto po29;
pa36:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t39 != ((uint8_t)4ULL)) goto pa38;
        goto po29;
pa38:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t41 != ((uint8_t)5ULL)) goto pa40;
        goto po29;
pa40:;
        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t43 != ((uint8_t)7ULL)) goto pa42;
        goto po29;
pa42:;
        uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t45 != ((uint8_t)50ULL)) goto pa44;
        goto po29;
pa44:;
        goto mn28;
po29:;
        goto me3;
mn28:;
ma5:;
        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t48 != ((uint8_t)47ULL)) goto mn47;
        gm_t1s75i08qtptd t49 = GM_LD(gm_t1s75i08qtptd, ((((uint8_t *)t2) + 8) + 8));
        F.l2 = t49;
        void * t50 = F.l0;
        gm_b0_1 t51 = gm_f_label_use_9lcr9t0((void *)NULL, cx, NULL, t50, &(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn47:;
ma6:;
        uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t53 != ((uint8_t)6ULL)) goto mn52;
        gm_t08olq97rscn3 t54 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t54;
        void * t55 = F.l0;
        gm_b0_1 t56 = gm_f_name_use_73pph7t((void *)NULL, cx, NULL, t55, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn52:;
ma7:;
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t58 != ((uint8_t)8ULL)) goto mn57;
        gm_t08olq97rscn3 t59 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t59;
        {
            {
                void * t62 = F.l0;
                void * t63 = gm_l1;
                gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 72));
                gm_tf5srh4mp4b3i t65 = gm_f_variant_at_9i78oa0((void *)NULL, cx, NULL, t62, t64, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        goto me3;
mn57:;
ma8:;
        uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t69 != ((uint8_t)9ULL)) goto pa68;
        goto po67;
pa68:;
        uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t71 != ((uint8_t)11ULL)) goto pa70;
        goto po67;
pa70:;
        uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t73 != ((uint8_t)15ULL)) goto pa72;
        goto po67;
pa72:;
        uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t75 != ((uint8_t)16ULL)) goto pa74;
        goto po67;
pa74:;
        uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t77 != ((uint8_t)18ULL)) goto pa76;
        goto po67;
pa76:;
        uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t79 != ((uint8_t)24ULL)) goto pa78;
        goto po67;
pa78:;
        uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t81 != ((uint8_t)31ULL)) goto pa80;
        goto po67;
pa80:;
        uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t83 != ((uint8_t)12ULL)) goto pa82;
        goto po67;
pa82:;
        uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t85 != ((uint8_t)25ULL)) goto pa84;
        goto po67;
pa84:;
        uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t87 != ((uint8_t)26ULL)) goto pa86;
        goto po67;
pa86:;
        uint8_t t89 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t89 != ((uint8_t)27ULL)) goto pa88;
        goto po67;
pa88:;
        uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t91 != ((uint8_t)35ULL)) goto pa90;
        goto po67;
pa90:;
        uint8_t t93 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t93 != ((uint8_t)10ULL)) goto pa92;
        goto po67;
pa92:;
        uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t95 != ((uint8_t)13ULL)) goto pa94;
        goto po67;
pa94:;
        uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t97 != ((uint8_t)17ULL)) goto pa96;
        goto po67;
pa96:;
        uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t99 != ((uint8_t)29ULL)) goto pa98;
        goto po67;
pa98:;
        uint8_t t101 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t101 != ((uint8_t)28ULL)) goto pa100;
        goto po67;
pa100:;
        uint8_t t103 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t103 != ((uint8_t)20ULL)) goto pa102;
        goto po67;
pa102:;
        uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t105 != ((uint8_t)21ULL)) goto pa104;
        goto po67;
pa104:;
        uint8_t t107 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t107 != ((uint8_t)23ULL)) goto pa106;
        goto po67;
pa106:;
        uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t109 != ((uint8_t)48ULL)) goto pa108;
        goto po67;
pa108:;
        uint8_t t111 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t111 != ((uint8_t)45ULL)) goto pa110;
        goto po67;
pa110:;
        uint8_t t113 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t113 != ((uint8_t)49ULL)) goto pa112;
        goto po67;
pa112:;
        goto mn66;
po67:;
        void * t114 = gm_l1;
        gm_closure t115 = ((gm_closure){(void *)gm_f_fn_rqpk5og, (void *)&F.h, cx});
        gm_f_for_each_child_tkrfoqu(t114, t115);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn66:;
ma9:;
        uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t117 != ((uint8_t)14ULL)) goto mn116;
        gm_tioi3jh9v22g5 t118 = GM_LD(gm_tioi3jh9v22g5, ((((uint8_t *)t2) + 8) + 8));
        F.l5 = t118;
        {
            void * t120 = F.l0;
            void * t121 = GM_LD(void *, (((uint8_t *)&F.l5) + 0));
            gm_b0_1 t122 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t120, t121);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t123 = F.l0;
            void * t124 = GM_LD(void *, (((uint8_t *)&F.l5) + 48));
            uint32_t t125 = GM_LD(uint32_t, ((((uint8_t *)t124) + 72) + 8));
            gm_b0_1 t126 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t123, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            if (!(((((uint8_t *)&F.l5) + 8))[32] != 0)) goto ie127;
            gm_t08olq97rscn3 t128 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l5) + 8));
            gm_l6 = t128;
            {
                void * t130 = F.l0;
                gm_t9qkh0rsooc8d t131;
                GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)8ULL));
                uint32_t t132 = GM_LD(uint32_t, ((((uint8_t *)&gm_l6) + 16) + 8));
                gm_b0_1 t133 = gm_f_declare_feg3ue0((void *)NULL, cx, NULL, t130, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l6)), t131, t132);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix127;
ie127:;
ix127:;
        }
        {
            void * t134 = F.l0;
            void * t135 = GM_LD(void *, (((uint8_t *)&F.l5) + 48));
            gm_b0_1 t136 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t134, t135);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t137 = F.l0;
            gm_f_pop_scope_vgka5ae(t137);
        }
        goto me3;
mn116:;
ma10:;
        uint8_t t139 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t139 != ((uint8_t)22ULL)) goto mn138;
        void * t140 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l7 = t140;
        gm_t08olq97rscn3 t141 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l8 = t141;
        void * t142 = F.l0;
        void * t143 = gm_l1;
        void * t144 = gm_l7;
        gm_b0_1 t145 = gm_f_field_7p9pmjo((void *)NULL, cx, NULL, t142, t143, t144, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn138:;
ma11:;
        uint8_t t147 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t147 != ((uint8_t)19ULL)) goto mn146;
        void * t148 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l9 = t148;
        gm_b16_8 t149 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l10 = t149;
        void * t150 = F.l0;
        void * t151 = gm_l1;
        void * t152 = gm_l9;
        gm_b16_8 t153 = gm_l10;
        gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&t153));
        gm_b0_1 t155 = gm_f_call_8vfr61j((void *)NULL, cx, NULL, t150, t151, t152, t154);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn146:;
ma12:;
        uint8_t t157 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t157 != ((uint8_t)30ULL)) goto mn156;
        void * t158 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l11 = t158;
        void * t159 = F.l0;
        void * t160 = gm_l11;
        gm_b0_1 t161 = gm_f_sig_89f514c((void *)NULL, cx, NULL, t159, t160);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn156:;
ma13:;
        uint8_t t163 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t163 != ((uint8_t)32ULL)) goto mn162;
        void * t164 = F.l0;
        void * t165 = gm_l1;
        gm_b0_1 t166 = gm_f_type_literal_nnom1cv((void *)NULL, cx, NULL, t164, t165);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn162:;
ma14:;
        uint8_t t168 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t168 != ((uint8_t)33ULL)) goto mn167;
        void * t169 = F.l0;
        void * t170 = gm_l1;
        gm_b0_1 t171 = gm_f_type_literal_nnom1cv((void *)NULL, cx, NULL, t169, t170);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn167:;
ma15:;
        uint8_t t173 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t173 != ((uint8_t)34ULL)) goto mn172;
        void * t174 = F.l0;
        void * t175 = gm_l1;
        gm_b0_1 t176 = gm_f_type_literal_nnom1cv((void *)NULL, cx, NULL, t174, t175);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn172:;
ma16:;
        uint8_t t178 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t178 != ((uint8_t)36ULL)) goto mn177;
        void * t179 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l12 = t179;
        void * t180 = F.l0;
        void * t181 = gm_l12;
        gm_b0_1 t182 = gm_f_fn_literal_sodo3l0((void *)NULL, cx, NULL, t180, t181);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn177:;
ma17:;
        uint8_t t184 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t184 != ((uint8_t)37ULL)) goto mn183;
        void * t185 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l13 = t185;
        void * t186 = F.l0;
        void * t187 = gm_l13;
        gm_b0_1 t188 = gm_f_handler_literal_bbdui98((void *)NULL, cx, NULL, t186, t187);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn183:;
ma18:;
        uint8_t t190 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t190 != ((uint8_t)38ULL)) goto mn189;
        goto me3;
mn189:;
ma19:;
        uint8_t t193 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t193 != ((uint8_t)39ULL)) goto mn192;
        void * t194 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l14 = t194;
        {
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                void * t197 = gm_l14;
                uint64_t t198 = GM_LD(uintptr_t, (((uint8_t *)t197) + 24) + 8);
                gm_l15 = t198;
            }
            {
                for (;;) {
                    uint64_t t200 = gm_l16;
                    uint64_t t201 = gm_l15;
                    uint8_t t202 = (t200 < t201);
                    if (!t202) goto lb199;
                    {
                        uint64_t t205 = gm_l16;
                        uint64_t t208 = GM_LD(uint64_t, ((uint8_t *)&t205));
                        gm_l17 = t208;
                        {
                            uint64_t t210 = gm_l17;
                            uint64_t t211 = (uintptr_t)t210;
                            void * t212 = gm_l14;
                            uint64_t t213 = GM_LD(uintptr_t, (((uint8_t *)t212) + 24) + 8);
                            if (t211 >= (uintptr_t)(t213)) gm_trap_index(t211, t213, gm_srcb2hdor4f6g0q, 464, 23);
                            uint8_t t216 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t212) + 24)) + t211 * 144) + 16) + 0));
                            if (t216 != ((uint8_t)0ULL)) goto mn215;
                            gm_tlivgedjs5ps3 t217 = GM_LD(gm_tlivgedjs5ps3, (((((uint8_t *)GM_LD(void *, (((uint8_t *)t212) + 24)) + t211 * 144) + 16) + 8) + 32));
                            F.l18 = t217;
                            void * t218 = F.l0;
                            void * t219 = (void *)((uint8_t *)&F.l18);
                            gm_b0_1 t220 = gm_f_sig_89f514c((void *)NULL, cx, NULL, t218, t219);
                            if (gm_unw.kind) {
                                goto gm_ll203;
                            }
                            goto me214;
mn215:;
                            uint8_t t222 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t212) + 24)) + t211 * 144) + 16) + 0));
                            if (t222 != ((uint8_t)1ULL)) goto mn221;
                            void * t223 = GM_LD(void *, (((((uint8_t *)GM_LD(void *, (((uint8_t *)t212) + 24)) + t211 * 144) + 16) + 8) + 32));
                            gm_l19 = t223;
                            void * t224 = F.l0;
                            void * t225 = gm_l19;
                            gm_b0_1 t226 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t224, t225);
                            if (gm_unw.kind) {
                                goto gm_ll203;
                            }
                            goto me214;
mn221:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 464, 17);
me214:;
                        }
                        goto me206;
mn207:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 463, 13);
me206:;
                    }
lc199:;
                    {
                        uint64_t t228 = gm_l16;
                        uint64_t r229; bool o230 = __builtin_add_overflow(t228, ((uint64_t)1ULL), &r229);
                        uint64_t t231 = r229;
                        gm_l16 = t231;
                    }
                }
gm_ll203:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb199; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc199; }
                }
                goto gm_lad;
lb199:;
            }
        }
        goto me3;
mn192:;
ma20:;
        uint8_t t233 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t233 != ((uint8_t)40ULL)) goto mn232;
        void * t234 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l20 = t234;
        void * t235 = F.l0;
        void * t236 = gm_l20;
        gm_b0_1 t237 = gm_f_if_expr_6oqub48((void *)NULL, cx, NULL, t235, t236);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn232:;
ma21:;
        uint8_t t239 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t239 != ((uint8_t)41ULL)) goto mn238;
        void * t240 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l21 = t240;
        void * t241 = F.l0;
        void * t242 = gm_l21;
        gm_b0_1 t243 = gm_f_match_expr_j2bvhsh((void *)NULL, cx, NULL, t241, t242);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn238:;
ma22:;
        uint8_t t245 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t245 != ((uint8_t)42ULL)) goto mn244;
        void * t246 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l22 = t246;
        {
            {
                gm_l24 = ((uint64_t)0ULL);
            }
            {
                void * t249 = gm_l22;
                uint64_t t250 = GM_LD(uintptr_t, (((uint8_t *)t249) + 40) + 8);
                gm_l23 = t250;
            }
            {
                for (;;) {
                    uint64_t t252 = gm_l24;
                    uint64_t t253 = gm_l23;
                    uint8_t t254 = (t252 < t253);
                    if (!t254) goto lb251;
                    {
                        uint64_t t257 = gm_l24;
                        uint64_t t260 = GM_LD(uint64_t, ((uint8_t *)&t257));
                        gm_l25 = t260;
                        {
                            void * t262 = F.l0;
                            uint64_t t263 = gm_l25;
                            uint64_t t264 = (uintptr_t)t263;
                            void * t265 = gm_l22;
                            uint64_t t266 = GM_LD(uintptr_t, (((uint8_t *)t265) + 40) + 8);
                            if (t264 >= (uintptr_t)(t266)) gm_trap_index(t264, t266, gm_srcb2hdor4f6g0q, 473, 50);
                            void * t267 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t265) + 40)) + t264 * 88);
                            gm_b0_1 t268 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t262, t267);
                            if (gm_unw.kind) {
                                goto gm_ll255;
                            }
                        }
                        goto me258;
mn259:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 473, 13);
me258:;
                    }
lc251:;
                    {
                        uint64_t t270 = gm_l24;
                        uint64_t r271; bool o272 = __builtin_add_overflow(t270, ((uint64_t)1ULL), &r271);
                        uint64_t t273 = r271;
                        gm_l24 = t273;
                    }
                }
gm_ll255:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb251; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc251; }
                }
                goto gm_lad;
lb251:;
            }
        }
        {
            void * t275 = gm_l22;
            void * t276 = GM_LD(void *, (((uint8_t *)t275) + 56));
            if (!(GM_LD(void *, ((uint8_t *)&t276)) != 0)) goto ie274;
            void * t277 = GM_LD(void *, ((uint8_t *)&t276));
            gm_l26 = t277;
            {
                void * t279 = F.l0;
                void * t280 = gm_l26;
                gm_b0_1 t281 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t279, t280);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix274;
ie274:;
ix274:;
        }
        {
            void * t282 = F.l0;
            void * t283 = gm_l22;
            gm_b0_1 t284 = gm_f_push_label_gg2j5c3((void *)NULL, cx, NULL, t282, &(*(gm_t1s75i08qtptd *)(((uint8_t *)t283) + 0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t285 = F.l0;
            void * t286 = gm_l22;
            void * t287 = (void *)(((uint8_t *)t286) + 64);
            gm_b0_1 t288 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t285, t287);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t289 = F.l0;
            void * t290 = gm_l22;
            gm_f_pop_label_e8oj3h3(t289, &(*(gm_t1s75i08qtptd *)(((uint8_t *)t290) + 0)));
        }
        goto me3;
mn244:;
ma23:;
        uint8_t t292 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t292 != ((uint8_t)43ULL)) goto mn291;
        void * t293 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l27 = t293;
        void * t294 = F.l0;
        void * t295 = gm_l27;
        gm_b0_1 t296 = gm_f_for_expr_f16jm2b((void *)NULL, cx, NULL, t294, t295);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn291:;
ma24:;
        uint8_t t298 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t298 != ((uint8_t)44ULL)) goto mn297;
        gm_t79rbdv4rafe5 t299 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t2) + 8) + 8));
        F.l28 = t299;
        void * t300 = F.l0;
        void * t301 = (void *)((uint8_t *)&F.l28);
        gm_b0_1 t302 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t300, t301);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn297:;
ma25:;
        uint8_t t304 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t304 != ((uint8_t)46ULL)) goto mn303;
        gm_tchr52g0ema53 t305 = GM_LD(gm_tchr52g0ema53, ((((uint8_t *)t2) + 8) + 8));
        F.l29 = t305;
        {
            void * t307 = F.l0;
            gm_b0_1 t308 = gm_f_label_use_9lcr9t0((void *)NULL, cx, NULL, t307, &(*(gm_t1s75i08qtptd *)(((uint8_t *)&F.l29) + 0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t310 = GM_LD(void *, (((uint8_t *)&F.l29) + 40));
            if (!(GM_LD(void *, ((uint8_t *)&t310)) != 0)) goto ie309;
            void * t311 = GM_LD(void *, ((uint8_t *)&t310));
            gm_l30 = t311;
            {
                void * t313 = F.l0;
                void * t314 = gm_l30;
                gm_b0_1 t315 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t313, t314);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix309;
ie309:;
ix309:;
        }
        goto me3;
mn303:;
ma26:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 437, 5);
me3:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_field_7p9pmjo(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, const void * a3) {
    struct gm_fr_field_7p9pmjo F;
    void * gm_l0; /* w */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    gm_t08olq97rscn3 gm_l3; /* name */
    uint64_t gm_l4; /* m */
    uint64_t gm_l5; /* d */
    uint64_t gm_l6; /* d */
    gm_tl874t2atnlh3 gm_l7; /* to */
    uint64_t gm_l8; /* d */
    gm_t88rcucg1b7uk gm_l9; /* t */
    GM_STACK_CHECK(&F, 574u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l2;
        gm_tu0a56t4bfvqh t4 = gm_f_classify_pdaj2gb((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma9; default: goto ma10; }
ma6:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t13 != ((uint8_t)0ULL)) goto mn12;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&t4) + 8));
        gm_l4 = t14;
        {
            void * t16 = gm_l0;
            void * t17 = gm_l2;
            gm_b0_1 t18 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
            void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
            uint64_t t23 = gm_l4;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
            gm_tf5srh4mp4b3i t25 = gm_f_lookup_59juqoo(t22, t23, t24);
            if (!((((uint8_t *)&t25))[8] != 0)) goto ie19;
            uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)&t25));
            gm_l5 = t26;
            {
                void * t28 = gm_l0;
                gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a3) + 16));
                uint64_t t30 = gm_l5;
                gm_tsncjtib3j1l1 t31;
                GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)0ULL));
                GM_ST(uint64_t, ((uint8_t *)&t31 + 8), t30);
                gm_b0_1 t32 = gm_f_use_decl_sfao4j5((void *)NULL, cx, NULL, t28, t29, &t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t33 = gm_l0;
                void * t34 = gm_l1;
                gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t34) + 72));
                uint64_t t36 = gm_l5;
                gm_tsncjtib3j1l1 t37;
                GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t37 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)0ULL));
                GM_ST(uint64_t, ((uint8_t *)&t37 + 8), t36);
                gm_b0_1 t38 = gm_f_note_receiver_6mb4kc5((void *)NULL, cx, NULL, t33, t35, &t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix19;
ie19:;
ix19:;
        }
        goto me5;
mn12:;
ma7:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t40 != ((uint8_t)1ULL)) goto mn39;
        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&t4) + 8));
        gm_l6 = t41;
        {
            void * t43 = gm_l0;
            void * t44 = gm_l2;
            gm_b0_1 t45 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t47 = gm_l0;
            uint64_t t48 = gm_l6;
            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
            gm_toceffb697p0u t50 = gm_f_member_def_9f4kiah((void *)NULL, cx, NULL, t47, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t50))[12] != 0)) goto ie46;
            gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&t50));
            gm_l7 = t51;
            {
                void * t53 = gm_l0;
                gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a3) + 16));
                gm_tl874t2atnlh3 t55 = gm_l7;
                gm_b0_1 t56 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t53, t54, t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix46;
ie46:;
ix46:;
        }
        goto me5;
mn39:;
ma8:;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t58 != ((uint8_t)2ULL)) goto mn57;
        uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&t4) + 8));
        gm_l8 = t59;
        {
            void * t61 = gm_l0;
            void * t62 = gm_l2;
            gm_b0_1 t63 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t64 = gm_l0;
            uint64_t t65 = gm_l8;
            gm_b0_1 t66 = gm_f_operation_use_ue1or58((void *)NULL, cx, NULL, t64, t65, &(*(gm_t08olq97rscn3 *)((uint8_t *)a3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me5;
mn57:;
ma9:;
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t68 != ((uint8_t)3ULL)) goto mn67;
        {
            void * t70 = gm_l0;
            void * t71 = gm_l2;
            gm_b0_1 t72 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t70, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t74 = gm_l0;
            void * t75 = gm_l2;
            gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t75) + 72));
            gm_tkn42u7na3t81 t77 = gm_f_type_of_tiqb21n(t74, GM_CONV(gm_cseahvg8v7p0vs, t76));
            if (!((((uint8_t *)&t77))[4] != 0)) goto ie73;
            gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t77));
            gm_l9 = t78;
            {
                {
                    void * t81 = gm_l0;
                    gm_t88rcucg1b7uk t82 = gm_l9;
                    uint8_t t83 = gm_f_field_use_838f8e8((void *)NULL, cx, NULL, t81, t82, &(*(gm_t08olq97rscn3 *)((uint8_t *)a3)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            goto ix73;
ie73:;
ix73:;
        }
        goto me5;
mn67:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 498, 5);
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
__attribute__((aligned(4))) uint8_t gm_f_field_use_838f8e8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2) {
    struct gm_fr_field_use_838f8e8 F;
    void * gm_l0; /* w */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_t08olq97rscn3 gm_l2; /* name */
    uint64_t gm_l3; /* d */
    gm_tl874t2atnlh3 gm_l4; /* to */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_t88rcucg1b7uk gm_l6; /* pointee */
    uint32_t gm_l7; /* id */
    void * gm_l8; /* info */
    GM_STACK_CHECK(&F, 576u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_tf5srh4mp4b3i t5 = gm_f_adt_decl_of_2bflsov(t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l3 = t6;
    }
    {
        void * t10 = gm_l0;
        uint64_t t11 = gm_l3;
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_toceffb697p0u t13 = gm_f_member_def_9f4kiah((void *)NULL, cx, NULL, t10, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t13))[12] != 0)) goto ie9;
        gm_tl874t2atnlh3 t14 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&t13));
        gm_l4 = t14;
        {
            void * t16 = gm_l0;
            gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
            gm_tl874t2atnlh3 t18 = gm_l4;
            gm_b0_1 t19 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t16, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t20 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_t88rcucg1b7uk t21 = gm_l1;
        gm_l5 = t21;
    }
    {
        for (;;) {
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
            void * t25 = (void *)(((uint8_t *)t24) + 8);
            gm_t88rcucg1b7uk t26 = gm_l5;
            gm_t53at80pfdus3 t27;
            gm_f_kind_jcnt4hm((void *)&t27, t25, GM_CONV(gm_csguankko4lmhd, t26));
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
            if (t28 != ((uint8_t)8ULL)) goto lb22;
            gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t27) + 8) + 4));
            gm_l6 = t29;
            {
                gm_t88rcucg1b7uk t32 = gm_l6;
                gm_l5 = t32;
            }
lc22:;
        }
lb22:;
    }
    {
        void * t34 = gm_l0;
        void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
        void * t36 = (void *)(((uint8_t *)t35) + 8);
        gm_t88rcucg1b7uk t37 = gm_l5;
        gm_t53at80pfdus3 t38;
        gm_f_kind_jcnt4hm((void *)&t38, t36, GM_CONV(gm_csguankko4lmhd, t37));
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&t38) + 0));
        if (t39 != ((uint8_t)17ULL)) goto ie33;
        uint32_t t40 = GM_LD(uint32_t, ((((uint8_t *)&t38) + 8) + 0));
        gm_l7 = t40;
        {
            uint32_t t42 = gm_l7;
            uint64_t t43 = (uint64_t)t42;
            uint64_t t44 = (uintptr_t)t43;
            void * t45 = gm_l0;
            void * t46 = GM_LD(void *, (((uint8_t *)t45) + 0));
            uint64_t t47 = GM_LD(uintptr_t, ((((uint8_t *)t46) + 1088) + 0) + 8);
            if (t44 >= (uintptr_t)(t47)) gm_trap_index(t44, t47, gm_srcb2hdor4f6g0q, 380, 18);
            void * t48 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t46) + 1088) + 0)) + t44 * 184);
            gm_l8 = t48;
        }
        {
            void * t50 = gm_l8;
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)t50) + 112));
            if (!t51) goto ie49;
            {
                void * t54 = gm_l8;
                uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t54) + 24) + 0));
                if (t55 != ((uint8_t)0ULL)) goto ie53;
                gm_tpautr4br7h3l t56 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t54) + 24) + 8));
                F.l9 = t56;
                {
                    uint64_t t59 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                    uint8_t t60 = (t59 > ((uint64_t)0ULL));
                    if (!t60) goto ie58;
                    {
                        void * t62 = gm_l0;
                        uint64_t t63 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t64 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                        if (t63 >= (uintptr_t)(t64)) gm_trap_index(t63, t64, gm_srcb2hdor4f6g0q, 383, 60);
                        gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)) + t63 * 40) + 16));
                        uint8_t t66 = gm_f_field_use_838f8e8((void *)NULL, cx, NULL, t62, t65, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t67 = 0;
                        F.ret = t66;
                        goto gm_fret;
                    }
                    goto ix58;
ie58:;
ix58:;
                }
                goto ix53;
ie53:;
ix53:;
            }
            goto ix49;
ie49:;
ix49:;
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        uint8_t t68 = 0;
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
__attribute__((aligned(4))) void gm_f_find_field_l0p9325(gm_b16_8 a0, gm_b16_8 a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_field_l0p9325 F;
    gm_b16_8 gm_l0; /* members */
    gm_b16_8 gm_l1; /* name */
    void * gm_l2; /* found */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t08olq97rscn3 gm_l9; /* n */
    gm_b16_8 gm_l11; /* e */
    GM_STACK_CHECK(&F, 762u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l3 = t3;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tfc3oljlofib6 t15 = GM_LD(gm_tfc3oljlofib6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 80));
                    F.l5 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        gm_t24301fas1b2a t20 = GM_LD(gm_t24301fas1b2a, (((uint8_t *)&F.l5) + 8));
                        F.l6 = t20;
                        {
                            {
                                gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 24));
                                gm_l7 = t23;
                            }
                            {
                                gm_l8 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t25 = gm_l8;
                                    uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    uint8_t t27 = (t25 < t26);
                                    if (!t27) goto lb24;
                                    {
                                        uint64_t t30 = gm_l8;
                                        uint64_t t31 = (uintptr_t)t30;
                                        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                        gm_t08olq97rscn3 t35 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t31 * 32));
                                        gm_l9 = t35;
                                        {
                                            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
                                            gm_b16_8 t39 = gm_l1;
                                            uint8_t t40 = (gm_eq0iqiubfm7e(((uint8_t *)&t38), ((uint8_t *)&t39)));
                                            if (!t40) goto ie37;
                                            {
                                                void * t43 = gm_l2;
                                                uint8_t t44;
                                                if (((((uint8_t *)t43))[28] != 0)) goto mn46;
                                                t44 = ((uint8_t)1);
                                                goto me45;
mn46:;
                                                t44 = ((uint8_t)0);
                                                goto me45;
mn47:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 288, 28);
me45:;
                                                if (!t44) goto ie42;
                                                {
                                                    void * t49 = gm_l2;
                                                    gm_tt7dbdq4n1egc t50;
                                                    memset((uint8_t *)&t50 + 25, 0, 3);
                                                    gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l9) + 16));
                                                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t50 + 0, t51);
                                                    gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 56));
                                                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t50 + 12, t52);
                                                    GM_ST(uint8_t, (uint8_t *)&t50 + 24, ((uint8_t)0));
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tkol5reu9t1u2 *)((uint8_t *)t49)) + 29, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tkol5reu9t1u2 *)((uint8_t *)t49)) + 30, 0);
                                                    ((uint8_t *)&(*(gm_tkol5reu9t1u2 *)((uint8_t *)t49)))[28] = 1;
                                                    GM_ST(gm_tt7dbdq4n1egc, (uint8_t *)&(*(gm_tkol5reu9t1u2 *)((uint8_t *)t49)) + 0, t50);
                                                }
                                                goto ix42;
ie42:;
ix42:;
                                            }
                                            {
                                                goto lb24;
                                            }
                                            goto ix37;
ie37:;
ix37:;
                                        }
                                        goto me33;
mn34:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 286, 17);
me33:;
                                    }
lc24:;
                                    {
                                        uint64_t t54 = gm_l8;
                                        uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                                        uint64_t t57 = r55;
                                        gm_l8 = t57;
                                    }
                                }
lb24:;
                            }
                        }
                        goto me17;
mn18:;
                        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                        if (t59 != ((uint8_t)1ULL)) goto mn58;
                        gm_t7bjbmukv26e7 t60 = GM_LD(gm_t7bjbmukv26e7, (((uint8_t *)&F.l5) + 8));
                        F.l10 = t60;
                        {
                            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 8));
                            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
                            gm_b16_8 t64 = gm_l1;
                            void * t65 = gm_l2;
                            gm_f_find_field_l0p9325(t63, t64, t65);
                        }
                        {
                            if (!(((((uint8_t *)&F.l10) + 24))[16] != 0)) goto ie66;
                            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 24));
                            gm_l11 = t67;
                            {
                                gm_b16_8 t69 = gm_l11;
                                gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                                gm_b16_8 t71 = gm_l1;
                                void * t72 = gm_l2;
                                gm_f_find_field_l0p9325(t70, t71, t72);
                            }
                            goto ix66;
ie66:;
ix66:;
                        }
                        goto me17;
mn58:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 284, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 283, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t74 = gm_l4;
                    uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                    uint64_t t77 = r75;
                    gm_l4 = t77;
                }
            }
lb4:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_find_payload_field_i16vrf1(gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, void * a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_payload_field_i16vrf1 F;
    gm_b16_8 gm_l0; /* members */
    gm_b16_8 gm_l1; /* variant */
    gm_b16_8 gm_l2; /* field */
    void * gm_l3; /* found */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    void * gm_l8; /* p */
    gm_b16_8 gm_l9; /* elems */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t08olq97rscn3 gm_l13; /* n */
    gm_b16_8 gm_l15; /* e */
    GM_STACK_CHECK(&F, 1004u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t03a3imodqi32 t15 = GM_LD(gm_t03a3imodqi32, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 72));
                    F.l6 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        gm_tnljbma3s3h16 t20 = GM_LD(gm_tnljbma3s3h16, (((uint8_t *)&F.l6) + 8));
                        F.l7 = t20;
                        {
                            gm_b16_8 t23 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l7) + 0) + 0));
                            gm_b16_8 t24 = gm_l1;
                            uint8_t t25 = (gm_eq0iqiubfm7e(((uint8_t *)&t23), ((uint8_t *)&t24)));
                            if (!t25) goto ie22;
                            {
                                void * t28 = GM_LD(void *, (((uint8_t *)&F.l7) + 32));
                                if (!(GM_LD(void *, ((uint8_t *)&t28)) != 0)) goto ie27;
                                void * t29 = GM_LD(void *, ((uint8_t *)&t28));
                                gm_l8 = t29;
                                {
                                    void * t32 = gm_l8;
                                    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                                    if (t33 != ((uint8_t)10ULL)) goto ie31;
                                    gm_b16_8 t34 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 8));
                                    gm_l9 = t34;
                                    {
                                        {
                                            gm_b16_8 t37 = gm_l9;
                                            gm_l10 = t37;
                                        }
                                        {
                                            gm_l11 = ((uint64_t)0ULL);
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t39 = gm_l11;
                                                uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                                uint8_t t41 = (t39 < t40);
                                                if (!t41) goto lb38;
                                                {
                                                    uint64_t t44 = gm_l11;
                                                    uint64_t t45 = (uintptr_t)t44;
                                                    uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                                    gm_tv1o1j44ii4db t49 = GM_LD(gm_tv1o1j44ii4db, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t45 * 136));
                                                    F.l12 = t49;
                                                    {
                                                        if (!(((((uint8_t *)&F.l12) + 0))[32] != 0)) goto ie51;
                                                        gm_t08olq97rscn3 t52 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l12) + 0));
                                                        gm_l13 = t52;
                                                        {
                                                            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                                                            gm_b16_8 t56 = gm_l2;
                                                            uint8_t t57 = (gm_eq0iqiubfm7e(((uint8_t *)&t55), ((uint8_t *)&t56)));
                                                            if (!t57) goto ie54;
                                                            {
                                                                void * t60 = gm_l3;
                                                                uint8_t t61;
                                                                if (((((uint8_t *)t60))[12] != 0)) goto mn63;
                                                                t61 = ((uint8_t)1);
                                                                goto me62;
mn63:;
                                                                t61 = ((uint8_t)0);
                                                                goto me62;
mn64:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 327, 44);
me62:;
                                                                if (!t61) goto ie59;
                                                                {
                                                                    void * t66 = gm_l3;
                                                                    gm_tl874t2atnlh3 t67 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 16));
                                                                    gm_toceffb697p0u t68;
                                                                    GM_ST(uint8_t, (uint8_t *)&t68 + 13, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t68 + 14, 0);
                                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t68), t67);
                                                                    ((uint8_t *)&t68)[12] = 1;
                                                                    GM_ST(gm_toceffb697p0u, ((uint8_t *)t66), t68);
                                                                }
                                                                goto ix59;
ie59:;
ix59:;
                                                            }
                                                            {
                                                                goto lb38;
                                                            }
                                                            goto ix54;
ie54:;
ix54:;
                                                        }
                                                        goto ix51;
ie51:;
ix51:;
                                                    }
                                                    goto me47;
mn48:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 324, 29);
me47:;
                                                }
lc38:;
                                                {
                                                    uint64_t t70 = gm_l11;
                                                    uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                                                    uint64_t t73 = r71;
                                                    gm_l11 = t73;
                                                }
                                            }
lb38:;
                                        }
                                    }
                                    goto ix31;
ie31:;
ix31:;
                                }
                                goto ix27;
ie27:;
ix27:;
                            }
                            goto ix22;
ie22:;
ix22:;
                        }
                        goto me17;
mn18:;
                        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t75 != ((uint8_t)1ULL)) goto mn74;
                        gm_t7bjbmukv26e7 t76 = GM_LD(gm_t7bjbmukv26e7, (((uint8_t *)&F.l6) + 8));
                        F.l14 = t76;
                        {
                            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 8));
                            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                            gm_b16_8 t80 = gm_l1;
                            gm_b16_8 t81 = gm_l2;
                            void * t82 = gm_l3;
                            gm_f_find_payload_field_i16vrf1(t79, t80, t81, t82);
                        }
                        {
                            if (!(((((uint8_t *)&F.l14) + 24))[16] != 0)) goto ie83;
                            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 24));
                            gm_l15 = t84;
                            {
                                gm_b16_8 t86 = gm_l15;
                                gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                                gm_b16_8 t88 = gm_l1;
                                gm_b16_8 t89 = gm_l2;
                                void * t90 = gm_l3;
                                gm_f_find_payload_field_i16vrf1(t87, t88, t89, t90);
                            }
                            goto ix83;
ie83:;
ix83:;
                        }
                        goto me17;
mn74:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 319, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 318, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t92 = gm_l5;
                    uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                    uint64_t t95 = r93;
                    gm_l5 = t95;
                }
            }
lb4:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_find_variant_kifk1mm(gm_b16_8 a0, gm_b16_8 a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_variant_kifk1mm F;
    gm_b16_8 gm_l0; /* members */
    gm_b16_8 gm_l1; /* name */
    void * gm_l2; /* found */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l8; /* e */
    GM_STACK_CHECK(&F, 585u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l3 = t3;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t03a3imodqi32 t15 = GM_LD(gm_t03a3imodqi32, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 72));
                    F.l5 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        gm_tnljbma3s3h16 t20 = GM_LD(gm_tnljbma3s3h16, (((uint8_t *)&F.l5) + 8));
                        F.l6 = t20;
                        {
                            gm_b16_8 t23 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l6) + 0) + 0));
                            gm_b16_8 t24 = gm_l1;
                            uint8_t t25 = (gm_eq0iqiubfm7e(((uint8_t *)&t23), ((uint8_t *)&t24)));
                            if (!t25) goto ie22;
                            {
                                void * t28 = gm_l2;
                                uint8_t t29;
                                if (((((uint8_t *)t28))[28] != 0)) goto mn31;
                                t29 = ((uint8_t)1);
                                goto me30;
mn31:;
                                t29 = ((uint8_t)0);
                                goto me30;
mn32:;
                                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 306, 24);
me30:;
                                if (!t29) goto ie27;
                                {
                                    void * t34 = gm_l2;
                                    gm_tt7dbdq4n1egc t35;
                                    memset((uint8_t *)&t35 + 25, 0, 3);
                                    gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l6) + 0) + 16));
                                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t35 + 0, t36);
                                    gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 48));
                                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t35 + 12, t37);
                                    GM_ST(uint8_t, (uint8_t *)&t35 + 24, ((uint8_t)1));
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tkol5reu9t1u2 *)((uint8_t *)t34)) + 29, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tkol5reu9t1u2 *)((uint8_t *)t34)) + 30, 0);
                                    ((uint8_t *)&(*(gm_tkol5reu9t1u2 *)((uint8_t *)t34)))[28] = 1;
                                    GM_ST(gm_tt7dbdq4n1egc, (uint8_t *)&(*(gm_tkol5reu9t1u2 *)((uint8_t *)t34)) + 0, t35);
                                }
                                goto ix27;
ie27:;
ix27:;
                            }
                            goto ix22;
ie22:;
ix22:;
                        }
                        goto me17;
mn18:;
                        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                        if (t39 != ((uint8_t)1ULL)) goto mn38;
                        gm_t7bjbmukv26e7 t40 = GM_LD(gm_t7bjbmukv26e7, (((uint8_t *)&F.l5) + 8));
                        F.l7 = t40;
                        {
                            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 8));
                            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                            gm_b16_8 t44 = gm_l1;
                            void * t45 = gm_l2;
                            gm_f_find_variant_kifk1mm(t43, t44, t45);
                        }
                        {
                            if (!(((((uint8_t *)&F.l7) + 24))[16] != 0)) goto ie46;
                            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 24));
                            gm_l8 = t47;
                            {
                                gm_b16_8 t49 = gm_l8;
                                gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                                gm_b16_8 t51 = gm_l1;
                                void * t52 = gm_l2;
                                gm_f_find_variant_kifk1mm(t50, t51, t52);
                            }
                            goto ix46;
ie46:;
ix46:;
                        }
                        goto me17;
mn38:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 303, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 302, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t54 = gm_l4;
                    uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                    uint64_t t57 = r55;
                    gm_l4 = t57;
                }
            }
lb4:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rqpk5og(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_use_l994th9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2) {
    struct gm_fr_fn_use_l994th9 F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* from */
    uint32_t gm_l2; /* f */
    gm_tl874t2atnlh3 gm_l3; /* to */
    uint64_t gm_l4; /* d */
    GM_STACK_CHECK(&F, 228u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l2;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 344) + 0) + 8);
        if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_srcb2hdor4f6g0q, 591, 11);
        gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t4 * 136) + 16));
        gm_l3 = t8;
    }
    {
        void * t10 = gm_l0;
        void * t11 = (void *)(((uint8_t *)t10) + 224);
        uint32_t t12 = gm_l2;
        gm_tf5srh4mp4b3i t13 = gm_f_get_1ji4too(t11, t12);
        if (!((((uint8_t *)&t13))[8] != 0)) goto ie9;
        uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t13));
        gm_l4 = t14;
        {
            void * t16 = gm_l0;
            gm_tl874t2atnlh3 t17 = gm_l1;
            uint64_t t18 = gm_l4;
            gm_tsncjtib3j1l1 t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t19 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t19 + 8), t18);
            gm_b0_1 t20 = gm_f_use_decl_sfao4j5((void *)NULL, cx, NULL, t16, t17, &t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix9;
ie9:;
        {
            void * t22 = gm_l0;
            gm_tl874t2atnlh3 t23 = gm_l1;
            gm_tl874t2atnlh3 t24 = gm_l3;
            gm_b0_1 t25 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t22, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix9:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_expr_f16jm2b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_for_expr_f16jm2b F;
    void * gm_l0; /* w */
    void * gm_l1; /* f */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 56));
        gm_b0_1 t5 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t2, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l1;
        uint32_t t8 = GM_LD(uint32_t, (((((uint8_t *)t7) + 64) + 24) + 8));
        gm_b0_1 t9 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t6, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t11 = gm_l1;
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 40) + 8);
            gm_l2 = t12;
        }
        {
            for (;;) {
                uint64_t t14 = gm_l3;
                uint64_t t15 = gm_l2;
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l3;
                    uint64_t t22 = GM_LD(uint64_t, ((uint8_t *)&t19));
                    gm_l4 = t22;
                    {
                        void * t24 = gm_l0;
                        uint64_t t25 = gm_l4;
                        uint64_t t26 = (uintptr_t)t25;
                        void * t27 = gm_l1;
                        uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 40) + 8);
                        if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_srcb2hdor4f6g0q, 716, 41);
                        void * t29 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t27) + 40)) + t26 * 64);
                        gm_tkn42u7na3t81 t30;
                        memset(&t30, 0, sizeof t30);
                        uint64_t t31 = gm_l4;
                        uint64_t t32 = (uintptr_t)t31;
                        void * t33 = gm_l1;
                        uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 40) + 8);
                        if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_srcb2hdor4f6g0q, 716, 58);
                        uint32_t t35 = GM_LD(uint32_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t33) + 40)) + t32 * 64) + 48) + 8));
                        gm_b0_1 t36 = gm_f_pattern_jo09k6f((void *)NULL, cx, NULL, t24, t29, t30, t35);
                        if (gm_unw.kind) {
                            goto gm_ll17;
                        }
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 716, 5);
me20:;
                }
lc13:;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
            }
gm_ll17:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb13; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc13; }
            }
            goto gm_lad;
lb13:;
        }
    }
    {
        void * t42 = gm_l0;
        void * t43 = gm_l1;
        gm_b0_1 t44 = gm_f_push_label_gg2j5c3((void *)NULL, cx, NULL, t42, &(*(gm_t1s75i08qtptd *)(((uint8_t *)t43) + 0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t45 = gm_l0;
        void * t46 = gm_l1;
        void * t47 = (void *)(((uint8_t *)t46) + 64);
        gm_b0_1 t48 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t45, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t49 = gm_l0;
        void * t50 = gm_l1;
        gm_f_pop_label_e8oj3h3(t49, &(*(gm_t1s75i08qtptd *)(((uint8_t *)t50) + 0)));
    }
    {
        void * t51 = gm_l0;
        gm_f_pop_scope_vgka5ae(t51);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_if_expr_6oqub48(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_if_expr_6oqub48 F;
    void * gm_l0; /* w */
    void * gm_l1; /* x */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l5; /* br */
    gm_t08olq97rscn3 gm_l8; /* name */
    void * gm_l9; /* body */
    GM_STACK_CHECK(&F, 480u);
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
            void * t3 = gm_l1;
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l4 = t14;
                    {
                        uint64_t t16 = gm_l4;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l1;
                        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcb2hdor4f6g0q, 681, 16);
                        void * t20 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)) + t17 * 72);
                        gm_l5 = t20;
                    }
                    {
                        void * t21 = gm_l0;
                        void * t22 = gm_l5;
                        uint32_t t23 = GM_LD(uint32_t, (((((uint8_t *)t22) + 32) + 24) + 8));
                        gm_b0_1 t24 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t21, t23);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        void * t25 = gm_l0;
                        void * t26 = gm_l5;
                        void * t27 = (void *)(((uint8_t *)t26) + 0);
                        gm_b0_1 t28 = gm_f_cond_d94lif5((void *)NULL, cx, NULL, t25, t27);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        void * t29 = gm_l0;
                        void * t30 = gm_l5;
                        void * t31 = (void *)(((uint8_t *)t30) + 32);
                        gm_b0_1 t32 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t29, t31);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        void * t33 = gm_l0;
                        gm_f_pop_scope_vgka5ae(t33);
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 680, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t35 = gm_l3;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l3 = t38;
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
        void * t40 = gm_l1;
        if (!(((((uint8_t *)t40) + 16))[48] != 0)) goto ie39;
        gm_ttja7ii61nila t41 = GM_LD(gm_ttja7ii61nila, (((uint8_t *)t40) + 16));
        F.l6 = t41;
        {
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
            if (t45 != ((uint8_t)0ULL)) goto mn44;
            gm_t79rbdv4rafe5 t46 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&F.l6) + 8));
            F.l7 = t46;
            void * t47 = gm_l0;
            void * t48 = (void *)((uint8_t *)&F.l7);
            gm_b0_1 t49 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t47, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me43;
mn44:;
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
            if (t51 != ((uint8_t)1ULL)) goto mn50;
            gm_t08olq97rscn3 t52 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)&F.l6) + 8) + 0));
            gm_l8 = t52;
            void * t53 = GM_LD(void *, ((((uint8_t *)&F.l6) + 8) + 32));
            gm_l9 = t53;
            {
                void * t55 = gm_l0;
                void * t56 = gm_l9;
                uint32_t t57 = GM_LD(uint32_t, ((((uint8_t *)t56) + 72) + 8));
                gm_b0_1 t58 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t55, t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t59 = gm_l0;
                gm_t9qkh0rsooc8d t60;
                GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)8ULL));
                uint32_t t61 = GM_LD(uint32_t, ((((uint8_t *)&gm_l8) + 16) + 8));
                gm_b0_1 t62 = gm_f_declare_feg3ue0((void *)NULL, cx, NULL, t59, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l8)), t60, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t63 = gm_l0;
                void * t64 = gm_l9;
                gm_b0_1 t65 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t63, t64);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t66 = gm_l0;
                gm_f_pop_scope_vgka5ae(t66);
            }
            goto me43;
mn50:;
            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 688, 9);
me43:;
        }
        goto ix39;
ie39:;
ix39:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_label_use_9lcr9t0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_label_use_9lcr9t0 F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l2; /* l */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 292u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_t1s75i08qtptd t2 = GM_LD(gm_t1s75i08qtptd, ((uint8_t *)a1));
        gm_t08olq97rscn3 t3;
        if (((((uint8_t *)&t2))[32] != 0)) {
            t3 = (*(gm_t08olq97rscn3 *)(((uint8_t *)&t2) + 0));
        } else {
            gm_b0_1 t4;
            uint8_t t5 = 0;
            F.ret = t4;
            goto gm_fret;
        }
        gm_l2 = t3;
    }
    {
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 72) + 0) + 8);
        gm_l3 = t7;
    }
    {
        for (;;) {
            uint64_t t9 = gm_l3;
            uint8_t t10 = (t9 > ((uint64_t)0ULL));
            if (!t10) goto lb8;
            {
                uint64_t t13 = gm_l3;
                uint64_t r14; bool o15 = __builtin_sub_overflow(t13, ((uint64_t)1ULL), &r14);
                if (o15) gm_trap("integer overflow in `-`", gm_srcb2hdor4f6g0q, 740, 9);
                uint64_t t16 = r14;
                gm_l3 = t16;
            }
            {
                uint64_t t18 = gm_l3;
                uint64_t t19 = (uintptr_t)t18;
                void * t20 = gm_l0;
                uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 72) + 0) + 8);
                if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srcb2hdor4f6g0q, 741, 12);
                gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 72) + 0)) + t19 * 32) + 0));
                gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                uint8_t t24 = (gm_eq0iqiubfm7e(((uint8_t *)&t22), ((uint8_t *)&t23)));
                if (!t24) goto ie17;
                {
                    void * t26 = gm_l0;
                    gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
                    uint64_t t28 = gm_l3;
                    uint64_t t29 = (uintptr_t)t28;
                    void * t30 = gm_l0;
                    uint64_t t31 = GM_LD(uintptr_t, ((((uint8_t *)t30) + 72) + 0) + 8);
                    if (t29 >= (uintptr_t)(t31)) gm_trap_index(t29, t31, gm_srcb2hdor4f6g0q, 742, 31);
                    gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t30) + 72) + 0)) + t29 * 32) + 16));
                    gm_b0_1 t33 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t26, t27, t32);
                    if (gm_unw.kind) {
                        goto gm_ll11;
                    }
                }
                {
                    gm_b0_1 t34;
                    uint8_t t35 = 0;
                    F.ret = t34;
                    goto gm_fret;
                }
                goto ix17;
ie17:;
ix17:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_local_binding_25t1g13(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_local_binding_25t1g13 F;
    void * gm_l0; /* w */
    void * gm_l1; /* b */
    void * gm_l2; /* t */
    void * gm_l3; /* lit */
    gm_t08olq97rscn3 gm_l4; /* name */
    gm_tl874t2atnlh3 gm_l5; /* head */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t08olq97rscn3 gm_l8; /* n */
    GM_STACK_CHECK(&F, 660u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 24));
        if (!(GM_LD(void *, ((uint8_t *)&t4)) != 0)) goto ie2;
        void * t5 = GM_LD(void *, ((uint8_t *)&t4));
        gm_l2 = t5;
        {
            void * t7 = gm_l0;
            void * t8 = gm_l2;
            gm_b0_1 t9 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l1;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 40));
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
        if (t13 != ((uint8_t)36ULL)) goto ie10;
        void * t14 = GM_LD(void *, ((((uint8_t *)t12) + 8) + 8));
        gm_l3 = t14;
        {
            void * t17 = gm_l1;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 8) + 8);
            uint8_t t19 = ((t18 == ((uint64_t)1ULL)));
            if (!t19) goto ie16;
            {
                uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
                void * t22 = gm_l1;
                uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)t22) + 8) + 8);
                if (t21 >= (uintptr_t)(t23)) gm_trap_index(t21, t23, gm_srcb2hdor4f6g0q, 652, 21);
                gm_t08olq97rscn3 t24 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, (((uint8_t *)t22) + 8)) + t21 * 32));
                gm_l4 = t24;
            }
            {
                void * t25 = gm_l0;
                gm_t9qkh0rsooc8d t26;
                GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)1ULL));
                uint32_t t27 = GM_LD(uint32_t, ((((uint8_t *)&gm_l4) + 16) + 8));
                gm_b0_1 t28 = gm_f_declare_feg3ue0((void *)NULL, cx, NULL, t25, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l4)), t26, t27);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint32_t t29 = GM_LD(uint32_t, ((((uint8_t *)&gm_l4) + 16) + 0));
                uint32_t t30 = GM_LD(uint32_t, ((((uint8_t *)&gm_l4) + 16) + 4));
                void * t31 = gm_l3;
                uint32_t t32 = GM_LD(uint32_t, (((((uint8_t *)t31) + 0) + 56) + 8));
                gm_tl874t2atnlh3 t33;
                GM_ST(uint32_t, ((uint8_t *)&t33 + 0), t29);
                GM_ST(uint32_t, ((uint8_t *)&t33 + 4), t30);
                GM_ST(uint32_t, ((uint8_t *)&t33 + 8), t32);
                gm_l5 = t33;
            }
            {
                void * t34 = gm_l0;
                void * t35 = GM_LD(void *, (((uint8_t *)t34) + 16));
                void * t36 = (void *)(((uint8_t *)t35) + 40);
                gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                gm_t6ok5tvcjc200 t38;
                void * t39 = gm_l0;
                void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
                gm_tl874t2atnlh3 t41 = gm_l5;
                gm_b16_8 t42 = gm_f_src_fjcc76p(t40, GM_CONV(gm_cseahvg8v7p0vs, t41));
                gm_b16_8 t43 = gm_f_trim_fdrfe6i(t42);
                GM_ST(gm_b16_8, (uint8_t *)&t38 + 0, t43);
                gm_th2lqbo77pvsc t44;
                memset(&t44, 0, sizeof t44);
                GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t38 + 16, t44);
                gm_tftb1odc739t5 t45 = gm_f_put_rt5d34n(t36, GM_CONV(gm_cseahvg8v7p0vs, t37), &t38);
                if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srcb2hdor4f6g0q, 656, 13);
                gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
            }
            {
                void * t47 = gm_l0;
                void * t48 = gm_l1;
                void * t49 = GM_LD(void *, (((uint8_t *)t48) + 40));
                gm_b0_1 t50 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t47, t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t51;
                uint8_t t52 = 0;
                F.ret = t51;
                goto gm_fret;
            }
            goto ix16;
ie16:;
ix16:;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t53 = gm_l0;
        void * t54 = gm_l1;
        void * t55 = GM_LD(void *, (((uint8_t *)t54) + 40));
        gm_b0_1 t56 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t53, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t58 = gm_l1;
            gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)t58) + 8));
            gm_l6 = t59;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t61 = gm_l7;
                uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t63 = (t61 < t62);
                if (!t63) goto lb60;
                {
                    uint64_t t66 = gm_l7;
                    uint64_t t67 = (uintptr_t)t66;
                    uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t08olq97rscn3 t71 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t67 * 32));
                    gm_l8 = t71;
                    {
                        void * t73 = gm_l0;
                        gm_t9qkh0rsooc8d t74;
                        GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)8ULL));
                        void * t75 = gm_l1;
                        uint32_t t76 = GM_LD(uint32_t, ((((uint8_t *)t75) + 48) + 8));
                        gm_b0_1 t77 = gm_f_declare_feg3ue0((void *)NULL, cx, NULL, t73, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l8)), t74, t76);
                        if (gm_unw.kind) {
                            goto gm_ll64;
                        }
                    }
                    goto me69;
mn70:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 662, 5);
me69:;
                }
lc60:;
                {
                    uint64_t t79 = gm_l7;
                    uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                    uint64_t t82 = r80;
                    gm_l7 = t82;
                }
            }
gm_ll64:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb60; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc60; }
            }
            goto gm_lad;
lb60:;
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
inline __attribute__((aligned(4))) void gm_f_lookup_decl_10pnl6c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_lookup_decl_10pnl6c F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* d */
    GM_STACK_CHECK(&F, 178u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
        gm_b16_8 t7 = gm_l1;
        gm_tf5srh4mp4b3i t8 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t4, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t8))[8] != 0)) goto ie2;
        uint64_t t9 = GM_LD(uint64_t, ((uint8_t *)&t8));
        gm_l2 = t9;
        {
            uint64_t t11 = gm_l2;
            gm_tsncjtib3j1l1 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t12 + 8), t11);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1335qftqvd8f *)gm_out) + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1335qftqvd8f *)gm_out) + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1335qftqvd8f *)gm_out) + 28, 0);
            ((uint8_t *)&(*(gm_t1335qftqvd8f *)gm_out))[24] = 1;
            GM_ST(gm_tsncjtib3j1l1, (uint8_t *)&(*(gm_t1335qftqvd8f *)gm_out) + 0, t12);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t14 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s4g2k7ups3ui6, 5) == 0));
        if (!t14) goto ie13;
        {
            void * t16 = gm_l0;
            memmove(gm_out, (((uint8_t *)t16) + 280), 32);
            goto gm_fdone;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_t1335qftqvd8f t17;
        memset(&t17, 0, sizeof t17);
        uint8_t t18 = 0;
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_toceffb697p0u gm_f_lookup_local_tdg7skl(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_local_tdg7skl F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    void * gm_l3; /* names */
    uint64_t gm_l4; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 32) + 0) + 8);
        gm_l2 = t3;
    }
    {
        for (;;) {
            uint64_t t5 = gm_l2;
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            if (!t6) goto lb4;
            {
                uint64_t t9 = gm_l2;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_srcb2hdor4f6g0q, 136, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
            }
            {
                uint64_t t13 = gm_l2;
                uint64_t t14 = (uintptr_t)t13;
                void * t15 = gm_l0;
                uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 32) + 0) + 8);
                if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcb2hdor4f6g0q, 137, 19);
                void * t17 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 32) + 0)) + t14 * 48) + 0);
                gm_l3 = t17;
            }
            {
                void * t18 = gm_l3;
                uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 0) + 8);
                gm_l4 = t19;
            }
            {
                for (;;) {
                    uint64_t t21 = gm_l4;
                    uint8_t t22 = (t21 > ((uint64_t)0ULL));
                    if (!t22) goto lb20;
                    {
                        uint64_t t25 = gm_l4;
                        uint64_t r26; bool o27 = __builtin_sub_overflow(t25, ((uint64_t)1ULL), &r26);
                        if (o27) gm_trap("integer overflow in `-`", gm_srcb2hdor4f6g0q, 140, 13);
                        uint64_t t28 = r26;
                        gm_l4 = t28;
                    }
                    {
                        uint64_t t30 = gm_l4;
                        uint64_t t31 = (uintptr_t)t30;
                        void * t32 = gm_l3;
                        uint64_t t33 = GM_LD(uintptr_t, (((uint8_t *)t32) + 0) + 8);
                        if (t31 >= (uintptr_t)(t33)) gm_trap_index(t31, t33, gm_srcb2hdor4f6g0q, 141, 16);
                        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)t32) + 0)) + t31 * 32) + 0));
                        gm_b16_8 t35 = gm_l1;
                        uint8_t t36 = (gm_eq0iqiubfm7e(((uint8_t *)&t34), ((uint8_t *)&t35)));
                        if (!t36) goto ie29;
                        {
                            uint64_t t38 = gm_l4;
                            uint64_t t39 = (uintptr_t)t38;
                            void * t40 = gm_l3;
                            uint64_t t41 = GM_LD(uintptr_t, (((uint8_t *)t40) + 0) + 8);
                            if (t39 >= (uintptr_t)(t41)) gm_trap_index(t39, t41, gm_srcb2hdor4f6g0q, 141, 53);
                            gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, (((uint8_t *)t40) + 0)) + t39 * 32) + 16));
                            gm_toceffb697p0u t43;
                            GM_ST(uint8_t, (uint8_t *)&t43 + 13, 0);
                            GM_ST(uint16_t, (uint8_t *)&t43 + 14, 0);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t43), t42);
                            ((uint8_t *)&t43)[12] = 1;
                            uint8_t t44 = 0;
                            F.ret = t43;
                            goto gm_fret;
                        }
                        goto ix29;
ie29:;
ix29:;
                    }
lc20:;
                }
lb20:;
            }
lc4:;
        }
lb4:;
    }
    {
        gm_toceffb697p0u t45;
        memset(&t45, 0, sizeof t45);
        uint8_t t46 = 0;
        F.ret = t45;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_match_expr_j2bvhsh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_match_expr_j2bvhsh F;
    void * gm_l0; /* w */
    void * gm_l1; /* m */
    gm_tkn42u7na3t81 gm_l2; /* ty */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    void * gm_l6; /* arm */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* j */
    void * gm_l10; /* g */
    GM_STACK_CHECK(&F, 510u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        gm_b0_1 t5 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t2, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l1;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t8) + 72));
        gm_tkn42u7na3t81 t10 = gm_f_type_of_tiqb21n(t6, GM_CONV(gm_cseahvg8v7p0vs, t9));
        gm_l2 = t10;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            void * t12 = gm_l1;
            uint64_t t13 = GM_LD(uintptr_t, (((uint8_t *)t12) + 8) + 8);
            gm_l3 = t13;
        }
        {
            for (;;) {
                uint64_t t15 = gm_l4;
                uint64_t t16 = gm_l3;
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l4;
                    uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&t20));
                    gm_l5 = t23;
                    {
                        uint64_t t25 = gm_l5;
                        uint64_t t26 = (uintptr_t)t25;
                        void * t27 = gm_l1;
                        uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 8) + 8);
                        if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_srcb2hdor4f6g0q, 704, 17);
                        void * t29 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t27) + 8)) + t26 * 48);
                        gm_l6 = t29;
                    }
                    {
                        void * t30 = gm_l0;
                        void * t31 = gm_l6;
                        uint32_t t32 = GM_LD(uint32_t, ((((uint8_t *)t31) + 32) + 8));
                        gm_b0_1 t33 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t30, t32);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    {
                        {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        {
                            void * t35 = gm_l6;
                            uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
                            gm_l7 = t36;
                        }
                        {
                            for (;;) {
                                uint64_t t38 = gm_l8;
                                uint64_t t39 = gm_l7;
                                uint8_t t40 = (t38 < t39);
                                if (!t40) goto lb37;
                                {
                                    uint64_t t43 = gm_l8;
                                    uint64_t t46 = GM_LD(uint64_t, ((uint8_t *)&t43));
                                    gm_l9 = t46;
                                    {
                                        void * t48 = gm_l0;
                                        uint64_t t49 = gm_l9;
                                        uint64_t t50 = (uintptr_t)t49;
                                        void * t51 = gm_l6;
                                        uint64_t t52 = GM_LD(uintptr_t, (((uint8_t *)t51) + 0) + 8);
                                        if (t50 >= (uintptr_t)(t52)) gm_trap_index(t50, t52, gm_srcb2hdor4f6g0q, 706, 47);
                                        void * t53 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t51) + 0)) + t50 * 64);
                                        gm_tkn42u7na3t81 t54 = gm_l2;
                                        uint64_t t55 = gm_l9;
                                        uint64_t t56 = (uintptr_t)t55;
                                        void * t57 = gm_l6;
                                        uint64_t t58 = GM_LD(uintptr_t, (((uint8_t *)t57) + 0) + 8);
                                        if (t56 >= (uintptr_t)(t58)) gm_trap_index(t56, t58, gm_srcb2hdor4f6g0q, 706, 64);
                                        uint32_t t59 = GM_LD(uint32_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t57) + 0)) + t56 * 64) + 48) + 8));
                                        gm_b0_1 t60 = gm_f_pattern_jo09k6f((void *)NULL, cx, NULL, t48, t53, t54, t59);
                                        if (gm_unw.kind) {
                                            goto gm_ll41;
                                        }
                                    }
                                    goto me44;
mn45:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 706, 9);
me44:;
                                }
lc37:;
                                {
                                    uint64_t t62 = gm_l8;
                                    uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                                    uint64_t t65 = r63;
                                    gm_l8 = t65;
                                }
                            }
gm_ll41:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb37; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc37; }
                            }
                            goto gm_ll18;
lb37:;
                        }
                    }
                    {
                        void * t67 = gm_l6;
                        void * t68 = GM_LD(void *, (((uint8_t *)t67) + 16));
                        if (!(GM_LD(void *, ((uint8_t *)&t68)) != 0)) goto ie66;
                        void * t69 = GM_LD(void *, ((uint8_t *)&t68));
                        gm_l10 = t69;
                        {
                            void * t71 = gm_l0;
                            void * t72 = gm_l10;
                            gm_b0_1 t73 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t71, t72);
                            if (gm_unw.kind) {
                                goto gm_ll18;
                            }
                        }
                        goto ix66;
ie66:;
ix66:;
                    }
                    {
                        void * t74 = gm_l0;
                        void * t75 = gm_l6;
                        void * t76 = GM_LD(void *, (((uint8_t *)t75) + 24));
                        gm_b0_1 t77 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t74, t76);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    {
                        void * t78 = gm_l0;
                        gm_f_pop_scope_vgka5ae(t78);
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 703, 5);
me21:;
                }
lc14:;
                {
                    uint64_t t80 = gm_l4;
                    uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
                    uint64_t t83 = r81;
                    gm_l4 = t83;
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
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_member_def_9f4kiah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_member_def_9f4kiah F;
    void * gm_l0; /* w */
    uint64_t gm_l1; /* d */
    gm_b16_8 gm_l2; /* name */
    void * gm_l3; /* decl */
    gm_b16_8 gm_l4; /* owner */
    gm_tkol5reu9t1u2 gm_l6; /* found */
    void * gm_l7; /* lit */
    void * gm_l8; /* lit */
    void * gm_l9; /* inner */
    void * gm_l10; /* lit */
    void * gm_l11; /* lit */
    gm_tt7dbdq4n1egc gm_l12; /* f */
    gm_b16_8 gm_l13; /* text */
    gm_b16_8 gm_l14; /* code */
    GM_STACK_CHECK(&F, 1192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t7)) gm_trap_index(t3, t7, gm_srcb2hdor4f6g0q, 346, 14);
        void * t8 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 240) + 0)) + t3 * 216);
        gm_l3 = t8;
    }
    {
        void * t9 = gm_l3;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((((uint8_t *)t9) + 0) + 0));
        gm_l4 = t10;
    }
    {
        void * t12 = gm_l3;
        uint8_t t13 = GM_LD(uint8_t, (((((uint8_t *)t12) + 40) + 64) + 0));
        if (t13 != ((uint8_t)0ULL)) goto ie11;
        gm_tv2uf4leil5hq t14 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t12) + 40) + 64) + 8));
        F.l5 = t14;
        {
            gm_tkol5reu9t1u2 t16;
            memset(&t16, 0, sizeof t16);
            gm_l6 = t16;
        }
        {
            void * t17 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
            uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            switch (t24) { case ((uint8_t)0ULL): goto ma22; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma22; case ((uint8_t)4ULL): goto ma22; case ((uint8_t)5ULL): goto ma22; case ((uint8_t)6ULL): goto ma22; case ((uint8_t)7ULL): goto ma22; case ((uint8_t)8ULL): goto ma22; case ((uint8_t)9ULL): goto ma22; case ((uint8_t)10ULL): goto ma22; case ((uint8_t)11ULL): goto ma22; case ((uint8_t)12ULL): goto ma22; case ((uint8_t)13ULL): goto ma22; case ((uint8_t)14ULL): goto ma22; case ((uint8_t)15ULL): goto ma22; case ((uint8_t)16ULL): goto ma22; case ((uint8_t)17ULL): goto ma22; case ((uint8_t)18ULL): goto ma22; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma22; case ((uint8_t)21ULL): goto ma22; case ((uint8_t)22ULL): goto ma22; case ((uint8_t)23ULL): goto ma22; case ((uint8_t)24ULL): goto ma22; case ((uint8_t)25ULL): goto ma22; case ((uint8_t)26ULL): goto ma22; case ((uint8_t)27ULL): goto ma22; case ((uint8_t)28ULL): goto ma22; case ((uint8_t)29ULL): goto ma22; case ((uint8_t)30ULL): goto ma22; case ((uint8_t)31ULL): goto ma22; case ((uint8_t)32ULL): goto ma19; case ((uint8_t)33ULL): goto ma20; case ((uint8_t)34ULL): goto ma22; case ((uint8_t)35ULL): goto ma21; case ((uint8_t)36ULL): goto ma22; case ((uint8_t)37ULL): goto ma22; case ((uint8_t)38ULL): goto ma22; case ((uint8_t)39ULL): goto ma22; case ((uint8_t)40ULL): goto ma22; case ((uint8_t)41ULL): goto ma22; case ((uint8_t)42ULL): goto ma22; case ((uint8_t)43ULL): goto ma22; case ((uint8_t)44ULL): goto ma22; case ((uint8_t)45ULL): goto ma22; case ((uint8_t)46ULL): goto ma22; case ((uint8_t)47ULL): goto ma22; case ((uint8_t)48ULL): goto ma22; case ((uint8_t)49ULL): goto ma22; case ((uint8_t)50ULL): goto ma22; default: goto ma22; }
ma19:;
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            if (t26 != ((uint8_t)32ULL)) goto mn25;
            void * t27 = GM_LD(void *, ((((uint8_t *)t17) + 8) + 8));
            gm_l7 = t27;
            void * t28 = gm_l7;
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 40));
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
            gm_b16_8 t31 = gm_l2;
            void * t32 = (void *)((uint8_t *)&gm_l6);
            gm_f_find_field_l0p9325(t30, t31, t32);
            goto me18;
mn25:;
ma20:;
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            if (t34 != ((uint8_t)33ULL)) goto mn33;
            void * t35 = GM_LD(void *, ((((uint8_t *)t17) + 8) + 8));
            gm_l8 = t35;
            void * t36 = gm_l8;
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)t36) + 48));
            gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
            gm_b16_8 t39 = gm_l2;
            void * t40 = (void *)((uint8_t *)&gm_l6);
            gm_f_find_variant_kifk1mm(t38, t39, t40);
            goto me18;
mn33:;
ma21:;
            uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            if (t42 != ((uint8_t)35ULL)) goto mn41;
            void * t43 = GM_LD(void *, (((((uint8_t *)t17) + 8) + 8) + 16));
            gm_l9 = t43;
            {
                void * t45 = gm_l9;
                uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                switch (t51) { case ((uint8_t)0ULL): goto ma49; case ((uint8_t)1ULL): goto ma49; case ((uint8_t)2ULL): goto ma49; case ((uint8_t)3ULL): goto ma49; case ((uint8_t)4ULL): goto ma49; case ((uint8_t)5ULL): goto ma49; case ((uint8_t)6ULL): goto ma49; case ((uint8_t)7ULL): goto ma49; case ((uint8_t)8ULL): goto ma49; case ((uint8_t)9ULL): goto ma49; case ((uint8_t)10ULL): goto ma49; case ((uint8_t)11ULL): goto ma49; case ((uint8_t)12ULL): goto ma49; case ((uint8_t)13ULL): goto ma49; case ((uint8_t)14ULL): goto ma49; case ((uint8_t)15ULL): goto ma49; case ((uint8_t)16ULL): goto ma49; case ((uint8_t)17ULL): goto ma49; case ((uint8_t)18ULL): goto ma49; case ((uint8_t)19ULL): goto ma49; case ((uint8_t)20ULL): goto ma49; case ((uint8_t)21ULL): goto ma49; case ((uint8_t)22ULL): goto ma49; case ((uint8_t)23ULL): goto ma49; case ((uint8_t)24ULL): goto ma49; case ((uint8_t)25ULL): goto ma49; case ((uint8_t)26ULL): goto ma49; case ((uint8_t)27ULL): goto ma49; case ((uint8_t)28ULL): goto ma49; case ((uint8_t)29ULL): goto ma49; case ((uint8_t)30ULL): goto ma49; case ((uint8_t)31ULL): goto ma49; case ((uint8_t)32ULL): goto ma47; case ((uint8_t)33ULL): goto ma48; case ((uint8_t)34ULL): goto ma49; case ((uint8_t)35ULL): goto ma49; case ((uint8_t)36ULL): goto ma49; case ((uint8_t)37ULL): goto ma49; case ((uint8_t)38ULL): goto ma49; case ((uint8_t)39ULL): goto ma49; case ((uint8_t)40ULL): goto ma49; case ((uint8_t)41ULL): goto ma49; case ((uint8_t)42ULL): goto ma49; case ((uint8_t)43ULL): goto ma49; case ((uint8_t)44ULL): goto ma49; case ((uint8_t)45ULL): goto ma49; case ((uint8_t)46ULL): goto ma49; case ((uint8_t)47ULL): goto ma49; case ((uint8_t)48ULL): goto ma49; case ((uint8_t)49ULL): goto ma49; case ((uint8_t)50ULL): goto ma49; default: goto ma49; }
ma47:;
                uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                if (t53 != ((uint8_t)32ULL)) goto mn52;
                void * t54 = GM_LD(void *, ((((uint8_t *)t45) + 8) + 8));
                gm_l10 = t54;
                void * t55 = gm_l10;
                gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)t55) + 40));
                gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                gm_b16_8 t58 = gm_l2;
                void * t59 = (void *)((uint8_t *)&gm_l6);
                gm_f_find_field_l0p9325(t57, t58, t59);
                goto me46;
mn52:;
ma48:;
                uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                if (t61 != ((uint8_t)33ULL)) goto mn60;
                void * t62 = GM_LD(void *, ((((uint8_t *)t45) + 8) + 8));
                gm_l11 = t62;
                void * t63 = gm_l11;
                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)t63) + 48));
                gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                gm_b16_8 t66 = gm_l2;
                void * t67 = (void *)((uint8_t *)&gm_l6);
                gm_f_find_variant_kifk1mm(t65, t66, t67);
                goto me46;
mn60:;
ma49:;
                goto me46;
mn68:;
ma50:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 354, 13);
me46:;
            }
            goto me18;
mn41:;
ma22:;
            goto me18;
mn70:;
ma23:;
            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 350, 5);
me18:;
        }
        {
            gm_tkol5reu9t1u2 t72 = gm_l6;
            gm_tt7dbdq4n1egc t73;
            if (((((uint8_t *)&t72))[28] != 0)) {
                t73 = (*(gm_tt7dbdq4n1egc *)(((uint8_t *)&t72) + 0));
            } else {
                gm_toceffb697p0u t74;
                memset(&t74, 0, sizeof t74);
                uint8_t t75 = 0;
                F.ret = t74;
                goto gm_fret;
            }
            gm_l12 = t73;
        }
        {
            void * t76 = gm_l0;
            void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
            gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l12) + 12));
            gm_b16_8 t79 = gm_f_src_fjcc76p(t77, GM_CONV(gm_cseahvg8v7p0vs, t78));
            gm_b16_8 t80 = gm_f_trim_fdrfe6i(t79);
            gm_l13 = t80;
        }
        {
            gm_b16_8 t81;
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 24));
            if (!t83) goto ie82;
            gm_t6j6smj7qoctu t85;
            gm_b16_8 t86 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t85 + 0, t86);
            gm_b16_8 t87 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t85 + 16, t87);
            gm_tabb710d9mq2e t88;
            gm_f_format_ahgk48j((void *)&t88, (void *)NULL, cx, NULL, &t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 8, gm_srcb2hdor4f6g0q, 364, 31);
            gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&t88) + 8));
            gm_b16_8 t90 = t89;
            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
            t81 = t91;
            goto ix82;
ie82:;
            gm_b16_8 t93 = gm_l13;
            gm_b16_8 t94 = t93;
            t81 = t94;
ix82:;
            gm_l14 = t81;
        }
        {
            void * t95 = gm_l0;
            gm_tl874t2atnlh3 t96 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l12) + 0));
            gm_t6ok5tvcjc200 t97;
            gm_b16_8 t98 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t97 + 0, t98);
            gm_th2lqbo77pvsc t99;
            memset(&t99, 0, sizeof t99);
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t97 + 16, t99);
            gm_b0_1 t100 = gm_f_def_once_pqgvduh((void *)NULL, cx, NULL, t95, t96, &t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tl874t2atnlh3 t101 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l12) + 0));
            gm_toceffb697p0u t102;
            GM_ST(uint8_t, (uint8_t *)&t102 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t102 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t102), t101);
            ((uint8_t *)&t102)[12] = 1;
            uint8_t t103 = 0;
            F.ret = t102;
            goto gm_fret;
        }
        goto ix11;
ie11:;
        {
            gm_toceffb697p0u t105;
            memset(&t105, 0, sizeof t105);
            uint8_t t106 = 0;
            F.ret = t105;
            goto gm_fret;
        }
ix11:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_name_use_73pph7t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_name_use_73pph7t F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l1; /* id */
    gm_tl874t2atnlh3 gm_l2; /* def */
    gm_tsncjtib3j1l1 gm_l3; /* d */
    GM_STACK_CHECK(&F, 236u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        gm_toceffb697p0u t5 = gm_f_lookup_local_tdg7skl(t3, t4);
        if (!((((uint8_t *)&t5))[12] != 0)) goto ie2;
        gm_tl874t2atnlh3 t6 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&t5));
        gm_l2 = t6;
        {
            void * t8 = gm_l0;
            gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
            gm_tl874t2atnlh3 t10 = gm_l2;
            gm_b0_1 t11 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t8, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
        void * t13 = gm_l0;
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        gm_t1335qftqvd8f t15;
        gm_f_lookup_decl_10pnl6c((void *)&t15, (void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t15))[24] != 0)) goto ie12;
        gm_l3 = (*(gm_tsncjtib3j1l1 *)(((uint8_t *)&t15) + 0));
        {
            void * t17 = gm_l0;
            gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
            gm_b0_1 t19 = gm_f_use_decl_sfao4j5((void *)NULL, cx, NULL, t17, t18, &(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix12;
ie12:;
ix12:;
ix2:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_note_receiver_6mb4kc5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_note_receiver_6mb4kc5 F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tsncjtib3j1l1 gm_l2; /* d */
    uint64_t gm_l3; /* key */
    gm_b16_8 gm_l4; /* r */
    uint64_t gm_l5; /* x */
    uint64_t gm_l7; /* m */
    gm_t88rcucg1b7uk gm_l8; /* t */
    GM_STACK_CHECK(&F, 1103u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    {
        uint64_t t2 = gm_f_ref_key_vam5ppg(&(*(gm_tsncjtib3j1l1 *)((uint8_t *)&gm_l2)));
        gm_l3 = t2;
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 168);
        uint64_t t6 = gm_l3;
        gm_th2lqbo77pvsc t7;
        gm_f_get_o6c8ntf((void *)&t7, t5, t6);
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie3;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l4 = t8;
        {
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 16));
            void * t12 = (void *)(((uint8_t *)t11) + 272);
            gm_tl874t2atnlh3 t13 = gm_l1;
            gm_b16_8 t14 = gm_l4;
            gm_tftb1odc739t5 t15 = gm_f_put_ss3ma85(t12, GM_CONV(gm_cseahvg8v7p0vs, t13), t14);
            if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srcb2hdor4f6g0q, 215, 9);
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        {
            gm_b0_1 t17;
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t20 != ((uint8_t)0ULL)) goto ie19;
        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        gm_l5 = t21;
        {
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
            uint64_t t25 = gm_l5;
            gm_tlabdfckddbfd t26;
            gm_f_decl_done_mk70ol9((void *)&t26, (void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6qooom8qa2do t27;
            if (((((uint8_t *)&t26))[208] != 0)) {
                t27 = (*(gm_t6qooom8qa2do *)(((uint8_t *)&t26) + 0));
            } else {
                gm_b0_1 t28;
                uint8_t t29 = 0;
                F.ret = t28;
                goto gm_fret;
            }
            F.l6 = t27;
        }
        {
            gm_trqfvmjtt4sij t30;
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
            switch (t37) { case ((uint8_t)0ULL): goto ma33; case ((uint8_t)1ULL): goto ma35; case ((uint8_t)2ULL): goto ma34; case ((uint8_t)3ULL): goto ma35; case ((uint8_t)4ULL): goto ma35; case ((uint8_t)5ULL): goto ma35; case ((uint8_t)6ULL): goto ma35; case ((uint8_t)7ULL): goto ma35; case ((uint8_t)8ULL): goto ma35; case ((uint8_t)9ULL): goto ma32; case ((uint8_t)10ULL): goto ma35; case ((uint8_t)11ULL): goto ma35; default: goto ma35; }
ma32:;
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
            if (t39 != ((uint8_t)9ULL)) goto mn38;
            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
            gm_l7 = t40;
            void * t41 = gm_l0;
            void * t42 = GM_LD(void *, (((uint8_t *)t41) + 0));
            uint64_t t43 = gm_l7;
            uint64_t t44 = gm_l5;
            uint64_t t45 = (uintptr_t)t44;
            void * t46 = gm_l0;
            void * t47 = GM_LD(void *, (((uint8_t *)t46) + 0));
            void * t48 = GM_LD(void *, (((uint8_t *)t47) + 0));
            uint64_t t49 = GM_LD(uintptr_t, ((((uint8_t *)t48) + 240) + 0) + 8);
            if (t45 >= (uintptr_t)(t49)) gm_trap_index(t45, t49, gm_srcb2hdor4f6g0q, 221, 46);
            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t48) + 240) + 0)) + t45 * 216) + 32));
            gm_trqfvmjtt4sij t51;
            gm_f_module_members_26b4s91((void *)&t51, (void *)NULL, cx, NULL, t42, t43, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t30 = t51;
            goto me31;
mn38:;
ma33:;
            uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
            if (t53 != ((uint8_t)0ULL)) goto mn52;
            gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
            gm_l8 = t54;
            void * t55 = gm_l0;
            void * t56 = GM_LD(void *, (((uint8_t *)t55) + 0));
            gm_t88rcucg1b7uk t57 = gm_l8;
            gm_trqfvmjtt4sij t58;
            gm_f_variant_members_nesi2hv((void *)&t58, (void *)NULL, cx, NULL, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t30 = t58;
            goto me31;
mn52:;
ma34:;
            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
            if (t60 != ((uint8_t)2ULL)) goto mn59;
            void * t61 = gm_l0;
            void * t62 = GM_LD(void *, (((uint8_t *)t61) + 0));
            uint64_t t63 = gm_l5;
            gm_trqfvmjtt4sij t64;
            gm_f_operation_members_62l824c((void *)&t64, (void *)NULL, cx, NULL, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t30 = t64;
            goto me31;
mn59:;
ma35:;
            gm_b0_1 t66;
            uint8_t t67 = 0;
            F.ret = t66;
            goto gm_fret;
            goto me31;
mn65:;
ma36:;
            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 220, 25);
me31:;
            F.l9 = t30;
        }
        {
            void * t68 = gm_l0;
            void * t69 = (void *)(((uint8_t *)t68) + 168);
            uint64_t t70 = gm_l3;
            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
            gm_tftb1odc739t5 t73 = gm_f_put_hku1qp6(t69, t70, t72);
            if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 1, gm_srcb2hdor4f6g0q, 226, 5);
            gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 1));
        }
        {
            void * t75 = gm_l0;
            void * t76 = GM_LD(void *, (((uint8_t *)t75) + 16));
            void * t77 = (void *)(((uint8_t *)t76) + 272);
            gm_tl874t2atnlh3 t78 = gm_l1;
            gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
            gm_tftb1odc739t5 t81 = gm_f_put_ss3ma85(t77, GM_CONV(gm_cseahvg8v7p0vs, t78), t80);
            if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_srcb2hdor4f6g0q, 227, 5);
            gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
        }
        goto ix19;
ie19:;
        {
            gm_b0_1 t84;
            uint8_t t85 = 0;
            F.ret = t84;
            goto gm_fret;
        }
ix19:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_operation_use_ue1or58(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2) {
    struct gm_fr_operation_use_ue1or58 F;
    void * gm_l0; /* w */
    uint64_t gm_l1; /* d */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* lit */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 340u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint64_t t4 = gm_l1;
        void * t5 = gm_f_effect_lit_26tkk47(t3, t4);
        void * t6;
        if ((GM_LD(void *, ((uint8_t *)&t5)) != 0)) {
            void * t7 = GM_LD(void *, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l3 = t6;
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            void * t11 = gm_l3;
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 24) + 8);
            gm_l4 = t12;
        }
        {
            for (;;) {
                uint64_t t14 = gm_l5;
                uint64_t t15 = gm_l4;
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l5;
                    uint64_t t22 = GM_LD(uint64_t, ((uint8_t *)&t19));
                    gm_l6 = t22;
                    {
                        uint64_t t25 = gm_l6;
                        uint64_t t26 = (uintptr_t)t25;
                        void * t27 = gm_l3;
                        uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 24) + 8);
                        if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_srcb2hdor4f6g0q, 526, 12);
                        gm_b16_8 t29 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t27) + 24)) + t26 * 80) + 0) + 0));
                        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                        uint8_t t31 = (gm_eq0iqiubfm7e(((uint8_t *)&t29), ((uint8_t *)&t30)));
                        if (!t31) goto ie24;
                        {
                            void * t33 = gm_l0;
                            gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
                            uint64_t t35 = gm_l1;
                            uint64_t t36 = gm_l6;
                            uint32_t t37 = (uint32_t)t36;
                            gm_tn68el8d14vku t38;
                            GM_ST(uint32_t, (uint8_t *)&t38 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t38 + 0), t35);
                            GM_ST(uint32_t, ((uint8_t *)&t38 + 8), t37);
                            gm_tsncjtib3j1l1 t39;
                            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tn68el8d14vku, ((uint8_t *)&t39 + 8), t38);
                            gm_b0_1 t40 = gm_f_use_decl_sfao4j5((void *)NULL, cx, NULL, t33, t34, &t39);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                        }
                        {
                            gm_b0_1 t41;
                            uint8_t t42 = 0;
                            F.ret = t41;
                            goto gm_fret;
                        }
                        goto ix24;
ie24:;
ix24:;
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 525, 5);
me20:;
                }
lc13:;
                {
                    uint64_t t44 = gm_l5;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l5 = t47;
                }
            }
gm_ll17:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb13; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc13; }
            }
            goto gm_lad;
lb13:;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_path_type_flfaob1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_path_type_flfaob1 F;
    void * gm_l0; /* w */
    void * gm_l1; /* path */
    gm_t08olq97rscn3 gm_l2; /* id */
    gm_tsncjtib3j1l1 gm_l3; /* d */
    uint64_t gm_l4; /* x */
    gm_t88rcucg1b7uk gm_l6; /* t */
    void * gm_l7; /* base */
    uint64_t gm_l8; /* d */
    gm_t88rcucg1b7uk gm_l10; /* t */
    GM_STACK_CHECK(&F, 1346u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)6ULL)) goto mn9;
        gm_t08olq97rscn3 t11 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t11;
        {
            void * t14 = gm_l0;
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
            gm_toceffb697p0u t16 = gm_f_lookup_local_tdg7skl(t14, t15);
            uint8_t t17;
            if (((((uint8_t *)&t16))[12] != 0)) goto mn19;
            t17 = ((uint8_t)0);
            goto me18;
mn19:;
            t17 = ((uint8_t)1);
            goto me18;
mn20:;
            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 803, 16);
me18:;
            if (!t17) goto ie13;
            {
                gm_tkn42u7na3t81 t22;
                memset(&t22, 0, sizeof t22);
                uint8_t t23 = 0;
                F.ret = t22;
                goto gm_fret;
            }
            goto ix13;
ie13:;
ix13:;
        }
        {
            void * t24 = gm_l0;
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
            gm_t1335qftqvd8f t26;
            gm_f_lookup_decl_10pnl6c((void *)&t26, (void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tsncjtib3j1l1 t27;
            if (((((uint8_t *)&t26))[24] != 0)) {
                t27 = (*(gm_tsncjtib3j1l1 *)(((uint8_t *)&t26) + 0));
            } else {
                gm_tkn42u7na3t81 t28;
                memset(&t28, 0, sizeof t28);
                uint8_t t29 = 0;
                F.ret = t28;
                goto gm_fret;
            }
            gm_l3 = t27;
        }
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            if (t31 != ((uint8_t)0ULL)) goto ie30;
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            gm_l4 = t32;
            {
                void * t34 = gm_l0;
                void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
                uint64_t t36 = gm_l4;
                gm_tlabdfckddbfd t37;
                gm_f_decl_done_mk70ol9((void *)&t37, (void *)NULL, cx, NULL, t35, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6qooom8qa2do t38;
                if (((((uint8_t *)&t37))[208] != 0)) {
                    t38 = (*(gm_t6qooom8qa2do *)(((uint8_t *)&t37) + 0));
                } else {
                    gm_tkn42u7na3t81 t39;
                    memset(&t39, 0, sizeof t39);
                    uint8_t t40 = 0;
                    F.ret = t39;
                    goto gm_fret;
                }
                F.l5 = t38;
            }
            {
                uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                if (t42 != ((uint8_t)0ULL)) goto ie41;
                gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 8));
                gm_l6 = t43;
                {
                    gm_t88rcucg1b7uk t45 = gm_l6;
                    gm_tkn42u7na3t81 t46;
                    GM_ST(uint8_t, (uint8_t *)&t46 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t46 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t46), t45);
                    ((uint8_t *)&t46)[4] = 1;
                    uint8_t t47 = 0;
                    F.ret = t46;
                    goto gm_fret;
                }
                goto ix41;
ie41:;
ix41:;
            }
            {
                gm_tkn42u7na3t81 t48;
                memset(&t48, 0, sizeof t48);
                uint8_t t49 = 0;
                F.ret = t48;
                goto gm_fret;
            }
            goto ix30;
ie30:;
            {
                gm_tkn42u7na3t81 t51;
                memset(&t51, 0, sizeof t51);
                uint8_t t52 = 0;
                F.ret = t51;
                goto gm_fret;
            }
ix30:;
        }
        goto me3;
mn9:;
ma5:;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t54 != ((uint8_t)22ULL)) goto mn53;
        void * t55 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l7 = t55;
        {
            void * t58 = gm_l0;
            void * t59 = gm_l7;
            gm_tu0a56t4bfvqh t60 = gm_f_classify_pdaj2gb((void *)NULL, cx, NULL, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
            if (t61 != ((uint8_t)1ULL)) goto ie57;
            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&t60) + 8));
            gm_l8 = t62;
            {
                void * t64 = gm_l0;
                void * t65 = GM_LD(void *, (((uint8_t *)t64) + 0));
                uint64_t t66 = gm_l8;
                gm_tlabdfckddbfd t67;
                gm_f_decl_done_mk70ol9((void *)&t67, (void *)NULL, cx, NULL, t65, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6qooom8qa2do t68;
                if (((((uint8_t *)&t67))[208] != 0)) {
                    t68 = (*(gm_t6qooom8qa2do *)(((uint8_t *)&t67) + 0));
                } else {
                    gm_tkn42u7na3t81 t69;
                    memset(&t69, 0, sizeof t69);
                    uint8_t t70 = 0;
                    F.ret = t69;
                    goto gm_fret;
                }
                F.l9 = t68;
            }
            {
                uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
                if (t72 != ((uint8_t)0ULL)) goto ie71;
                gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 8));
                gm_l10 = t73;
                {
                    gm_t88rcucg1b7uk t75 = gm_l10;
                    gm_tkn42u7na3t81 t76;
                    GM_ST(uint8_t, (uint8_t *)&t76 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t76 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t76), t75);
                    ((uint8_t *)&t76)[4] = 1;
                    uint8_t t77 = 0;
                    F.ret = t76;
                    goto gm_fret;
                }
                goto ix71;
ie71:;
ix71:;
            }
            goto ix57;
ie57:;
ix57:;
        }
        {
            gm_tkn42u7na3t81 t78;
            memset(&t78, 0, sizeof t78);
            uint8_t t79 = 0;
            F.ret = t78;
            goto gm_fret;
        }
        goto me3;
mn53:;
ma6:;
        gm_tkn42u7na3t81 t81;
        memset(&t81, 0, sizeof t81);
        uint8_t t82 = 0;
        F.ret = t81;
        goto gm_fret;
        goto me3;
mn80:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 801, 5);
me3:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_pattern_jo09k6f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, uint32_t a3) {
    struct gm_fr_pattern_jo09k6f F;
    void * gm_l0; /* w */
    void * gm_l1; /* p */
    gm_tkn42u7na3t81 gm_l2; /* ty */
    uint32_t gm_l3; /* from */
    gm_t08olq97rscn3 gm_l4; /* id */
    gm_t08olq97rscn3 gm_l5; /* id */
    void * gm_l6; /* e */
    void * gm_l7; /* lo */
    void * gm_l8; /* hi */
    void * gm_l9; /* inner */
    gm_tkn42u7na3t81 gm_l10; /* err */
    gm_t88rcucg1b7uk gm_l11; /* t */
    gm_t88rcucg1b7uk gm_l12; /* e */
    gm_tagc77imt2chm gm_l13; /* pp */
    gm_tkn42u7na3t81 gm_l14; /* owner */
    gm_t08olq97rscn3 gm_l15; /* id */
    gm_t88rcucg1b7uk gm_l16; /* t */
    gm_t88rcucg1b7uk gm_l17; /* pt */
    gm_b16_8 gm_l18; /* args */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    void * gm_l22; /* a */
    gm_tkn42u7na3t81 gm_l23; /* sub */
    gm_t08olq97rscn3 gm_l24; /* n */
    gm_t88rcucg1b7uk gm_l25; /* t */
    gm_t08olq97rscn3 gm_l26; /* v */
    gm_t88rcucg1b7uk gm_l27; /* t */
    gm_b16_8 gm_l28; /* args */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* i */
    GM_STACK_CHECK(&F, 1298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; default: goto ma13; }
ma4:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t16 != ((uint8_t)0ULL)) goto mn15;
        goto me3;
mn15:;
ma5:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t19 != ((uint8_t)4ULL)) goto mn18;
        goto me3;
mn18:;
ma6:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t22 != ((uint8_t)1ULL)) goto mn21;
        gm_t08olq97rscn3 t23 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t23;
        void * t24 = gm_l0;
        gm_t9qkh0rsooc8d t25;
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)8ULL));
        uint32_t t26 = gm_l3;
        gm_b0_1 t27 = gm_f_declare_feg3ue0((void *)NULL, cx, NULL, t24, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l4)), t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn21:;
ma7:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t29 != ((uint8_t)2ULL)) goto mn28;
        gm_t08olq97rscn3 t30 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l5 = t30;
        void * t31 = gm_l0;
        gm_t9qkh0rsooc8d t32;
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)8ULL));
        uint32_t t33 = gm_l3;
        gm_b0_1 t34 = gm_f_declare_feg3ue0((void *)NULL, cx, NULL, t31, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l5)), t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn28:;
ma8:;
        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t36 != ((uint8_t)3ULL)) goto mn35;
        void * t37 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l6 = t37;
        void * t38 = gm_l0;
        void * t39 = gm_l6;
        gm_b0_1 t40 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn35:;
ma9:;
        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t42 != ((uint8_t)5ULL)) goto mn41;
        void * t43 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l7 = t43;
        void * t44 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l8 = t44;
        {
            void * t46 = gm_l0;
            void * t47 = gm_l7;
            gm_b0_1 t48 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t49 = gm_l0;
            void * t50 = gm_l8;
            gm_b0_1 t51 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t49, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn41:;
ma10:;
        uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t53 != ((uint8_t)6ULL)) goto mn52;
        void * t54 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l9 = t54;
        {
            gm_tkn42u7na3t81 t56;
            memset(&t56, 0, sizeof t56);
            gm_l10 = t56;
        }
        {
            if (!((((uint8_t *)&gm_l2))[4] != 0)) goto ie57;
            gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
            gm_l11 = t58;
            {
                void * t61 = gm_l0;
                void * t62 = GM_LD(void *, (((uint8_t *)t61) + 0));
                void * t63 = (void *)(((uint8_t *)t62) + 8);
                gm_t88rcucg1b7uk t64 = gm_l11;
                gm_t53at80pfdus3 t65;
                gm_f_kind_jcnt4hm((void *)&t65, t63, GM_CONV(gm_csguankko4lmhd, t64));
                uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t65) + 0));
                if (t66 != ((uint8_t)14ULL)) goto ie60;
                gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t65) + 8) + 0));
                gm_l12 = t67;
                {
                    gm_t88rcucg1b7uk t69 = gm_l12;
                    gm_tkn42u7na3t81 t70;
                    GM_ST(uint8_t, (uint8_t *)&t70 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t70 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t70), t69);
                    ((uint8_t *)&t70)[4] = 1;
                    gm_l10 = t70;
                }
                goto ix60;
ie60:;
ix60:;
            }
            goto ix57;
ie57:;
ix57:;
        }
        {
            void * t71 = gm_l0;
            void * t72 = gm_l9;
            gm_tkn42u7na3t81 t73 = gm_l10;
            uint32_t t74 = gm_l3;
            gm_b0_1 t75 = gm_f_pattern_jo09k6f((void *)NULL, cx, NULL, t71, t72, t73, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn52:;
ma11:;
        uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t77 != ((uint8_t)7ULL)) goto mn76;
        gm_tagc77imt2chm t78 = GM_LD(gm_tagc77imt2chm, ((((uint8_t *)t2) + 8) + 8));
        gm_l13 = t78;
        {
            gm_tkn42u7na3t81 t80 = gm_l2;
            gm_l14 = t80;
        }
        {
            void * t82 = GM_LD(void *, (((uint8_t *)&gm_l13) + 0));
            uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)t82) + 8) + 0));
            if (t83 != ((uint8_t)8ULL)) goto ie81;
            gm_t08olq97rscn3 t84 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t82) + 8) + 8));
            gm_l15 = t84;
            {
                if (!((((uint8_t *)&gm_l2))[4] != 0)) goto ie86;
                gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
                gm_l16 = t87;
                {
                    {
                        void * t90 = gm_l0;
                        gm_t88rcucg1b7uk t91 = gm_l16;
                        gm_tf5srh4mp4b3i t92 = gm_f_variant_use_29ooua3((void *)NULL, cx, NULL, t90, t91, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l15)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                }
                goto ix86;
ie86:;
ix86:;
            }
            goto ix81;
ie81:;
            {
                void * t94 = gm_l0;
                void * t95 = GM_LD(void *, (((uint8_t *)&gm_l13) + 0));
                gm_b0_1 t96 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t94, t95);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t98 = gm_l0;
                void * t99 = GM_LD(void *, (((uint8_t *)&gm_l13) + 0));
                gm_tkn42u7na3t81 t100 = gm_f_path_type_flfaob1((void *)NULL, cx, NULL, t98, t99);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t100))[4] != 0)) goto ie97;
                gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t100));
                gm_l17 = t101;
                {
                    gm_t88rcucg1b7uk t103 = gm_l17;
                    gm_tkn42u7na3t81 t104;
                    GM_ST(uint8_t, (uint8_t *)&t104 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t104 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t104), t103);
                    ((uint8_t *)&t104)[4] = 1;
                    gm_l14 = t104;
                }
                goto ix97;
ie97:;
ix97:;
            }
ix81:;
        }
        {
            if (!(((((uint8_t *)&gm_l13) + 8))[16] != 0)) goto ie105;
            gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
            gm_l18 = t106;
            {
                {
                    gm_l20 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    gm_l19 = t109;
                }
                {
                    for (;;) {
                        uint64_t t111 = gm_l20;
                        uint64_t t112 = gm_l19;
                        uint8_t t113 = (t111 < t112);
                        if (!t113) goto lb110;
                        {
                            uint64_t t116 = gm_l20;
                            uint64_t t119 = GM_LD(uint64_t, ((uint8_t *)&t116));
                            gm_l21 = t119;
                            {
                                uint64_t t121 = gm_l21;
                                uint64_t t122 = (uintptr_t)t121;
                                uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                if (t122 >= (uintptr_t)(t123)) gm_trap_index(t122, t123, gm_srcb2hdor4f6g0q, 781, 27);
                                void * t124 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t122 * 120);
                                gm_l22 = t124;
                            }
                            {
                                gm_tkn42u7na3t81 t125;
                                memset(&t125, 0, sizeof t125);
                                gm_l23 = t125;
                            }
                            {
                                void * t127 = gm_l22;
                                if (!(((((uint8_t *)t127) + 0))[32] != 0)) goto ie126;
                                gm_t08olq97rscn3 t128 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)t127) + 0));
                                gm_l24 = t128;
                                {
                                    if (!((((uint8_t *)&gm_l14))[4] != 0)) goto ie130;
                                    gm_t88rcucg1b7uk t131 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l14));
                                    gm_l25 = t131;
                                    {
                                        {
                                            void * t134 = gm_l0;
                                            gm_t88rcucg1b7uk t135 = gm_l25;
                                            uint8_t t136 = gm_f_field_use_838f8e8((void *)NULL, cx, NULL, t134, t135, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l24)));
                                            if (gm_unw.kind) {
                                                goto gm_ll114;
                                            }
                                        }
                                    }
                                    goto ix130;
ie130:;
ix130:;
                                }
                                goto ix126;
ie126:;
ix126:;
                            }
                            {
                                void * t138 = GM_LD(void *, (((uint8_t *)&gm_l13) + 0));
                                uint8_t t139 = GM_LD(uint8_t, ((((uint8_t *)t138) + 8) + 0));
                                if (t139 != ((uint8_t)8ULL)) goto ie137;
                                gm_t08olq97rscn3 t140 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t138) + 8) + 8));
                                gm_l26 = t140;
                                {
                                    if (!((((uint8_t *)&gm_l2))[4] != 0)) goto ie142;
                                    gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
                                    gm_l27 = t143;
                                    {
                                        void * t145 = gm_l0;
                                        gm_t88rcucg1b7uk t146 = gm_l27;
                                        gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 0));
                                        gm_tkn42u7na3t81 t148 = gm_f_variant_payload_3id0koh(t145, GM_CONV(gm_csguankko4lmhd, t146), t147);
                                        gm_l23 = t148;
                                    }
                                    goto ix142;
ie142:;
ix142:;
                                }
                                goto ix137;
ie137:;
ix137:;
                            }
                            {
                                void * t149 = gm_l0;
                                void * t150 = gm_l22;
                                void * t151 = (void *)(((uint8_t *)t150) + 40);
                                gm_tkn42u7na3t81 t152 = gm_l23;
                                uint32_t t153 = gm_l3;
                                gm_b0_1 t154 = gm_f_pattern_jo09k6f((void *)NULL, cx, NULL, t149, t151, t152, t153);
                                if (gm_unw.kind) {
                                    goto gm_ll114;
                                }
                            }
                            goto me117;
mn118:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 780, 17);
me117:;
                        }
lc110:;
                        {
                            uint64_t t156 = gm_l20;
                            uint64_t r157; bool o158 = __builtin_add_overflow(t156, ((uint64_t)1ULL), &r157);
                            uint64_t t159 = r157;
                            gm_l20 = t159;
                        }
                    }
gm_ll114:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb110; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc110; }
                    }
                    goto gm_lad;
lb110:;
                }
            }
            goto ix105;
ie105:;
ix105:;
        }
        goto me3;
mn76:;
ma12:;
        uint8_t t161 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t161 != ((uint8_t)8ULL)) goto mn160;
        gm_b16_8 t162 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l28 = t162;
        {
            {
                gm_l30 = ((uint64_t)0ULL);
            }
            {
                uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                gm_l29 = t165;
            }
            {
                for (;;) {
                    uint64_t t167 = gm_l30;
                    uint64_t t168 = gm_l29;
                    uint8_t t169 = (t167 < t168);
                    if (!t169) goto lb166;
                    {
                        uint64_t t172 = gm_l30;
                        uint64_t t175 = GM_LD(uint64_t, ((uint8_t *)&t172));
                        gm_l31 = t175;
                        {
                            void * t177 = gm_l0;
                            uint64_t t178 = gm_l31;
                            uint64_t t179 = (uintptr_t)t178;
                            uint64_t t180 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                            if (t179 >= (uintptr_t)(t180)) gm_trap_index(t179, t180, gm_srcb2hdor4f6g0q, 794, 47);
                            void * t181 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t179 * 120) + 40);
                            gm_tkn42u7na3t81 t182;
                            memset(&t182, 0, sizeof t182);
                            uint32_t t183 = gm_l3;
                            gm_b0_1 t184 = gm_f_pattern_jo09k6f((void *)NULL, cx, NULL, t177, t181, t182, t183);
                            if (gm_unw.kind) {
                                goto gm_ll170;
                            }
                        }
                        goto me173;
mn174:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 794, 13);
me173:;
                    }
lc166:;
                    {
                        uint64_t t186 = gm_l30;
                        uint64_t r187; bool o188 = __builtin_add_overflow(t186, ((uint64_t)1ULL), &r187);
                        uint64_t t189 = r187;
                        gm_l30 = t189;
                    }
                }
gm_ll170:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb166; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc166; }
                }
                goto gm_lad;
lb166:;
            }
        }
        goto me3;
mn160:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 753, 5);
me3:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_payload_field_3o532rn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3) {
    struct gm_fr_payload_field_3o532rn F;
    void * gm_l0; /* w */
    uint64_t gm_l1; /* d */
    gm_b16_8 gm_l2; /* variant */
    gm_t08olq97rscn3 gm_l3; /* field */
    void * gm_l4; /* decl */
    void * gm_l6; /* lit */
    gm_toceffb697p0u gm_l7; /* found */
    gm_tl874t2atnlh3 gm_l8; /* to */
    GM_STACK_CHECK(&F, 396u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t7)) gm_trap_index(t3, t7, gm_srcb2hdor4f6g0q, 421, 14);
        void * t8 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 240) + 0)) + t3 * 216);
        gm_l4 = t8;
    }
    {
        void * t10 = gm_l4;
        uint8_t t11 = GM_LD(uint8_t, (((((uint8_t *)t10) + 40) + 64) + 0));
        if (t11 != ((uint8_t)0ULL)) goto ie9;
        gm_tv2uf4leil5hq t12 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t10) + 40) + 64) + 8));
        F.l5 = t12;
        {
            void * t15 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
            if (t16 != ((uint8_t)33ULL)) goto ie14;
            void * t17 = GM_LD(void *, ((((uint8_t *)t15) + 8) + 8));
            gm_l6 = t17;
            {
                gm_toceffb697p0u t19;
                memset(&t19, 0, sizeof t19);
                gm_l7 = t19;
            }
            {
                void * t20 = gm_l6;
                gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)t20) + 48));
                gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
                gm_b16_8 t23 = gm_l2;
                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
                void * t25 = (void *)((uint8_t *)&gm_l7);
                gm_f_find_payload_field_i16vrf1(t22, t23, t24, t25);
            }
            {
                if (!((((uint8_t *)&gm_l7))[12] != 0)) goto ie26;
                gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l7));
                gm_l8 = t27;
                {
                    void * t29 = gm_l0;
                    gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a3) + 16));
                    gm_tl874t2atnlh3 t31 = gm_l8;
                    gm_b0_1 t32 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t29, t30, t31);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix26;
ie26:;
ix26:;
            }
            goto ix14;
ie14:;
            {
                gm_b0_1 t34;
                uint8_t t35 = 0;
                F.ret = t34;
                goto gm_fret;
            }
ix14:;
        }
        goto ix9;
ie9:;
        {
            gm_b0_1 t37;
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
ix9:;
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
extern __attribute__((aligned(4))) void gm_f_pop_label_e8oj3h3(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_pop_label_e8oj3h3_c(void * a0, gm_t1s75i08qtptd a1) { gm_f_pop_label_e8oj3h3(a0, &a1); }
extern __attribute__((aligned(4))) void gm_f_pop_scope_vgka5ae(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_label_gg2j5c3(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_push_label_gg2j5c3 F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l2; /* l */
    GM_STACK_CHECK(&F, 322u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        if (!((((uint8_t *)&F.l1))[32] != 0)) goto ie2;
        gm_t08olq97rscn3 t3 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&F.l1));
        gm_l2 = t3;
        {
            void * t5 = gm_l0;
            void * t6 = (void *)(((uint8_t *)t5) + 72);
            gm_t08olq97rscn3 t7;
            memset((uint8_t *)&t7 + 28, 0, 4);
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t7 + 0, t8);
            gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t7 + 16, t9);
            gm_tftb1odc739t5 t10 = gm_f_push_kiclmcn_2(t6, &t7);
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srcb2hdor4f6g0q, 726, 9);
            gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
        }
        {
            void * t12 = gm_l0;
            gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
            gm_t6ok5tvcjc200 t14;
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
            gm_tnpifv20irnph t16;
            GM_ST(gm_b16_8, ((uint8_t *)&t16 + 0), t15);
            gm_tabb710d9mq2e t17;
            gm_f_format_ekcps7q((void *)&t17, (void *)NULL, cx, NULL, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_srcb2hdor4f6g0q, 727, 40);
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            GM_ST(gm_b16_8, (uint8_t *)&t14 + 0, t19);
            gm_th2lqbo77pvsc t20;
            memset(&t20, 0, sizeof t20);
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t14 + 16, t20);
            gm_b0_1 t21 = gm_f_def_once_pqgvduh((void *)NULL, cx, NULL, t12, t13, &t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_u4jfguj(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ref_key_vam5ppg(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ref_key_vam5ppg F;
    gm_tsncjtib3j1l1 gm_l0; /* d */
    uint64_t gm_l1; /* x */
    uint64_t gm_l2; /* x */
    uint32_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 166u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    uint64_t t6 = gm_l1;
    uint64_t t7 = (uint64_t)t6;
    t1 = t7;
    goto me2;
mn3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t9 != ((uint8_t)1ULL)) goto mn8;
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 0));
    gm_l2 = t10;
    uint32_t t11 = GM_LD(uint32_t, ((((uint8_t *)a0) + 8) + 8));
    gm_l3 = t11;
    unsigned __int128 n12 = (unsigned __int128)((uint64_t)60ULL);
    if (n12 >= 64) gm_trap_shift((uint64_t)n12, "u64", gm_srcb2hdor4f6g0q, 83, 19);
    uint64_t r13 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n12);
    bool o14 = (uint64_t)(r13 >> (int)n12) != ((uint64_t)1ULL);
    if (o14) gm_trap("integer overflow in `<<`", gm_srcb2hdor4f6g0q, 83, 19);
    uint64_t t15 = r13;
    uint64_t t16 = gm_l2;
    uint64_t t17 = (uint64_t)t16;
    unsigned __int128 n18 = (unsigned __int128)((uint64_t)16ULL);
    if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_srcb2hdor4f6g0q, 83, 36);
    uint64_t r19 = (uint64_t)((uint64_t)t17 << (int)n18);
    bool o20 = (uint64_t)(r19 >> (int)n18) != t17;
    if (o20) gm_trap("integer overflow in `<<`", gm_srcb2hdor4f6g0q, 83, 36);
    uint64_t t21 = r19;
    uint64_t t22 = (uint64_t)(t15 | t21);
    uint32_t t23 = gm_l3;
    uint64_t t24 = (uint64_t)t23;
    uint64_t t25 = (uint64_t)(t22 | t24);
    t1 = t25;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 81, 37);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint64_t gm_f_ref_key_vam5ppg_c(gm_tsncjtib3j1l1 a0) { return gm_f_ref_key_vam5ppg(&a0); }
__attribute__((aligned(4))) gm_b0_1 gm_f_stmt_ald930b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stmt_ald930b F;
    void * gm_l0; /* w */
    void * gm_l1; /* s */
    gm_b16_8 gm_l3; /* places */
    void * gm_l4; /* value */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    void * gm_l8; /* e */
    void * gm_l9; /* e */
    void * gm_l10; /* body */
    void * gm_l11; /* x */
    void * gm_l14; /* value */
    void * gm_l15; /* fallback */
    gm_tkn42u7na3t81 gm_l16; /* ty */
    GM_STACK_CHECK(&F, 871u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; default: goto ma13; }
ma4:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t16 != ((uint8_t)0ULL)) goto mn15;
        gm_tv2uf4leil5hq t17 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t2) + 8) + 8) + 16));
        F.l2 = t17;
        void * t18 = gm_l0;
        void * t19 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t20 = gm_f_local_binding_25t1g13((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn15:;
ma5:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t22 != ((uint8_t)1ULL)) goto mn21;
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l3 = t23;
        void * t24 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 24));
        gm_l4 = t24;
        {
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                gm_l5 = t27;
            }
            {
                for (;;) {
                    uint64_t t29 = gm_l6;
                    uint64_t t30 = gm_l5;
                    uint8_t t31 = (t29 < t30);
                    if (!t31) goto lb28;
                    {
                        uint64_t t34 = gm_l6;
                        uint64_t t37 = GM_LD(uint64_t, ((uint8_t *)&t34));
                        gm_l7 = t37;
                        {
                            void * t39 = gm_l0;
                            uint64_t t40 = gm_l7;
                            uint64_t t41 = (uintptr_t)t40;
                            uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcb2hdor4f6g0q, 612, 46);
                            void * t43 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t41 * 88);
                            gm_b0_1 t44 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t39, t43);
                            if (gm_unw.kind) {
                                goto gm_ll32;
                            }
                        }
                        goto me35;
mn36:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 612, 13);
me35:;
                    }
lc28:;
                    {
                        uint64_t t46 = gm_l6;
                        uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                        uint64_t t49 = r47;
                        gm_l6 = t49;
                    }
                }
gm_ll32:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb28; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc28; }
                }
                goto gm_lad;
lb28:;
            }
        }
        {
            void * t50 = gm_l0;
            void * t51 = gm_l4;
            gm_b0_1 t52 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn21:;
ma6:;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t54 != ((uint8_t)2ULL)) goto mn53;
        void * t55 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l8 = t55;
        void * t56 = gm_l0;
        void * t57 = gm_l8;
        gm_b0_1 t58 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t56, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn53:;
ma7:;
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t60 != ((uint8_t)6ULL)) goto mn59;
        void * t61 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l9 = t61;
        void * t62 = gm_l0;
        void * t63 = gm_l9;
        gm_b0_1 t64 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t62, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn59:;
ma8:;
        uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t66 != ((uint8_t)3ULL)) goto mn65;
        void * t67 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l10 = t67;
        void * t68 = gm_l0;
        void * t69 = gm_l10;
        gm_b0_1 t70 = gm_f_stmt_ald930b((void *)NULL, cx, NULL, t68, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn65:;
ma9:;
        uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t72 != ((uint8_t)4ULL)) goto mn71;
        void * t73 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l11 = t73;
        void * t74 = gm_l0;
        void * t75 = gm_l11;
        gm_b0_1 t76 = gm_f_when_stmt_r5g5fsm((void *)NULL, cx, NULL, t74, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn71:;
ma10:;
        uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t78 != ((uint8_t)5ULL)) goto mn77;
        gm_t43d6nbdfap6p t79 = GM_LD(gm_t43d6nbdfap6p, ((((uint8_t *)t2) + 8) + 8));
        F.l12 = t79;
        {
            void * t81 = gm_l0;
            uint32_t t82 = GM_LD(uint32_t, (((((uint8_t *)&F.l12) + 72) + 24) + 8));
            gm_b0_1 t83 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t81, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t84 = gm_l0;
            void * t85 = (void *)(((uint8_t *)&F.l12) + 40);
            gm_b0_1 t86 = gm_f_cond_d94lif5((void *)NULL, cx, NULL, t84, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t87 = gm_l0;
            gm_b0_1 t88 = gm_f_push_label_gg2j5c3((void *)NULL, cx, NULL, t87, &(*(gm_t1s75i08qtptd *)(((uint8_t *)&F.l12) + 0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t89 = gm_l0;
            void * t90 = (void *)(((uint8_t *)&F.l12) + 72);
            gm_b0_1 t91 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t89, t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t92 = gm_l0;
            gm_f_pop_label_e8oj3h3(t92, &(*(gm_t1s75i08qtptd *)(((uint8_t *)&F.l12) + 0)));
        }
        {
            void * t93 = gm_l0;
            gm_f_pop_scope_vgka5ae(t93);
        }
        goto me3;
mn77:;
ma11:;
        uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t95 != ((uint8_t)7ULL)) goto mn94;
        gm_t0d02142k4tu8 t96 = GM_LD(gm_t0d02142k4tu8, (((((uint8_t *)t2) + 8) + 8) + 0));
        F.l13 = t96;
        void * t97 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 64));
        gm_l14 = t97;
        void * t98 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 72));
        gm_l15 = t98;
        {
            void * t100 = gm_l0;
            void * t101 = gm_l14;
            gm_b0_1 t102 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t100, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t103 = gm_l0;
            void * t104 = gm_l15;
            gm_b0_1 t105 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t103, t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t106 = gm_l0;
            void * t107 = gm_l14;
            gm_tl874t2atnlh3 t108 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t107) + 72));
            gm_tkn42u7na3t81 t109 = gm_f_type_of_tiqb21n(t106, GM_CONV(gm_cseahvg8v7p0vs, t108));
            gm_l16 = t109;
        }
        {
            void * t110 = gm_l0;
            void * t111 = (void *)((uint8_t *)&F.l13);
            gm_tkn42u7na3t81 t112 = gm_l16;
            void * t113 = gm_l1;
            uint32_t t114 = GM_LD(uint32_t, ((((uint8_t *)t113) + 128) + 8));
            gm_b0_1 t115 = gm_f_pattern_jo09k6f((void *)NULL, cx, NULL, t110, t111, t112, t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn94:;
ma12:;
        uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t117 != ((uint8_t)8ULL)) goto mn116;
        gm_t79rbdv4rafe5 t118 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t2) + 8) + 8));
        F.l17 = t118;
        void * t119 = gm_l0;
        void * t120 = (void *)((uint8_t *)&F.l17);
        gm_b0_1 t121 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t119, t120);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn116:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 609, 5);
me3:;
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
extern __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_tiqb21n(void * a0, gm_cseahvg8v7p0vs a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_use_decl_sfao4j5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_variant_at_9i78oa0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_variant_at_9i78oa0 F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* t */
    uint64_t gm_l7; /* d */
    GM_STACK_CHECK(&F, 416u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
        void * t4 = (void *)(((uint8_t *)t3) + 56);
        gm_tl874t2atnlh3 t5 = gm_l1;
        gm_tqetrgo0q24b5 t6;
        gm_f_get_b2m4si4((void *)&t6, t4, GM_CONV(gm_cseahvg8v7p0vs, t5));
        gm_trqfvmjtt4sij t7;
        if (((((uint8_t *)&t6))[40] != 0)) {
            t7 = (*(gm_trqfvmjtt4sij *)(((uint8_t *)&t6) + 0));
        } else {
            gm_tf5srh4mp4b3i t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        F.l3 = t7;
    }
    {
        {
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
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
                    gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t19 * 4));
                    gm_l6 = t23;
                    {
                        void * t26 = gm_l0;
                        gm_t88rcucg1b7uk t27 = gm_l6;
                        gm_tf5srh4mp4b3i t28 = gm_f_variant_use_29ooua3((void *)NULL, cx, NULL, t26, t27, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)));
                        if (gm_unw.kind) {
                            goto gm_ll16;
                        }
                        if (!((((uint8_t *)&t28))[8] != 0)) goto ie25;
                        uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)&t28));
                        gm_l7 = t29;
                        {
                            uint64_t t31 = gm_l7;
                            gm_tf5srh4mp4b3i t32;
                            GM_ST(uint8_t, (uint8_t *)&t32 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t32 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t32 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t32), t31);
                            ((uint8_t *)&t32)[8] = 1;
                            uint8_t t33 = 0;
                            F.ret = t32;
                            goto gm_fret;
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 393, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t35 = gm_l5;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l5 = t38;
                }
            }
gm_ll16:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
            }
            goto gm_lad;
lb12:;
        }
    }
    {
        gm_tf5srh4mp4b3i t39;
        memset(&t39, 0, sizeof t39);
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_variant_payload_3id0koh(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_variant_payload_3id0koh F;
    void * gm_l0; /* w */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* name */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_t88rcucg1b7uk gm_l4; /* i */
    gm_t88rcucg1b7uk gm_l5; /* pointee */
    uint32_t gm_l6; /* id */
    gm_b16_8 gm_l7; /* variants */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 571u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    {
        gm_t88rcucg1b7uk t2 = gm_l1;
        gm_l3 = t2;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb3;
            {
                void * t6 = gm_l0;
                void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
                void * t8 = (void *)(((uint8_t *)t7) + 8);
                gm_t88rcucg1b7uk t9 = gm_l3;
                gm_t53at80pfdus3 t10;
                gm_f_kind_jcnt4hm((void *)&t10, t8, GM_CONV(gm_csguankko4lmhd, t9));
                uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
                switch (t16) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma14; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma14; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma14; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma14; case ((uint8_t)18ULL): goto ma14; case ((uint8_t)19ULL): goto ma14; case ((uint8_t)20ULL): goto ma14; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma14; default: goto ma14; }
ma12:;
                uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
                if (t18 != ((uint8_t)13ULL)) goto mn17;
                gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t10) + 8));
                gm_l4 = t19;
                {
                    gm_t88rcucg1b7uk t21 = gm_l4;
                    gm_l3 = t21;
                }
                goto me11;
mn17:;
ma13:;
                uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
                if (t23 != ((uint8_t)8ULL)) goto mn22;
                gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t10) + 8) + 4));
                gm_l5 = t24;
                {
                    gm_t88rcucg1b7uk t26 = gm_l5;
                    gm_l3 = t26;
                }
                goto me11;
mn22:;
ma14:;
                goto lb3;
                goto me11;
mn27:;
ma15:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 824, 9);
me11:;
            }
lc3:;
        }
lb3:;
    }
    {
        void * t29 = gm_l0;
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
        void * t31 = (void *)(((uint8_t *)t30) + 8);
        gm_t88rcucg1b7uk t32 = gm_l3;
        gm_t53at80pfdus3 t33;
        gm_f_kind_jcnt4hm((void *)&t33, t31, GM_CONV(gm_csguankko4lmhd, t32));
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t33) + 0));
        if (t34 != ((uint8_t)17ULL)) goto ie28;
        uint32_t t35 = GM_LD(uint32_t, ((((uint8_t *)&t33) + 8) + 0));
        gm_l6 = t35;
        {
            uint32_t t38 = gm_l6;
            uint64_t t39 = (uint64_t)t38;
            uint64_t t40 = (uintptr_t)t39;
            void * t41 = gm_l0;
            void * t42 = GM_LD(void *, (((uint8_t *)t41) + 0));
            uint64_t t43 = GM_LD(uintptr_t, ((((uint8_t *)t42) + 1088) + 0) + 8);
            if (t40 >= (uintptr_t)(t43)) gm_trap_index(t40, t43, gm_srcb2hdor4f6g0q, 831, 28);
            uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t42) + 1088) + 0)) + t40 * 184) + 24) + 0));
            if (t44 != ((uint8_t)1ULL)) goto ie37;
            gm_b16_8 t45 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t42) + 1088) + 0)) + t40 * 184) + 24) + 8) + 8));
            gm_l7 = t45;
            {
                {
                    gm_b16_8 t48 = gm_l7;
                    gm_l8 = t48;
                }
                {
                    gm_l9 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t50 = gm_l9;
                        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        uint8_t t52 = (t50 < t51);
                        if (!t52) goto lb49;
                        {
                            uint64_t t55 = gm_l9;
                            uint64_t t56 = (uintptr_t)t55;
                            uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                            gm_tckdobunaulee t60 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t56 * 72));
                            F.l10 = t60;
                            {
                                gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                                gm_b16_8 t64 = gm_l2;
                                uint8_t t65 = (gm_eq0iqiubfm7e(((uint8_t *)&t63), ((uint8_t *)&t64)));
                                if (!t65) goto ie62;
                                {
                                    gm_tkn42u7na3t81 t67 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l10) + 16));
                                    uint8_t t68 = 0;
                                    F.ret = t67;
                                    goto gm_fret;
                                }
                                goto ix62;
ie62:;
ix62:;
                            }
                            goto me58;
mn59:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 832, 5);
me58:;
                        }
lc49:;
                        {
                            uint64_t t70 = gm_l9;
                            uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                            uint64_t t73 = r71;
                            gm_l9 = t73;
                        }
                    }
lb49:;
                }
            }
            {
                gm_tkn42u7na3t81 t74;
                memset(&t74, 0, sizeof t74);
                uint8_t t75 = 0;
                F.ret = t74;
                goto gm_fret;
            }
            goto ix37;
ie37:;
            {
                gm_tkn42u7na3t81 t77;
                memset(&t77, 0, sizeof t77);
                uint8_t t78 = 0;
                F.ret = t77;
                goto gm_fret;
            }
ix37:;
        }
        goto ix28;
ie28:;
        {
            gm_tkn42u7na3t81 t80;
            memset(&t80, 0, sizeof t80);
            uint8_t t81 = 0;
            F.ret = t80;
            goto gm_fret;
        }
ix28:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_variant_use_29ooua3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2) {
    struct gm_fr_variant_use_29ooua3 F;
    void * gm_l0; /* w */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_t88rcucg1b7uk gm_l4; /* i */
    gm_t88rcucg1b7uk gm_l5; /* ok */
    gm_t88rcucg1b7uk gm_l6; /* pointee */
    uint64_t gm_l7; /* d */
    gm_tl874t2atnlh3 gm_l8; /* to */
    GM_STACK_CHECK(&F, 406u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_t88rcucg1b7uk t2 = gm_l1;
        gm_l3 = t2;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb3;
            {
                void * t6 = gm_l0;
                void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
                void * t8 = (void *)(((uint8_t *)t7) + 8);
                gm_t88rcucg1b7uk t9 = gm_l3;
                gm_t53at80pfdus3 t10;
                gm_f_kind_jcnt4hm((void *)&t10, t8, GM_CONV(gm_csguankko4lmhd, t9));
                uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
                switch (t17) { case ((uint8_t)0ULL): goto ma15; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma15; case ((uint8_t)3ULL): goto ma15; case ((uint8_t)4ULL): goto ma15; case ((uint8_t)5ULL): goto ma15; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma15; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma15; case ((uint8_t)10ULL): goto ma15; case ((uint8_t)11ULL): goto ma15; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma15; case ((uint8_t)16ULL): goto ma15; case ((uint8_t)17ULL): goto ma15; case ((uint8_t)18ULL): goto ma15; case ((uint8_t)19ULL): goto ma15; case ((uint8_t)20ULL): goto ma15; case ((uint8_t)21ULL): goto ma15; case ((uint8_t)22ULL): goto ma15; case ((uint8_t)23ULL): goto ma15; case ((uint8_t)24ULL): goto ma15; case ((uint8_t)25ULL): goto ma15; case ((uint8_t)26ULL): goto ma15; default: goto ma15; }
ma12:;
                uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
                if (t19 != ((uint8_t)13ULL)) goto mn18;
                gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t10) + 8));
                gm_l4 = t20;
                {
                    gm_t88rcucg1b7uk t22 = gm_l4;
                    gm_l3 = t22;
                }
                goto me11;
mn18:;
ma13:;
                uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
                if (t24 != ((uint8_t)14ULL)) goto mn23;
                gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t10) + 8) + 4));
                gm_l5 = t25;
                {
                    gm_t88rcucg1b7uk t27 = gm_l5;
                    gm_l3 = t27;
                }
                goto me11;
mn23:;
ma14:;
                uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
                if (t29 != ((uint8_t)8ULL)) goto mn28;
                gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t10) + 8) + 4));
                gm_l6 = t30;
                {
                    gm_t88rcucg1b7uk t32 = gm_l6;
                    gm_l3 = t32;
                }
                goto me11;
mn28:;
ma15:;
                goto lb3;
                goto me11;
mn33:;
ma16:;
                gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 403, 9);
me11:;
            }
lc3:;
        }
lb3:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
        gm_t88rcucg1b7uk t37 = gm_l3;
        gm_tf5srh4mp4b3i t38 = gm_f_adt_decl_of_2bflsov(t36, GM_CONV(gm_csguankko4lmhd, t37));
        if (!((((uint8_t *)&t38))[8] != 0)) goto ie34;
        uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&t38));
        gm_l7 = t39;
        {
            void * t42 = gm_l0;
            uint64_t t43 = gm_l7;
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            gm_toceffb697p0u t45 = gm_f_member_def_9f4kiah((void *)NULL, cx, NULL, t42, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t45))[12] != 0)) goto ie41;
            gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&t45));
            gm_l8 = t46;
            {
                void * t48 = gm_l0;
                gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                gm_tl874t2atnlh3 t50 = gm_l8;
                gm_b0_1 t51 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t48, t49, t50);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint64_t t52 = gm_l7;
                gm_tf5srh4mp4b3i t53;
                GM_ST(uint8_t, (uint8_t *)&t53 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t53 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t53 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t53), t52);
                ((uint8_t *)&t53)[8] = 1;
                uint8_t t54 = 0;
                F.ret = t53;
                goto gm_fret;
            }
            goto ix41;
ie41:;
ix41:;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        gm_tf5srh4mp4b3i t55;
        memset(&t55, 0, sizeof t55);
        uint8_t t56 = 0;
        F.ret = t55;
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
__attribute__((aligned(4))) void gm_f_walker_8qtbjaj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint64_t a3) {
    struct gm_fr_walker_8qtbjaj F;
    void * gm_l0; /* s */
    void * gm_l1; /* facts */
    void * gm_l2; /* idx */
    uint64_t gm_l3; /* module */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint32_t gm_l9; /* f */
    uint64_t gm_l10; /* p */
    uint64_t gm_l11; /* yd */
    void * gm_l12; /* lit */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    GM_STACK_CHECK(&F, 838u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        GM_ST(void *, (uint8_t *)&F.l4 + 0, t2);
        void * t3 = gm_l1;
        GM_ST(void *, (uint8_t *)&F.l4 + 8, t3);
        void * t4 = gm_l2;
        GM_ST(void *, (uint8_t *)&F.l4 + 16, t4);
        uint64_t t5 = gm_l3;
        GM_ST(uint64_t, (uint8_t *)&F.l4 + 24, t5);
        gm_f_list_ubba23m_2((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l4 + 32)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_rk274m7_2((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l4 + 72)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_7pa51u7((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l4 + 112)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_oh3lgki((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l4 + 168)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_s7cd6p5((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l4 + 224)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t1335qftqvd8f t6;
        memset(&t6, 0, sizeof t6);
        GM_ST(gm_t1335qftqvd8f, (uint8_t *)&F.l4 + 280, t6);
    }
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            void * t8 = gm_l0;
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
            uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 240) + 0) + 8);
            gm_l5 = t10;
        }
        {
            for (;;) {
                uint64_t t12 = gm_l6;
                uint64_t t13 = gm_l5;
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    uint64_t t17 = gm_l6;
                    uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&t17));
                    gm_l7 = t20;
                    {
                        void * t23 = gm_l0;
                        uint64_t t24 = gm_l7;
                        gm_tagrkbqh054h7 t25;
                        gm_f_state_a5bnsjb((void *)&t25, (void *)NULL, cx, NULL, t23, t24);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
                        if (t26 != ((uint8_t)2ULL)) goto ie22;
                        F.l8 = (*(gm_t6qooom8qa2do *)(((uint8_t *)&t25) + 8));
                        {
                            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                            if (t29 != ((uint8_t)5ULL)) goto ie28;
                            uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&F.l8) + 8));
                            gm_l9 = t30;
                            {
                                void * t33 = (void *)(((uint8_t *)&F.l4) + 224);
                                uint32_t t34 = gm_l9;
                                uint8_t t35 = gm_f_contains_2dnkft5(t33, t34);
                                uint8_t t36 = (uint8_t)!t35;
                                if (!t36) goto ie32;
                                {
                                    void * t38 = (void *)(((uint8_t *)&F.l4) + 224);
                                    uint32_t t39 = gm_l9;
                                    uint64_t t40 = gm_l7;
                                    gm_tftb1odc739t5 t41 = gm_f_put_sokl06f(t38, t39, t40);
                                    if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcb2hdor4f6g0q, 102, 49);
                                    gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                                }
                                goto ix32;
ie32:;
ix32:;
                            }
                            goto ix28;
ie28:;
ix28:;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 99, 5);
me18:;
                }
lc11:;
                {
                    uint64_t t44 = gm_l6;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l6 = t47;
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
        void * t49 = gm_l0;
        void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
        if (!(((((uint8_t *)t50) + 320))[8] != 0)) goto ie48;
        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 320));
        gm_l10 = t51;
        {
            void * t54 = gm_l0;
            void * t55 = GM_LD(void *, (((uint8_t *)t54) + 0));
            uint64_t t56 = gm_l10;
            gm_b16_8 t57;
            GM_ST(void *, (uint8_t *)&t57, (void *)(gm_ses7tdrdt8a6u));
            GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(5));
            gm_tf5srh4mp4b3i t58 = gm_f_lookup_59juqoo(t55, t56, t57);
            if (!((((uint8_t *)&t58))[8] != 0)) goto ie53;
            uint64_t t59 = GM_LD(uint64_t, ((uint8_t *)&t58));
            gm_l11 = t59;
            {
                uint64_t t62 = gm_l11;
                uint64_t t63 = (uintptr_t)t62;
                void * t64 = gm_l0;
                void * t65 = GM_LD(void *, (((uint8_t *)t64) + 0));
                uint64_t t66 = GM_LD(uintptr_t, ((((uint8_t *)t65) + 240) + 0) + 8);
                if (t63 >= (uintptr_t)(t66)) gm_trap_index(t63, t66, gm_srcb2hdor4f6g0q, 108, 16);
                uint8_t t67 = ((GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t65) + 240) + 0)) + t63 * 216) + 40) + 56) + 0) == ((uint8_t)2ULL)));
                if (!t67) goto ie61;
                {
                    void * t70 = gm_l0;
                    uint64_t t71 = gm_l11;
                    void * t72 = gm_f_effect_lit_26tkk47(t70, t71);
                    if (!(GM_LD(void *, ((uint8_t *)&t72)) != 0)) goto ie69;
                    void * t73 = GM_LD(void *, ((uint8_t *)&t72));
                    gm_l12 = t73;
                    {
                        {
                            gm_l14 = ((uint64_t)0ULL);
                        }
                        {
                            void * t76 = gm_l12;
                            uint64_t t77 = GM_LD(uintptr_t, (((uint8_t *)t76) + 24) + 8);
                            gm_l13 = t77;
                        }
                        {
                            for (;;) {
                                uint64_t t79 = gm_l14;
                                uint64_t t80 = gm_l13;
                                uint8_t t81 = (t79 < t80);
                                if (!t81) goto lb78;
                                {
                                    uint64_t t84 = gm_l14;
                                    uint64_t t87 = GM_LD(uint64_t, ((uint8_t *)&t84));
                                    gm_l15 = t87;
                                    {
                                        uint64_t t90 = gm_l15;
                                        uint64_t t91 = (uintptr_t)t90;
                                        void * t92 = gm_l12;
                                        uint64_t t93 = GM_LD(uintptr_t, (((uint8_t *)t92) + 24) + 8);
                                        if (t91 >= (uintptr_t)(t93)) gm_trap_index(t91, t93, gm_srcb2hdor4f6g0q, 111, 28);
                                        uint8_t t94 = (((GM_LD(uintptr_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t92) + 24)) + t91 * 80) + 0) + 0) + 8) == 5) && memcmp(GM_LD(void *, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t92) + 24)) + t91 * 80) + 0) + 0)), gm_s4g2k7ups3ui6, 5) == 0));
                                        if (!t94) goto ie89;
                                        {
                                            uint64_t t96 = gm_l11;
                                            uint64_t t97 = gm_l15;
                                            uint32_t t98 = (uint32_t)t97;
                                            gm_tn68el8d14vku t99;
                                            GM_ST(uint32_t, (uint8_t *)&t99 + 12, 0);
                                            GM_ST(uint64_t, ((uint8_t *)&t99 + 0), t96);
                                            GM_ST(uint32_t, ((uint8_t *)&t99 + 8), t98);
                                            gm_tsncjtib3j1l1 t100;
                                            GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)1ULL));
                                            GM_ST(gm_tn68el8d14vku, ((uint8_t *)&t100 + 8), t99);
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1335qftqvd8f *)(((uint8_t *)&F.l4) + 280)) + 25, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1335qftqvd8f *)(((uint8_t *)&F.l4) + 280)) + 26, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1335qftqvd8f *)(((uint8_t *)&F.l4) + 280)) + 28, 0);
                                            ((uint8_t *)&(*(gm_t1335qftqvd8f *)(((uint8_t *)&F.l4) + 280)))[24] = 1;
                                            GM_ST(gm_tsncjtib3j1l1, (uint8_t *)&(*(gm_t1335qftqvd8f *)(((uint8_t *)&F.l4) + 280)) + 0, t100);
                                        }
                                        {
                                            goto lb78;
                                        }
                                        goto ix89;
ie89:;
ix89:;
                                    }
                                    goto me85;
mn86:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 110, 21);
me85:;
                                }
lc78:;
                                {
                                    uint64_t t102 = gm_l14;
                                    uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                                    uint64_t t105 = r103;
                                    gm_l14 = t105;
                                }
                            }
lb78:;
                        }
                    }
                    goto ix69;
ie69:;
ix69:;
                }
                goto ix61;
ie61:;
ix61:;
            }
            goto ix53;
ie53:;
ix53:;
        }
        goto ix48;
ie48:;
ix48:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l4), 312);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_when_stmt_r5g5fsm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_when_stmt_r5g5fsm F;
    void * gm_l0; /* w */
    void * gm_l1; /* x */
    void * gm_l4; /* x2 */
    GM_STACK_CHECK(&F, 210u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        gm_b0_1 t5 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t2, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l1;
        void * t8 = (void *)(((uint8_t *)t7) + 8);
        gm_b0_1 t9 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t6, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t11 = gm_l1;
        if (!(((((uint8_t *)t11) + 48))[48] != 0)) goto ie10;
        gm_tapdr2ag17fh7 t12 = GM_LD(gm_tapdr2ag17fh7, (((uint8_t *)t11) + 48));
        F.l2 = t12;
        {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t16 != ((uint8_t)0ULL)) goto mn15;
            gm_t79rbdv4rafe5 t17 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&F.l2) + 8));
            F.l3 = t17;
            void * t18 = gm_l0;
            void * t19 = (void *)((uint8_t *)&F.l3);
            gm_b0_1 t20 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me14;
mn15:;
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t22 != ((uint8_t)1ULL)) goto mn21;
            void * t23 = GM_LD(void *, (((uint8_t *)&F.l2) + 8));
            gm_l4 = t23;
            void * t24 = gm_l0;
            void * t25 = gm_l4;
            gm_b0_1 t26 = gm_f_when_stmt_r5g5fsm((void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me14;
mn21:;
            gm_trap("no match arm matched the scrutinee", gm_srcb2hdor4f6g0q, 641, 9);
me14:;
        }
        goto ix10;
ie10:;
ix10:;
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
