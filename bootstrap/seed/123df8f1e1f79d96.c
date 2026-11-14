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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { void * a[1]; } gm_tn3r4rvlh0rsv;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t88rcucg1b7uk v1; } f1; } gm_t5e6kkuq21nig;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn3r4rvlh0rsv) == 8 && _Alignof(gm_tn3r4rvlh0rsv) == 8 && offsetof(gm_tn3r4rvlh0rsv, a) == 0, "layout of `[1]^Expr`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5e6kkuq21nig) == 16 && _Alignof(gm_t5e6kkuq21nig) == 8 && offsetof(gm_t5e6kkuq21nig, f0) == 0 && offsetof(gm_t5e6kkuq21nig, f1) == 8, "layout of `ArrayLen`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tbngattl1fo51 f0; uint8_t f1; uint8_t pe[3]; } gm_thetuhtouve7f;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_th2lqbo77pvsc f0; void * f1; } gm_tpouv9r7lrri2;
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?ValTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_thetuhtouve7f) == 12 && _Alignof(gm_thetuhtouve7f) == 4 && offsetof(gm_thetuhtouve7f, f0) == 0 && offsetof(gm_thetuhtouve7f, f1) == 8, "layout of `?(inclusive: bool, elem: Ty)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tpouv9r7lrri2) == 32 && _Alignof(gm_tpouv9r7lrri2) == 8 && offsetof(gm_tpouv9r7lrri2, f0) == 0 && offsetof(gm_tpouv9r7lrri2, f1) == 24, "layout of `SpreadPart`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tb38v5s9j1r32 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc9qv69frb08e;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_tc9qv69frb08e) == 136 && _Alignof(gm_tc9qv69frb08e) == 8 && offsetof(gm_tc9qv69frb08e, f0) == 0 && offsetof(gm_tc9qv69frb08e, f1) == 8 && offsetof(gm_tc9qv69frb08e, f2) == 120, "layout of `Pat`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_array_type_construction_vgjg706 {
    gm_frame h;
    void * ret;
};
struct gm_fr_box_mut_dlc4mlv {
    gm_frame h;
    void * ret;
};
struct gm_fr_construct_array_qkofm98 {
    gm_frame h;
    gm_tc3oht4ksth9d l8; /* e */
    gm_t7te2tdpebbt4 l13; /* a */
    gm_tc3oht4ksth9d l15; /* e */
    gm_trqfvmjtt4sij l22; /* values */
    gm_trqfvmjtt4sij l23; /* values */
    gm_t7te2tdpebbt4 l27; /* a */
    gm_trqfvmjtt4sij l28; /* stmts */
    gm_t7te2tdpebbt4 l31; /* a */
    gm_tc3oht4ksth9d l32; /* e */
    gm_tc3oht4ksth9d l34; /* e */
    void * ret;
};
struct gm_fr_construct_enum_array_d7cisuu {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* names */
    gm_tckdobunaulee l10; /* v */
    gm_tc3oht4ksth9d l12; /* x */
    gm_trqfvmjtt4sij l14; /* stmts */
    gm_trqfvmjtt4sij l15; /* slots */
    gm_trqfvmjtt4sij l18; /* written */
    gm_t7te2tdpebbt4 l22; /* a */
    gm_tc3oht4ksth9d l24; /* x */
    gm_trqfvmjtt4sij l37; /* missing */
    gm_trqfvmjtt4sij l42; /* values */
    void * ret;
};
struct gm_fr_construct_param_array_btk60nl {
    gm_frame h;
    gm_tc3oht4ksth9d l6; /* x */
    void * ret;
};
struct gm_fr_construction_elem_type_qm1ncub {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_empty_slice_3v2gjd4_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
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
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_enum_array_index_kma2hbd {
    gm_frame h;
    void * ret;
};
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_given_27f1vc1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_math_vector_09mtuao {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_vector_gkq6b88 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_58nr42v_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
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
struct gm_fr_mk_pat_hr2l6c4 {
    gm_frame h;
    gm_tb38v5s9j1r32 l1; /* kind */
    void * ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_t69m56meqefs8 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_r2k6rjf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h9tbucc_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j7fb9h1 {
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
struct gm_fr_unsupported_r3r7qdh {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_variant_position_fdpecs3 {
    gm_frame h;
    gm_tckdobunaulee l10; /* var */
    gm_trqfvmjtt4sij l15; /* arms */
    void * ret;
};
struct gm_fr_without_call_t1kfc9e {
    gm_frame h;
    gm_t2gtbh39ptekd l10; /* kind */
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_len_eielif2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_array_type_construction_vgjg706(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void * gm_f_box_mut_dlc4mlv(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_builtin_a7cjd7d(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_construct_array_qkofm98(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1, gm_tf5srh4mp4b3i a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_enum_array_d7cisuu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_param_array_btk60nl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_construction_elem_type_qm1ncub(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) void * gm_f_enum_array_index_kma2hbd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, void * a4, gm_tl874t2atnlh3 a5);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_field_value_19vleud(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_format_47t5q9n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_blrf705(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d0bas9o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_frab70n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h75o0bg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_i7ngra8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n8a3chu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ti6vs6t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_v7qttjn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_given_27f1vc1(void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_o9r57ha(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_math_vector_09mtuao(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_length_is_type_soi53ah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_math_vector_conversion_0cttjm0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_math_vector_parts_49c7865(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_thetuhtouve7f gm_f_range_of_dmmo768(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_ty_uk6c1lp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_spread_parts_e62q2l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_variant_position_fdpecs3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_conversion_32a9cuf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_without_call_t1kfc9e(void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_k652m2ksgeg[40] __attribute__((aligned(8)));
extern uint8_t gm_ksnfevokhod[40] __attribute__((aligned(8)));
extern uint8_t gm_kv89j160jqp[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0etnbnk4miq5[5];
extern const uint8_t gm_s0k8snlvukl86[66];
extern const uint8_t gm_s3nknbqjesra1[19];
extern const uint8_t gm_sdjbnp04akbbb[50];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sksrclikobn5n[37];
extern const uint8_t gm_soiet16j0e8ar[34];
extern const uint8_t gm_spevct0q3cjgb[5];
extern const uint8_t gm_sqirjnd2segia[40];
extern const uint8_t gm_sqof8dgnasnqk[69];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src5cfou479ljhn[];
extern const char gm_srck5orfpjti60c[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_suv2tallru6a1[2];
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
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) void * gm_f_box_mut_dlc4mlv(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_box_mut_dlc4mlv F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_tq7s98ihonkju t2 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t1)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ksnfevokhod)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t2))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t2) + 8, gm_src5cfou479ljhn, 49, 63);
    void * t3 = GM_LD(void *, (((uint8_t *)&t2) + 8));
    F.ret = t3;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8q3kspo F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        int32_t t9 = gm_f_mag_cmp_s39veq6(t6, t8);
        uint8_t t10 = ((t9 == ((int32_t)0LL)));
        t1 = t10;
    } else t1 = 0;
    F.ret = t1;
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
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_error_expr_o6qt7e2 F;
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
    gm_t88rcucg1b7uk t3 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_error_ki8i3k6 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)26ULL));
    gm_t88rcucg1b7uk t3 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t3;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_0cs85gs F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
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
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_3cp9o8d(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 1);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_given_27f1vc1(void *env, gm_node *cx, gm_node *inst, uint64_t a0) {
    struct gm_fr_given_27f1vc1 F;
    uint64_t gm_l0; /* n */
    GM_STACK_CHECK(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint64_t t3 = gm_l0;
    uint8_t t4 = ((t3 == ((uint64_t)1ULL)));
    if (!t4) goto ie2;
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s0etnbnk4miq5));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(4));
    gm_b16_8 t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    uint64_t t9 = gm_l0;
    gm_t4ce0jd3tdro4 t10;
    GM_ST(uint64_t, ((uint8_t *)&t10 + 0), t9);
    gm_tabb710d9mq2e t11;
    gm_f_format_n8a3chu((void *)&t11, (void *)NULL, cx, NULL, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_src5cfou479ljhn, 244, 76);
    gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
    gm_b16_8 t13 = t12;
    gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
    t1 = t14;
ix2:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)26ULL)));
    F.ret = t4;
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
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t9 != ((uint8_t)0ULL)) goto pa8;
    goto po7;
pa8:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t11 != ((uint8_t)1ULL)) goto pa10;
    goto po7;
pa10:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t13 != ((uint8_t)2ULL)) goto pa12;
    goto po7;
pa12:;
    goto mn6;
po7:;
    t4 = ((uint8_t)1);
    goto me5;
mn6:;
    t4 = ((uint8_t)0);
    goto me5;
mn14:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me5:;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_math_vector_09mtuao(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_math_vector_09mtuao F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_tb7u19loo3j87 t3;
    gm_f_math_vector_parts_49c7865((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    if (((((uint8_t *)&t3))[16] != 0)) goto mn6;
    t4 = ((uint8_t)0);
    goto me5;
mn6:;
    t4 = ((uint8_t)1);
    goto me5;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 26, 62);
me5:;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_vector_gkq6b88 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_tb7u19loo3j87 t3;
    gm_f_vector_parts_crrfopm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    if (((((uint8_t *)&t3))[16] != 0)) goto mn6;
    t4 = ((uint8_t)0);
    goto me5;
mn6:;
    t4 = ((uint8_t)1);
    goto me5;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 25, 57);
me5:;
    F.ret = t4;
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3v2gjd4_3();
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
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_pat_hr2l6c4 F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_tc9qv69frb08e t1;
    memset((uint8_t *)&t1 + 4, 0, 4);
    memset((uint8_t *)&t1 + 132, 0, 4);
    gm_t88rcucg1b7uk t2 = gm_l0;
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t1 + 0, t2);
    memcpy((uint8_t *)&t1 + 8, ((uint8_t *)&F.l1), 112);
    gm_tl874t2atnlh3 t3 = gm_l2;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 120, t3);
    gm_tq7s98ihonkju t4 = gm_f_new_1b2khgi((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k652m2ksgeg)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 311, 83);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_3ef746b F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_0jmnnpk(t10, ((uint64_t)1ULL), t12);
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
        gm_b16_8 t21 = gm_l1;
        GM_ST(gm_b16_8, ((uint8_t *)t20 + t18 * 16), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k_3 F;
    void * gm_l0; /* l */
    gm_tbbqefuhv2r8h gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_3v2gjd4_3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_h9tbucc_3(t10, ((uint64_t)1ULL), t12);
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
        gm_tbbqefuhv2r8h t21 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)a1));
        GM_ST(gm_tbbqefuhv2r8h, ((uint8_t *)t20 + t18 * 24), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d943vgp_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_g3tddrb_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_61fb705_2(t10, ((uint64_t)1ULL), t12);
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
        gm_t69m56meqefs8 t21 = F.l1;
        GM_ST(gm_t69m56meqefs8, ((uint8_t *)t20 + t18 * 88), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_r2k6rjf F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_8q3kspo();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_j7fb9h1(t10, ((uint64_t)1ULL), t12);
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
        void * t21 = gm_l1;
        GM_ST(void *, ((uint8_t *)t20 + t18 * 8), t21);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0jmnnpk F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3cp9o8d F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)1ULL), ((uint64_t)1ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_61fb705_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h9tbucc_3 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j7fb9h1 F;
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
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_r3r7qdh F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 133u);
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
        gm_b0_1 t6 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        gm_tl874t2atnlh3 t9 = gm_l1;
        void * t10 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11 = 0;
        F.ret = t10;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_t6j6smj7qoctu t4;
        gm_b16_8 t5 = GM_LD(gm_b16_8, gm_c3t36dlh4g5);
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tabb710d9mq2e t7;
        gm_f_format_e79cvtl((void *)&t7, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srclv0dcbbfhves, 635, 19);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_b0_1 t10 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t9);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_qnrd30f F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)1ULL));
    gm_t88rcucg1b7uk t3 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t3;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_array_type_construction_vgjg706(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_array_type_construction_vgjg706 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* len_ast */
    void * gm_l3; /* elem */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* call */
    gm_b16_8 gm_l6; /* args */
    void * gm_l7; /* l */
    gm_t88rcucg1b7uk gm_l8; /* t */
    gm_t88rcucg1b7uk gm_l9; /* et */
    gm_tnnjeerolos0a gm_l10; /* kind */
    gm_t88rcucg1b7uk gm_l11; /* index */
    gm_t88rcucg1b7uk gm_l12; /* elem_ty */
    gm_tf5srh4mp4b3i gm_l13; /* len */
    void * gm_l14; /* l */
    uint64_t gm_l15; /* n */
    gm_t88rcucg1b7uk gm_l16; /* pl */
    gm_t88rcucg1b7uk gm_l17; /* t */
    GM_STACK_CHECK(&F, 1092u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l3;
        void * t3 = gm_f_construction_call_ktqtrpo(t2);
        void * t4;
        if ((GM_LD(void *, ((uint8_t *)&t3)) != 0)) {
            void * t5 = GM_LD(void *, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            void * t6 = gm_l0;
            gm_tl874t2atnlh3 t7 = gm_l4;
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s3nknbqjesra1));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(18));
            void * t9 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t6, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l5 = t4;
    }
    {
        void * t12 = gm_l5;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
        if (t13 != ((uint8_t)19ULL)) goto ie11;
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((((uint8_t *)t12) + 8) + 8) + 8));
        gm_l6 = t14;
        {
            void * t17 = gm_l2;
            if (!(GM_LD(void *, ((uint8_t *)&t17)) != 0)) goto ie16;
            void * t18 = GM_LD(void *, ((uint8_t *)&t17));
            gm_l7 = t18;
            void * t19 = gm_l0;
            void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
            void * t21 = gm_l0;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 8));
            void * t23 = gm_l7;
            uint8_t t24 = gm_f_length_is_type_soi53ah((void *)NULL, cx, NULL, t20, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t24) goto ie16;
            {
                void * t26 = gm_l0;
                void * t27 = gm_l1;
                void * t28 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t27);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t29 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t26, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t29;
            }
            {
                void * t31 = gm_l0;
                void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
                void * t33 = (void *)(((uint8_t *)t32) + 8);
                gm_t88rcucg1b7uk t34 = gm_l8;
                gm_t53at80pfdus3 t35;
                gm_f_kind_jcnt4hm((void *)&t35, t33, GM_CONV(gm_csguankko4lmhd, t34));
                uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
                if (t36 != ((uint8_t)11ULL)) goto ie30;
                gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t35) + 8) + 8));
                gm_l9 = t37;
                gm_tnnjeerolos0a t38 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&t35) + 8) + 12));
                gm_l10 = t38;
                uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                if (t39 != ((uint8_t)1ULL)) goto ie30;
                gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 4));
                gm_l11 = t40;
                {
                    void * t42 = gm_l0;
                    gm_t88rcucg1b7uk t43 = gm_l8;
                    gm_t88rcucg1b7uk t44 = gm_l11;
                    gm_t88rcucg1b7uk t45 = gm_l9;
                    gm_b16_8 t46 = gm_l6;
                    gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                    gm_tl874t2atnlh3 t48 = gm_l4;
                    void * t49 = gm_f_construct_enum_array_d7cisuu((void *)NULL, cx, NULL, t42, t43, t44, t45, t47, t48);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t50 = 0;
                    F.ret = t49;
                    goto gm_fret;
                }
                goto ix30;
ie30:;
ix30:;
            }
            {
                void * t51 = gm_l0;
                void * t52 = GM_LD(void *, (((uint8_t *)t51) + 0));
                gm_tl874t2atnlh3 t53 = gm_l4;
                void * t54 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t52, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t55 = 0;
                F.ret = t54;
                goto gm_fret;
            }
            goto ix16;
ie16:;
ix16:;
        }
        {
            void * t56 = gm_l0;
            void * t57 = gm_l3;
            gm_tkn42u7na3t81 t58 = gm_f_construction_elem_type_qm1ncub((void *)NULL, cx, NULL, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t59;
            if (((((uint8_t *)&t58))[4] != 0)) {
                gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t58) + 0));
                t59 = t60;
            } else {
                void * t61 = gm_l0;
                void * t62 = GM_LD(void *, (((uint8_t *)t61) + 0));
                gm_tl874t2atnlh3 t63 = gm_l4;
                void * t64 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t62, t63);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t65 = 0;
                F.ret = t64;
                goto gm_fret;
            }
            gm_l12 = t59;
        }
        {
            gm_tf5srh4mp4b3i t66;
            memset(&t66, 0, sizeof t66);
            gm_l13 = t66;
        }
        {
            void * t68 = gm_l2;
            if (!(GM_LD(void *, ((uint8_t *)&t68)) != 0)) goto ie67;
            void * t69 = GM_LD(void *, ((uint8_t *)&t68));
            gm_l14 = t69;
            {
                void * t71 = gm_l0;
                void * t72 = GM_LD(void *, (((uint8_t *)t71) + 0));
                void * t73 = gm_l0;
                uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)t73) + 8));
                void * t75 = gm_l14;
                gm_t5e6kkuq21nig t76 = gm_f_array_len_eielif2((void *)NULL, cx, NULL, t72, t74, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&t76) + 0));
                switch (t82) { case ((uint8_t)0ULL): goto ma78; case ((uint8_t)1ULL): goto ma79; case ((uint8_t)2ULL): goto ma80; default: goto ma81; }
ma78:;
                uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&t76) + 0));
                if (t84 != ((uint8_t)0ULL)) goto mn83;
                uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&t76) + 8));
                gm_l15 = t85;
                {
                    uint64_t t87 = gm_l15;
                    gm_tf5srh4mp4b3i t88;
                    GM_ST(uint8_t, (uint8_t *)&t88 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t88 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t88 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t88), t87);
                    ((uint8_t *)&t88)[8] = 1;
                    gm_l13 = t88;
                }
                goto me77;
mn83:;
ma79:;
                uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t76) + 0));
                if (t90 != ((uint8_t)1ULL)) goto mn89;
                gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t76) + 8));
                gm_l16 = t91;
                {
                    void * t93 = gm_l0;
                    void * t94 = GM_LD(void *, (((uint8_t *)t93) + 0));
                    void * t95 = (void *)(((uint8_t *)t94) + 8);
                    gm_t88rcucg1b7uk t96 = gm_l16;
                    gm_t88rcucg1b7uk t97 = gm_l12;
                    gm_tgglga8ro3isc t98;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t98 + 0), t96);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t98 + 4), t97);
                    gm_t53at80pfdus3 t99;
                    memset(&t99, 0, sizeof t99);
                    GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)20ULL));
                    GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t99 + 8), t98);
                    gm_t88rcucg1b7uk t100 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t95, &t99);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l17 = t100;
                }
                {
                    void * t101 = gm_l0;
                    gm_t88rcucg1b7uk t102 = gm_l17;
                    gm_t88rcucg1b7uk t103 = gm_l12;
                    gm_b16_8 t104 = gm_l6;
                    gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
                    gm_tl874t2atnlh3 t106 = gm_l4;
                    void * t107 = gm_f_construct_param_array_btk60nl((void *)NULL, cx, NULL, t101, t102, t103, t105, t106);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t108 = 0;
                    F.ret = t107;
                    goto gm_fret;
                }
                goto me77;
mn89:;
ma80:;
                uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&t76) + 0));
                if (t110 != ((uint8_t)2ULL)) goto mn109;
                void * t111 = gm_l0;
                void * t112 = GM_LD(void *, (((uint8_t *)t111) + 0));
                gm_tl874t2atnlh3 t113 = gm_l4;
                void * t114 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t112, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t115 = 0;
                F.ret = t114;
                goto gm_fret;
                goto me77;
mn109:;
ma81:;
                gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 16, 9);
me77:;
            }
            goto ix67;
ie67:;
ix67:;
        }
        {
            void * t116 = gm_l0;
            gm_tkn42u7na3t81 t117;
            memset(&t117, 0, sizeof t117);
            gm_tf5srh4mp4b3i t118 = gm_l13;
            gm_t88rcucg1b7uk t119 = gm_l12;
            gm_b16_8 t120 = gm_l6;
            gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
            gm_tl874t2atnlh3 t122 = gm_l4;
            void * t123 = gm_f_construct_array_qkofm98((void *)NULL, cx, NULL, t116, t117, t118, t119, t121, t122);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t124 = 0;
            F.ret = t123;
            goto gm_fret;
        }
        goto ix11;
ie11:;
        {
            void * t126 = gm_l0;
            gm_tl874t2atnlh3 t127 = gm_l4;
            gm_b16_8 t128;
            GM_ST(void *, (uint8_t *)&t128, (void *)(gm_s3nknbqjesra1));
            GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(18));
            void * t129 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t126, t127, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t130 = 0;
            F.ret = t129;
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
extern __attribute__((aligned(4))) void * gm_f_box_mut_dlc4mlv(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_construct_array_qkofm98(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1, gm_tf5srh4mp4b3i a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_construct_array_qkofm98 F;
    void * gm_l0; /* cx */
    gm_tkn42u7na3t81 gm_l1; /* ty */
    gm_tf5srh4mp4b3i gm_l2; /* len */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_t08olq97rscn3 gm_l7; /* n */
    uint64_t gm_l9; /* l */
    void * gm_l10; /* v */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t08olq97rscn3 gm_l14; /* n */
    uint64_t gm_l16; /* n */
    void * gm_l17; /* v */
    gm_t88rcucg1b7uk gm_l18; /* vt */
    gm_t88rcucg1b7uk gm_l19; /* target */
    void * gm_l20; /* c */
    void * gm_l21; /* c */
    uint8_t gm_l24; /* spreading */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    void * gm_l33; /* v */
    gm_b16_8 gm_l35; /* parts */
    gm_b16_8 gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    gm_tpouv9r7lrri2 gm_l38; /* p */
    uint64_t gm_l39; /* n */
    gm_t88rcucg1b7uk gm_l40; /* t */
    gm_t88rcucg1b7uk gm_l41; /* t */
    void * gm_l42; /* arr */
    GM_STACK_CHECK(&F, 3566u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l6 = t4;
    }
    {
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t7 = ((t6 == ((uint64_t)1ULL)));
        if (!t7) goto ie5;
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_src5cfou479ljhn, 167, 42);
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t8 * 144) + 0) + 0));
        if (t10 != ((uint8_t)1ULL)) goto ie5;
        gm_t08olq97rscn3 t11 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t8 * 144) + 0) + 8) + 0));
        gm_l7 = t11;
        gm_tc3oht4ksth9d t12 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t8 * 144) + 0) + 8) + 32));
        F.l8 = t12;
        uint8_t t13 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l7) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l7) + 0)), gm_spevct0q3cjgb, 4) == 0));
        if (!t13) goto ie5;
        {
            gm_tf5srh4mp4b3i t15 = gm_l2;
            uint64_t t16;
            if (((((uint8_t *)&t15))[8] != 0)) {
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
                t16 = t17;
            } else {
                {
                    void * t19 = gm_l0;
                    gm_tl874t2atnlh3 t20 = gm_l5;
                    gm_b16_8 t21;
                    GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sdjbnp04akbbb));
                    GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(49));
                    gm_b0_1 t22 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t19, t20, t21);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t23 = gm_l0;
                void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
                gm_tl874t2atnlh3 t25 = gm_l5;
                void * t26 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t24, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t27 = 0;
                F.ret = t26;
                goto gm_fret;
            }
            gm_l9 = t16;
        }
        {
            void * t28 = gm_l0;
            void * t29 = (void *)((uint8_t *)&F.l8);
            gm_t88rcucg1b7uk t30 = gm_l3;
            void * t31 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t28, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t31;
        }
        {
            gm_tkn42u7na3t81 t32 = gm_l1;
            gm_t88rcucg1b7uk t33;
            if (((((uint8_t *)&t32))[4] != 0)) {
                gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t32) + 0));
                t33 = t34;
            } else {
                void * t35 = gm_l6;
                uint64_t t36 = gm_l9;
                gm_t88rcucg1b7uk t37 = gm_l3;
                gm_t88rcucg1b7uk t38 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t35, t36, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t33 = t38;
            }
            void * t39 = gm_l10;
            gm_teti411eujvf4 t40;
            memset(&t40, 0, sizeof t40);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)37ULL));
            GM_ST(void *, ((uint8_t *)&t40 + 8), t39);
            gm_tl874t2atnlh3 t41 = gm_l5;
            void * t42 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t33, &t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t43 = 0;
            F.ret = t42;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        {
            gm_b16_8 t45 = gm_l4;
            gm_l11 = t45;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t47 = gm_l12;
                uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t49 = (t47 < t48);
                if (!t49) goto lb46;
                {
                    uint64_t t52 = gm_l12;
                    uint64_t t53 = (uintptr_t)t52;
                    uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_t7te2tdpebbt4 t57 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t53 * 144));
                    F.l13 = t57;
                    {
                        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        if (t60 != ((uint8_t)1ULL)) goto ie59;
                        gm_t08olq97rscn3 t61 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l13) + 0) + 8) + 0));
                        gm_l14 = t61;
                        uint8_t t62 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l14) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l14) + 0)), gm_spevct0q3cjgb, 4) == 0));
                        if (!t62) goto ie59;
                        {
                            void * t64 = gm_l0;
                            gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l13) + 128));
                            gm_b16_8 t66;
                            GM_ST(void *, (uint8_t *)&t66, (void *)(gm_soiet16j0e8ar));
                            GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(33));
                            gm_b0_1 t67 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t64, t65, t66);
                            if (gm_unw.kind) {
                                goto gm_ll50;
                            }
                        }
                        {
                            void * t68 = gm_l0;
                            void * t69 = GM_LD(void *, (((uint8_t *)t68) + 0));
                            gm_tl874t2atnlh3 t70 = gm_l5;
                            void * t71 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t69, t70);
                            if (gm_unw.kind) {
                                goto gm_ll50;
                            }
                            uint8_t t72 = 0;
                            F.ret = t71;
                            goto gm_fret;
                        }
                        goto ix59;
ie59:;
ix59:;
                    }
                    goto me55;
mn56:;
                    gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 175, 5);
me55:;
                }
lc46:;
                {
                    uint64_t t74 = gm_l12;
                    uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                    uint64_t t77 = r75;
                    gm_l12 = t77;
                }
            }
gm_ll50:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb46; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc46; }
            }
            goto gm_lad;
lb46:;
        }
    }
    {
        uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t80 = ((t79 == ((uint64_t)1ULL)));
        if (!t80) goto ie78;
        uint64_t t81 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t81 >= (uintptr_t)(t82)) gm_trap_index(t81, t82, gm_src5cfou479ljhn, 182, 44);
        uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t81 * 144) + 0) + 0));
        if (t83 != ((uint8_t)0ULL)) goto ie78;
        gm_tc3oht4ksth9d t84 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t81 * 144) + 0) + 8));
        F.l15 = t84;
        uint8_t t85 = (!(GM_LD(uint8_t, (((uint8_t *)&F.l15) + 8) + 0) == ((uint8_t)5ULL)));
        if (!t85) goto ie78;
        {
            if (!((((uint8_t *)&gm_l2))[8] != 0)) goto ie87;
            uint64_t t88 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
            gm_l16 = t88;
            {
                void * t90 = gm_l0;
                void * t91 = (void *)((uint8_t *)&F.l15);
                gm_t88rcucg1b7uk t92 = gm_l3;
                gm_tkn42u7na3t81 t93;
                GM_ST(uint8_t, (uint8_t *)&t93 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t93 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t93), t92);
                ((uint8_t *)&t93)[4] = 1;
                void * t94 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t90, t91, t93);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t94;
            }
            {
                void * t95 = gm_l0;
                void * t96 = (void *)(((uint8_t *)t95) + 96);
                void * t97 = gm_l0;
                void * t98 = GM_LD(void *, (((uint8_t *)t97) + 0));
                void * t99 = gm_l17;
                gm_t88rcucg1b7uk t100 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t99) + 0));
                gm_t88rcucg1b7uk t101 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t96, t98, t100);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t101;
            }
            {
                gm_tkn42u7na3t81 t102 = gm_l1;
                gm_t88rcucg1b7uk t103;
                if (((((uint8_t *)&t102))[4] != 0)) {
                    gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t102) + 0));
                    t103 = t104;
                } else {
                    void * t105 = gm_l6;
                    uint64_t t106 = gm_l16;
                    gm_t88rcucg1b7uk t107 = gm_l3;
                    gm_t88rcucg1b7uk t108 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t105, t106, t107);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    t103 = t108;
                }
                gm_l19 = t103;
            }
            {
                void * t110 = gm_l6;
                gm_t88rcucg1b7uk t111 = gm_l18;
                gm_t53at80pfdus3 t112;
                gm_f_kind_jcnt4hm((void *)&t112, t110, GM_CONV(gm_csguankko4lmhd, t111));
                uint8_t t113;
                uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&t112) + 0));
                if (t116 != ((uint8_t)11ULL)) goto mn115;
                t113 = ((uint8_t)1);
                goto me114;
mn115:;
                t113 = ((uint8_t)0);
                goto me114;
mn117:;
                gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 187, 16);
me114:;
                if (!t113) goto ie109;
                gm_t88rcucg1b7uk t118 = gm_l18;
                gm_t88rcucg1b7uk t119 = gm_l3;
                uint8_t t120 = (!gm_eq9m6mvtmgde(((uint8_t *)&t118), ((uint8_t *)&t119)));
                if (!t120) goto ie109;
                {
                    uint8_t t123 = 0;
                    void * t124 = gm_l6;
                    gm_t88rcucg1b7uk t125 = gm_l18;
                    uint8_t t126 = gm_f_is_vector_gkq6b88(t124, GM_CONV(gm_csguankko4lmhd, t125));
                    if (!t126) {
                        void * t127 = gm_l6;
                        gm_t88rcucg1b7uk t128 = gm_l19;
                        uint8_t t129 = gm_f_is_vector_gkq6b88(t127, GM_CONV(gm_csguankko4lmhd, t128));
                        t123 = t129;
                    } else t123 = 1;
                    if (!t123) goto ie122;
                    {
                        void * t132 = gm_l0;
                        gm_t88rcucg1b7uk t133 = gm_l19;
                        void * t134 = gm_l17;
                        gm_tl874t2atnlh3 t135 = gm_l5;
                        void * t136 = gm_f_vector_conversion_32a9cuf((void *)NULL, cx, NULL, t132, t133, t134, t135);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (!(GM_LD(void *, ((uint8_t *)&t136)) != 0)) goto ie131;
                        void * t137 = GM_LD(void *, ((uint8_t *)&t136));
                        gm_l20 = t137;
                        {
                            void * t139 = gm_l20;
                            uint8_t t140 = 0;
                            F.ret = t139;
                            goto gm_fret;
                        }
                        goto ix131;
ie131:;
ix131:;
                    }
                    goto ix122;
ie122:;
ix122:;
                }
                {
                    uint8_t t142 = 0;
                    void * t143 = gm_l6;
                    gm_t88rcucg1b7uk t144 = gm_l18;
                    uint8_t t145 = gm_f_is_math_vector_09mtuao(t143, GM_CONV(gm_csguankko4lmhd, t144));
                    if (!t145) {
                        void * t146 = gm_l6;
                        gm_t88rcucg1b7uk t147 = gm_l19;
                        uint8_t t148 = gm_f_is_math_vector_09mtuao(t146, GM_CONV(gm_csguankko4lmhd, t147));
                        t142 = t148;
                    } else t142 = 1;
                    if (!t142) goto ie141;
                    {
                        void * t151 = gm_l0;
                        gm_t88rcucg1b7uk t152 = gm_l19;
                        void * t153 = gm_l17;
                        gm_tl874t2atnlh3 t154 = gm_l5;
                        void * t155 = gm_f_math_vector_conversion_0cttjm0((void *)NULL, cx, NULL, t151, t152, t153, t154);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (!(GM_LD(void *, ((uint8_t *)&t155)) != 0)) goto ie150;
                        void * t156 = GM_LD(void *, ((uint8_t *)&t155));
                        gm_l21 = t156;
                        {
                            void * t158 = gm_l21;
                            uint8_t t159 = 0;
                            F.ret = t158;
                            goto gm_fret;
                        }
                        goto ix150;
ie150:;
ix150:;
                    }
                    goto ix141;
ie141:;
ix141:;
                }
                goto ix109;
ie109:;
ix109:;
            }
            {
                void * t160 = gm_l0;
                void * t161 = gm_l17;
                gm_t88rcucg1b7uk t162 = gm_l3;
                void * t163 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t160, t161, t162);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t163;
            }
            {
                uint64_t t165 = gm_l16;
                uint8_t t166 = (!(t165 == ((uint64_t)1ULL)));
                if (!t166) goto ie164;
                {
                    void * t168 = gm_l0;
                    gm_tl874t2atnlh3 t169 = gm_l5;
                    gm_t3877pk7lv748 t170;
                    void * t171 = gm_l0;
                    gm_t88rcucg1b7uk t172 = gm_l19;
                    gm_b16_8 t173 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t171, t172);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t170 + 0, t173);
                    uint64_t t174 = gm_l16;
                    GM_ST(uint64_t, (uint8_t *)&t170 + 16, t174);
                    gm_tabb710d9mq2e t175;
                    gm_f_format_h75o0bg((void *)&t175, (void *)NULL, cx, NULL, &t170);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t175))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t175) + 8, gm_src5cfou479ljhn, 197, 32);
                    gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&t175) + 8));
                    gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&t176));
                    gm_b0_1 t178 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t168, t169, t177);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t179 = gm_l0;
                    void * t180 = GM_LD(void *, (((uint8_t *)t179) + 0));
                    gm_tl874t2atnlh3 t181 = gm_l5;
                    void * t182 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t180, t181);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t183 = 0;
                    F.ret = t182;
                    goto gm_fret;
                }
                goto ix164;
ie164:;
ix164:;
            }
            {
                gm_f_list_vp1djv1_2((void *)&F.l22, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t184 = (void *)((uint8_t *)&F.l22);
                void * t185 = gm_l17;
                gm_tftb1odc739t5 t186 = gm_f_push_d943vgp_2(t184, &(*(gm_t69m56meqefs8 *)((uint8_t *)t185)));
                if (((((uint8_t *)&t186))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t186) + 1, gm_src5cfou479ljhn, 201, 13);
                gm_b0_1 t187 = GM_LD(gm_b0_1, (((uint8_t *)&t186) + 1));
            }
            {
                gm_t88rcucg1b7uk t188 = gm_l19;
                gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                gm_teti411eujvf4 t190;
                memset(&t190, 0, sizeof t190);
                GM_ST(uint8_t, ((uint8_t *)&t190 + 0), ((uint8_t)36ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t190 + 8), t189);
                gm_tl874t2atnlh3 t191 = gm_l5;
                void * t192 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t188, &t190, t191);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t193 = 0;
                F.ret = t192;
                goto gm_fret;
            }
            goto ix87;
ie87:;
ix87:;
        }
        goto ix78;
ie78:;
ix78:;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l23, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l24 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t195 = gm_l4;
            gm_l25 = t195;
        }
        {
            gm_l26 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t197 = gm_l26;
                uint64_t t198 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                uint8_t t199 = (t197 < t198);
                if (!t199) goto lb196;
                {
                    uint64_t t202 = gm_l26;
                    uint64_t t203 = (uintptr_t)t202;
                    uint64_t t204 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                    gm_t7te2tdpebbt4 t207 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t203 * 144));
                    F.l27 = t207;
                    {
                        uint8_t t210;
                        uint8_t t213 = GM_LD(uint8_t, ((((uint8_t *)&F.l27) + 0) + 0));
                        if (t213 != ((uint8_t)2ULL)) goto mn212;
                        t210 = ((uint8_t)1);
                        goto me211;
mn212:;
                        t210 = ((uint8_t)0);
                        goto me211;
mn214:;
                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 210, 12);
me211:;
                        if (!t210) goto ie209;
                        {
                            gm_l24 = ((uint8_t)1);
                        }
                        goto ix209;
ie209:;
ix209:;
                    }
                    goto me205;
mn206:;
                    gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 209, 5);
me205:;
                }
lc196:;
                {
                    uint64_t t217 = gm_l26;
                    uint64_t r218; bool o219 = __builtin_add_overflow(t217, ((uint64_t)1ULL), &r218);
                    uint64_t t220 = r218;
                    gm_l26 = t220;
                }
            }
lb196:;
        }
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l28, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t222 = gm_l4;
            gm_l29 = t222;
        }
        {
            gm_l30 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t224 = gm_l30;
                uint64_t t225 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                uint8_t t226 = (t224 < t225);
                if (!t226) goto lb223;
                {
                    uint64_t t229 = gm_l30;
                    uint64_t t230 = (uintptr_t)t229;
                    uint64_t t231 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                    gm_t7te2tdpebbt4 t234 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t230 * 144));
                    F.l31 = t234;
                    {
                        uint8_t t241 = GM_LD(uint8_t, ((((uint8_t *)&F.l31) + 0) + 0));
                        switch (t241) { case ((uint8_t)0ULL): goto ma237; case ((uint8_t)1ULL): goto ma239; case ((uint8_t)2ULL): goto ma238; default: goto ma239; }
ma237:;
                        uint8_t t243 = GM_LD(uint8_t, ((((uint8_t *)&F.l31) + 0) + 0));
                        if (t243 != ((uint8_t)0ULL)) goto mn242;
                        gm_tc3oht4ksth9d t244 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l31) + 0) + 8));
                        F.l32 = t244;
                        {
                            void * t246 = gm_l0;
                            void * t247 = (void *)((uint8_t *)&F.l32);
                            gm_t88rcucg1b7uk t248 = gm_l3;
                            void * t249 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t246, t247, t248);
                            if (gm_unw.kind) {
                                goto gm_ll227;
                            }
                            gm_l33 = t249;
                        }
                        {
                            void * t250 = (void *)((uint8_t *)&F.l23);
                            void * t251;
                            uint8_t t253 = gm_l24;
                            if (!t253) goto ie252;
                            void * t255 = gm_l0;
                            void * t256 = gm_l33;
                            void * t257 = (void *)((uint8_t *)&F.l28);
                            void * t258 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t255, t256, t257);
                            if (gm_unw.kind) {
                                goto gm_ll227;
                            }
                            void * t259 = t258;
                            t251 = t259;
                            goto ix252;
ie252:;
                            void * t261 = gm_l33;
                            void * t262 = t261;
                            t251 = t262;
ix252:;
                            gm_tftb1odc739t5 t263 = gm_f_push_d943vgp_2(t250, &(*(gm_t69m56meqefs8 *)((uint8_t *)t251)));
                            if (((((uint8_t *)&t263))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t263) + 1, gm_src5cfou479ljhn, 217, 17);
                            gm_b0_1 t264 = GM_LD(gm_b0_1, (((uint8_t *)&t263) + 1));
                        }
                        goto me236;
mn242:;
ma238:;
                        uint8_t t266 = GM_LD(uint8_t, ((((uint8_t *)&F.l31) + 0) + 0));
                        if (t266 != ((uint8_t)2ULL)) goto mn265;
                        gm_tc3oht4ksth9d t267 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l31) + 0) + 8));
                        F.l34 = t267;
                        {
                            void * t269 = gm_l0;
                            void * t270 = (void *)((uint8_t *)&F.l34);
                            void * t271 = (void *)((uint8_t *)&F.l28);
                            gm_th2lqbo77pvsc t272;
                            gm_f_spread_parts_e62q2l2((void *)&t272, (void *)NULL, cx, NULL, t269, t270, t271);
                            if (gm_unw.kind) {
                                goto gm_ll227;
                            }
                            gm_b16_8 t273;
                            if (((((uint8_t *)&t272))[16] != 0)) {
                                gm_b16_8 t274 = GM_LD(gm_b16_8, (((uint8_t *)&t272) + 0));
                                t273 = t274;
                            } else {
                                void * t275 = gm_l0;
                                void * t276 = GM_LD(void *, (((uint8_t *)t275) + 0));
                                gm_tl874t2atnlh3 t277 = gm_l5;
                                void * t278 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t276, t277);
                                if (gm_unw.kind) {
                                    goto gm_ll227;
                                }
                                uint8_t t279 = 0;
                                F.ret = t278;
                                goto gm_fret;
                            }
                            gm_l35 = t273;
                        }
                        {
                            {
                                gm_b16_8 t281 = gm_l35;
                                gm_l36 = t281;
                            }
                            {
                                gm_l37 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t283 = gm_l37;
                                    uint64_t t284 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                                    uint8_t t285 = (t283 < t284);
                                    if (!t285) goto lb282;
                                    {
                                        uint64_t t288 = gm_l37;
                                        uint64_t t289 = (uintptr_t)t288;
                                        uint64_t t290 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                                        gm_tpouv9r7lrri2 t293 = GM_LD(gm_tpouv9r7lrri2, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t289 * 32));
                                        gm_l38 = t293;
                                        {
                                            uint8_t t296;
                                            if ((((((uint8_t *)&gm_l38) + 0))[16] != 0)) goto mn298;
                                            t296 = ((uint8_t)0);
                                            goto me297;
mn298:;
                                            t296 = ((uint8_t)1);
                                            goto me297;
mn299:;
                                            gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 222, 24);
me297:;
                                            if (!t296) goto ie295;
                                            {
                                                void * t301 = gm_l0;
                                                gm_tl874t2atnlh3 t302 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l34) + 72));
                                                gm_b16_8 t303;
                                                GM_ST(void *, (uint8_t *)&t303, (void *)(gm_sqof8dgnasnqk));
                                                GM_ST(uintptr_t, (uint8_t *)&t303 + 8, (uintptr_t)(68));
                                                gm_b0_1 t304 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t301, t302, t303);
                                                if (gm_unw.kind) {
                                                    goto gm_ll286;
                                                }
                                            }
                                            {
                                                void * t305 = gm_l0;
                                                void * t306 = GM_LD(void *, (((uint8_t *)t305) + 0));
                                                gm_tl874t2atnlh3 t307 = gm_l5;
                                                void * t308 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t306, t307);
                                                if (gm_unw.kind) {
                                                    goto gm_ll286;
                                                }
                                                uint8_t t309 = 0;
                                                F.ret = t308;
                                                goto gm_fret;
                                            }
                                            goto ix295;
ie295:;
ix295:;
                                        }
                                        {
                                            void * t310 = (void *)((uint8_t *)&F.l23);
                                            void * t311 = gm_l0;
                                            void * t312 = GM_LD(void *, (((uint8_t *)&gm_l38) + 24));
                                            gm_t88rcucg1b7uk t313 = gm_l3;
                                            void * t314 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t311, t312, t313);
                                            if (gm_unw.kind) {
                                                goto gm_ll286;
                                            }
                                            gm_tftb1odc739t5 t315 = gm_f_push_d943vgp_2(t310, &(*(gm_t69m56meqefs8 *)((uint8_t *)t314)));
                                            if (((((uint8_t *)&t315))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t315) + 1, gm_src5cfou479ljhn, 226, 21);
                                            gm_b0_1 t316 = GM_LD(gm_b0_1, (((uint8_t *)&t315) + 1));
                                        }
                                        goto me291;
mn292:;
                                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 221, 17);
me291:;
                                    }
lc282:;
                                    {
                                        uint64_t t318 = gm_l37;
                                        uint64_t r319; bool o320 = __builtin_add_overflow(t318, ((uint64_t)1ULL), &r319);
                                        uint64_t t321 = r319;
                                        gm_l37 = t321;
                                    }
                                }
gm_ll286:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb282; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc282; }
                                }
                                goto gm_ll227;
lb282:;
                            }
                        }
                        goto me236;
mn265:;
ma239:;
                        void * t323 = gm_l0;
                        gm_tl874t2atnlh3 t324 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l31) + 128));
                        gm_b16_8 t325;
                        GM_ST(void *, (uint8_t *)&t325, (void *)(gm_sqirjnd2segia));
                        GM_ST(uintptr_t, (uint8_t *)&t325 + 8, (uintptr_t)(39));
                        gm_b0_1 t326 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t323, t324, t325);
                        if (gm_unw.kind) {
                            goto gm_ll227;
                        }
                        goto me236;
mn322:;
ma240:;
                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 214, 9);
me236:;
                    }
                    goto me232;
mn233:;
                    gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 213, 5);
me232:;
                }
lc223:;
                {
                    uint64_t t328 = gm_l30;
                    uint64_t r329; bool o330 = __builtin_add_overflow(t328, ((uint64_t)1ULL), &r329);
                    uint64_t t331 = r329;
                    gm_l30 = t331;
                }
            }
gm_ll227:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb223; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc223; }
            }
            goto gm_lad;
lb223:;
        }
    }
    {
        if (!((((uint8_t *)&gm_l2))[8] != 0)) goto ie332;
        uint64_t t333 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        gm_l39 = t333;
        uint64_t t334 = gm_l39;
        uint64_t t335 = GM_LD(uintptr_t, (((uint8_t *)&F.l23) + 0) + 8);
        uint64_t t336 = (uint64_t)t335;
        uint8_t t337 = (!(t334 == t336));
        if (!t337) goto ie332;
        {
            gm_tkn42u7na3t81 t339 = gm_l1;
            gm_t88rcucg1b7uk t340;
            if (((((uint8_t *)&t339))[4] != 0)) {
                gm_t88rcucg1b7uk t341 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t339) + 0));
                t340 = t341;
            } else {
                void * t342 = gm_l6;
                uint64_t t343 = gm_l39;
                gm_t88rcucg1b7uk t344 = gm_l3;
                gm_t88rcucg1b7uk t345 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t342, t343, t344);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t340 = t345;
            }
            gm_l40 = t340;
        }
        {
            void * t346 = gm_l0;
            gm_tl874t2atnlh3 t347 = gm_l5;
            gm_tq3m5qgvkpje8 t348;
            void * t349 = gm_l0;
            gm_t88rcucg1b7uk t350 = gm_l40;
            gm_b16_8 t351 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t349, t350);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t348 + 0, t351);
            uint64_t t352 = gm_l39;
            GM_ST(uint64_t, (uint8_t *)&t348 + 16, t352);
            uint64_t t353 = GM_LD(uintptr_t, (((uint8_t *)&F.l23) + 0) + 8);
            gm_b16_8 t354 = gm_f_given_27f1vc1((void *)NULL, cx, NULL, t353);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t348 + 24, t354);
            gm_tabb710d9mq2e t355;
            gm_f_format_blrf705((void *)&t355, (void *)NULL, cx, NULL, &t348);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t355))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t355) + 8, gm_src5cfou479ljhn, 234, 24);
            gm_b16_8 t356 = GM_LD(gm_b16_8, (((uint8_t *)&t355) + 8));
            gm_b16_8 t357 = GM_LD(gm_b16_8, ((uint8_t *)&t356));
            gm_b0_1 t358 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t346, t347, t357);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t359 = gm_l0;
            void * t360 = GM_LD(void *, (((uint8_t *)t359) + 0));
            gm_tl874t2atnlh3 t361 = gm_l5;
            void * t362 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t360, t361);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t363 = 0;
            F.ret = t362;
            goto gm_fret;
        }
        goto ix332;
ie332:;
ix332:;
    }
    {
        gm_tkn42u7na3t81 t364 = gm_l1;
        gm_t88rcucg1b7uk t365;
        if (((((uint8_t *)&t364))[4] != 0)) {
            gm_t88rcucg1b7uk t366 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t364) + 0));
            t365 = t366;
        } else {
            void * t367 = gm_l6;
            uint64_t t368 = GM_LD(uintptr_t, (((uint8_t *)&F.l23) + 0) + 8);
            uint64_t t369 = (uint64_t)t368;
            gm_t88rcucg1b7uk t370 = gm_l3;
            gm_t88rcucg1b7uk t371 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t367, t369, t370);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t365 = t371;
        }
        gm_l41 = t365;
    }
    {
        gm_t88rcucg1b7uk t372 = gm_l41;
        gm_b16_8 t373 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
        gm_teti411eujvf4 t374;
        memset(&t374, 0, sizeof t374);
        GM_ST(uint8_t, ((uint8_t *)&t374 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t374 + 8), t373);
        gm_tl874t2atnlh3 t375 = gm_l5;
        void * t376 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t372, &t374, t375);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l42 = t376;
    }
    {
        uint64_t t378 = GM_LD(uintptr_t, (((uint8_t *)&F.l28) + 0) + 8);
        uint8_t t379 = ((t378 == ((uint64_t)0ULL)));
        if (!t379) goto ie377;
        {
            void * t381 = gm_l42;
            uint8_t t382 = 0;
            F.ret = t381;
            goto gm_fret;
        }
        goto ix377;
ie377:;
ix377:;
    }
    {
        gm_t88rcucg1b7uk t383 = gm_l41;
        gm_b16_8 t384 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
        void * t385 = gm_l42;
        gm_tl874t2atnlh3 t386 = gm_l5;
        void * t387 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t383, t384, t385, t386);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t388 = 0;
        F.ret = t387;
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
__attribute__((aligned(4))) void * gm_f_construct_enum_array_d7cisuu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_construct_enum_array_d7cisuu F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_t88rcucg1b7uk gm_l2; /* index */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_tep2drsl9408b gm_l7; /* uv */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t08olq97rscn3 gm_l11; /* n */
    void * gm_l13; /* v */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint8_t gm_l19; /* bad_args */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_t08olq97rscn3 gm_l23; /* name */
    gm_b16_8 gm_l25; /* fill */
    uint64_t gm_l26; /* i */
    void * gm_l27; /* v */
    gm_b16_8 gm_l28; /* first */
    uint8_t gm_l29; /* out_of_order */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* k */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* w */
    void * gm_l36; /* v */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* i */
    gm_b16_8 gm_l41; /* n */
    gm_b16_8 gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    void * gm_l45; /* s */
    void * gm_l46; /* arr */
    GM_STACK_CHECK(&F, 3479u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        gm_f_list_epvducg((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        gm_t88rcucg1b7uk t5 = gm_l2;
        gm_tdlpv0q69lov1 t6;
        gm_f_union_variants_a7khaqp((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        if (!((((uint8_t *)&t6))[24] != 0)) goto ie2;
        gm_l7 = (*(gm_tep2drsl9408b *)(((uint8_t *)&t6) + 0));
        {
            {
                gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                gm_l8 = t9;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t11 = gm_l9;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t13 = (t11 < t12);
                    if (!t13) goto lb10;
                    {
                        uint64_t t16 = gm_l9;
                        uint64_t t17 = (uintptr_t)t16;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        gm_tckdobunaulee t21 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t17 * 72));
                        F.l10 = t21;
                        {
                            void * t23 = (void *)((uint8_t *)&F.l6);
                            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                            gm_tftb1odc739t5 t25 = gm_f_push_3ef746b(t23, t24);
                            if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_src5cfou479ljhn, 67, 32);
                            gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
                        }
                        goto me19;
mn20:;
                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 67, 9);
me19:;
                    }
lc10:;
                    {
                        uint64_t t28 = gm_l9;
                        uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                        uint64_t t31 = r29;
                        gm_l9 = t31;
                    }
                }
lb10:;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t34 = ((t33 == ((uint64_t)1ULL)));
        if (!t34) goto ie32;
        uint64_t t35 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_src5cfou479ljhn, 69, 42);
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t35 * 144) + 0) + 0));
        if (t37 != ((uint8_t)1ULL)) goto ie32;
        gm_t08olq97rscn3 t38 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t35 * 144) + 0) + 8) + 0));
        gm_l11 = t38;
        gm_tc3oht4ksth9d t39 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t35 * 144) + 0) + 8) + 32));
        F.l12 = t39;
        uint8_t t40 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l11) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l11) + 0)), gm_spevct0q3cjgb, 4) == 0));
        if (!t40) goto ie32;
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 0));
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        uint8_t t43 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t42) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t42))) + i * 16, ((uint8_t *)&t41))) { t43 = 1; break; }
        uint8_t t44 = (uint8_t)(!t43);
        if (!t44) goto ie32;
        {
            void * t46 = gm_l0;
            void * t47 = (void *)((uint8_t *)&F.l12);
            gm_t88rcucg1b7uk t48 = gm_l3;
            void * t49 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t46, t47, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t49;
        }
        {
            gm_t88rcucg1b7uk t50 = gm_l1;
            void * t51 = gm_l13;
            gm_teti411eujvf4 t52;
            memset(&t52, 0, sizeof t52);
            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)37ULL));
            GM_ST(void *, ((uint8_t *)&t52 + 8), t51);
            gm_tl874t2atnlh3 t53 = gm_l5;
            void * t54 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t50, &t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t55 = 0;
            F.ret = t54;
            goto gm_fret;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_pmp06kq((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_l16 = t57;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t59 = gm_l17;
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t61 = (t59 < t60);
                if (!t61) goto lb58;
                {
                    uint64_t t64 = gm_l17;
                    uint64_t t65 = (uintptr_t)t64;
                    uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    {
                        void * t70 = (void *)((uint8_t *)&F.l15);
                        gm_tftb1odc739t5 t71 = gm_f_push_r2k6rjf(t70, ((void *)0));
                        if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_src5cfou479ljhn, 75, 28);
                        gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                    }
                    goto me67;
mn68:;
                    gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 75, 5);
me67:;
                }
lc58:;
                {
                    uint64_t t74 = gm_l17;
                    uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                    uint64_t t77 = r75;
                    gm_l17 = t77;
                }
            }
lb58:;
        }
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l19 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t79 = gm_l4;
            gm_l20 = t79;
        }
        {
            gm_l21 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t81 = gm_l21;
                uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                uint8_t t83 = (t81 < t82);
                if (!t83) goto lb80;
                {
                    uint64_t t86 = gm_l21;
                    uint64_t t87 = (uintptr_t)t86;
                    uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    gm_t7te2tdpebbt4 t91 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t87 * 144));
                    F.l22 = t91;
                    {
                        uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 0) + 0));
                        if (t95 != ((uint8_t)1ULL)) goto mn94;
                        gm_t08olq97rscn3 t96 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l22) + 0) + 8) + 0));
                        gm_l23 = t96;
                        gm_tc3oht4ksth9d t97 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)&F.l22) + 0) + 8) + 32));
                        F.l24 = t97;
                        {
                            gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                            gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
                            gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 0));
                            gm_tf5srh4mp4b3i t102 = gm_f_index_of_o9r57ha(t100, t101);
                            uint64_t t103;
                            if (((((uint8_t *)&t102))[8] != 0)) {
                                uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&t102) + 0));
                                t103 = t104;
                            } else {
                                {
                                    gm_b16_8 t106;
                                    uint8_t t108 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l23) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l23) + 0)), gm_spevct0q3cjgb, 4) == 0));
                                    if (!t108) goto ie107;
                                    gm_b16_8 t110;
                                    GM_ST(void *, (uint8_t *)&t110, (void *)(gm_sksrclikobn5n));
                                    GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(36));
                                    gm_b16_8 t111 = t110;
                                    t106 = t111;
                                    goto ix107;
ie107:;
                                    gm_b16_8 t113;
                                    GM_ST(void *, (uint8_t *)&t113, (void *)(gm_sr4qbrk6tlgaa));
                                    GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(0));
                                    gm_b16_8 t114 = t113;
                                    t106 = t114;
ix107:;
                                    gm_l25 = t106;
                                }
                                {
                                    void * t115 = gm_l0;
                                    gm_tl874t2atnlh3 t116 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l23) + 16));
                                    gm_tfeab279c16rr t117;
                                    void * t118 = gm_l0;
                                    gm_t88rcucg1b7uk t119 = gm_l2;
                                    gm_b16_8 t120 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t118, t119);
                                    if (gm_unw.kind) {
                                        goto gm_ll84;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t117 + 0, t120);
                                    gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 0));
                                    GM_ST(gm_b16_8, (uint8_t *)&t117 + 16, t121);
                                    gm_b16_8 t122 = gm_l25;
                                    GM_ST(gm_b16_8, (uint8_t *)&t117 + 32, t122);
                                    gm_tabb710d9mq2e t123;
                                    gm_f_format_i7ngra8((void *)&t123, (void *)NULL, cx, NULL, &t117);
                                    if (gm_unw.kind) {
                                        goto gm_ll84;
                                    }
                                    if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 8, gm_src5cfou479ljhn, 83, 41);
                                    gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&t123) + 8));
                                    gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
                                    gm_b0_1 t126 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t115, t116, t125);
                                    if (gm_unw.kind) {
                                        goto gm_ll84;
                                    }
                                }
                                {
                                    {
                                        void * t128 = gm_l0;
                                        void * t129 = (void *)((uint8_t *)&F.l24);
                                        gm_tkn42u7na3t81 t130;
                                        memset(&t130, 0, sizeof t130);
                                        void * t131 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t128, t129, t130);
                                        if (gm_unw.kind) {
                                            goto gm_ll84;
                                        }
                                    }
                                }
                                {
                                    gm_l19 = ((uint8_t)1);
                                }
                                goto lc80;
                            }
                            gm_l26 = t103;
                        }
                        {
                            uint64_t t133 = gm_l26;
                            uint64_t t134 = (uintptr_t)t133;
                            uint64_t t135 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                            if (t134 >= (uintptr_t)(t135)) gm_trap_index(t134, t135, gm_src5cfou479ljhn, 88, 20);
                            void * t136 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t134 * 8));
                            uint8_t t137;
                            if ((GM_LD(void *, ((uint8_t *)&t136)) != 0)) goto mn139;
                            t137 = ((uint8_t)0);
                            goto me138;
mn139:;
                            t137 = ((uint8_t)1);
                            goto me138;
mn140:;
                            gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 88, 20);
me138:;
                            if (!t137) goto ie132;
                            {
                                void * t142 = gm_l0;
                                gm_tl874t2atnlh3 t143 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l23) + 16));
                                gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 0));
                                gm_tnpifv20irnph t145;
                                GM_ST(gm_b16_8, ((uint8_t *)&t145 + 0), t144);
                                gm_tabb710d9mq2e t146;
                                gm_f_format_47t5q9n((void *)&t146, (void *)NULL, cx, NULL, t145);
                                if (gm_unw.kind) {
                                    goto gm_ll84;
                                }
                                if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 8, gm_src5cfou479ljhn, 88, 69);
                                gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&t146) + 8));
                                gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&t147));
                                gm_b0_1 t149 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t142, t143, t148);
                                if (gm_unw.kind) {
                                    goto gm_ll84;
                                }
                            }
                            goto ix132;
ie132:;
ix132:;
                        }
                        {
                            void * t150 = gm_l0;
                            void * t151 = (void *)((uint8_t *)&F.l24);
                            gm_t88rcucg1b7uk t152 = gm_l3;
                            void * t153 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t150, t151, t152);
                            if (gm_unw.kind) {
                                goto gm_ll84;
                            }
                            gm_l27 = t153;
                        }
                        {
                            void * t154 = (void *)((uint8_t *)&F.l18);
                            uint64_t t155 = gm_l26;
                            gm_tftb1odc739t5 t156 = gm_f_push_ucgmlo2(t154, t155);
                            if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 1, gm_src5cfou479ljhn, 90, 17);
                            gm_b0_1 t157 = GM_LD(gm_b0_1, (((uint8_t *)&t156) + 1));
                        }
                        {
                            uint64_t t158 = gm_l26;
                            uint64_t t159 = (uintptr_t)t158;
                            uint64_t t160 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                            if (t159 >= (uintptr_t)(t160)) gm_trap_index(t159, t160, gm_src5cfou479ljhn, 91, 17);
                            void * t161 = gm_l27;
                            GM_ST(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t159 * 8), t161);
                        }
                        goto me93;
mn94:;
                        {
                            uint8_t t165 = gm_l19;
                            uint8_t t166 = (uint8_t)!t165;
                            if (!t166) goto ie164;
                            {
                                gm_b16_8 t168;
                                uint64_t t170 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                                uint8_t t171 = (t170 > ((uint64_t)0ULL));
                                if (!t171) goto ie169;
                                uint64_t t173 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t174 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                                if (t173 >= (uintptr_t)(t174)) gm_trap_index(t173, t174, gm_src5cfou479ljhn, 95, 55);
                                gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t173 * 16));
                                gm_b16_8 t176 = t175;
                                t168 = t176;
                                goto ix169;
ie169:;
                                gm_b16_8 t178;
                                GM_ST(void *, (uint8_t *)&t178, (void *)(gm_suv2tallru6a1));
                                GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(1));
                                gm_b16_8 t179 = t178;
                                t168 = t179;
ix169:;
                                gm_l28 = t168;
                            }
                            {
                                void * t180 = gm_l0;
                                gm_tl874t2atnlh3 t181 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l22) + 128));
                                gm_t0uuhaljsf410 t182;
                                void * t183 = gm_l0;
                                gm_t88rcucg1b7uk t184 = gm_l1;
                                gm_b16_8 t185 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t183, t184);
                                if (gm_unw.kind) {
                                    goto gm_ll84;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t182 + 0, t185);
                                void * t186 = gm_l0;
                                gm_t88rcucg1b7uk t187 = gm_l2;
                                gm_b16_8 t188 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t186, t187);
                                if (gm_unw.kind) {
                                    goto gm_ll84;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t182 + 16, t188);
                                void * t189 = gm_l0;
                                gm_t88rcucg1b7uk t190 = gm_l1;
                                gm_b16_8 t191 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t189, t190);
                                if (gm_unw.kind) {
                                    goto gm_ll84;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t182 + 32, t191);
                                gm_b16_8 t192 = gm_l28;
                                GM_ST(gm_b16_8, (uint8_t *)&t182 + 48, t192);
                                gm_tabb710d9mq2e t193;
                                gm_f_format_d0bas9o((void *)&t193, (void *)NULL, cx, NULL, &t182);
                                if (gm_unw.kind) {
                                    goto gm_ll84;
                                }
                                if (((((uint8_t *)&t193))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t193) + 8, gm_src5cfou479ljhn, 96, 38);
                                gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&t193) + 8));
                                gm_b16_8 t195 = GM_LD(gm_b16_8, ((uint8_t *)&t194));
                                gm_b0_1 t196 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t180, t181, t195);
                                if (gm_unw.kind) {
                                    goto gm_ll84;
                                }
                            }
                            goto ix164;
ie164:;
ix164:;
                        }
                        {
                            gm_l19 = ((uint8_t)1);
                        }
                        goto me93;
mn162:;
                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 79, 9);
me93:;
                    }
                    goto me89;
mn90:;
                    gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 78, 5);
me89:;
                }
lc80:;
                {
                    uint64_t t198 = gm_l21;
                    uint64_t r199; bool o200 = __builtin_add_overflow(t198, ((uint64_t)1ULL), &r199);
                    uint64_t t201 = r199;
                    gm_l21 = t201;
                }
            }
gm_ll84:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb80; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc80; }
            }
            goto gm_lad;
lb80:;
        }
    }
    {
        gm_l29 = ((uint8_t)0);
    }
    {
        {
            gm_l31 = ((uint64_t)1ULL);
        }
        {
            uint64_t t203 = GM_LD(uintptr_t, (((uint8_t *)&F.l18) + 0) + 8);
            uint64_t t204 = (t203 > ((uint64_t)1ULL) ? t203 : ((uint64_t)1ULL));
            gm_l30 = t204;
        }
        {
            for (;;) {
                uint64_t t206 = gm_l31;
                uint64_t t207 = gm_l30;
                uint8_t t208 = (t206 < t207);
                if (!t208) goto lb205;
                {
                    uint64_t t211 = gm_l31;
                    uint64_t t214 = GM_LD(uint64_t, ((uint8_t *)&t211));
                    gm_l32 = t214;
                    {
                        uint64_t t217 = gm_l32;
                        uint64_t r218; bool o219 = __builtin_sub_overflow(t217, ((uint64_t)1ULL), &r218);
                        if (o219) gm_trap("integer overflow in `-`", gm_src5cfou479ljhn, 104, 26);
                        uint64_t t220 = r218;
                        uint64_t t221 = (uintptr_t)t220;
                        uint64_t t222 = GM_LD(uintptr_t, (((uint8_t *)&F.l18) + 0) + 8);
                        if (t221 >= (uintptr_t)(t222)) gm_trap_index(t221, t222, gm_src5cfou479ljhn, 104, 12);
                        uint64_t t223 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l18) + 0)) + t221 * 8));
                        uint64_t t224 = gm_l32;
                        uint64_t t225 = (uintptr_t)t224;
                        uint64_t t226 = GM_LD(uintptr_t, (((uint8_t *)&F.l18) + 0) + 8);
                        if (t225 >= (uintptr_t)(t226)) gm_trap_index(t225, t226, gm_src5cfou479ljhn, 104, 35);
                        uint64_t t227 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l18) + 0)) + t225 * 8));
                        uint8_t t228 = (t223 > t227);
                        if (!t228) goto ie216;
                        {
                            gm_l29 = ((uint8_t)1);
                        }
                        goto ix216;
ie216:;
ix216:;
                    }
                    goto me212;
mn213:;
                    gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 103, 5);
me212:;
                }
lc205:;
                {
                    uint64_t t231 = gm_l31;
                    uint64_t r232; bool o233 = __builtin_add_overflow(t231, ((uint64_t)1ULL), &r232);
                    uint64_t t234 = r232;
                    gm_l31 = t234;
                }
            }
lb205:;
        }
    }
    {
        uint8_t t236 = gm_l29;
        if (!t236) goto ie235;
        {
            {
                gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
                gm_l33 = t239;
            }
            {
                gm_l34 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t241 = gm_l34;
                    uint64_t t242 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                    uint8_t t243 = (t241 < t242);
                    if (!t243) goto lb240;
                    {
                        uint64_t t246 = gm_l34;
                        uint64_t t247 = (uintptr_t)t246;
                        uint64_t t248 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                        uint64_t t249 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t247 * 8));
                        uint64_t t252 = GM_LD(uint64_t, ((uint8_t *)&t249));
                        gm_l35 = t252;
                        {
                            uint64_t t255 = gm_l35;
                            uint64_t t256 = (uintptr_t)t255;
                            uint64_t t257 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                            if (t256 >= (uintptr_t)(t257)) gm_trap_index(t256, t257, gm_src5cfou479ljhn, 108, 21);
                            void * t258 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t256 * 8));
                            if (!(GM_LD(void *, ((uint8_t *)&t258)) != 0)) goto ie254;
                            void * t259 = GM_LD(void *, ((uint8_t *)&t258));
                            gm_l36 = t259;
                            {
                                uint64_t t261 = gm_l35;
                                uint64_t t262 = (uintptr_t)t261;
                                uint64_t t263 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                if (t262 >= (uintptr_t)(t263)) gm_trap_index(t262, t263, gm_src5cfou479ljhn, 108, 38);
                                uint8_t *t264 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t262 * 8);
                                void * t265 = gm_l0;
                                void * t266 = gm_l36;
                                void * t267 = (void *)((uint8_t *)&F.l14);
                                void * t268 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t265, t266, t267);
                                if (gm_unw.kind) {
                                    goto gm_ll244;
                                }
                                GM_ST(void *, t264, t268);
                            }
                            goto ix254;
ie254:;
ix254:;
                        }
                        goto me250;
mn251:;
                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 107, 9);
me250:;
                    }
lc240:;
                    {
                        uint64_t t270 = gm_l34;
                        uint64_t r271; bool o272 = __builtin_add_overflow(t270, ((uint64_t)1ULL), &r271);
                        uint64_t t273 = r271;
                        gm_l34 = t273;
                    }
                }
gm_ll244:;
                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb240; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc240; }
                }
                goto gm_lad;
lb240:;
            }
        }
        goto ix235;
ie235:;
ix235:;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l37, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t275 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t276 = GM_LD(gm_b16_8, ((uint8_t *)&t275));
            gm_l38 = t276;
        }
        {
            gm_l39 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t278 = gm_l39;
                uint64_t t279 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                uint8_t t280 = (t278 < t279);
                if (!t280) goto lb277;
                {
                    gm_tf1bjg81u0glv t283;
                    uint64_t t284 = gm_l39;
                    GM_ST(uint64_t, (uint8_t *)&t283 + 0, t284);
                    uint64_t t285 = gm_l39;
                    uint64_t t286 = (uintptr_t)t285;
                    uint64_t t287 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                    gm_b16_8 t288 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t286 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t283 + 8, t288);
                    uint64_t t291 = GM_LD(uint64_t, (((uint8_t *)&t283) + 0));
                    gm_l40 = t291;
                    gm_b16_8 t292 = GM_LD(gm_b16_8, (((uint8_t *)&t283) + 8));
                    gm_l41 = t292;
                    {
                        uint64_t t295 = gm_l40;
                        uint64_t t296 = (uintptr_t)t295;
                        uint64_t t297 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                        if (t296 >= (uintptr_t)(t297)) gm_trap_index(t296, t297, gm_src5cfou479ljhn, 113, 12);
                        void * t298 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t296 * 8));
                        uint8_t t299;
                        if ((GM_LD(void *, ((uint8_t *)&t298)) != 0)) goto mn301;
                        t299 = ((uint8_t)1);
                        goto me300;
mn301:;
                        t299 = ((uint8_t)0);
                        goto me300;
mn302:;
                        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 113, 12);
me300:;
                        if (!t299) goto ie294;
                        {
                            uint64_t t305 = GM_LD(uintptr_t, (((uint8_t *)&F.l37) + 0) + 8);
                            uint8_t t306 = (t305 > ((uint64_t)0ULL));
                            if (!t306) goto ie304;
                            {
                                void * t308 = (void *)((uint8_t *)&F.l37);
                                gm_b16_8 t309;
                                GM_ST(void *, (uint8_t *)&t309, (void *)(gm_sg5gdnrith2i6));
                                GM_ST(uintptr_t, (uint8_t *)&t309 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t310 = gm_f_extend_0cs85gs(t308, t309);
                                if (((((uint8_t *)&t310))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t310) + 1, gm_src5cfou479ljhn, 114, 40);
                                gm_b0_1 t311 = GM_LD(gm_b0_1, (((uint8_t *)&t310) + 1));
                            }
                            goto ix304;
ie304:;
ix304:;
                        }
                        {
                            void * t312 = (void *)((uint8_t *)&F.l37);
                            gm_b16_8 t313 = gm_l41;
                            gm_tnpifv20irnph t314;
                            GM_ST(gm_b16_8, ((uint8_t *)&t314 + 0), t313);
                            gm_tabb710d9mq2e t315;
                            gm_f_format_h4jcodi((void *)&t315, (void *)NULL, cx, NULL, t314);
                            if (gm_unw.kind) {
                                goto gm_ll281;
                            }
                            if (((((uint8_t *)&t315))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t315) + 8, gm_src5cfou479ljhn, 115, 28);
                            gm_b16_8 t316 = GM_LD(gm_b16_8, (((uint8_t *)&t315) + 8));
                            gm_b16_8 t317 = GM_LD(gm_b16_8, ((uint8_t *)&t316));
                            gm_tftb1odc739t5 t318 = gm_f_extend_0cs85gs(t312, t317);
                            if (((((uint8_t *)&t318))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t318) + 1, gm_src5cfou479ljhn, 115, 13);
                            gm_b0_1 t319 = GM_LD(gm_b0_1, (((uint8_t *)&t318) + 1));
                        }
                        goto ix294;
ie294:;
ix294:;
                    }
                    goto me289;
mn290:;
                    gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 112, 5);
me289:;
                }
lc277:;
                {
                    uint64_t t321 = gm_l39;
                    uint64_t r322; bool o323 = __builtin_add_overflow(t321, ((uint64_t)1ULL), &r322);
                    uint64_t t324 = r322;
                    gm_l39 = t324;
                }
            }
gm_ll281:;
            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb277; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc277; }
            }
            goto gm_lad;
lb277:;
        }
    }
    {
        uint64_t t326 = GM_LD(uintptr_t, (((uint8_t *)&F.l37) + 0) + 8);
        uint8_t t327 = (t326 > ((uint64_t)0ULL));
        if (!t327) goto ie325;
        uint8_t t328 = gm_l19;
        uint8_t t329 = (uint8_t)!t328;
        if (!t329) goto ie325;
        {
            void * t331 = gm_l0;
            gm_tl874t2atnlh3 t332 = gm_l5;
            gm_t6j6smj7qoctu t333;
            void * t334 = gm_l0;
            gm_t88rcucg1b7uk t335 = gm_l1;
            gm_b16_8 t336 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t334, t335);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t333 + 0, t336);
            gm_b16_8 t337 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t333 + 16, t337);
            gm_tabb710d9mq2e t338;
            gm_f_format_v7qttjn((void *)&t338, (void *)NULL, cx, NULL, &t333);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t338))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t338) + 8, gm_src5cfou479ljhn, 119, 24);
            gm_b16_8 t339 = GM_LD(gm_b16_8, (((uint8_t *)&t338) + 8));
            gm_b16_8 t340 = GM_LD(gm_b16_8, ((uint8_t *)&t339));
            gm_b0_1 t341 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t331, t332, t340);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix325;
ie325:;
ix325:;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l42, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t343 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            gm_l43 = t343;
        }
        {
            gm_l44 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t345 = gm_l44;
                uint64_t t346 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                uint8_t t347 = (t345 < t346);
                if (!t347) goto lb344;
                {
                    uint64_t t350 = gm_l44;
                    uint64_t t351 = (uintptr_t)t350;
                    uint64_t t352 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                    void * t353 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t351 * 8));
                    void * t356 = GM_LD(void *, ((uint8_t *)&t353));
                    gm_l45 = t356;
                    {
                        void * t358 = (void *)((uint8_t *)&F.l42);
                        void * t359 = gm_l45;
                        void * t360;
                        if ((GM_LD(void *, ((uint8_t *)&t359)) != 0)) {
                            void * t361 = GM_LD(void *, (((uint8_t *)&t359) + 0));
                            t360 = t361;
                        } else {
                            void * t362 = gm_l0;
                            void * t363 = GM_LD(void *, (((uint8_t *)t362) + 0));
                            gm_tl874t2atnlh3 t364 = gm_l5;
                            void * t365 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t363, t364);
                            if (gm_unw.kind) {
                                goto gm_ll348;
                            }
                            t360 = t365;
                        }
                        gm_tftb1odc739t5 t366 = gm_f_push_d943vgp_2(t358, &(*(gm_t69m56meqefs8 *)((uint8_t *)t360)));
                        if (((((uint8_t *)&t366))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t366) + 1, gm_src5cfou479ljhn, 122, 28);
                        gm_b0_1 t367 = GM_LD(gm_b0_1, (((uint8_t *)&t366) + 1));
                    }
                    goto me354;
mn355:;
                    gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 122, 5);
me354:;
                }
lc344:;
                {
                    uint64_t t369 = gm_l44;
                    uint64_t r370; bool o371 = __builtin_add_overflow(t369, ((uint64_t)1ULL), &r370);
                    uint64_t t372 = r370;
                    gm_l44 = t372;
                }
            }
gm_ll348:;
            if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb344; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc344; }
            }
            goto gm_lad;
lb344:;
        }
    }
    {
        gm_t88rcucg1b7uk t373 = gm_l1;
        gm_b16_8 t374 = GM_LD(gm_b16_8, (((uint8_t *)&F.l42) + 0));
        gm_teti411eujvf4 t375;
        memset(&t375, 0, sizeof t375);
        GM_ST(uint8_t, ((uint8_t *)&t375 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t375 + 8), t374);
        gm_tl874t2atnlh3 t376 = gm_l5;
        void * t377 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t373, &t375, t376);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l46 = t377;
    }
    {
        uint64_t t379 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
        uint8_t t380 = ((t379 == ((uint64_t)0ULL)));
        if (!t380) goto ie378;
        {
            void * t382 = gm_l46;
            uint8_t t383 = 0;
            F.ret = t382;
            goto gm_fret;
        }
        goto ix378;
ie378:;
ix378:;
    }
    {
        gm_t88rcucg1b7uk t384 = gm_l1;
        gm_b16_8 t385 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        void * t386 = gm_l46;
        gm_tl874t2atnlh3 t387 = gm_l5;
        void * t388 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t384, t385, t386, t387);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t389 = 0;
        F.ret = t388;
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
inline __attribute__((aligned(4))) void * gm_f_construct_param_array_btk60nl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_construct_param_array_btk60nl F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t08olq97rscn3 gm_l5; /* n */
    void * gm_l7; /* v */
    GM_STACK_CHECK(&F, 529u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)1ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t5 >= (uintptr_t)(t6)) gm_trap_index(t5, t6, gm_src5cfou479ljhn, 54, 42);
        uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t5 * 144) + 0) + 0));
        if (t7 != ((uint8_t)1ULL)) goto ie2;
        gm_t08olq97rscn3 t8 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t5 * 144) + 0) + 8) + 0));
        gm_l5 = t8;
        gm_tc3oht4ksth9d t9 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t5 * 144) + 0) + 8) + 32));
        F.l6 = t9;
        uint8_t t10 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l5) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l5) + 0)), gm_spevct0q3cjgb, 4) == 0));
        if (!t10) goto ie2;
        {
            void * t12 = gm_l0;
            void * t13 = (void *)((uint8_t *)&F.l6);
            gm_t88rcucg1b7uk t14 = gm_l2;
            void * t15 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t12, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t15;
        }
        {
            gm_t88rcucg1b7uk t16 = gm_l1;
            void * t17 = gm_l7;
            gm_teti411eujvf4 t18;
            memset(&t18, 0, sizeof t18);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)37ULL));
            GM_ST(void *, ((uint8_t *)&t18 + 8), t17);
            gm_tl874t2atnlh3 t19 = gm_l4;
            void * t20 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t16, &t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l0;
        gm_tl874t2atnlh3 t23 = gm_l4;
        void * t24 = gm_l0;
        gm_t88rcucg1b7uk t25 = gm_l1;
        gm_b16_8 t26 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t27;
        GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t26);
        gm_tabb710d9mq2e t28;
        gm_f_format_ti6vs6t((void *)&t28, (void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_src5cfou479ljhn, 58, 20);
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
        gm_b0_1 t31 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t22, t23, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t32 = gm_l0;
        void * t33 = GM_LD(void *, (((uint8_t *)t32) + 0));
        gm_tl874t2atnlh3 t34 = gm_l4;
        void * t35 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t36 = 0;
        F.ret = t35;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_construction_elem_type_qm1ncub(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_construction_elem_type_qm1ncub F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK(&F, 134u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        void * t4 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t5 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t2, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t5;
    }
    {
        gm_tkn42u7na3t81 t6;
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        void * t10 = (void *)(((uint8_t *)t9) + 8);
        gm_t88rcucg1b7uk t11 = gm_l2;
        uint8_t t12 = gm_f_is_error_8k27gd1(t10, GM_CONV(gm_csguankko4lmhd, t11));
        if (!t12) goto ie7;
        gm_tkn42u7na3t81 t14;
        memset(&t14, 0, sizeof t14);
        gm_tkn42u7na3t81 t15 = t14;
        t6 = t15;
        goto ix7;
ie7:;
        gm_t88rcucg1b7uk t17 = gm_l2;
        gm_t88rcucg1b7uk t18 = t17;
        gm_tkn42u7na3t81 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t19), t18);
        ((uint8_t *)&t19)[4] = 1;
        t6 = t19;
ix7:;
        uint8_t t20 = 0;
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
__attribute__((aligned(4))) void * gm_f_enum_array_index_kma2hbd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, void * a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_enum_array_index_kma2hbd F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_t88rcucg1b7uk gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    void * gm_l4; /* index_ast */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* i */
    gm_t88rcucg1b7uk gm_l7; /* it */
    gm_t88rcucg1b7uk gm_l8; /* bt */
    gm_b16_8 gm_l9; /* hint */
    void * gm_l10; /* pos */
    GM_STACK_CHECK(&F, 991u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l4;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_tkn42u7na3t81 t5;
        GM_ST(uint8_t, (uint8_t *)&t5 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t5 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5), t4);
        ((uint8_t *)&t5)[4] = 1;
        void * t6 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t2, t3, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t6;
    }
    {
        void * t7 = gm_l0;
        void * t8 = (void *)(((uint8_t *)t7) + 96);
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        void * t11 = gm_l6;
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t11) + 0));
        gm_t88rcucg1b7uk t13 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t8, t10, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t13;
    }
    {
        void * t15 = gm_l0;
        void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
        void * t17 = (void *)(((uint8_t *)t16) + 8);
        gm_t88rcucg1b7uk t18 = gm_l7;
        uint8_t t19 = gm_f_is_error_8k27gd1(t17, GM_CONV(gm_csguankko4lmhd, t18));
        if (!t19) goto ie14;
        {
            void * t21 = gm_l0;
            void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
            gm_tl874t2atnlh3 t23 = gm_l5;
            void * t24 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        gm_t88rcucg1b7uk t27 = gm_l7;
        gm_t88rcucg1b7uk t28 = gm_l2;
        uint8_t t29 = (!gm_eq9m6mvtmgde(((uint8_t *)&t27), ((uint8_t *)&t28)));
        if (!t29) goto ie26;
        {
            void * t31 = gm_l0;
            void * t32 = (void *)(((uint8_t *)t31) + 96);
            void * t33 = gm_l0;
            void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
            void * t35 = gm_l1;
            gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t35) + 0));
            gm_t88rcucg1b7uk t37 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t32, t34, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t37;
        }
        {
            gm_b16_8 t38;
            uint8_t t40 = 0;
            uint8_t t41 = 0;
            void * t42 = gm_l0;
            void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
            void * t44 = (void *)(((uint8_t *)t43) + 8);
            gm_t88rcucg1b7uk t45 = gm_l7;
            gm_t53at80pfdus3 t46;
            gm_f_kind_jcnt4hm((void *)&t46, t44, GM_CONV(gm_csguankko4lmhd, t45));
            uint8_t t47;
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t46) + 0));
            if (t50 != ((uint8_t)23ULL)) goto mn49;
            t47 = ((uint8_t)1);
            goto me48;
mn49:;
            t47 = ((uint8_t)0);
            goto me48;
mn51:;
            gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 136, 20);
me48:;
            if (!t47) {
                void * t52 = gm_l0;
                void * t53 = GM_LD(void *, (((uint8_t *)t52) + 0));
                void * t54 = (void *)(((uint8_t *)t53) + 8);
                gm_t88rcucg1b7uk t55 = gm_l7;
                uint8_t t56 = gm_f_is_integer_8d4f9ek(t54, GM_CONV(gm_csguankko4lmhd, t55));
                t41 = t56;
            } else t41 = 1;
            if (!t41) {
                void * t57 = gm_l0;
                void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
                gm_t88rcucg1b7uk t59 = gm_l7;
                gm_thetuhtouve7f t60 = gm_f_range_of_dmmo768((void *)NULL, cx, NULL, t58, t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t61;
                if (((((uint8_t *)&t60))[8] != 0)) goto mn63;
                t61 = ((uint8_t)0);
                goto me62;
mn63:;
                t61 = ((uint8_t)1);
                goto me62;
mn64:;
                gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 136, 81);
me62:;
                t40 = t61;
            } else t40 = 1;
            if (!t40) goto ie39;
            gm_b16_8 t66;
            GM_ST(void *, (uint8_t *)&t66, (void *)(gm_s0k8snlvukl86));
            GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(65));
            gm_b16_8 t67 = t66;
            t38 = t67;
            goto ix39;
ie39:;
            gm_b16_8 t69;
            GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(0));
            gm_b16_8 t70 = t69;
            t38 = t70;
ix39:;
            gm_l9 = t38;
        }
        {
            void * t71 = gm_l0;
            void * t72 = gm_l6;
            gm_tl874t2atnlh3 t73 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t72) + 72));
            gm_t0uuhaljsf410 t74;
            void * t75 = gm_l0;
            gm_t88rcucg1b7uk t76 = gm_l8;
            gm_b16_8 t77 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t75, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t74 + 0, t77);
            void * t78 = gm_l0;
            gm_t88rcucg1b7uk t79 = gm_l2;
            gm_b16_8 t80 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t74 + 16, t80);
            void * t81 = gm_l0;
            gm_t88rcucg1b7uk t82 = gm_l7;
            gm_b16_8 t83 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t81, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t74 + 32, t83);
            gm_b16_8 t84 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t74 + 48, t84);
            gm_tabb710d9mq2e t85;
            gm_f_format_frab70n((void *)&t85, (void *)NULL, cx, NULL, &t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 8, gm_src5cfou479ljhn, 137, 26);
            gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&t85) + 8));
            gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
            gm_b0_1 t88 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t71, t73, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t89 = gm_l0;
            void * t90 = GM_LD(void *, (((uint8_t *)t89) + 0));
            gm_tl874t2atnlh3 t91 = gm_l5;
            void * t92 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t90, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t93 = 0;
            F.ret = t92;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t94 = gm_l0;
        void * t95 = gm_l6;
        gm_t88rcucg1b7uk t96 = gm_l2;
        gm_tl874t2atnlh3 t97 = gm_l5;
        void * t98 = gm_f_variant_position_fdpecs3((void *)NULL, cx, NULL, t94, t95, t96, t97);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t98;
    }
    {
        gm_t88rcucg1b7uk t99 = gm_l3;
        gm_tlcl4u558l6p5 t100;
        memset((uint8_t *)&t100 + 17, 0, 7);
        void * t101 = gm_l1;
        GM_ST(void *, (uint8_t *)&t100 + 0, t101);
        void * t102 = gm_l10;
        GM_ST(void *, (uint8_t *)&t100 + 8, t102);
        GM_ST(uint8_t, (uint8_t *)&t100 + 16, ((uint8_t)0));
        gm_teti411eujvf4 t103;
        memset(&t103, 0, sizeof t103);
        GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)20ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t103 + 8), t100);
        gm_tl874t2atnlh3 t104 = gm_l5;
        void * t105 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t99, &t103, t104);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t106 = 0;
        F.ret = t105;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_given_27f1vc1(void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void * gm_f_variant_position_fdpecs3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_variant_position_fdpecs3 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* e */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_tep2drsl9408b gm_l5; /* uv */
    uint8_t gm_l6; /* counts_up */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint8_t gm_l11; /* signed */
    uint16_t gm_l12; /* bits */
    gm_tn3r4rvlh0rsv gm_l13; /* <temporary array> */
    void * gm_l14; /* t */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    GM_STACK_CHECK(&F, 1359u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        gm_t88rcucg1b7uk t7 = gm_l2;
        gm_tdlpv0q69lov1 t8;
        gm_f_union_variants_a7khaqp((void *)&t8, t6, GM_CONV(gm_csguankko4lmhd, t7));
        gm_tep2drsl9408b t9;
        if (((((uint8_t *)&t8))[24] != 0)) {
            t9 = (*(gm_tep2drsl9408b *)(((uint8_t *)&t8) + 0));
        } else {
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
            gm_tl874t2atnlh3 t12 = gm_l3;
            void * t13 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        gm_l5 = t9;
    }
    {
        gm_l6 = ((uint8_t)1);
    }
    {
        {
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
            gm_l7 = t17;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t19 = gm_l8;
                uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t21 = (t19 < t20);
                if (!t21) goto lb18;
                {
                    gm_t3l8idb2m6c1r t24;
                    uint64_t t25 = gm_l8;
                    GM_ST(uint64_t, (uint8_t *)&t24 + 0, t25);
                    uint64_t t26 = gm_l8;
                    uint64_t t27 = (uintptr_t)t26;
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    memcpy((uint8_t *)&t24 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t27 * 72), 72);
                    uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&t24) + 0));
                    gm_l9 = t31;
                    F.l10 = (*(gm_tckdobunaulee *)(((uint8_t *)&t24) + 8));
                    {
                        gm_tbci5ospk5nnn t34 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l10) + 24));
                        uint64_t t35 = gm_l9;
                        uint64_t t36 = (uint64_t)t35;
                        gm_tbci5ospk5nnn t37;
                        gm_f_from_u64_ud13q5v((void *)&t37, (void *)NULL, cx, NULL, t36);
                        if (gm_unw.kind) {
                            goto gm_ll22;
                        }
                        uint8_t t38 = gm_f_eq_60fjn21(&t34, &t37);
                        uint8_t t39 = (uint8_t)!t38;
                        if (!t39) goto ie33;
                        {
                            gm_l6 = ((uint8_t)0);
                        }
                        goto ix33;
ie33:;
ix33:;
                    }
                    goto me29;
mn30:;
                    gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 150, 5);
me29:;
                }
lc18:;
                {
                    uint64_t t42 = gm_l8;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                    uint64_t t45 = r43;
                    gm_l8 = t45;
                }
            }
gm_ll22:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb18; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc18; }
            }
            goto gm_lad;
lb18:;
        }
    }
    {
        uint8_t t47 = gm_l6;
        if (!t47) goto ie46;
        void * t48 = gm_l4;
        gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 16));
        gm_t53at80pfdus3 t50;
        gm_f_kind_jcnt4hm((void *)&t50, t48, GM_CONV(gm_csguankko4lmhd, t49));
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
        if (t51 != ((uint8_t)0ULL)) goto ie46;
        uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)&t50) + 8) + 0));
        gm_l11 = t52;
        uint16_t t53 = GM_LD(uint16_t, ((((uint8_t *)&t50) + 8) + 2));
        gm_l12 = t53;
        uint8_t t54 = gm_l11;
        uint8_t t55 = (uint8_t)!t54;
        if (!t55) goto ie46;
        uint16_t t56 = gm_l12;
        uint8_t t57 = (t56 <= ((uint16_t)32ULL));
        if (!t57) goto ie46;
        {
        }
        {
            gm_tvdg94bk7aa22 t59;
            GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t59 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)14ULL));
            gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 16));
            {
                void * t62 = gm_l1;
                gm_tn3r4rvlh0rsv t63;
                GM_ST(void *, ((uint8_t *)&t63 + 0), t62);
                gm_l13 = t63;
            }
            void * t64 = ((uint8_t *)&gm_l13);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src5cfou479ljhn, 154, 36);
            gm_b16_8 t65;
            GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t64 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t66 = t65;
            gm_tl874t2atnlh3 t67 = gm_l3;
            void * t68 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t59, t60, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t68;
        }
        {
            void * t69 = gm_l0;
            void * t70 = gm_l14;
            void * t71 = gm_l4;
            gm_t88rcucg1b7uk t72 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t73 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t69, t70, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t74 = 0;
            F.ret = t73;
            goto gm_fret;
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        gm_f_list_58nr42v_3((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            uint64_t t76 = GM_LD(uintptr_t, (((uint8_t *)&gm_l5) + 0) + 8);
            gm_l16 = t76;
        }
        {
            for (;;) {
                uint64_t t78 = gm_l17;
                uint64_t t79 = gm_l16;
                uint8_t t80 = (t78 < t79);
                if (!t80) goto lb77;
                {
                    uint64_t t83 = gm_l17;
                    uint64_t t86 = GM_LD(uint64_t, ((uint8_t *)&t83));
                    gm_l18 = t86;
                    {
                        void * t88 = (void *)((uint8_t *)&F.l15);
                        gm_tbbqefuhv2r8h t89;
                        gm_t88rcucg1b7uk t90 = gm_l2;
                        uint64_t t91 = gm_l18;
                        uint32_t t92 = (uint32_t)t91;
                        gm_ttggq7ng4kp3p t93;
                        GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t93 + 0), t92);
                        GM_ST(void *, ((uint8_t *)&t93 + 8), ((void *)0));
                        gm_tb38v5s9j1r32 t94;
                        memset(&t94, 0, sizeof t94);
                        GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)8ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t94 + 8), t93);
                        gm_tl874t2atnlh3 t95 = gm_l3;
                        void * t96 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t90, &t94, t95);
                        if (gm_unw.kind) {
                            goto gm_ll81;
                        }
                        GM_ST(void *, (uint8_t *)&t89 + 0, t96);
                        GM_ST(void *, (uint8_t *)&t89 + 8, ((void *)0));
                        void * t97 = gm_l4;
                        gm_t88rcucg1b7uk t98 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t97);
                        if (gm_unw.kind) {
                            goto gm_ll81;
                        }
                        uint64_t t99 = gm_l18;
                        uint64_t t100 = (uint64_t)t99;
                        gm_tbci5ospk5nnn t101;
                        gm_f_from_u64_ud13q5v((void *)&t101, (void *)NULL, cx, NULL, t100);
                        if (gm_unw.kind) {
                            goto gm_ll81;
                        }
                        gm_teti411eujvf4 t102;
                        GM_ST(uint8_t, (uint8_t *)&t102 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t102 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t102 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t102 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t102 + 8), t101);
                        gm_tl874t2atnlh3 t103 = gm_l3;
                        void * t104 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t98, &t102, t103);
                        if (gm_unw.kind) {
                            goto gm_ll81;
                        }
                        GM_ST(void *, (uint8_t *)&t89 + 16, t104);
                        gm_tftb1odc739t5 t105 = gm_f_push_bpovr2k_3(t88, &t89);
                        if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 1, gm_src5cfou479ljhn, 159, 9);
                        gm_b0_1 t106 = GM_LD(gm_b0_1, (((uint8_t *)&t105) + 1));
                    }
                    goto me84;
mn85:;
                    gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 158, 5);
me84:;
                }
lc77:;
                {
                    uint64_t t108 = gm_l17;
                    uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                    uint64_t t111 = r109;
                    gm_l17 = t111;
                }
            }
gm_ll81:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb77; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc77; }
            }
            goto gm_lad;
lb77:;
        }
    }
    {
        void * t112 = gm_l4;
        gm_t88rcucg1b7uk t113 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t112);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkaapcii86udk t114;
        void * t115 = gm_l1;
        GM_ST(void *, (uint8_t *)&t114 + 0, t115);
        gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t114 + 8, t116);
        gm_teti411eujvf4 t117;
        memset(&t117, 0, sizeof t117);
        GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)44ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t117 + 8), t114);
        gm_tl874t2atnlh3 t118 = gm_l3;
        void * t119 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t113, &t117, t118);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t120 = 0;
        F.ret = t119;
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
__attribute__((aligned(4))) void * gm_f_without_call_t1kfc9e(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_without_call_t1kfc9e F;
    void * gm_l0; /* e */
    void * gm_l1; /* callee */
    gm_t3mf58gv9kf3n gm_l2; /* a */
    void * gm_l3; /* inner */
    uint8_t gm_l4; /* c */
    void * gm_l5; /* inner */
    uint8_t gm_l6; /* c */
    void * gm_l7; /* inner */
    uint8_t gm_l8; /* c */
    void * gm_l9; /* inner */
    GM_STACK_CHECK(&F, 899u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_t2gtbh39ptekd t3;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma5; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma11; case ((uint8_t)30ULL): goto ma11; case ((uint8_t)31ULL): goto ma11; case ((uint8_t)32ULL): goto ma11; case ((uint8_t)33ULL): goto ma11; case ((uint8_t)34ULL): goto ma11; case ((uint8_t)35ULL): goto ma11; case ((uint8_t)36ULL): goto ma11; case ((uint8_t)37ULL): goto ma11; case ((uint8_t)38ULL): goto ma11; case ((uint8_t)39ULL): goto ma11; case ((uint8_t)40ULL): goto ma11; case ((uint8_t)41ULL): goto ma11; case ((uint8_t)42ULL): goto ma11; case ((uint8_t)43ULL): goto ma11; case ((uint8_t)44ULL): goto ma11; case ((uint8_t)45ULL): goto ma11; case ((uint8_t)46ULL): goto ma11; case ((uint8_t)47ULL): goto ma11; case ((uint8_t)48ULL): goto ma11; case ((uint8_t)49ULL): goto ma11; case ((uint8_t)50ULL): goto ma11; default: goto ma11; }
ma5:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)19ULL)) goto mn14;
        void * t16 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l1 = t16;
        void * t17 = gm_l1;
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
        goto me4;
mn14:;
ma6:;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t20 != ((uint8_t)28ULL)) goto mn19;
        gm_t3mf58gv9kf3n t21 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t21;
        void * t22 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        void * t23 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        void * t24 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t25 = gm_f_box_mut_dlc4mlv((void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3mf58gv9kf3n t26;
        GM_ST(void *, ((uint8_t *)&t26 + 0), t22);
        GM_ST(void *, ((uint8_t *)&t26 + 8), t25);
        gm_t2gtbh39ptekd t27;
        memset(&t27, 0, sizeof t27);
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)28ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t27 + 8), t26);
        t3 = t27;
        goto me4;
mn19:;
ma7:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t29 != ((uint8_t)24ULL)) goto mn28;
        void * t30 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t30;
        void * t31 = gm_l3;
        void * t32 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t33 = gm_f_box_mut_dlc4mlv((void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t2gtbh39ptekd t34;
        memset(&t34, 0, sizeof t34);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)24ULL));
        GM_ST(void *, ((uint8_t *)&t34 + 8), t33);
        t3 = t34;
        goto me4;
mn28:;
ma8:;
        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t36 != ((uint8_t)25ULL)) goto mn35;
        uint8_t t37 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l4 = t37;
        void * t38 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l5 = t38;
        uint8_t t39 = gm_l4;
        void * t40 = gm_l5;
        void * t41 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t42 = gm_f_box_mut_dlc4mlv((void *)NULL, cx, NULL, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgtub0v94d5u5 t43;
        GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), t39);
        GM_ST(void *, ((uint8_t *)&t43 + 8), t42);
        gm_t2gtbh39ptekd t44;
        memset(&t44, 0, sizeof t44);
        GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)25ULL));
        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t44 + 8), t43);
        t3 = t44;
        goto me4;
mn35:;
ma9:;
        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t46 != ((uint8_t)26ULL)) goto mn45;
        uint8_t t47 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l6 = t47;
        void * t48 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l7 = t48;
        uint8_t t49 = gm_l6;
        void * t50 = gm_l7;
        void * t51 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t52 = gm_f_box_mut_dlc4mlv((void *)NULL, cx, NULL, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgtub0v94d5u5 t53;
        GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t53 + 0), t49);
        GM_ST(void *, ((uint8_t *)&t53 + 8), t52);
        gm_t2gtbh39ptekd t54;
        memset(&t54, 0, sizeof t54);
        GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)26ULL));
        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t54 + 8), t53);
        t3 = t54;
        goto me4;
mn45:;
ma10:;
        uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t56 != ((uint8_t)27ULL)) goto mn55;
        uint8_t t57 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l8 = t57;
        void * t58 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l9 = t58;
        uint8_t t59 = gm_l8;
        void * t60 = gm_l9;
        void * t61 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t62 = gm_f_box_mut_dlc4mlv((void *)NULL, cx, NULL, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgtub0v94d5u5 t63;
        GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t63 + 0), t59);
        GM_ST(void *, ((uint8_t *)&t63 + 8), t62);
        gm_t2gtbh39ptekd t64;
        memset(&t64, 0, sizeof t64);
        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)27ULL));
        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t64 + 8), t63);
        t3 = t64;
        goto me4;
mn55:;
ma11:;
        void * t66 = gm_l0;
        uint8_t t67 = 0;
        F.ret = t66;
        goto gm_fret;
        goto me4;
mn65:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_src5cfou479ljhn, 36, 26);
me4:;
        F.l10 = t3;
    }
    {
        gm_tc3oht4ksth9d t68;
        memset((uint8_t *)&t68 + 4, 0, 4);
        memset((uint8_t *)&t68 + 84, 0, 4);
        void * t69 = gm_l0;
        uint32_t t70 = GM_LD(uint32_t, (((uint8_t *)t69) + 0));
        GM_ST(uint32_t, (uint8_t *)&t68 + 0, t70);
        memcpy((uint8_t *)&t68 + 8, ((uint8_t *)&F.l10), 64);
        void * t71 = gm_l0;
        gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t71) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t68 + 72, t72);
        gm_tq7s98ihonkju t73 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &t68, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kv89j160jqp)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 8, gm_src5cfou479ljhn, 45, 12);
        void * t74 = GM_LD(void *, (((uint8_t *)&t73) + 8));
        uint8_t t75 = 0;
        F.ret = t74;
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
