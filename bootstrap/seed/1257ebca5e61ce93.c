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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
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
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
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
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tammvh1iespi3;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_taqu3436kubri;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { void * f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tcpfb8f2ne9p9;
typedef struct { gm_t66j1im9ip128 f0; uint8_t f1; uint8_t pe[7]; } gm_td8hfgklo8dil;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_tekl8c9g34a9k;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; gm_tl874t2atnlh3 f3; } gm_tgk5j0pob2pqa;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; uint64_t f3; gm_b16_8 f4; uint64_t f5; uint64_t f6; uint64_t f7; } gm_tkhl5gcodu1a6;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; void * f3; } gm_tl93vjpcesj48;
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; uint64_t f1; } gm_trevnmecc5pjt;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_tiilmfd16v3fc f0; gm_b16_8 f1; } gm_t1cs2fjnrqap1;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; gm_tkn42u7na3t81 v1; } f1; } gm_t1qr0p6kln6tf;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t08olq97rscn3 f0; gm_b16_8 f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t99vmhfllumbh;
typedef struct { uint64_t f0; gm_tkhl5gcodu1a6 f1; } gm_t9gtoko04lpgj;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tammvh1iespi3) == 56 && _Alignof(gm_tammvh1iespi3) == 8 && offsetof(gm_tammvh1iespi3, f0) == 0 && offsetof(gm_tammvh1iespi3, f1) == 16 && offsetof(gm_tammvh1iespi3, f2) == 24 && offsetof(gm_tammvh1iespi3, f3) == 40, "layout of `EffectInfo`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_taqu3436kubri) == 20 && _Alignof(gm_taqu3436kubri) == 4 && offsetof(gm_taqu3436kubri, f0) == 0 && offsetof(gm_taqu3436kubri, f1) == 4 && offsetof(gm_taqu3436kubri, f2) == 8, "layout of `HandlerBreak`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_tcpfb8f2ne9p9) == 16 && _Alignof(gm_tcpfb8f2ne9p9) == 8 && offsetof(gm_tcpfb8f2ne9p9, f0) == 0 && offsetof(gm_tcpfb8f2ne9p9, f1) == 8, "layout of `(cont: ^Expr, r: Ty)`");
_Static_assert(sizeof(gm_td8hfgklo8dil) == 40 && _Alignof(gm_td8hfgklo8dil) == 8 && offsetof(gm_td8hfgklo8dil, f0) == 0 && offsetof(gm_td8hfgklo8dil, f1) == 32, "layout of `?(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
_Static_assert(sizeof(gm_tekl8c9g34a9k) == 32 && _Alignof(gm_tekl8c9g34a9k) == 8 && offsetof(gm_tekl8c9g34a9k, f0) == 0 && offsetof(gm_tekl8c9g34a9k, f1) == 16 && offsetof(gm_tekl8c9g34a9k, f2) == 20, "layout of `DeclWithCheck`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tgk5j0pob2pqa) == 24 && _Alignof(gm_tgk5j0pob2pqa) == 4 && offsetof(gm_tgk5j0pob2pqa, f0) == 0 && offsetof(gm_tgk5j0pob2pqa, f1) == 4 && offsetof(gm_tgk5j0pob2pqa, f2) == 8 && offsetof(gm_tgk5j0pob2pqa, f3) == 12, "layout of `LabelBreak`");
typedef struct { uint64_t f0; gm_t5v0c8ia6a5mo f1; } gm_tgsvmddj3e85e;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; uint8_t f4; uint8_t pe[7]; } gm_tieqmg4s05m8e;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_tiilmfd16v3fc f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tj44oivvogvtl;
typedef struct { gm_t5v0c8ia6a5mo f0; uint8_t f1; uint8_t pe[7]; } gm_tj8m6gmlgjilo;
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkhl5gcodu1a6) == 80 && _Alignof(gm_tkhl5gcodu1a6) == 8 && offsetof(gm_tkhl5gcodu1a6, f0) == 0 && offsetof(gm_tkhl5gcodu1a6, f1) == 8 && offsetof(gm_tkhl5gcodu1a6, f2) == 16 && offsetof(gm_tkhl5gcodu1a6, f3) == 32 && offsetof(gm_tkhl5gcodu1a6, f4) == 40 && offsetof(gm_tkhl5gcodu1a6, f5) == 56 && offsetof(gm_tkhl5gcodu1a6, f6) == 64 && offsetof(gm_tkhl5gcodu1a6, f7) == 72, "layout of `PendingHandler`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl93vjpcesj48) == 48 && _Alignof(gm_tl93vjpcesj48) == 8 && offsetof(gm_tl93vjpcesj48, f0) == 0 && offsetof(gm_tl93vjpcesj48, f1) == 8 && offsetof(gm_tl93vjpcesj48, f2) == 24 && offsetof(gm_tl93vjpcesj48, f3) == 40, "layout of `NestedFn`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trevnmecc5pjt) == 24 && _Alignof(gm_trevnmecc5pjt) == 8 && offsetof(gm_trevnmecc5pjt, f0) == 0 && offsetof(gm_trevnmecc5pjt, f1) == 16, "layout of `Shielded`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t pe[4]; } gm_tuanp0jbrlb6j;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
typedef struct { gm_tcpfb8f2ne9p9 f0; uint8_t f1; uint8_t pe[7]; } gm_tuqc9k0g5qf5e;
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
typedef struct { gm_tiilmfd16v3fc f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; gm_b16_8 f3; } gm_tvc83pqnepc2e;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1cs2fjnrqap1) == 40 && _Alignof(gm_t1cs2fjnrqap1) == 8 && offsetof(gm_t1cs2fjnrqap1, f0) == 0 && offsetof(gm_t1cs2fjnrqap1, f1) == 24, "layout of `HandlerLit`");
_Static_assert(sizeof(gm_t1qr0p6kln6tf) == 12 && _Alignof(gm_t1qr0p6kln6tf) == 4 && offsetof(gm_t1qr0p6kln6tf, f0) == 0 && offsetof(gm_t1qr0p6kln6tf, f1) == 4, "layout of `LabelValue`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t f2; uint8_t p3[3]; gm_t1qr0p6kln6tf f3; uint8_t pe[4]; } gm_t8enl5g7b22jb;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t99vmhfllumbh) == 72 && _Alignof(gm_t99vmhfllumbh) == 8 && offsetof(gm_t99vmhfllumbh, f0) == 0 && offsetof(gm_t99vmhfllumbh, f1) == 32 && offsetof(gm_t99vmhfllumbh, f2) == 48 && offsetof(gm_t99vmhfllumbh, f3) == 56, "layout of `Clause`");
_Static_assert(sizeof(gm_t9gtoko04lpgj) == 88 && _Alignof(gm_t9gtoko04lpgj) == 8 && offsetof(gm_t9gtoko04lpgj, f0) == 0 && offsetof(gm_t9gtoko04lpgj, f1) == 8, "layout of `(i: usize, x: PendingHandler)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_tgsvmddj3e85e) == 64 && _Alignof(gm_tgsvmddj3e85e) == 8 && offsetof(gm_tgsvmddj3e85e, f0) == 0 && offsetof(gm_tgsvmddj3e85e, f1) == 8, "layout of `(i: usize, x: ParamInfo)`");
_Static_assert(sizeof(gm_tieqmg4s05m8e) == 168 && _Alignof(gm_tieqmg4s05m8e) == 8 && offsetof(gm_tieqmg4s05m8e, f0) == 0 && offsetof(gm_tieqmg4s05m8e, f1) == 40 && offsetof(gm_tieqmg4s05m8e, f2) == 80 && offsetof(gm_tieqmg4s05m8e, f3) == 120 && offsetof(gm_tieqmg4s05m8e, f4) == 160, "layout of `WithHandlers`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tj44oivvogvtl) == 40 && _Alignof(gm_tj44oivvogvtl) == 8 && offsetof(gm_tj44oivvogvtl, f0) == 0 && offsetof(gm_tj44oivvogvtl, f1) == 24, "layout of `Performed`");
_Static_assert(sizeof(gm_tj8m6gmlgjilo) == 64 && _Alignof(gm_tj8m6gmlgjilo) == 8 && offsetof(gm_tj8m6gmlgjilo, f0) == 0 && offsetof(gm_tj8m6gmlgjilo, f1) == 56, "layout of `?ParamInfo`");
typedef struct { gm_tuanp0jbrlb6j f0; uint8_t f1; uint8_t pe[7]; } gm_tjvtgq0p0sehv;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
typedef struct { gm_tddtjfggdat0d f0; uint8_t f1; uint8_t pe[7]; } gm_tt775pork2bs8;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tuanp0jbrlb6j) == 32 && _Alignof(gm_tuanp0jbrlb6j) == 8 && offsetof(gm_tuanp0jbrlb6j, f0) == 0 && offsetof(gm_tuanp0jbrlb6j, f1) == 24, "layout of `ForLabel`");
_Static_assert(sizeof(gm_tuqc9k0g5qf5e) == 24 && _Alignof(gm_tuqc9k0g5qf5e) == 8 && offsetof(gm_tuqc9k0g5qf5e, f0) == 0 && offsetof(gm_tuqc9k0g5qf5e, f1) == 16, "layout of `?(cont: ^Expr, r: Ty)`");
_Static_assert(sizeof(gm_tvc83pqnepc2e) == 64 && _Alignof(gm_tvc83pqnepc2e) == 8 && offsetof(gm_tvc83pqnepc2e, f0) == 0 && offsetof(gm_tvc83pqnepc2e, f1) == 24 && offsetof(gm_tvc83pqnepc2e, f2) == 32 && offsetof(gm_tvc83pqnepc2e, f3) == 48, "layout of `HandlerDeclInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t8enl5g7b22jb) == 48 && _Alignof(gm_t8enl5g7b22jb) == 8 && offsetof(gm_t8enl5g7b22jb, f0) == 0 && offsetof(gm_t8enl5g7b22jb, f1) == 24 && offsetof(gm_t8enl5g7b22jb, f2) == 28 && offsetof(gm_t8enl5g7b22jb, f3) == 32, "layout of `LabelInfo`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
typedef struct { gm_t8enl5g7b22jb f0; uint8_t f1; uint8_t pe[7]; } gm_te5gbhu3us3k4;
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tjvtgq0p0sehv) == 40 && _Alignof(gm_tjvtgq0p0sehv) == 8 && offsetof(gm_tjvtgq0p0sehv, f0) == 0 && offsetof(gm_tjvtgq0p0sehv, f1) == 32, "layout of `?ForLabel`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_tkn42u7na3t81 f3; uint8_t p4[4]; gm_tjvtgq0p0sehv f4; gm_tkn42u7na3t81 f5; gm_tqu3m2l3n8mnc f6; } gm_tnqmlaad6uchl;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_tt775pork2bs8) == 56 && _Alignof(gm_tt775pork2bs8) == 8 && offsetof(gm_tt775pork2bs8, f0) == 0 && offsetof(gm_tt775pork2bs8, f1) == 48, "layout of `?FnTy`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_tnqmlaad6uchl f0; uint8_t f1; uint8_t pe[7]; } gm_t403eokj7ifg9;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_te5gbhu3us3k4) == 56 && _Alignof(gm_te5gbhu3us3k4) == 8 && offsetof(gm_te5gbhu3us3k4, f0) == 0 && offsetof(gm_te5gbhu3us3k4, f1) == 48, "layout of `?LabelInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_tnqmlaad6uchl) == 112 && _Alignof(gm_tnqmlaad6uchl) == 8 && offsetof(gm_tnqmlaad6uchl, f0) == 0 && offsetof(gm_tnqmlaad6uchl, f1) == 4 && offsetof(gm_tnqmlaad6uchl, f2) == 12 && offsetof(gm_tnqmlaad6uchl, f3) == 20 && offsetof(gm_tnqmlaad6uchl, f4) == 32 && offsetof(gm_tnqmlaad6uchl, f5) == 72 && offsetof(gm_tnqmlaad6uchl, f6) == 80, "layout of `ClauseInfo`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t403eokj7ifg9) == 120 && _Alignof(gm_t403eokj7ifg9) == 8 && offsetof(gm_t403eokj7ifg9, f0) == 0 && offsetof(gm_t403eokj7ifg9, f1) == 112, "layout of `?ClauseInfo`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_trqfvmjtt4sij f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; uint32_t f5; uint8_t f6; uint8_t p7[3]; uint64_t f7; gm_t9805qtonbsag f8; uint8_t f9; uint8_t f10; uint8_t p11[2]; gm_tkn42u7na3t81 f11; uint8_t p12[4]; gm_t403eokj7ifg9 f12; gm_trqfvmjtt4sij f13; gm_trqfvmjtt4sij f14; } gm_tjhr0bdm69ios;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_tjhr0bdm69ios f0; uint8_t f1; uint8_t pe[7]; } gm_ta7fvse6v13pf;
_Static_assert(sizeof(gm_tjhr0bdm69ios) == 384 && _Alignof(gm_tjhr0bdm69ios) == 8 && offsetof(gm_tjhr0bdm69ios, f0) == 0 && offsetof(gm_tjhr0bdm69ios, f1) == 40 && offsetof(gm_tjhr0bdm69ios, f2) == 48 && offsetof(gm_tjhr0bdm69ios, f3) == 64 && offsetof(gm_tjhr0bdm69ios, f4) == 104 && offsetof(gm_tjhr0bdm69ios, f5) == 144 && offsetof(gm_tjhr0bdm69ios, f6) == 148 && offsetof(gm_tjhr0bdm69ios, f7) == 152 && offsetof(gm_tjhr0bdm69ios, f8) == 160 && offsetof(gm_tjhr0bdm69ios, f9) == 168 && offsetof(gm_tjhr0bdm69ios, f10) == 169 && offsetof(gm_tjhr0bdm69ios, f11) == 172 && offsetof(gm_tjhr0bdm69ios, f12) == 184 && offsetof(gm_tjhr0bdm69ios, f13) == 304 && offsetof(gm_tjhr0bdm69ios, f14) == 344, "layout of `Frame`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_ta7fvse6v13pf) == 392 && _Alignof(gm_ta7fvse6v13pf) == 8 && offsetof(gm_ta7fvse6v13pf, f0) == 0 && offsetof(gm_ta7fvse6v13pf, f1) == 384, "layout of `?Frame`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_cancel_stmt_pfi7e6b {
    gm_frame h;
    void * ret;
};
struct gm_fr_check_handled_coincidenc_10aiigb {
    gm_frame h;
    gm_tj44oivvogvtl l5; /* p */
    gm_b0_1 ret;
};
struct gm_fr_check_one_handler_euhh29f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_check_pending_handler_l4msk6j {
    gm_frame h;
    gm_tkhl5gcodu1a6 l7; /* x */
    gm_tkhl5gcodu1a6 l9; /* p */
    gm_trqfvmjtt4sij l10; /* frames */
    gm_tjhr0bdm69ios l11; /* f */
    gm_trqfvmjtt4sij l13; /* hidden_scopes */
    gm_trqfvmjtt4sij l14; /* hidden_depths */
    gm_trqfvmjtt4sij l15; /* hidden_tails */
    gm_trqfvmjtt4sij l21; /* tail */
    gm_trqfvmjtt4sij l22; /* hidden_consts */
    gm_trqfvmjtt4sij l24; /* hidden_labels */
    gm_trqfvmjtt4sij l26; /* hidden_handled */
    gm_tj44oivvogvtl l31; /* x */
    gm_tjhr0bdm69ios l35; /* f */
    gm_tjhr0bdm69ios l36; /* outer */
    gm_b0_1 ret;
};
struct gm_fr_check_pending_handlers_o_sunakri {
    gm_frame h;
    gm_tkhl5gcodu1a6 l6; /* p */
    gm_b0_1 ret;
};
struct gm_fr_clause_body_93ierdg {
    gm_frame h;
    void * ret;
};
struct gm_fr_clause_fn_qrr198q {
    gm_frame h;
    gm_tnqmlaad6uchl l4; /* info */
    gm_tk9n3vlila9qg l8; /* frame */
    uint32_t ret;
};
struct gm_fr_clause_signature_9v4rkv8 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* params */
    gm_t5v0c8ia6a5mo l7; /* p */
    gm_t5v0c8ia6a5mo l9; /* p */
    gm_tep2drsl9408b ret;
};
struct gm_fr_clause_value_tpeb80m {
    gm_frame h;
    gm_t79rbdv4rafe5 l5; /* b */
    void * ret;
};
struct gm_fr_cont_operand_i35ab8d {
    gm_frame h;
    gm_tuqc9k0g5qf5e ret;
};
struct gm_fr_declare_clause_params_7bqbscg {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* ids */
    gm_t5v0c8ia6a5mo l7; /* p */
    gm_b16_8 ret;
};
struct gm_fr_depth_fr7pn6n {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_effect_name_prpp0ip {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_3g747ka {
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
struct gm_fr_empty_slice_7athnlt {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f6g91fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_gbnq8as {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_he774nl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lio7tu0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p5lo8ed_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qlbp545 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_u0hn964 {
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
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_fiber_intrinsic_os7v2ur {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* positional */
    gm_t7te2tdpebbt4 l7; /* a */
    gm_tc3oht4ksth9d l8; /* e */
    gm_tt775pork2bs8 l12; /* sig */
    gm_tddtjfggdat0d l13; /* g */
    gm_tddtjfggdat0d l14; /* fsig */
    gm_trqfvmjtt4sij l19; /* rest */
    gm_tnqmlaad6uchl l24; /* c */
    gm_trqfvmjtt4sij l27; /* vals */
    void * ret;
};
struct gm_fr_find_clause_ar0ta3q {
    gm_frame h;
    void * ret;
};
struct gm_fr_handler_clauses_mvttmfg {
    gm_frame h;
    gm_tammvh1iespi3 l6; /* info */
    gm_trqfvmjtt4sij l7; /* clauses */
    gm_tv0k2vg3054a6 l10; /* o */
    gm_tnqmlaad6uchl l13; /* info_ */
    gm_b0_1 ret;
};
struct gm_fr_handler_lit_u1lbff2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_handler_literal_avosuha {
    gm_frame h;
    void * ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_in_ctl_clause_f2fv82c {
    gm_frame h;
    gm_tnqmlaad6uchl l1; /* c */
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1jabh22 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7mlcud7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fs7kvfg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fu87ckm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_g3vmkan_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_lgereu6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_p0pjtrh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_t7903ef {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1 {
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
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_new_handler_literal_bv98g00 {
    gm_frame h;
    gm_tammvh1iespi3 l3; /* info */
    gm_t99vmhfllumbh l7; /* c */
    gm_tv0k2vg3054a6 l11; /* o */
    gm_t99vmhfllumbh l15; /* d */
    gm_t9805qtonbsag ret;
};
struct gm_fr_note_near_misses_hr4e2cd {
    gm_frame h;
    gm_tj44oivvogvtl l5; /* p */
    gm_b0_1 ret;
};
struct gm_fr_pending_handler_literal_3hqdgg4 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* lens */
    gm_trqfvmjtt4sij l7; /* sc */
    void * ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_pop_8sodrk9 {
    gm_frame h;
    gm_td8hfgklo8dil ret;
};
struct gm_fr_pop_dt2bd2s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* x */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_pop_f3uajlg_2 {
    gm_frame h;
    gm_tjhr0bdm69ios l2; /* x */
    gm_ta7fvse6v13pf ret;
};
struct gm_fr_pop_ruvva9t {
    gm_frame h;
    gm_t8enl5g7b22jb l2; /* x */
    gm_te5gbhu3us3k4 ret;
};
struct gm_fr_pop_scope_g5j7o4e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_remove_handled_performs_uvibbqm {
    gm_frame h;
    gm_tj44oivvogvtl l6; /* p */
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resume_expr_l33thg3 {
    gm_frame h;
    void * ret;
};
struct gm_fr_shield_2k6eclc {
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
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_swap_frame_4j63sdr {
    gm_frame h;
    gm_tjhr0bdm69ios l1; /* f */
    gm_tjhr0bdm69ios l2; /* out */
    gm_tjhr0bdm69ios ret;
};
struct gm_fr_truncate_36vf02g {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_7aasjoi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_7r782uc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_daelje0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_dsb08pk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_eoppkeo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_itj4ge7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_k0voaim {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ohk62mc {
    gm_frame h;
    void * ret;
};
struct gm_fr_with_breaks_19nlbuk {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* breaks */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_with_expr_vi78dtj {
    gm_frame h;
    gm_tieqmg4s05m8e l8; /* hs */
    gm_trqfvmjtt4sij l17; /* breaks */
    gm_tv0k2vg3054a6 l34; /* o */
    void * ret;
};
struct gm_fr_with_handlers_2dg7aho {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* values */
    gm_trqfvmjtt4sij l5; /* handled */
    gm_trqfvmjtt4sij l6; /* literal_ids */
    gm_trqfvmjtt4sij l7; /* decl_ids */
    gm_tc3oht4ksth9d l11; /* h */
    gm_t4gut2c9fj1kt l19; /* d */
    gm_tvc83pqnepc2e l23; /* info */
    gm_trqfvmjtt4sij l25; /* effs */
    gm_tieqmg4s05m8e ret;
};
struct gm_fr_with_stack_9rur454 {
    gm_frame h;
    gm_tv0k2vg3054a6 l10; /* o */
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_block_expected_bv4ukm2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) void * gm_f_cancel_stmt_pfi7e6b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_handled_coincidenc_10aiigb(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_one_handler_euhh29f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_pending_handler_l4msk6j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_pending_handlers_o_sunakri(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_clause_body_93ierdg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) uint32_t gm_f_clause_fn_qrr198q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3, const void * a4);
__attribute__((aligned(4))) void gm_f_clause_signature_9v4rkv8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_clause_value_tpeb80m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_cont_operand_i35ab8d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_declare_clause_params_7bqbscg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_declare_temps_a7fc14v(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) uint32_t gm_f_depth_fr7pn6n(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_effect_map_b2f0sd4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3g747ka(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7athnlt(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gbnq8as(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lio7tu0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qlbp545(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u0hn964(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_enter_fn_vo5kel1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_exit_fn_kj6vbu8(void *gm_out, void * a0);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_43vto3v(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_5b5kjjl(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_717snqd(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_74ac4hb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_ehh2367(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_oe972vj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_fiber_intrinsic_os7v2ur(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_find_clause_ar0ta3q(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_2leeume(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_2ommnur(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_74vh99o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_beqekat(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fskpepf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_i0hivvb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jb19283(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jtr5odb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kvhj8q4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mg3bghj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_pbrbg1u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_pskrfl8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ql3u2no(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rd4d2vn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_sgnhhmi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tv3i6qj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vc10pth(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_handler_clauses_mvttmfg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_tkn42u7na3t81 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_handler_lit_u1lbff2(void * a0);
__attribute__((aligned(4))) void * gm_f_handler_literal_avosuha(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_handlers_of_type_5gareb4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_in_ctl_clause_f2fv82c(void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7mlcud7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fs7kvfg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fu87ckm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_g3vmkan_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_lgereu6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_p0pjtrh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_t7903ef(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_frame_r6k3kmp(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_may_coincide_f6v7r98(void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_new_handler_literal_bv98g00(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_label_3207tcb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2, gm_t1qr0p6kln6tf a3);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_near_misses_hr4e2cd(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_pending_handler_literal_3hqdgg4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_f3uajlg_2(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_26bmlpd(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3onuaru(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8h1r7s6_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8quniiv(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bu0mlkm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_f4l3f1p(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_f5fsh94(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_gi2g734(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_jd7smj6(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_v83fn9o(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_record_effects_0eel5vu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_remove_5pvnsnk(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_remove_handled_performs_uvibbqm(void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3, uint8_t a4);
__attribute__((aligned(4))) void gm_f_remove_v59hrnl(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_remove_vrfdutr(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_effect_ref_bggirs2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_resume_expr_l33thg3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_save_frame_8libfct(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_shield_2k6eclc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_swap_frame_4j63sdr(void *gm_out, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_truncate_36vf02g(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_7aasjoi(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_eoppkeo(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_itj4ge7(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_k0voaim(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_with_breaks_19nlbuk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint32_t a2);
__attribute__((aligned(4))) void * gm_f_with_expr_vi78dtj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) void gm_f_with_handlers_2dg7aho(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) void * gm_f_with_stack_9rur454(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0jbakoflqfkm[2];
extern const uint8_t gm_s82i64ms8kog2[94];
extern const uint8_t gm_sbn0999b7p1ph[83];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfljf7mvjr19m[106];
extern const uint8_t gm_sh5prd89hpoup[85];
extern const uint8_t gm_si1m7q6ia0hto[7];
extern const uint8_t gm_smoi1nh7b1b6o[65];
extern const uint8_t gm_sn6sqlran7h3r[7];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcn0mgmph5u0oo[];
extern const char gm_srcod98slk3vnra[];
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
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 80);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srcgfglnv8p059r, 96, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_name_prpp0ip F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 216) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 283, 62);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 216) + 0)) + t3 * 16));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_emit_dph5v1e F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        gm_f_push_o4ckdr1(t4, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_effects_r0iv3pv F;
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t2 = t1;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3g747ka(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3g747ka F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 20)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7athnlt(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7athnlt F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 224)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f6g91fe F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 224)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gbnq8as(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_gbnq8as F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lio7tu0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lio7tu0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p5lo8ed_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 384)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qlbp545(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qlbp545 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u0hn964(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_u0hn964 F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_infer_3o82a0b F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 40) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 252, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)2ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_in_ctl_clause_f2fv82c(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_in_ctl_clause_f2fv82c F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK_SHALLOW(&F, 487u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_t403eokj7ifg9 t3 = GM_LD(gm_t403eokj7ifg9, (((uint8_t *)t2) + 800));
        gm_t403eokj7ifg9 t4 = t3;
        gm_tnqmlaad6uchl t5;
        if (((((uint8_t *)&t4))[112] != 0)) {
            gm_tnqmlaad6uchl t6 = GM_LD(gm_tnqmlaad6uchl, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        F.l1 = t5;
    }
    {
        uint8_t t8 = 0;
        uint8_t t9 = ((GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0) + 0) == ((uint8_t)2ULL)));
        if (t9) {
            uint8_t t10;
            if ((((((uint8_t *)&F.l1) + 32))[32] != 0)) goto mn12;
            t10 = ((uint8_t)1);
            goto me11;
mn12:;
            t10 = ((uint8_t)0);
            goto me11;
mn13:;
            gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 492, 34);
me11:;
            t8 = t10;
        } else t8 = 0;
        uint8_t t14 = 0;
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_n83ttnb F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK_SHALLOW(&F, 182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
        gm_l2 = t8;
        {
            uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t14 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix2:;
    }
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
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_084fcd6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
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
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1jabh22 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_f6g91fe();
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
inline __attribute__((aligned(4))) void gm_f_list_7mlcud7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7mlcud7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_gbnq8as();
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
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_af74s0q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_he774nl();
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
inline __attribute__((aligned(4))) void gm_f_list_fs7kvfg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fs7kvfg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_7athnlt();
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
inline __attribute__((aligned(4))) void gm_f_list_fu87ckm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fu87ckm F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3g747ka();
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
inline __attribute__((aligned(4))) void gm_f_list_g3vmkan_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g3vmkan_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_p5lo8ed_2();
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
inline __attribute__((aligned(4))) void gm_f_list_lgereu6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_lgereu6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_u0hn964();
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
inline __attribute__((aligned(4))) void gm_f_list_p0pjtrh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_p0pjtrh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_qlbp545();
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
inline __attribute__((aligned(4))) void gm_f_list_t7903ef(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_t7903ef F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lio7tu0();
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_g3tddrb();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_never_ty_b949mgo F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)5ULL));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_570g79n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint32_t gm_l2; /* x */
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
            gm_t9805qtonbsag t8;
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
        uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 4));
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
        uint32_t t29 = gm_l2;
        gm_t9805qtonbsag t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t30), t29);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_8sodrk9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t66j1im9ip128 gm_l2; /* x */
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
            gm_td8hfgklo8dil t8;
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
        gm_t66j1im9ip128 t17 = GM_LD(gm_t66j1im9ip128, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 32));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_t66j1im9ip128 t29 = gm_l2;
        gm_td8hfgklo8dil t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 36, 0);
        GM_ST(gm_t66j1im9ip128, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[32] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_dt2bd2s F;
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
            gm_tqetrgo0q24b5 t8;
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
        gm_trqfvmjtt4sij t17 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 40));
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_trqfvmjtt4sij t29 = F.l2;
        gm_tqetrgo0q24b5 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 44, 0);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[40] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_f3uajlg_2(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_f3uajlg_2 F;
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
            gm_ta7fvse6v13pf t8;
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
        memmove(&F.l2, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 384), sizeof F.l2);
    }
    {
        void * t17 = gm_l0;
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 0) + 8);
        void * t20 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)));
        uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t22 = gm_l1;
        uint64_t r23; bool o24 = __builtin_sub_overflow(t22, ((uint64_t)1ULL), &r23);
        if (o24) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t25 = r23;
        uint64_t t26 = (uintptr_t)t25;
        if (t21 > t26 || t26 > (uintptr_t)t19) gm_trap_slice(t21, t26, t19, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t20 + t21 * 384)));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26 - t21));
        GM_ST(gm_b16_8, (((uint8_t *)t17) + 0), t27);
    }
    {
        gm_tjhr0bdm69ios t28 = F.l2;
        gm_ta7fvse6v13pf t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 385, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 386, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 388, 0);
        GM_ST(gm_tjhr0bdm69ios, ((uint8_t *)&t29), t28);
        ((uint8_t *)&t29)[384] = 1;
        uint8_t t30 = 0;
        F.ret = t29;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_ruvva9t F;
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
            gm_te5gbhu3us3k4 t8;
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
        gm_t8enl5g7b22jb t17 = GM_LD(gm_t8enl5g7b22jb, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 48));
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_t8enl5g7b22jb t29 = F.l2;
        gm_te5gbhu3us3k4 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 52, 0);
        GM_ST(gm_t8enl5g7b22jb, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[48] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_scope_g5j7o4e F;
    void * gm_l0; /* cx */
    gm_t66j1im9ip128 gm_l1; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 272u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            void * t4 = (void *)(((uint8_t *)t3) + 56);
            gm_tqetrgo0q24b5 t5;
            gm_f_pop_dt2bd2s((void *)&t5, t4);
        }
    }
    {
        {
            void * t7 = gm_l0;
            void * t8 = (void *)(((uint8_t *)t7) + 616);
            gm_t9805qtonbsag t9 = gm_f_pop_570g79n(t8);
        }
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 656);
        gm_td8hfgklo8dil t13;
        gm_f_pop_8sodrk9((void *)&t13, t12);
        gm_td8hfgklo8dil t14 = t13;
        if (!((((uint8_t *)&t14))[32] != 0)) goto ie10;
        gm_t66j1im9ip128 t15 = GM_LD(gm_t66j1im9ip128, ((uint8_t *)&t14));
        gm_l1 = t15;
        {
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
            gm_f_restore_scope_edqq1v8(t18, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l1)));
        }
        goto ix10;
ie10:;
ix10:;
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
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_scope_edqq1v8 F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* mark */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 1488);
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)a1) + 0));
        gm_f_truncate_7r782uc(t3, t4);
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 1528);
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        gm_f_truncate_daelje0(t6, t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 1608);
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
        gm_f_truncate_dsb08pk(t9, t10);
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 1568);
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
        gm_f_truncate_upeumbt(t12, t13);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_ref_aqt1t7c F;
    void * gm_l0; /* t */
    void * gm_l1; /* e */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 153u);
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
        void * t5 = gm_l1;
        gm_b0_1 t6 = gm_f_effect_ref_6sjtehp((void *)NULL, cx, NULL, t4, t5);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_slice_5dt9121 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
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
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)10ULL));
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
inline __attribute__((aligned(4))) void gm_f_swap_frame_4j63sdr(void *gm_out, void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_swap_frame_4j63sdr F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_f_save_frame_8libfct((void *)&F.l2, t2);
    }
    {
        void * t3 = gm_l0;
        gm_f_load_frame_r6k3kmp(t3, &(*(gm_tjhr0bdm69ios *)((uint8_t *)a1)));
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 384);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_36vf02g(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_36vf02g F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 224)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_7aasjoi(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_7aasjoi F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 48)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_7r782uc F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_daelje0 F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_dsb08pk F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_eoppkeo(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_eoppkeo F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 4)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_itj4ge7(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_itj4ge7 F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 40)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_k0voaim(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_k0voaim F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_upeumbt F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 224)));
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
inline __attribute__((aligned(4))) void * gm_f_cancel_stmt_pfi7e6b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_cancel_stmt_pfi7e6b F;
    void * gm_l0; /* cx */
    void * gm_l1; /* k */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_tcpfb8f2ne9p9 gm_l3; /* c */
    GM_STACK_CHECK(&F, 330u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_si1m7q6ia0hto));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        void * t4 = gm_l1;
        gm_tuqc9k0g5qf5e t5;
        gm_f_cont_operand_i35ab8d((void *)&t5, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tuqc9k0g5qf5e t6 = t5;
        gm_tcpfb8f2ne9p9 t7;
        if (((((uint8_t *)&t6))[16] != 0)) {
            gm_tcpfb8f2ne9p9 t8 = GM_LD(gm_tcpfb8f2ne9p9, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
            gm_tl874t2atnlh3 t11 = gm_l2;
            void * t12 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l3 = t7;
    }
    {
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
        void * t16 = (void *)(((uint8_t *)t15) + 8);
        gm_t88rcucg1b7uk t17 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t18 = GM_LD(void *, (((uint8_t *)&gm_l3) + 0));
        gm_teti411eujvf4 t19;
        memset(&t19, 0, sizeof t19);
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)54ULL));
        GM_ST(void *, ((uint8_t *)&t19 + 8), t18);
        gm_tl874t2atnlh3 t20 = gm_l2;
        void * t21 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t17, &t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t22 = 0;
        F.ret = t21;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_handled_coincidenc_10aiigb(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_check_handled_coincidenc_10aiigb F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* start */
    gm_b16_8 gm_l2; /* handled */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tiilmfd16v3fc gm_l8; /* x */
    GM_STACK_CHECK(&F, 905u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 192) + 0) + 8);
            void * t5 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t3) + 192) + 0)));
            uint64_t t6 = gm_l1;
            uint64_t t7 = (uintptr_t)t6;
            if (t7 > (uintptr_t)t4 || (uintptr_t)t4 > (uintptr_t)t4) gm_trap_slice(t7, (uintptr_t)t4, t4, gm_srcn0mgmph5u0oo, 190, 14);
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t7 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)((uintptr_t)t4 - t7));
            gm_l3 = t8;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l4;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l4;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tj44oivvogvtl t20 = GM_LD(gm_tj44oivvogvtl, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t16 * 40));
                    F.l5 = t20;
                    {
                        uint32_t t23 = GM_LD(uint32_t, ((((uint8_t *)&F.l5) + 0) + 0));
                        uint8_t t24 = ((t23 == ((uint32_t)0ULL)));
                        if (!t24) goto ie22;
                        {
                            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            uint8_t t28 = (t27 > ((uint64_t)0ULL));
                            if (!t28) goto ie26;
                            {
                                void * t30 = gm_l0;
                                gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 24));
                                gm_t6j6smj7qoctu t32;
                                void * t33 = gm_l0;
                                void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
                                void * t35 = (void *)(((uint8_t *)t34) + 8);
                                void * t36 = (void *)(((uint8_t *)&F.l5) + 0);
                                gm_b16_8 t37 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t35, t36);
                                if (gm_unw.kind) {
                                    goto gm_ll13;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t32 + 0, t37);
                                void * t38 = gm_l0;
                                void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
                                void * t40 = (void *)(((uint8_t *)t39) + 8);
                                uint64_t t41 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                                if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcn0mgmph5u0oo, 193, 205);
                                void * t43 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t41 * 24);
                                gm_b16_8 t44 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t40, t43);
                                if (gm_unw.kind) {
                                    goto gm_ll13;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t32 + 16, t44);
                                gm_tabb710d9mq2e t45;
                                gm_f_format_rd4d2vn((void *)&t45, (void *)NULL, cx, NULL, &t32);
                                if (gm_unw.kind) {
                                    goto gm_ll13;
                                }
                                gm_tabb710d9mq2e t46 = t45;
                                if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_srcn0mgmph5u0oo, 193, 34);
                                gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
                                gm_b16_8 t48 = t47;
                                gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                                gm_b0_1 t50 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t30, t31, t49);
                                if (gm_unw.kind) {
                                    goto gm_ll13;
                                }
                            }
                            {
                                goto lc9;
                            }
                            goto ix26;
ie26:;
ix26:;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    {
                        {
                            gm_b16_8 t52 = gm_l2;
                            gm_l6 = t52;
                        }
                        {
                            gm_l7 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t54 = gm_l7;
                                uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                uint8_t t56 = (t54 < t55);
                                if (!t56) goto lb53;
                                {
                                    uint64_t t59 = gm_l7;
                                    uint64_t t60 = (uintptr_t)t59;
                                    uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    gm_tiilmfd16v3fc t64 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t60 * 24));
                                    gm_l8 = t64;
                                    {
                                        void * t67 = gm_l0;
                                        void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
                                        void * t69 = (void *)(((uint8_t *)&F.l5) + 0);
                                        void * t70 = (void *)((uint8_t *)&gm_l8);
                                        uint8_t t71 = gm_f_may_coincide_f6v7r98(t68, t69, t70);
                                        if (!t71) goto ie66;
                                        {
                                            void * t73 = gm_l0;
                                            gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 24));
                                            gm_t6j6smj7qoctu t75;
                                            void * t76 = gm_l0;
                                            void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
                                            void * t78 = (void *)(((uint8_t *)t77) + 8);
                                            void * t79 = (void *)(((uint8_t *)&F.l5) + 0);
                                            gm_b16_8 t80 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t78, t79);
                                            if (gm_unw.kind) {
                                                goto gm_ll57;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t75 + 0, t80);
                                            void * t81 = gm_l0;
                                            void * t82 = GM_LD(void *, (((uint8_t *)t81) + 0));
                                            void * t83 = (void *)(((uint8_t *)t82) + 8);
                                            void * t84 = (void *)((uint8_t *)&gm_l8);
                                            gm_b16_8 t85 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t83, t84);
                                            if (gm_unw.kind) {
                                                goto gm_ll57;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t75 + 16, t85);
                                            gm_tabb710d9mq2e t86;
                                            gm_f_format_sgnhhmi((void *)&t86, (void *)NULL, cx, NULL, &t75);
                                            if (gm_unw.kind) {
                                                goto gm_ll57;
                                            }
                                            gm_tabb710d9mq2e t87 = t86;
                                            if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 8, gm_srcn0mgmph5u0oo, 199, 34);
                                            gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&t87) + 8));
                                            gm_b16_8 t89 = t88;
                                            gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                                            gm_b0_1 t91 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t73, t74, t90);
                                            if (gm_unw.kind) {
                                                goto gm_ll57;
                                            }
                                        }
                                        {
                                            goto lb53;
                                        }
                                        goto ix66;
ie66:;
ix66:;
                                    }
                                    goto me62;
mn63:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 197, 9);
me62:;
                                }
lc53:;
                                {
                                    uint64_t t93 = gm_l7;
                                    uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                                    uint64_t t96 = r94;
                                    gm_l7 = t96;
                                }
                            }
gm_ll57:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb53; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc53; }
                            }
                            goto gm_ll13;
lb53:;
                        }
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 190, 5);
me18:;
                }
lc9:;
                {
                    uint64_t t98 = gm_l4;
                    uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                    uint64_t t101 = r99;
                    gm_l4 = t101;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_one_handler_euhh29f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_check_one_handler_euhh29f F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* handled */
    gm_tiilmfd16v3fc gm_l2; /* eref */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tiilmfd16v3fc gm_l6; /* x */
    GM_STACK_CHECK(&F, 402u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_l3 = a3;
    {
        {
            gm_b16_8 t3 = gm_l1;
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
                    gm_tiilmfd16v3fc t15 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 24));
                    gm_l6 = t15;
                    {
                        void * t18 = (void *)((uint8_t *)&gm_l6);
                        void * t19 = (void *)((uint8_t *)&gm_l2);
                        int32_t t20 = gm_f_cmp_ref_8ntrqll(t18, t19);
                        uint8_t t21 = ((t20 == ((int32_t)0LL)));
                        if (!t21) goto ie17;
                        {
                            void * t23 = gm_l0;
                            gm_tl874t2atnlh3 t24 = gm_l3;
                            void * t25 = gm_l0;
                            void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
                            void * t27 = (void *)(((uint8_t *)t26) + 8);
                            uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
                            gm_b16_8 t29 = gm_f_effect_name_prpp0ip(t27, t28);
                            gm_tnpifv20irnph t30;
                            GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t29);
                            gm_tabb710d9mq2e t31;
                            gm_f_format_2ommnur((void *)&t31, (void *)NULL, cx, NULL, t30);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_tabb710d9mq2e t32 = t31;
                            if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_srcn0mgmph5u0oo, 145, 59);
                            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
                            gm_b16_8 t34 = t33;
                            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
                            gm_b0_1 t36 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t23, t24, t35);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 144, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t38 = gm_l5;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l5 = t41;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_pending_handler_l4msk6j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tkn42u7na3t81 a2) {
    struct gm_fr_check_pending_handler_l4msk6j F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* hid */
    gm_tkn42u7na3t81 gm_l2; /* with_expect */
    gm_tf5srh4mp4b3i gm_l3; /* found */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    uint64_t gm_l8; /* i */
    uint64_t gm_l12; /* nscopes */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* k */
    void * gm_l19; /* sc */
    uint64_t gm_l20; /* len */
    uint64_t gm_l23; /* nc */
    uint64_t gm_l25; /* nl */
    uint64_t gm_l27; /* nh */
    uint64_t gm_l28; /* performed */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l34; /* k */
    GM_STACK_CHECK(&F, 5731u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_tf5srh4mp4b3i t2;
        memset(&t2, 0, sizeof t2);
        gm_l3 = t2;
    }
    {
        {
            void * t4 = gm_l0;
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 1040) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
            gm_l4 = t7;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l5;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    gm_t9gtoko04lpgj t14;
                    uint64_t t15 = gm_l5;
                    GM_ST(uint64_t, (uint8_t *)&t14 + 0, t15);
                    uint64_t t16 = gm_l5;
                    uint64_t t17 = (uintptr_t)t16;
                    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    memcpy((uint8_t *)&t14 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t17 * 80), 80);
                    gm_t9gtoko04lpgj t19 = t14;
                    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&t19) + 0));
                    gm_l6 = t22;
                    gm_tkhl5gcodu1a6 t23 = GM_LD(gm_tkhl5gcodu1a6, (((uint8_t *)&t19) + 8));
                    F.l7 = t23;
                    {
                        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&F.l7) + 0));
                        uint32_t t27 = gm_l1;
                        uint8_t t28 = ((t26 == t27));
                        if (!t28) goto ie25;
                        {
                            uint64_t t30 = gm_l6;
                            gm_tf5srh4mp4b3i t31;
                            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t31), t30);
                            ((uint8_t *)&t31)[8] = 1;
                            gm_l3 = t31;
                        }
                        {
                            goto lb8;
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 326, 5);
me20:;
                }
lc8:;
                {
                    uint64_t t33 = gm_l5;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l5 = t36;
                }
            }
lb8:;
        }
    }
    {
        gm_tf5srh4mp4b3i t37 = gm_l3;
        gm_tf5srh4mp4b3i t38 = t37;
        uint64_t t39;
        if (((((uint8_t *)&t38))[8] != 0)) {
            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&t38) + 0));
            t39 = t40;
        } else {
            gm_b0_1 t41;
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        gm_l8 = t39;
    }
    {
        void * t43 = gm_l0;
        void * t44 = (void *)(((uint8_t *)t43) + 1040);
        uint64_t t45 = gm_l8;
        gm_f_remove_5pvnsnk((void *)&F.l9, t44, t45);
    }
    {
        gm_f_list_g3vmkan_2((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            void * t47 = gm_l0;
            uint64_t t48 = GM_LD(uintptr_t, ((((uint8_t *)t47) + 576) + 0) + 8);
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 32));
            uint8_t t50 = (t48 > t49);
            if (!t50) goto lb46;
            {
                void * t53 = gm_l0;
                void * t54 = (void *)(((uint8_t *)t53) + 576);
                gm_ta7fvse6v13pf t55;
                gm_f_pop_f3uajlg_2((void *)&t55, t54);
                gm_ta7fvse6v13pf t56 = t55;
                gm_tjhr0bdm69ios t57;
                if (((((uint8_t *)&t56))[384] != 0)) {
                    gm_tjhr0bdm69ios t58 = GM_LD(gm_tjhr0bdm69ios, (((uint8_t *)&t56) + 0));
                    t57 = t58;
                } else {
                    goto lb46;
                }
                F.l11 = t57;
            }
            {
                void * t59 = (void *)((uint8_t *)&F.l10);
                void * t60 = gm_l0;
                gm_tjhr0bdm69ios t61;
                gm_f_swap_frame_4j63sdr((void *)&t61, t60, &(*(gm_tjhr0bdm69ios *)((uint8_t *)&F.l11)));
                gm_tftb1odc739t5 t62 = gm_f_push_8h1r7s6_2(t59, &t61);
                gm_tftb1odc739t5 t63 = t62;
                if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srcn0mgmph5u0oo, 338, 9);
                gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
            }
lc46:;
        }
lb46:;
    }
    {
        uint64_t t65 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 40) + 8);
        gm_l12 = t65;
    }
    {
        gm_f_list_t7903ef((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t66 = (void *)((uint8_t *)&F.l13);
        void * t67 = gm_l0;
        uint64_t t68 = GM_LD(uintptr_t, ((((uint8_t *)t67) + 56) + 0) + 8);
        void * t69 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t67) + 56) + 0)));
        uint64_t t70 = gm_l12;
        uint64_t t71 = (uintptr_t)t70;
        if (t71 > (uintptr_t)t68 || (uintptr_t)t68 > (uintptr_t)t68) gm_trap_slice(t71, (uintptr_t)t68, t68, gm_srcn0mgmph5u0oo, 342, 26);
        gm_b16_8 t72;
        GM_ST(void *, (uint8_t *)&t72, (void *)(((uint8_t *)t69 + t71 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)((uintptr_t)t68 - t71));
        gm_b16_8 t73 = t72;
        gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
        gm_tftb1odc739t5 t75 = gm_f_extend_oe972vj(t66, t74);
        gm_tftb1odc739t5 t76 = t75;
        if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcn0mgmph5u0oo, 342, 5);
        gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
    }
    {
        void * t78 = gm_l0;
        void * t79 = (void *)(((uint8_t *)t78) + 56);
        uint64_t t80 = gm_l12;
        gm_f_truncate_itj4ge7(t79, t80);
    }
    {
        gm_f_list_go2qg0b((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t81 = (void *)((uint8_t *)&F.l14);
        void * t82 = gm_l0;
        uint64_t t83 = GM_LD(uintptr_t, ((((uint8_t *)t82) + 616) + 0) + 8);
        void * t84 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t82) + 616) + 0)));
        uint64_t t85 = gm_l12;
        uint64_t t86 = (uintptr_t)t85;
        if (t86 > (uintptr_t)t83 || (uintptr_t)t83 > (uintptr_t)t83) gm_trap_slice(t86, (uintptr_t)t83, t83, gm_srcn0mgmph5u0oo, 345, 26);
        gm_b16_8 t87;
        GM_ST(void *, (uint8_t *)&t87, (void *)(((uint8_t *)t84 + t86 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)((uintptr_t)t83 - t86));
        gm_b16_8 t88 = t87;
        gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
        gm_tftb1odc739t5 t90 = gm_f_extend_7hs67uj(t81, t89);
        gm_tftb1odc739t5 t91 = t90;
        if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 1, gm_srcn0mgmph5u0oo, 345, 5);
        gm_b0_1 t92 = GM_LD(gm_b0_1, (((uint8_t *)&t91) + 1));
    }
    {
        void * t93 = gm_l0;
        void * t94 = (void *)(((uint8_t *)t93) + 616);
        uint64_t t95 = gm_l12;
        gm_f_truncate_eoppkeo(t94, t95);
    }
    {
        gm_f_list_7mlcud7((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            uint64_t t97 = gm_l12;
            gm_l16 = t97;
        }
        {
            for (;;) {
                uint64_t t99 = gm_l17;
                uint64_t t100 = gm_l16;
                uint8_t t101 = (t99 < t100);
                if (!t101) goto lb98;
                {
                    uint64_t t104 = gm_l17;
                    uint64_t t105 = t104;
                    uint64_t t108 = GM_LD(uint64_t, ((uint8_t *)&t105));
                    gm_l18 = t108;
                    {
                        uint64_t t110 = gm_l18;
                        uint64_t t111 = (uintptr_t)t110;
                        void * t112 = gm_l0;
                        uint64_t t113 = GM_LD(uintptr_t, ((((uint8_t *)t112) + 56) + 0) + 8);
                        if (t111 >= (uintptr_t)(t113)) gm_trap_index(t111, t113, gm_srcn0mgmph5u0oo, 349, 16);
                        void * t114 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t112) + 56) + 0)) + t111 * 40);
                        gm_l19 = t114;
                    }
                    {
                        uint64_t t115 = gm_l18;
                        uint64_t t116 = (uintptr_t)t115;
                        uint64_t t117 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 40) + 8);
                        if (t116 >= (uintptr_t)(t117)) gm_trap_index(t116, t117, gm_srcn0mgmph5u0oo, 350, 16);
                        uint64_t t118 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 40)) + t116 * 8));
                        void * t119 = gm_l19;
                        uint64_t t120 = GM_LD(uintptr_t, (((uint8_t *)t119) + 0) + 8);
                        uint64_t t121 = (t118 < t120 ? t118 : t120);
                        gm_l20 = t121;
                    }
                    {
                        gm_f_list_fs7kvfg((void *)&F.l21, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll102;
                        }
                    }
                    {
                        void * t122 = (void *)((uint8_t *)&F.l21);
                        void * t123 = gm_l19;
                        uint64_t t124 = GM_LD(uintptr_t, (((uint8_t *)t123) + 0) + 8);
                        void * t125 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t123) + 0)));
                        uint64_t t126 = gm_l20;
                        uint64_t t127 = (uintptr_t)t126;
                        if (t127 > (uintptr_t)t124 || (uintptr_t)t124 > (uintptr_t)t124) gm_trap_slice(t127, (uintptr_t)t124, t124, gm_srcn0mgmph5u0oo, 352, 21);
                        gm_b16_8 t128;
                        GM_ST(void *, (uint8_t *)&t128, (void *)(((uint8_t *)t125 + t127 * 224)));
                        GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)((uintptr_t)t124 - t127));
                        gm_b16_8 t129 = t128;
                        gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
                        gm_tftb1odc739t5 t131 = gm_f_extend_5b5kjjl(t122, t130);
                        gm_tftb1odc739t5 t132 = t131;
                        if (((((uint8_t *)&t132))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t132) + 1, gm_srcn0mgmph5u0oo, 352, 9);
                        gm_b0_1 t133 = GM_LD(gm_b0_1, (((uint8_t *)&t132) + 1));
                    }
                    {
                        void * t134 = (void *)((uint8_t *)&F.l15);
                        gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
                        gm_tftb1odc739t5 t136 = gm_f_push_f4l3f1p(t134, t135);
                        gm_tftb1odc739t5 t137 = t136;
                        if (((((uint8_t *)&t137))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t137) + 1, gm_srcn0mgmph5u0oo, 353, 9);
                        gm_b0_1 t138 = GM_LD(gm_b0_1, (((uint8_t *)&t137) + 1));
                    }
                    {
                        void * t139 = gm_l19;
                        uint64_t t140 = gm_l20;
                        gm_f_truncate_36vf02g(t139, t140);
                    }
                    goto me106;
mn107:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 348, 5);
me106:;
                }
lc98:;
                {
                    uint64_t t142 = gm_l17;
                    uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                    uint64_t t145 = r143;
                    gm_l17 = t145;
                }
            }
gm_ll102:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb98; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc98; }
            }
            goto gm_lad;
lb98:;
        }
    }
    {
        gm_f_list_1jabh22((void *)&F.l22, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t146 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 56));
        void * t147 = gm_l0;
        void * t148 = GM_LD(void *, (((uint8_t *)t147) + 0));
        uint64_t t149 = GM_LD(uintptr_t, ((((uint8_t *)t148) + 1568) + 0) + 8);
        uint64_t t150 = (t146 < t149 ? t146 : t149);
        gm_l23 = t150;
    }
    {
        void * t151 = (void *)((uint8_t *)&F.l22);
        void * t152 = gm_l0;
        void * t153 = GM_LD(void *, (((uint8_t *)t152) + 0));
        uint64_t t154 = GM_LD(uintptr_t, ((((uint8_t *)t153) + 1568) + 0) + 8);
        void * t155 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t153) + 1568) + 0)));
        uint64_t t156 = gm_l23;
        uint64_t t157 = (uintptr_t)t156;
        if (t157 > (uintptr_t)t154 || (uintptr_t)t154 > (uintptr_t)t154) gm_trap_slice(t157, (uintptr_t)t154, t154, gm_srcn0mgmph5u0oo, 358, 26);
        gm_b16_8 t158;
        GM_ST(void *, (uint8_t *)&t158, (void *)(((uint8_t *)t155 + t157 * 224)));
        GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)((uintptr_t)t154 - t157));
        gm_b16_8 t159 = t158;
        gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&t159));
        gm_tftb1odc739t5 t161 = gm_f_extend_ehh2367(t151, t160);
        gm_tftb1odc739t5 t162 = t161;
        if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 1, gm_srcn0mgmph5u0oo, 358, 5);
        gm_b0_1 t163 = GM_LD(gm_b0_1, (((uint8_t *)&t162) + 1));
    }
    {
        void * t164 = gm_l0;
        void * t165 = GM_LD(void *, (((uint8_t *)t164) + 0));
        void * t166 = (void *)(((uint8_t *)t165) + 1568);
        uint64_t t167 = gm_l23;
        gm_f_truncate_upeumbt(t166, t167);
    }
    {
        gm_f_list_lgereu6((void *)&F.l24, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 64));
        void * t169 = gm_l0;
        uint64_t t170 = GM_LD(uintptr_t, ((((uint8_t *)t169) + 232) + 0) + 8);
        uint64_t t171 = (t168 < t170 ? t168 : t170);
        gm_l25 = t171;
    }
    {
        void * t172 = (void *)((uint8_t *)&F.l24);
        void * t173 = gm_l0;
        uint64_t t174 = GM_LD(uintptr_t, ((((uint8_t *)t173) + 232) + 0) + 8);
        void * t175 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t173) + 232) + 0)));
        uint64_t t176 = gm_l25;
        uint64_t t177 = (uintptr_t)t176;
        if (t177 > (uintptr_t)t174 || (uintptr_t)t174 > (uintptr_t)t174) gm_trap_slice(t177, (uintptr_t)t174, t174, gm_srcn0mgmph5u0oo, 362, 26);
        gm_b16_8 t178;
        GM_ST(void *, (uint8_t *)&t178, (void *)(((uint8_t *)t175 + t177 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)((uintptr_t)t174 - t177));
        gm_b16_8 t179 = t178;
        gm_b16_8 t180 = GM_LD(gm_b16_8, ((uint8_t *)&t179));
        gm_tftb1odc739t5 t181 = gm_f_extend_717snqd(t172, t180);
        gm_tftb1odc739t5 t182 = t181;
        if (((((uint8_t *)&t182))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t182) + 1, gm_srcn0mgmph5u0oo, 362, 5);
        gm_b0_1 t183 = GM_LD(gm_b0_1, (((uint8_t *)&t182) + 1));
    }
    {
        void * t184 = gm_l0;
        void * t185 = (void *)(((uint8_t *)t184) + 232);
        uint64_t t186 = gm_l25;
        gm_f_truncate_7aasjoi(t185, t186);
    }
    {
        gm_f_list_084fcd6((void *)&F.l26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t187 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 72));
        void * t188 = gm_l0;
        uint64_t t189 = GM_LD(uintptr_t, ((((uint8_t *)t188) + 1000) + 0) + 8);
        uint64_t t190 = (t187 < t189 ? t187 : t189);
        gm_l27 = t190;
    }
    {
        void * t191 = (void *)((uint8_t *)&F.l26);
        void * t192 = gm_l0;
        uint64_t t193 = GM_LD(uintptr_t, ((((uint8_t *)t192) + 1000) + 0) + 8);
        void * t194 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t192) + 1000) + 0)));
        uint64_t t195 = gm_l27;
        uint64_t t196 = (uintptr_t)t195;
        if (t196 > (uintptr_t)t193 || (uintptr_t)t193 > (uintptr_t)t193) gm_trap_slice(t196, (uintptr_t)t193, t193, gm_srcn0mgmph5u0oo, 366, 27);
        gm_b16_8 t197;
        GM_ST(void *, (uint8_t *)&t197, (void *)(((uint8_t *)t194 + t196 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)((uintptr_t)t193 - t196));
        gm_b16_8 t198 = t197;
        gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)&t198));
        gm_tftb1odc739t5 t200 = gm_f_extend_74ac4hb(t191, t199);
        gm_tftb1odc739t5 t201 = t200;
        if (((((uint8_t *)&t201))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t201) + 1, gm_srcn0mgmph5u0oo, 366, 5);
        gm_b0_1 t202 = GM_LD(gm_b0_1, (((uint8_t *)&t201) + 1));
    }
    {
        void * t203 = gm_l0;
        void * t204 = (void *)(((uint8_t *)t203) + 1000);
        uint64_t t205 = gm_l27;
        gm_f_truncate_k0voaim(t204, t205);
    }
    {
        void * t206 = gm_l0;
        uint64_t t207 = GM_LD(uintptr_t, ((((uint8_t *)t206) + 192) + 0) + 8);
        gm_l28 = t207;
    }
    {
        void * t208 = gm_l0;
        uint32_t t209 = gm_l1;
        void * t210 = GM_LD(void *, (((uint8_t *)&F.l9) + 8));
        gm_tkn42u7na3t81 t211 = gm_l2;
        gm_tl874t2atnlh3 t212 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l9) + 16));
        gm_b0_1 t213 = gm_f_handler_clauses_mvttmfg((void *)NULL, cx, NULL, t208, t209, t210, t211, t212);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t215 = gm_l0;
            uint64_t t216 = GM_LD(uintptr_t, ((((uint8_t *)t215) + 192) + 0) + 8);
            void * t217 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t215) + 192) + 0)));
            uint64_t t218 = gm_l28;
            uint64_t t219 = (uintptr_t)t218;
            if (t219 > (uintptr_t)t216 || (uintptr_t)t216 > (uintptr_t)t216) gm_trap_slice(t219, (uintptr_t)t216, t216, gm_srcn0mgmph5u0oo, 370, 14);
            gm_b16_8 t220;
            GM_ST(void *, (uint8_t *)&t220, (void *)(((uint8_t *)t217 + t219 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t220 + 8, (uintptr_t)((uintptr_t)t216 - t219));
            gm_l29 = t220;
        }
        {
            gm_l30 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t222 = gm_l30;
                uint64_t t223 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                uint8_t t224 = (t222 < t223);
                if (!t224) goto lb221;
                {
                    uint64_t t227 = gm_l30;
                    uint64_t t228 = (uintptr_t)t227;
                    uint64_t t229 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                    gm_tj44oivvogvtl t232 = GM_LD(gm_tj44oivvogvtl, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t228 * 40));
                    F.l31 = t232;
                    {
                        void * t234 = gm_l0;
                        gm_tl874t2atnlh3 t235 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l31) + 24));
                        uint64_t t236 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 72));
                        gm_b0_1 t237 = gm_f_shield_2k6eclc((void *)NULL, cx, NULL, t234, t235, t236);
                        if (gm_unw.kind) {
                            goto gm_ll225;
                        }
                    }
                    goto me230;
mn231:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 370, 5);
me230:;
                }
lc221:;
                {
                    uint64_t t239 = gm_l30;
                    uint64_t r240; bool o241 = __builtin_add_overflow(t239, ((uint64_t)1ULL), &r240);
                    uint64_t t242 = r240;
                    gm_l30 = t242;
                }
            }
gm_ll225:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb221; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc221; }
            }
            goto gm_lad;
lb221:;
        }
    }
    {
        void * t243 = gm_l0;
        void * t244 = (void *)(((uint8_t *)t243) + 1000);
        gm_b16_8 t245 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
        gm_b16_8 t246 = t245;
        gm_b16_8 t247 = GM_LD(gm_b16_8, ((uint8_t *)&t246));
        gm_tftb1odc739t5 t248 = gm_f_extend_74ac4hb(t244, t247);
        gm_tftb1odc739t5 t249 = t248;
        if (((((uint8_t *)&t249))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t249) + 1, gm_srcn0mgmph5u0oo, 371, 5);
        gm_b0_1 t250 = GM_LD(gm_b0_1, (((uint8_t *)&t249) + 1));
    }
    {
        void * t251 = gm_l0;
        void * t252 = (void *)(((uint8_t *)t251) + 232);
        gm_b16_8 t253 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
        gm_b16_8 t254 = t253;
        gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
        gm_tftb1odc739t5 t256 = gm_f_extend_717snqd(t252, t255);
        gm_tftb1odc739t5 t257 = t256;
        if (((((uint8_t *)&t257))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t257) + 1, gm_srcn0mgmph5u0oo, 372, 5);
        gm_b0_1 t258 = GM_LD(gm_b0_1, (((uint8_t *)&t257) + 1));
    }
    {
        void * t259 = gm_l0;
        void * t260 = GM_LD(void *, (((uint8_t *)t259) + 0));
        void * t261 = (void *)(((uint8_t *)t260) + 1568);
        gm_b16_8 t262 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
        gm_b16_8 t263 = t262;
        gm_b16_8 t264 = GM_LD(gm_b16_8, ((uint8_t *)&t263));
        gm_tftb1odc739t5 t265 = gm_f_extend_ehh2367(t261, t264);
        gm_tftb1odc739t5 t266 = t265;
        if (((((uint8_t *)&t266))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t266) + 1, gm_srcn0mgmph5u0oo, 373, 5);
        gm_b0_1 t267 = GM_LD(gm_b0_1, (((uint8_t *)&t266) + 1));
    }
    {
        {
            gm_l33 = ((uint64_t)0ULL);
        }
        {
            uint64_t t269 = gm_l12;
            gm_l32 = t269;
        }
        {
            for (;;) {
                uint64_t t271 = gm_l33;
                uint64_t t272 = gm_l32;
                uint8_t t273 = (t271 < t272);
                if (!t273) goto lb270;
                {
                    uint64_t t276 = gm_l33;
                    uint64_t t277 = t276;
                    uint64_t t280 = GM_LD(uint64_t, ((uint8_t *)&t277));
                    gm_l34 = t280;
                    {
                        uint64_t t282 = gm_l34;
                        uint64_t t283 = (uintptr_t)t282;
                        void * t284 = gm_l0;
                        uint64_t t285 = GM_LD(uintptr_t, ((((uint8_t *)t284) + 56) + 0) + 8);
                        if (t283 >= (uintptr_t)(t285)) gm_trap_index(t283, t285, gm_srcn0mgmph5u0oo, 374, 27);
                        void * t286 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t284) + 56) + 0)) + t283 * 40);
                        uint64_t t287 = gm_l34;
                        uint64_t t288 = (uintptr_t)t287;
                        uint64_t t289 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                        if (t288 >= (uintptr_t)(t289)) gm_trap_index(t288, t289, gm_srcn0mgmph5u0oo, 374, 53);
                        gm_b16_8 t290 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t288 * 16));
                        gm_b16_8 t291 = t290;
                        gm_b16_8 t292 = GM_LD(gm_b16_8, ((uint8_t *)&t291));
                        gm_tftb1odc739t5 t293 = gm_f_extend_5b5kjjl(t286, t292);
                        gm_tftb1odc739t5 t294 = t293;
                        if (((((uint8_t *)&t294))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t294) + 1, gm_srcn0mgmph5u0oo, 374, 27);
                        gm_b0_1 t295 = GM_LD(gm_b0_1, (((uint8_t *)&t294) + 1));
                    }
                    goto me278;
mn279:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 374, 5);
me278:;
                }
lc270:;
                {
                    uint64_t t297 = gm_l33;
                    uint64_t r298; bool o299 = __builtin_add_overflow(t297, ((uint64_t)1ULL), &r298);
                    uint64_t t300 = r298;
                    gm_l33 = t300;
                }
            }
lb270:;
        }
    }
    {
        void * t301 = gm_l0;
        void * t302 = (void *)(((uint8_t *)t301) + 56);
        gm_b16_8 t303 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        gm_b16_8 t304 = t303;
        gm_b16_8 t305 = GM_LD(gm_b16_8, ((uint8_t *)&t304));
        gm_tftb1odc739t5 t306 = gm_f_extend_oe972vj(t302, t305);
        gm_tftb1odc739t5 t307 = t306;
        if (((((uint8_t *)&t307))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t307) + 1, gm_srcn0mgmph5u0oo, 375, 5);
        gm_b0_1 t308 = GM_LD(gm_b0_1, (((uint8_t *)&t307) + 1));
    }
    {
        void * t309 = gm_l0;
        void * t310 = (void *)(((uint8_t *)t309) + 616);
        gm_b16_8 t311 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        gm_b16_8 t312 = t311;
        gm_b16_8 t313 = GM_LD(gm_b16_8, ((uint8_t *)&t312));
        gm_tftb1odc739t5 t314 = gm_f_extend_7hs67uj(t310, t313);
        gm_tftb1odc739t5 t315 = t314;
        if (((((uint8_t *)&t315))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t315) + 1, gm_srcn0mgmph5u0oo, 376, 5);
        gm_b0_1 t316 = GM_LD(gm_b0_1, (((uint8_t *)&t315) + 1));
    }
    {
        for (;;) {
            void * t318 = (void *)((uint8_t *)&F.l10);
            gm_ta7fvse6v13pf t319;
            gm_f_pop_f3uajlg_2((void *)&t319, t318);
            gm_ta7fvse6v13pf t320 = t319;
            if (!((((uint8_t *)&t320))[384] != 0)) goto lb317;
            gm_tjhr0bdm69ios t321 = GM_LD(gm_tjhr0bdm69ios, ((uint8_t *)&t320));
            F.l35 = t321;
            {
                void * t324 = gm_l0;
                gm_f_swap_frame_4j63sdr((void *)&F.l36, t324, &(*(gm_tjhr0bdm69ios *)((uint8_t *)&F.l35)));
            }
            {
                void * t325 = gm_l0;
                void * t326 = (void *)(((uint8_t *)t325) + 576);
                gm_tftb1odc739t5 t327 = gm_f_push_8h1r7s6_2(t326, &(*(gm_tjhr0bdm69ios *)((uint8_t *)&F.l36)));
                gm_tftb1odc739t5 t328 = t327;
                if (((((uint8_t *)&t328))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t328) + 1, gm_srcn0mgmph5u0oo, 379, 9);
                gm_b0_1 t329 = GM_LD(gm_b0_1, (((uint8_t *)&t328) + 1));
            }
lc317:;
        }
lb317:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_pending_handlers_o_sunakri(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_pending_handlers_o_sunakri F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* depth */
    uint64_t gm_l2; /* scopes */
    gm_t9805qtonbsag gm_l3; /* hid */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint32_t gm_l7; /* h */
    GM_STACK_CHECK(&F, 352u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 576) + 0) + 8);
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 56) + 0) + 8);
        gm_l2 = t5;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb6;
            {
                gm_t9805qtonbsag t9;
                memset(&t9, 0, sizeof t9);
                gm_l3 = t9;
            }
            {
                {
                    void * t11 = gm_l0;
                    gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)t11) + 1040) + 0));
                    gm_l4 = t12;
                }
                {
                    gm_l5 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t14 = gm_l5;
                        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        uint8_t t16 = (t14 < t15);
                        if (!t16) goto lb13;
                        {
                            uint64_t t19 = gm_l5;
                            uint64_t t20 = (uintptr_t)t19;
                            uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            gm_tkhl5gcodu1a6 t24 = GM_LD(gm_tkhl5gcodu1a6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t20 * 80));
                            F.l6 = t24;
                            {
                                uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 32));
                                uint64_t t28 = gm_l1;
                                uint8_t t29 = ((t27 == t28));
                                if (!t29) goto ie26;
                                uint64_t t30 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 40) + 8);
                                uint64_t t31 = gm_l2;
                                uint8_t t32 = (t30 >= t31);
                                if (!t32) goto ie26;
                                {
                                    uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 0));
                                    gm_t9805qtonbsag t35;
                                    GM_ST(uint8_t, (uint8_t *)&t35 + 5, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t35 + 6, 0);
                                    GM_ST(uint32_t, ((uint8_t *)&t35), t34);
                                    ((uint8_t *)&t35)[4] = 1;
                                    gm_l3 = t35;
                                }
                                {
                                    goto lb13;
                                }
                                goto ix26;
ie26:;
ix26:;
                            }
                            goto me22;
mn23:;
                            gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 401, 9);
me22:;
                        }
lc13:;
                        {
                            uint64_t t37 = gm_l5;
                            uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                            uint64_t t40 = r38;
                            gm_l5 = t40;
                        }
                    }
lb13:;
                }
            }
            {
                gm_t9805qtonbsag t41 = gm_l3;
                gm_t9805qtonbsag t42 = t41;
                uint32_t t43;
                if (((((uint8_t *)&t42))[4] != 0)) {
                    uint32_t t44 = GM_LD(uint32_t, (((uint8_t *)&t42) + 0));
                    t43 = t44;
                } else {
                    gm_b0_1 t45;
                    uint8_t t46 = 0;
                    F.ret = t45;
                    goto gm_fret;
                }
                gm_l7 = t43;
            }
            {
                void * t47 = gm_l0;
                uint32_t t48 = gm_l7;
                gm_tkn42u7na3t81 t49;
                memset(&t49, 0, sizeof t49);
                gm_b0_1 t50 = gm_f_check_pending_handler_l4msk6j((void *)NULL, cx, NULL, t47, t48, t49);
                if (gm_unw.kind) {
                    goto gm_ll7;
                }
            }
lc6:;
        }
gm_ll7:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
        }
        goto gm_lad;
lb6:;
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
inline __attribute__((aligned(4))) void * gm_f_clause_body_93ierdg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_clause_body_93ierdg F;
    void * gm_l0; /* cx */
    void * gm_l1; /* body */
    gm_t9qkh0rsooc8d gm_l2; /* kind */
    gm_t88rcucg1b7uk gm_l3; /* ret */
    GM_STACK_CHECK(&F, 316u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        void * t6 = gm_l1;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t6) + 0));
        gm_t53at80pfdus3 t8;
        gm_f_kind_jcnt4hm((void *)&t8, t5, GM_CONV(gm_csguankko4lmhd, t7));
        gm_t53at80pfdus3 t9 = t8;
        uint8_t t10 = ((GM_LD(uint8_t, ((uint8_t *)&t9) + 0) == ((uint8_t)5ULL)));
        if (!t10) goto ie2;
        {
            void * t12 = gm_l1;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t15 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)2ULL)));
        if (!t15) goto ie14;
        {
            void * t18 = gm_l0;
            void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
            void * t20 = (void *)(((uint8_t *)t19) + 8);
            void * t21 = gm_l1;
            gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t21) + 0));
            uint8_t t23 = gm_f_is_error_8k27gd1(t20, GM_CONV(gm_csguankko4lmhd, t22));
            uint8_t t24 = (uint8_t)!t23;
            if (!t24) goto ie17;
            {
                void * t26 = gm_l0;
                void * t27 = gm_l1;
                gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t27) + 72));
                gm_b16_8 t29;
                GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sfljf7mvjr19m));
                GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(105));
                gm_b0_1 t30 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t26, t28, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix17;
ie17:;
ix17:;
        }
        {
            void * t31 = gm_l1;
            uint8_t t32 = 0;
            F.ret = t31;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t33 = gm_l0;
        void * t34 = gm_l1;
        gm_t88rcucg1b7uk t35 = gm_l3;
        void * t36 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t33, t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) uint32_t gm_f_clause_fn_qrr198q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3, const void * a4) {
    struct gm_fr_clause_fn_qrr198q F;
    void * gm_l0; /* cx */
    void * gm_l1; /* c */
    gm_b16_8 gm_l2; /* params */
    gm_t88rcucg1b7uk gm_l3; /* ret */
    uint32_t gm_l5; /* fn_id */
    gm_b16_8 gm_l6; /* ids */
    void * gm_l7; /* body */
    GM_STACK_CHECK(&F, 1028u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 32) + 8);
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t6 = (!(t4 == t5));
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l1;
            gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t9) + 56));
            gm_t3877pk7lv748 t11;
            void * t12 = gm_l1;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((((uint8_t *)t12) + 0) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 0, t13);
            uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            GM_ST(uint64_t, (uint8_t *)&t11 + 16, t14);
            gm_tabb710d9mq2e t15;
            gm_f_format_2leeume((void *)&t15, (void *)NULL, cx, NULL, &t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t16 = t15;
            if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_srcn0mgmph5u0oo, 423, 26);
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
            gm_b16_8 t18 = t17;
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            gm_b0_1 t20 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t8, t10, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
        gm_t9qkh0rsooc8d t23;
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)2ULL));
        void * t24 = gm_l1;
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((((uint8_t *)t24) + 0) + 0));
        void * t26 = gm_l1;
        gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t26) + 56));
        void * t28 = gm_l0;
        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 8));
        gm_b16_8 t30 = gm_l2;
        gm_t88rcucg1b7uk t31 = gm_l3;
        gm_tnpifv20irnph t32 = gm_f_empty_effects_r0iv3pv();
        uint32_t t33 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t22, t23, t25, t27, t29, t30, t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t33;
    }
    {
        void * t34 = gm_l0;
        uint32_t t35 = gm_l5;
        gm_t88rcucg1b7uk t36 = gm_l3;
        gm_tnpifv20irnph t37 = gm_f_empty_effects_r0iv3pv();
        gm_b0_1 t38 = gm_f_enter_fn_vo5kel1((void *)NULL, cx, NULL, t34, t35, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t39 = gm_l0;
        gm_tnqmlaad6uchl t40 = GM_LD(gm_tnqmlaad6uchl, ((uint8_t *)a4));
        gm_t403eokj7ifg9 t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 113, 0);
        GM_ST(uint16_t, (uint8_t *)&t41 + 114, 0);
        GM_ST(uint32_t, (uint8_t *)&t41 + 116, 0);
        GM_ST(gm_tnqmlaad6uchl, ((uint8_t *)&t41), t40);
        ((uint8_t *)&t41)[112] = 1;
        GM_ST(gm_t403eokj7ifg9, (((uint8_t *)t39) + 800), t41);
    }
    {
        void * t42 = gm_l0;
        void * t43 = gm_l1;
        gm_b16_8 t44 = gm_l2;
        gm_b16_8 t45 = t44;
        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
        gm_b16_8 t47 = gm_f_declare_clause_params_7bqbscg((void *)NULL, cx, NULL, t42, t43, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t47;
    }
    {
        void * t48 = gm_l0;
        void * t49 = gm_l1;
        gm_t9qkh0rsooc8d t50 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a4) + 0));
        gm_t88rcucg1b7uk t51 = gm_l3;
        void * t52 = gm_f_clause_value_tpeb80m((void *)NULL, cx, NULL, t48, t49, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t52;
    }
    {
        void * t53 = gm_l0;
        gm_f_exit_fn_kj6vbu8((void *)&F.l8, t53);
    }
    {
        void * t54 = gm_l0;
        void * t55 = (void *)(((uint8_t *)t54) + 192);
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 16));
        gm_b16_8 t57 = t56;
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
        gm_tftb1odc739t5 t59 = gm_f_extend_43vto3v(t55, t58);
        gm_tftb1odc739t5 t60 = t59;
        if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_srcn0mgmph5u0oo, 431, 5);
        gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
    }
    {
        uint32_t t62 = gm_l5;
        uint64_t t63 = (uint64_t)t62;
        uint64_t t64 = (uintptr_t)t63;
        void * t65 = gm_l0;
        void * t66 = GM_LD(void *, (((uint8_t *)t65) + 0));
        uint64_t t67 = GM_LD(uintptr_t, ((((uint8_t *)t66) + 384) + 0) + 8);
        if (t64 >= (uintptr_t)(t67)) gm_trap_index(t64, t67, gm_srcn0mgmph5u0oo, 432, 5);
        void * t68 = gm_l0;
        gm_t9805qtonbsag t69 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t68) + 400));
        GM_ST(gm_t9805qtonbsag, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t66) + 384) + 0)) + t64 * 64) + 48), t69);
    }
    {
        void * t70 = gm_l0;
        void * t71 = (void *)(((uint8_t *)t70) + 704);
        gm_tl93vjpcesj48 t72;
        memset((uint8_t *)&t72 + 4, 0, 4);
        uint32_t t73 = gm_l5;
        GM_ST(uint32_t, (uint8_t *)&t72 + 0, t73);
        gm_b16_8 t74 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t72 + 8, t74);
        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t72 + 24, t75);
        void * t76 = gm_l7;
        GM_ST(void *, (uint8_t *)&t72 + 40, t76);
        gm_tftb1odc739t5 t77 = gm_f_push_jd7smj6(t71, &t72);
        gm_tftb1odc739t5 t78 = t77;
        if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 1, gm_srcn0mgmph5u0oo, 433, 5);
        gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 1));
    }
    {
        uint32_t t80 = gm_l5;
        uint8_t t81 = 0;
        F.ret = t80;
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
__attribute__((aligned(4))) void gm_f_clause_signature_9v4rkv8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_clause_signature_9v4rkv8 F;
    void * gm_l0; /* s */
    void * gm_l1; /* eref */
    void * gm_l2; /* o */
    gm_b16_8 gm_l3; /* m */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t88rcucg1b7uk gm_l8; /* ret */
    gm_tl874t2atnlh3 gm_l10; /* span */
    GM_STACK_CHECK(&F, 1322u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 0));
        uint64_t t4 = (uint64_t)t3;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 1048) + 0) + 8);
        if (t5 >= (uintptr_t)(t7)) gm_trap_index(t5, t7, gm_srcn0mgmph5u0oo, 468, 21);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 1048) + 0)) + t5 * 56) + 24));
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        void * t11 = gm_l1;
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)t11) + 8));
        gm_b16_8 t13 = gm_f_effect_map_b2f0sd4((void *)NULL, cx, NULL, t10, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t13;
    }
    {
        gm_f_list_af74s0q((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t15 = gm_l2;
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 24));
            gm_l5 = t16;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l6;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l6;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t5v0c8ia6a5mo t28 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t24 * 56));
                    F.l7 = t28;
                    {
                        void * t30 = (void *)((uint8_t *)&F.l4);
                        gm_t5v0c8ia6a5mo t31;
                        memset((uint8_t *)&t31 + 49, 0, 7);
                        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t31 + 0, t32);
                        void * t33 = gm_l0;
                        gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 16));
                        gm_b16_8 t35 = gm_l3;
                        gm_b16_8 t36 = t35;
                        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                        gm_t88rcucg1b7uk t38 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t33, t34, t37);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t31 + 16, t38);
                        gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l7) + 20));
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t31 + 20, t39);
                        GM_ST(void *, (uint8_t *)&t31 + 32, ((void *)0));
                        gm_t9805qtonbsag t40;
                        memset(&t40, 0, sizeof t40);
                        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t31 + 40, t40);
                        GM_ST(uint8_t, (uint8_t *)&t31 + 48, ((uint8_t)0));
                        gm_tftb1odc739t5 t41 = gm_f_push_o8u01sh(t30, &t31);
                        gm_tftb1odc739t5 t42 = t41;
                        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srcn0mgmph5u0oo, 470, 25);
                        gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 470, 5);
me26:;
                }
lc17:;
                {
                    uint64_t t45 = gm_l6;
                    uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                    uint64_t t48 = r46;
                    gm_l6 = t48;
                }
            }
gm_ll21:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
            }
            goto gm_lad;
lb17:;
        }
    }
    {
        void * t49 = gm_l0;
        void * t50 = gm_l2;
        gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t50) + 40));
        gm_b16_8 t52 = gm_l3;
        gm_b16_8 t53 = t52;
        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
        gm_t88rcucg1b7uk t55 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t49, t51, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t55;
    }
    {
        void * t57 = gm_l2;
        uint8_t t58 = (!(GM_LD(uint8_t, (((uint8_t *)t57) + 16) + 0) == ((uint8_t)2ULL)));
        if (!t58) goto ie56;
        {
            memset((uint8_t *)&(*(gm_tep2drsl9408b *)gm_out) + 20, 0, 4);
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tep2drsl9408b *)gm_out) + 0, t60);
            gm_t88rcucg1b7uk t61 = gm_l8;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_tep2drsl9408b *)gm_out) + 16, t61);
            goto gm_fdone;
        }
        goto ix56;
ie56:;
ix56:;
    }
    {
        gm_tl874t2atnlh3 t62;
        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t65 = t64;
        void * t66 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t65)));
        uint64_t t67 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t65) + 8);
        uint64_t t68 = (t67 - 1);
        gm_tj8m6gmlgjilo t69;
        if (t68 < t67) {
        gm_t5v0c8ia6a5mo t70 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)t66 + t68 * 56));
        gm_tj8m6gmlgjilo t71;
        GM_ST(uint8_t, (uint8_t *)&t71 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t71 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t71 + 60, 0);
        GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t71), t70);
        ((uint8_t *)&t71)[56] = 1;
            t69 = t71;
        } else {
        gm_tj8m6gmlgjilo t72;
        memset(&t72, 0, sizeof t72);
            t69 = t72;
        }
        gm_tj8m6gmlgjilo t73 = t69;
        if (!((((uint8_t *)&t73))[56] != 0)) goto ie63;
        gm_t5v0c8ia6a5mo t74 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)&t73));
        F.l9 = t74;
        gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l9) + 20));
        gm_tl874t2atnlh3 t77 = t76;
        t62 = t77;
        goto ix63;
ie63:;
        gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        gm_tl874t2atnlh3 t80 = t79;
        t62 = t80;
ix63:;
        gm_l10 = t62;
    }
    {
        void * t81 = (void *)((uint8_t *)&F.l4);
        gm_t5v0c8ia6a5mo t82;
        memset((uint8_t *)&t82 + 49, 0, 7);
        gm_b16_8 t83;
        GM_ST(void *, (uint8_t *)&t83, (void *)(gm_s0jbakoflqfkm));
        GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(1));
        GM_ST(gm_b16_8, (uint8_t *)&t82 + 0, t83);
        void * t84 = gm_l0;
        void * t85 = (void *)(((uint8_t *)t84) + 8);
        gm_t88rcucg1b7uk t86 = gm_l8;
        gm_t53at80pfdus3 t87;
        memset(&t87, 0, sizeof t87);
        GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)18ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t87 + 8), t86);
        gm_t88rcucg1b7uk t88 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t85, &t87);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t82 + 16, t88);
        gm_tl874t2atnlh3 t89 = gm_l10;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t82 + 20, t89);
        GM_ST(void *, (uint8_t *)&t82 + 32, ((void *)0));
        gm_t9805qtonbsag t90;
        memset(&t90, 0, sizeof t90);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t82 + 40, t90);
        GM_ST(uint8_t, (uint8_t *)&t82 + 48, ((uint8_t)0));
        gm_tftb1odc739t5 t91 = gm_f_push_o8u01sh(t81, &t82);
        gm_tftb1odc739t5 t92 = t91;
        if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_srcn0mgmph5u0oo, 474, 5);
        gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
    }
    {
        memset((uint8_t *)&(*(gm_tep2drsl9408b *)gm_out) + 20, 0, 4);
        gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        void * t95 = gm_l0;
        void * t96 = (void *)(((uint8_t *)t95) + 8);
        gm_t88rcucg1b7uk t97 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tep2drsl9408b *)gm_out) + 0, t94);
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_tep2drsl9408b *)gm_out) + 16, t97);
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
inline __attribute__((aligned(4))) void * gm_f_clause_value_tpeb80m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_clause_value_tpeb80m F;
    void * gm_l0; /* cx */
    void * gm_l1; /* c */
    gm_t9qkh0rsooc8d gm_l2; /* kind */
    gm_t88rcucg1b7uk gm_l3; /* ret */
    gm_tkn42u7na3t81 gm_l4; /* expect */
    void * gm_l6; /* body */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_tkn42u7na3t81 t2;
        uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)2ULL)));
        if (!t4) goto ie3;
        gm_tkn42u7na3t81 t6;
        memset(&t6, 0, sizeof t6);
        gm_tkn42u7na3t81 t7 = t6;
        t2 = t7;
        goto ix3;
ie3:;
        gm_t88rcucg1b7uk t9 = gm_l3;
        gm_t88rcucg1b7uk t10 = t9;
        gm_tkn42u7na3t81 t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[4] = 1;
        t2 = t11;
ix3:;
        gm_l4 = t2;
    }
    {
        void * t12 = gm_l1;
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 48));
        void * t14;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
        if (t17 != ((uint8_t)44ULL)) goto mn16;
        gm_t79rbdv4rafe5 t18 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t13) + 8) + 8));
        F.l5 = t18;
        void * t19 = gm_l0;
        void * t20 = (void *)((uint8_t *)&F.l5);
        gm_tkn42u7na3t81 t21 = gm_l4;
        void * t22 = gm_f_block_expected_bv4ukm2((void *)NULL, cx, NULL, t19, t20, ((uint8_t)1), t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t14 = t22;
        goto me15;
mn16:;
        void * t24 = gm_l0;
        void * t25 = gm_l1;
        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 48));
        gm_tkn42u7na3t81 t27 = gm_l4;
        void * t28 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t24, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t14 = t28;
        goto me15;
mn23:;
        gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 456, 13);
me15:;
        gm_l6 = t14;
    }
    {
        void * t29 = gm_l0;
        void * t30 = gm_l6;
        gm_t9qkh0rsooc8d t31 = gm_l2;
        gm_t88rcucg1b7uk t32 = gm_l3;
        void * t33 = gm_f_clause_body_93ierdg((void *)NULL, cx, NULL, t29, t30, t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t33;
    }
    {
        void * t34 = gm_l0;
        void * t35 = gm_l6;
        void * t36 = gm_f_declare_temps_a7fc14v((void *)NULL, cx, NULL, t34, ((uint64_t)0ULL), t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) void gm_f_cont_operand_i35ab8d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_cont_operand_i35ab8d F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* what */
    void * gm_l2; /* k */
    void * gm_l3; /* v */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_t88rcucg1b7uk gm_l5; /* r */
    GM_STACK_CHECK(&F, 877u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint8_t t4 = gm_f_in_ctl_clause_f2fv82c(t3);
        uint8_t t5 = (uint8_t)!t4;
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = gm_l2;
            gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t8) + 72));
            gm_b16_8 t10 = gm_l1;
            gm_tnpifv20irnph t11;
            GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
            gm_tabb710d9mq2e t12;
            gm_f_format_i0hivvb((void *)&t12, (void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t13 = t12;
            if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_srcn0mgmph5u0oo, 498, 26);
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
            gm_b16_8 t15 = t14;
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            gm_b0_1 t17 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t7, t9, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
        void * t19 = gm_l0;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)t19) + 520));
        if (!t20) goto ie18;
        {
            void * t22 = gm_l0;
            void * t23 = gm_l2;
            gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t23) + 72));
            gm_b16_8 t25 = gm_l1;
            gm_tnpifv20irnph t26;
            GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
            gm_tabb710d9mq2e t27;
            gm_f_format_jtr5odb((void *)&t27, (void *)NULL, cx, NULL, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t28 = t27;
            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_srcn0mgmph5u0oo, 500, 26);
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
            gm_b16_8 t30 = t29;
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
            gm_b0_1 t32 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t22, t24, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix18;
ie18:;
ix18:;
ix2:;
    }
    {
        void * t33 = gm_l0;
        void * t34 = gm_l2;
        gm_tkn42u7na3t81 t35;
        memset(&t35, 0, sizeof t35);
        void * t36 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t33, t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t36;
    }
    {
        void * t37 = gm_l0;
        void * t38 = (void *)(((uint8_t *)t37) + 96);
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
        void * t41 = gm_l3;
        gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t41) + 0));
        gm_t88rcucg1b7uk t43 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t38, t40, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t43;
    }
    {
        void * t44 = gm_l0;
        void * t45 = GM_LD(void *, (((uint8_t *)t44) + 0));
        void * t46 = (void *)(((uint8_t *)t45) + 8);
        gm_t88rcucg1b7uk t47 = gm_l4;
        gm_t53at80pfdus3 t48;
        gm_f_kind_jcnt4hm((void *)&t48, t46, GM_CONV(gm_csguankko4lmhd, t47));
        gm_t53at80pfdus3 t49 = t48;
        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
        switch (t55) { case ((uint8_t)0ULL): goto ma53; case ((uint8_t)1ULL): goto ma53; case ((uint8_t)2ULL): goto ma53; case ((uint8_t)3ULL): goto ma53; case ((uint8_t)4ULL): goto ma53; case ((uint8_t)5ULL): goto ma53; case ((uint8_t)6ULL): goto ma53; case ((uint8_t)7ULL): goto ma53; case ((uint8_t)8ULL): goto ma53; case ((uint8_t)9ULL): goto ma53; case ((uint8_t)10ULL): goto ma53; case ((uint8_t)11ULL): goto ma53; case ((uint8_t)12ULL): goto ma53; case ((uint8_t)13ULL): goto ma53; case ((uint8_t)14ULL): goto ma53; case ((uint8_t)15ULL): goto ma53; case ((uint8_t)16ULL): goto ma53; case ((uint8_t)17ULL): goto ma53; case ((uint8_t)18ULL): goto ma51; case ((uint8_t)19ULL): goto ma53; case ((uint8_t)20ULL): goto ma53; case ((uint8_t)21ULL): goto ma53; case ((uint8_t)22ULL): goto ma53; case ((uint8_t)23ULL): goto ma53; case ((uint8_t)24ULL): goto ma53; case ((uint8_t)25ULL): goto ma53; case ((uint8_t)26ULL): goto ma52; default: goto ma53; }
ma51:;
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
        if (t57 != ((uint8_t)18ULL)) goto mn56;
        gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t49) + 8));
        gm_l5 = t58;
        void * t59 = gm_l3;
        gm_t88rcucg1b7uk t60 = gm_l5;
        gm_tcpfb8f2ne9p9 t61;
        GM_ST(uint32_t, (uint8_t *)&t61 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t61 + 0), t59);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t61 + 8), t60);
        gm_tuqc9k0g5qf5e t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t62 + 20, 0);
        GM_ST(gm_tcpfb8f2ne9p9, ((uint8_t *)&t62), t61);
        ((uint8_t *)&t62)[16] = 1;
        uint8_t t63 = 0;
        F.ret = t62;
        goto gm_fret;
        goto me50;
mn56:;
ma52:;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
        if (t65 != ((uint8_t)26ULL)) goto mn64;
        gm_tuqc9k0g5qf5e t66;
        memset(&t66, 0, sizeof t66);
        uint8_t t67 = 0;
        F.ret = t66;
        goto gm_fret;
        goto me50;
mn64:;
ma53:;
        {
            void * t70 = gm_l0;
            void * t71 = gm_l2;
            gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t71) + 72));
            void * t73 = gm_l0;
            gm_t88rcucg1b7uk t74 = gm_l4;
            gm_b16_8 t75 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t73, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t76;
            GM_ST(gm_b16_8, ((uint8_t *)&t76 + 0), t75);
            gm_tabb710d9mq2e t77;
            gm_f_format_pbrbg1u((void *)&t77, (void *)NULL, cx, NULL, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t78 = t77;
            if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 8, gm_srcn0mgmph5u0oo, 508, 30);
            gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&t78) + 8));
            gm_b16_8 t80 = t79;
            gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
            gm_b0_1 t82 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t70, t72, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tuqc9k0g5qf5e t83;
            memset(&t83, 0, sizeof t83);
            uint8_t t84 = 0;
            F.ret = t83;
            goto gm_fret;
        }
        goto me50;
mn68:;
ma54:;
        gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 504, 5);
me50:;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_declare_clause_params_7bqbscg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_declare_clause_params_7bqbscg F;
    void * gm_l0; /* cx */
    void * gm_l1; /* c */
    gm_b16_8 gm_l2; /* params */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_b16_8 gm_l8; /* name */
    gm_tl874t2atnlh3 gm_l9; /* at */
    uint32_t gm_l10; /* id */
    GM_STACK_CHECK(&F, 989u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_go2qg0b((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
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
                    gm_tgsvmddj3e85e t10;
                    uint64_t t11 = gm_l5;
                    GM_ST(uint64_t, (uint8_t *)&t10 + 0, t11);
                    uint64_t t12 = gm_l5;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    memcpy((uint8_t *)&t10 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t13 * 56), 56);
                    gm_tgsvmddj3e85e t15 = t10;
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
                    gm_l6 = t18;
                    gm_t5v0c8ia6a5mo t19 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t15) + 8));
                    F.l7 = t19;
                    {
                        gm_b16_8 t21;
                        uint64_t t23 = gm_l6;
                        void * t24 = gm_l1;
                        uint64_t t25 = GM_LD(uintptr_t, (((uint8_t *)t24) + 32) + 8);
                        uint8_t t26 = (t23 < t25);
                        if (!t26) goto ie22;
                        uint64_t t28 = gm_l6;
                        uint64_t t29 = (uintptr_t)t28;
                        void * t30 = gm_l1;
                        uint64_t t31 = GM_LD(uintptr_t, (((uint8_t *)t30) + 32) + 8);
                        if (t29 >= (uintptr_t)(t31)) gm_trap_index(t29, t31, gm_srcn0mgmph5u0oo, 442, 39);
                        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)t30) + 32)) + t29 * 32) + 0));
                        gm_b16_8 t33 = t32;
                        t21 = t33;
                        goto ix22;
ie22:;
                        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                        gm_b16_8 t36 = t35;
                        t21 = t36;
ix22:;
                        gm_l8 = t21;
                    }
                    {
                        gm_tl874t2atnlh3 t37;
                        uint64_t t39 = gm_l6;
                        void * t40 = gm_l1;
                        uint64_t t41 = GM_LD(uintptr_t, (((uint8_t *)t40) + 32) + 8);
                        uint8_t t42 = (t39 < t41);
                        if (!t42) goto ie38;
                        uint64_t t44 = gm_l6;
                        uint64_t t45 = (uintptr_t)t44;
                        void * t46 = gm_l1;
                        uint64_t t47 = GM_LD(uintptr_t, (((uint8_t *)t46) + 32) + 8);
                        if (t45 >= (uintptr_t)(t47)) gm_trap_index(t45, t47, gm_srcn0mgmph5u0oo, 443, 37);
                        gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, (((uint8_t *)t46) + 32)) + t45 * 32) + 16));
                        gm_tl874t2atnlh3 t49 = t48;
                        t37 = t49;
                        goto ix38;
ie38:;
                        void * t51 = gm_l1;
                        gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t51) + 56));
                        gm_tl874t2atnlh3 t53 = t52;
                        t37 = t53;
ix38:;
                        gm_l9 = t37;
                    }
                    {
                        void * t54 = gm_l0;
                        gm_b16_8 t55 = gm_l8;
                        gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 16));
                        gm_tl874t2atnlh3 t57 = gm_l9;
                        uint32_t t58 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t54, t55, t56, ((uint8_t)0), t57);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_l10 = t58;
                    }
                    {
                        uint8_t t60 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l8)), gm_sfdbfrv46lihd, 1) == 0));
                        if (!t60) goto ie59;
                        {
                            void * t62 = gm_l0;
                            gm_b16_8 t63 = gm_l8;
                            uint32_t t64 = gm_l10;
                            gm_t930q5l38hdap t65;
                            memset(&t65, 0, sizeof t65);
                            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)0ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t65 + 8), t64);
                            gm_tl874t2atnlh3 t66 = gm_l9;
                            gm_b0_1 t67 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t62, t63, &t65, t66);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto ix59;
ie59:;
ix59:;
                    }
                    {
                        void * t68 = (void *)((uint8_t *)&F.l3);
                        uint32_t t69 = gm_l10;
                        gm_tftb1odc739t5 t70 = gm_f_push_im9d4ro(t68, t69);
                        gm_tftb1odc739t5 t71 = t70;
                        if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srcn0mgmph5u0oo, 446, 9);
                        gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 441, 5);
me16:;
                }
lc4:;
                {
                    uint64_t t74 = gm_l5;
                    uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                    uint64_t t77 = r75;
                    gm_l5 = t77;
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
        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        uint8_t t79 = 0;
        F.ret = t78;
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
__attribute__((aligned(4))) void * gm_f_fiber_intrinsic_os7v2ur(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_fiber_intrinsic_os7v2ur F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* tys */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    void * gm_l9; /* f */
    void * gm_l10; /* fv */
    gm_t88rcucg1b7uk gm_l11; /* ft */
    gm_t88rcucg1b7uk gm_l15; /* a */
    void * gm_l16; /* av */
    gm_t88rcucg1b7uk gm_l17; /* stack_ty */
    void * gm_l18; /* sv */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_tiilmfd16v3fc gm_l22; /* e */
    uint8_t gm_l23; /* own */
    gm_tiilmfd16v3fc gm_l25; /* o */
    gm_tnpifv20irnph gm_l26; /* set */
    GM_STACK_CHECK(&F, 2878u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        uint8_t t6 = 0;
        void * t7 = gm_l0;
        uint8_t t8 = gm_f_in_ctl_clause_f2fv82c(t7);
        uint8_t t9 = (uint8_t)!t8;
        if (!t9) {
            void * t10 = gm_l0;
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t10) + 520));
            t6 = t11;
        } else t6 = 1;
        if (!t6) goto ie5;
        {
            void * t13 = gm_l0;
            gm_tl874t2atnlh3 t14 = gm_l2;
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_smoi1nh7b1b6o));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(64));
            gm_b0_1 t16 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t13, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_f_list_vp1djv1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t18 = gm_l1;
            gm_l5 = t18;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t20 = gm_l6;
                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t22 = (t20 < t21);
                if (!t22) goto lb19;
                {
                    uint64_t t25 = gm_l6;
                    uint64_t t26 = (uintptr_t)t25;
                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t7te2tdpebbt4 t30 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t26 * 144));
                    F.l7 = t30;
                    {
                        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                        if (t33 != ((uint8_t)0ULL)) goto ie32;
                        gm_tc3oht4ksth9d t34 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l7) + 0) + 8));
                        F.l8 = t34;
                        {
                            void * t36 = (void *)((uint8_t *)&F.l4);
                            gm_tftb1odc739t5 t37 = gm_f_push_d943vgp(t36, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l8)));
                            gm_tftb1odc739t5 t38 = t37;
                            if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srcn0mgmph5u0oo, 543, 39);
                            gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
                        }
                        goto ix32;
ie32:;
ix32:;
                    }
                    goto me28;
mn29:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 542, 5);
me28:;
                }
lc19:;
                {
                    uint64_t t41 = gm_l6;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l6 = t44;
                }
            }
lb19:;
        }
    }
    {
        uint64_t t46 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        uint8_t t47 = (!(t46 == ((uint64_t)3ULL)));
        if (!t47) goto ie45;
        {
            void * t49 = gm_l0;
            gm_tl874t2atnlh3 t50 = gm_l2;
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sh5prd89hpoup));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(84));
            gm_b0_1 t52 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t49, t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t53 = gm_l0;
            void * t54 = GM_LD(void *, (((uint8_t *)t53) + 0));
            gm_tl874t2atnlh3 t55 = gm_l2;
            void * t56 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t57 = 0;
            F.ret = t56;
            goto gm_fret;
        }
        goto ix45;
ie45:;
ix45:;
    }
    {
        uint64_t t58 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t59 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        if (t58 >= (uintptr_t)(t59)) gm_trap_index(t58, t59, gm_srcn0mgmph5u0oo, 549, 11);
        void * t60 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t58 * 88);
        gm_l9 = t60;
    }
    {
        void * t61 = gm_l0;
        void * t62 = gm_l9;
        gm_tkn42u7na3t81 t63;
        memset(&t63, 0, sizeof t63);
        void * t64 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t61, t62, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t64;
    }
    {
        void * t65 = gm_l0;
        void * t66 = (void *)(((uint8_t *)t65) + 96);
        void * t67 = gm_l0;
        void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
        void * t69 = gm_l10;
        gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t69) + 0));
        gm_t88rcucg1b7uk t71 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t66, t68, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t71;
    }
    {
        gm_tt775pork2bs8 t72;
        memset(&t72, 0, sizeof t72);
        F.l12 = t72;
    }
    {
        void * t74 = gm_l3;
        gm_t88rcucg1b7uk t75 = gm_l11;
        gm_t53at80pfdus3 t76;
        gm_f_kind_jcnt4hm((void *)&t76, t74, GM_CONV(gm_csguankko4lmhd, t75));
        gm_t53at80pfdus3 t77 = t76;
        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&t77) + 0));
        if (t78 != ((uint8_t)16ULL)) goto ie73;
        gm_tddtjfggdat0d t79 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t77) + 8));
        F.l13 = t79;
        {
            uint64_t t82 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
            uint8_t t83 = ((t82 == ((uint64_t)1ULL)));
            if (!t83) goto ie81;
            void * t84 = gm_l3;
            gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 16));
            gm_t53at80pfdus3 t86;
            gm_f_kind_jcnt4hm((void *)&t86, t84, GM_CONV(gm_csguankko4lmhd, t85));
            gm_t53at80pfdus3 t87 = t86;
            uint8_t t88 = ((GM_LD(uint8_t, ((uint8_t *)&t87) + 0) == ((uint8_t)5ULL)));
            if (!t88) goto ie81;
            uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 40));
            uint8_t t90 = (uint8_t)!t89;
            if (!t90) goto ie81;
            {
                gm_tddtjfggdat0d t92 = F.l13;
                gm_tt775pork2bs8 t93;
                GM_ST(uint8_t, (uint8_t *)&t93 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&t93 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&t93 + 52, 0);
                GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t93), t92);
                ((uint8_t *)&t93)[48] = 1;
                F.l12 = t93;
            }
            goto ix81;
ie81:;
ix81:;
        }
        goto ix73;
ie73:;
ix73:;
    }
    {
        gm_tt775pork2bs8 t94 = F.l12;
        gm_tt775pork2bs8 t95 = t94;
        gm_tddtjfggdat0d t96;
        if (((((uint8_t *)&t95))[48] != 0)) {
            gm_tddtjfggdat0d t97 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t95) + 0));
            t96 = t97;
        } else {
            {
                void * t100 = gm_l3;
                gm_t88rcucg1b7uk t101 = gm_l11;
                uint8_t t102 = gm_f_is_error_8k27gd1(t100, GM_CONV(gm_csguankko4lmhd, t101));
                if (!t102) goto ie99;
                {
                    void * t104 = gm_l0;
                    void * t105 = GM_LD(void *, (((uint8_t *)t104) + 0));
                    gm_tl874t2atnlh3 t106 = gm_l2;
                    void * t107 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t105, t106);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t108 = 0;
                    F.ret = t107;
                    goto gm_fret;
                }
                goto ix99;
ie99:;
ix99:;
            }
            {
                void * t109 = gm_l0;
                void * t110 = gm_l9;
                gm_tl874t2atnlh3 t111 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t110) + 72));
                void * t112 = gm_l0;
                gm_t88rcucg1b7uk t113 = gm_l11;
                gm_b16_8 t114 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t112, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t115;
                GM_ST(gm_b16_8, ((uint8_t *)&t115 + 0), t114);
                gm_tabb710d9mq2e t116;
                gm_f_format_pskrfl8((void *)&t116, (void *)NULL, cx, NULL, t115);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t117 = t116;
                if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 8, gm_srcn0mgmph5u0oo, 558, 26);
                gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
                gm_b16_8 t119 = t118;
                gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
                gm_b0_1 t121 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t109, t111, t120);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t122 = gm_l0;
            void * t123 = GM_LD(void *, (((uint8_t *)t122) + 0));
            gm_tl874t2atnlh3 t124 = gm_l2;
            void * t125 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t123, t124);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t126 = 0;
            F.ret = t125;
            goto gm_fret;
        }
        F.l14 = t96;
    }
    {
        uint64_t t127 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t128 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
        if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcn0mgmph5u0oo, 561, 10);
        gm_t88rcucg1b7uk t129 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)) + t127 * 4));
        gm_l15 = t129;
    }
    {
        void * t131 = gm_l3;
        gm_t88rcucg1b7uk t132 = gm_l15;
        gm_t53at80pfdus3 t133;
        gm_f_kind_jcnt4hm((void *)&t133, t131, GM_CONV(gm_csguankko4lmhd, t132));
        gm_t53at80pfdus3 t134 = t133;
        uint8_t t135;
        uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&t134) + 0));
        switch (t141) { case ((uint8_t)0ULL): goto ma139; case ((uint8_t)1ULL): goto ma139; case ((uint8_t)2ULL): goto ma139; case ((uint8_t)3ULL): goto ma139; case ((uint8_t)4ULL): goto ma139; case ((uint8_t)5ULL): goto ma139; case ((uint8_t)6ULL): goto ma139; case ((uint8_t)7ULL): goto ma139; case ((uint8_t)8ULL): goto ma139; case ((uint8_t)9ULL): goto ma139; case ((uint8_t)10ULL): goto ma139; case ((uint8_t)11ULL): goto ma139; case ((uint8_t)12ULL): goto ma139; case ((uint8_t)13ULL): goto ma139; case ((uint8_t)14ULL): goto ma139; case ((uint8_t)15ULL): goto ma137; case ((uint8_t)16ULL): goto ma139; case ((uint8_t)17ULL): goto ma139; case ((uint8_t)18ULL): goto ma139; case ((uint8_t)19ULL): goto ma139; case ((uint8_t)20ULL): goto ma139; case ((uint8_t)21ULL): goto ma139; case ((uint8_t)22ULL): goto ma139; case ((uint8_t)23ULL): goto ma139; case ((uint8_t)24ULL): goto ma139; case ((uint8_t)25ULL): goto ma138; case ((uint8_t)26ULL): goto ma139; default: goto ma139; }
ma137:;
        uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&t134) + 0));
        if (t143 != ((uint8_t)15ULL)) goto mn142;
        t135 = ((uint8_t)1);
        goto me136;
mn142:;
ma138:;
        uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&t134) + 0));
        if (t145 != ((uint8_t)25ULL)) goto mn144;
        t135 = ((uint8_t)1);
        goto me136;
mn144:;
ma139:;
        t135 = ((uint8_t)0);
        goto me136;
mn146:;
ma140:;
        gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 562, 8);
me136:;
        if (!t135) goto ie130;
        {
            void * t148 = gm_l0;
            uint64_t t149 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t150 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
            if (t149 >= (uintptr_t)(t150)) gm_trap_index(t149, t150, gm_srcn0mgmph5u0oo, 563, 18);
            gm_tl874t2atnlh3 t151 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t149 * 88) + 72));
            void * t152 = gm_l0;
            gm_t88rcucg1b7uk t153 = gm_l15;
            gm_b16_8 t154 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t152, t153);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t155;
            GM_ST(gm_b16_8, ((uint8_t *)&t155 + 0), t154);
            gm_tabb710d9mq2e t156;
            gm_f_format_jb19283((void *)&t156, (void *)NULL, cx, NULL, t155);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t157 = t156;
            if (((((uint8_t *)&t157))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t157) + 8, gm_srcn0mgmph5u0oo, 563, 44);
            gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&t157) + 8));
            gm_b16_8 t159 = t158;
            gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&t159));
            gm_b0_1 t161 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t148, t151, t160);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix130;
ie130:;
ix130:;
    }
    {
        void * t162 = gm_l0;
        void * t163 = gm_l0;
        uint64_t t164 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t165 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        if (t164 >= (uintptr_t)(t165)) gm_trap_index(t164, t165, gm_srcn0mgmph5u0oo, 565, 30);
        void * t166 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t164 * 88);
        gm_t88rcucg1b7uk t167 = gm_l15;
        gm_tkn42u7na3t81 t168;
        GM_ST(uint8_t, (uint8_t *)&t168 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t168 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t168), t167);
        ((uint8_t *)&t168)[4] = 1;
        void * t169 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t163, t166, t168);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t170 = gm_l15;
        void * t171 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t162, t169, t170);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t171;
    }
    {
        void * t172 = gm_l3;
        void * t173 = gm_l3;
        gm_t88rcucg1b7uk t174 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t173, ((uint8_t)0), ((uint16_t)8ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t175 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t172, ((uint8_t)0), t174);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l17 = t175;
    }
    {
        void * t176 = gm_l0;
        void * t177 = gm_l0;
        uint64_t t178 = (uintptr_t)((uint64_t)2ULL);
        uint64_t t179 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        if (t178 >= (uintptr_t)(t179)) gm_trap_index(t178, t179, gm_srcn0mgmph5u0oo, 567, 30);
        void * t180 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t178 * 88);
        gm_t88rcucg1b7uk t181 = gm_l17;
        gm_tkn42u7na3t81 t182;
        GM_ST(uint8_t, (uint8_t *)&t182 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t182 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t182), t181);
        ((uint8_t *)&t182)[4] = 1;
        void * t183 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t177, t180, t182);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t184 = gm_l17;
        void * t185 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t176, t183, t184);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t185;
    }
    {
        gm_f_list_084fcd6((void *)&F.l19, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t187 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l14) + 24) + 0));
            gm_l20 = t187;
        }
        {
            gm_l21 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t189 = gm_l21;
                uint64_t t190 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                uint8_t t191 = (t189 < t190);
                if (!t191) goto lb188;
                {
                    uint64_t t194 = gm_l21;
                    uint64_t t195 = (uintptr_t)t194;
                    uint64_t t196 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    gm_tiilmfd16v3fc t199 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t195 * 24));
                    gm_l22 = t199;
                    {
                        gm_l23 = ((uint8_t)0);
                    }
                    {
                        void * t202 = gm_l0;
                        if (!(((((uint8_t *)t202) + 800))[112] != 0)) goto ie201;
                        gm_tnqmlaad6uchl t203 = GM_LD(gm_tnqmlaad6uchl, (((uint8_t *)t202) + 800));
                        F.l24 = t203;
                        if (!(((((uint8_t *)&F.l24) + 80))[24] != 0)) goto ie201;
                        gm_tiilmfd16v3fc t204 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&F.l24) + 80));
                        gm_l25 = t204;
                        void * t205 = (void *)((uint8_t *)&gm_l25);
                        void * t206 = (void *)((uint8_t *)&gm_l22);
                        int32_t t207 = gm_f_cmp_ref_8ntrqll(t205, t206);
                        uint8_t t208 = ((t207 == ((int32_t)0LL)));
                        if (!t208) goto ie201;
                        {
                            gm_l23 = ((uint8_t)1);
                        }
                        goto ix201;
ie201:;
ix201:;
                    }
                    {
                        uint8_t t211 = gm_l23;
                        uint8_t t212 = (uint8_t)!t211;
                        if (!t212) goto ie210;
                        {
                            void * t214 = (void *)((uint8_t *)&F.l19);
                            gm_tftb1odc739t5 t215 = gm_f_push_d1r4h19(t214, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l22)));
                            gm_tftb1odc739t5 t216 = t215;
                            if (((((uint8_t *)&t216))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t216) + 1, gm_srcn0mgmph5u0oo, 574, 22);
                            gm_b0_1 t217 = GM_LD(gm_b0_1, (((uint8_t *)&t216) + 1));
                        }
                        goto ix210;
ie210:;
ix210:;
                    }
                    goto me197;
mn198:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 571, 5);
me197:;
                }
lc188:;
                {
                    uint64_t t219 = gm_l21;
                    uint64_t r220; bool o221 = __builtin_add_overflow(t219, ((uint64_t)1ULL), &r220);
                    uint64_t t222 = r220;
                    gm_l21 = t222;
                }
            }
lb188:;
        }
    }
    {
        gm_b16_8 t223 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
        gm_b16_8 t224 = t223;
        gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&t224));
        gm_tnpifv20irnph t226 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t225);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l26 = t226;
    }
    {
        void * t227 = gm_l0;
        void * t228 = (void *)((uint8_t *)&gm_l26);
        gm_tl874t2atnlh3 t229 = gm_l2;
        gm_b0_1 t230 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t227, t228, t229);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l27, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t231 = (void *)((uint8_t *)&F.l27);
        void * t232 = gm_l10;
        gm_tftb1odc739t5 t233 = gm_f_push_d943vgp_2(t231, &(*(gm_t69m56meqefs8 *)((uint8_t *)t232)));
        gm_tftb1odc739t5 t234 = t233;
        if (((((uint8_t *)&t234))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t234) + 1, gm_srcn0mgmph5u0oo, 579, 5);
        gm_b0_1 t235 = GM_LD(gm_b0_1, (((uint8_t *)&t234) + 1));
    }
    {
        void * t236 = (void *)((uint8_t *)&F.l27);
        void * t237 = gm_l16;
        gm_tftb1odc739t5 t238 = gm_f_push_d943vgp_2(t236, &(*(gm_t69m56meqefs8 *)((uint8_t *)t237)));
        gm_tftb1odc739t5 t239 = t238;
        if (((((uint8_t *)&t239))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t239) + 1, gm_srcn0mgmph5u0oo, 580, 5);
        gm_b0_1 t240 = GM_LD(gm_b0_1, (((uint8_t *)&t239) + 1));
    }
    {
        void * t241 = (void *)((uint8_t *)&F.l27);
        void * t242 = gm_l18;
        gm_tftb1odc739t5 t243 = gm_f_push_d943vgp_2(t241, &(*(gm_t69m56meqefs8 *)((uint8_t *)t242)));
        gm_tftb1odc739t5 t244 = t243;
        if (((((uint8_t *)&t244))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t244) + 1, gm_srcn0mgmph5u0oo, 581, 5);
        gm_b0_1 t245 = GM_LD(gm_b0_1, (((uint8_t *)&t244) + 1));
    }
    {
        void * t246 = gm_l3;
        void * t247 = gm_l3;
        gm_t88rcucg1b7uk t248 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t247);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t249;
        memset(&t249, 0, sizeof t249);
        GM_ST(uint8_t, ((uint8_t *)&t249 + 0), ((uint8_t)18ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t249 + 8), t248);
        gm_t88rcucg1b7uk t250 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t246, &t249);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tt8o5kebou62r t251;
        memset((uint8_t *)&t251 + 12, 0, 4);
        gm_tvdg94bk7aa22 t252;
        GM_ST(uint8_t, (uint8_t *)&t252 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t252 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t252 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t252 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t252 + 0), ((uint8_t)73ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t251 + 0, t252);
        gm_b16_8 t253 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t251 + 16, t253);
        gm_teti411eujvf4 t254;
        GM_ST(uint8_t, (uint8_t *)&t254 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t254 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t254 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t254 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t254 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t254 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t254 + 0), ((uint8_t)33ULL));
        GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t254 + 8), t251);
        gm_tl874t2atnlh3 t255 = gm_l2;
        void * t256 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t250, &t254, t255);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t257 = 0;
        F.ret = t256;
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
inline __attribute__((aligned(4))) void * gm_f_find_clause_ar0ta3q(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_clause_ar0ta3q F;
    void * gm_l0; /* lit */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 276u);
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
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 8) + 8);
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
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l4 = t15;
                    {
                        uint64_t t18 = gm_l4;
                        uint64_t t19 = (uintptr_t)t18;
                        void * t20 = gm_l0;
                        uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)t20) + 8) + 8);
                        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srcn0mgmph5u0oo, 414, 12);
                        gm_b16_8 t22 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t20) + 8)) + t19 * 72) + 0) + 0));
                        gm_b16_8 t23 = gm_l1;
                        gm_b16_8 t24 = t22;
                        gm_b16_8 t25 = t23;
                        uint8_t t26 = (gm_eq0iqiubfm7e(((uint8_t *)&t24), ((uint8_t *)&t25)));
                        if (!t26) goto ie17;
                        {
                            uint64_t t28 = gm_l4;
                            uint64_t t29 = (uintptr_t)t28;
                            void * t30 = gm_l0;
                            uint64_t t31 = GM_LD(uintptr_t, (((uint8_t *)t30) + 8) + 8);
                            if (t29 >= (uintptr_t)(t31)) gm_trap_index(t29, t31, gm_srcn0mgmph5u0oo, 414, 55);
                            void * t32 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t30) + 8)) + t29 * 72);
                            uint8_t t33 = 0;
                            F.ret = t32;
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 413, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t35 = gm_l3;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l3 = t38;
                }
            }
lb5:;
        }
    }
    {
        uint8_t t39 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_handler_clauses_mvttmfg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_tkn42u7na3t81 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_handler_clauses_mvttmfg F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* hid */
    void * gm_l2; /* lit */
    gm_tkn42u7na3t81 gm_l3; /* with_expect */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_tiilmfd16v3fc gm_l5; /* eref */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    void * gm_l11; /* c */
    gm_tep2drsl9408b gm_l12; /* sig */
    GM_STACK_CHECK(&F, 934u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 504) + 0) + 8);
        if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_srcn0mgmph5u0oo, 298, 13);
        gm_tiilmfd16v3fc t8 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 504) + 0)) + t4 * 40) + 0));
        gm_l5 = t8;
    }
    {
        uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 0));
        uint64_t t10 = (uint64_t)t9;
        uint64_t t11 = (uintptr_t)t10;
        void * t12 = gm_l0;
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 1048) + 0) + 8);
        if (t11 >= (uintptr_t)(t14)) gm_trap_index(t11, t14, gm_srcn0mgmph5u0oo, 299, 13);
        gm_tammvh1iespi3 t15 = GM_LD(gm_tammvh1iespi3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 1048) + 0)) + t11 * 56));
        F.l6 = t15;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 40));
            gm_l8 = t17;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t19 = gm_l9;
                uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t21 = (t19 < t20);
                if (!t21) goto lb18;
                {
                    uint64_t t24 = gm_l9;
                    uint64_t t25 = (uintptr_t)t24;
                    uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_tv0k2vg3054a6 t29 = GM_LD(gm_tv0k2vg3054a6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t25 * 48));
                    F.l10 = t29;
                    {
                        void * t31 = gm_l2;
                        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                        void * t33 = gm_f_find_clause_ar0ta3q(t31, t32);
                        void * t34 = t33;
                        void * t35;
                        if ((GM_LD(void *, ((uint8_t *)&t34)) != 0)) {
                            void * t36 = GM_LD(void *, (((uint8_t *)&t34) + 0));
                            t35 = t36;
                        } else {
                            {
                                void * t38 = gm_l0;
                                gm_tl874t2atnlh3 t39 = gm_l4;
                                gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                                gm_tnpifv20irnph t41;
                                GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t40);
                                gm_tabb710d9mq2e t42;
                                gm_f_format_tv3i6qj((void *)&t42, (void *)NULL, cx, NULL, t41);
                                if (gm_unw.kind) {
                                    goto gm_ll22;
                                }
                                gm_tabb710d9mq2e t43 = t42;
                                if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcn0mgmph5u0oo, 303, 28);
                                gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
                                gm_b16_8 t45 = t44;
                                gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                                gm_b0_1 t47 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t38, t39, t46);
                                if (gm_unw.kind) {
                                    goto gm_ll22;
                                }
                            }
                            goto lc18;
                        }
                        gm_l11 = t35;
                    }
                    {
                        void * t48 = gm_l0;
                        void * t49 = GM_LD(void *, (((uint8_t *)t48) + 0));
                        void * t50 = (void *)((uint8_t *)&gm_l5);
                        void * t51 = (void *)((uint8_t *)&F.l10);
                        gm_f_clause_signature_9v4rkv8((void *)&gm_l12, (void *)NULL, cx, NULL, t49, t50, t51);
                        if (gm_unw.kind) {
                            goto gm_ll22;
                        }
                    }
                    {
                        memset((uint8_t *)&F.l13 + 1, 0, 3);
                        memset((uint8_t *)&F.l13 + 28, 0, 4);
                        gm_t9qkh0rsooc8d t52 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l10) + 16));
                        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l13 + 0, t52);
                        uint32_t t53 = gm_l1;
                        gm_t9805qtonbsag t54;
                        GM_ST(uint8_t, (uint8_t *)&t54 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t54 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t54), t53);
                        ((uint8_t *)&t54)[4] = 1;
                        GM_ST(gm_t9805qtonbsag, (uint8_t *)&F.l13 + 4, t54);
                        gm_t9805qtonbsag t55;
                        memset(&t55, 0, sizeof t55);
                        GM_ST(gm_t9805qtonbsag, (uint8_t *)&F.l13 + 12, t55);
                        gm_tkn42u7na3t81 t56 = gm_l3;
                        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&F.l13 + 20, t56);
                        gm_tjvtgq0p0sehv t57;
                        memset(&t57, 0, sizeof t57);
                        GM_ST(gm_tjvtgq0p0sehv, (uint8_t *)&F.l13 + 32, t57);
                        gm_tkn42u7na3t81 t58;
                        memset(&t58, 0, sizeof t58);
                        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&F.l13 + 72, t58);
                        uint32_t t59 = gm_l1;
                        uint64_t t60 = (uint64_t)t59;
                        uint64_t t61 = (uintptr_t)t60;
                        void * t62 = gm_l0;
                        void * t63 = GM_LD(void *, (((uint8_t *)t62) + 0));
                        uint64_t t64 = GM_LD(uintptr_t, ((((uint8_t *)t63) + 504) + 0) + 8);
                        if (t61 >= (uintptr_t)(t64)) gm_trap_index(t61, t64, gm_srcn0mgmph5u0oo, 307, 115);
                        gm_tiilmfd16v3fc t65 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t63) + 504) + 0)) + t61 * 40) + 0));
                        gm_tqu3m2l3n8mnc t66;
                        GM_ST(uint8_t, (uint8_t *)&t66 + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&t66 + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&t66 + 28, 0);
                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t66), t65);
                        ((uint8_t *)&t66)[24] = 1;
                        GM_ST(gm_tqu3m2l3n8mnc, (uint8_t *)&F.l13 + 80, t66);
                    }
                    {
                        void * t67 = (void *)((uint8_t *)&F.l7);
                        void * t68 = gm_l0;
                        void * t69 = gm_l11;
                        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                        gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l12) + 16));
                        uint32_t t72 = gm_f_clause_fn_qrr198q((void *)NULL, cx, NULL, t68, t69, t70, t71, &(*(gm_tnqmlaad6uchl *)((uint8_t *)&F.l13)));
                        if (gm_unw.kind) {
                            goto gm_ll22;
                        }
                        gm_tftb1odc739t5 t73 = gm_f_push_im9d4ro(t67, t72);
                        gm_tftb1odc739t5 t74 = t73;
                        if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 1, gm_srcn0mgmph5u0oo, 308, 9);
                        gm_b0_1 t75 = GM_LD(gm_b0_1, (((uint8_t *)&t74) + 1));
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 301, 5);
me27:;
                }
lc18:;
                {
                    uint64_t t77 = gm_l9;
                    uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                    uint64_t t80 = r78;
                    gm_l9 = t80;
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
        uint32_t t81 = gm_l1;
        uint64_t t82 = (uint64_t)t81;
        uint64_t t83 = (uintptr_t)t82;
        void * t84 = gm_l0;
        void * t85 = GM_LD(void *, (((uint8_t *)t84) + 0));
        uint64_t t86 = GM_LD(uintptr_t, ((((uint8_t *)t85) + 504) + 0) + 8);
        if (t83 >= (uintptr_t)(t86)) gm_trap_index(t83, t86, gm_srcn0mgmph5u0oo, 310, 5);
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t85) + 504) + 0)) + t83 * 40) + 24), t87);
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
inline __attribute__((aligned(4))) void * gm_f_handler_lit_u1lbff2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_handler_lit_u1lbff2 F;
    void * gm_l0; /* e */
    void * gm_l1; /* inner */
    void * gm_l2; /* lit */
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2;
    uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma4; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma5; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t10 != ((uint8_t)9ULL)) goto mn9;
    void * t11 = GM_LD(void *, ((((uint8_t *)t1) + 8) + 8));
    gm_l1 = t11;
    void * t12 = gm_l1;
    void * t13 = gm_f_handler_lit_u1lbff2(t12);
    t2 = t13;
    goto me3;
mn9:;
ma5:;
    uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t15 != ((uint8_t)37ULL)) goto mn14;
    void * t16 = GM_LD(void *, ((((uint8_t *)t1) + 8) + 8));
    gm_l2 = t16;
    void * t17 = gm_l2;
    t2 = t17;
    goto me3;
mn14:;
ma6:;
    t2 = ((void *)0);
    goto me3;
mn18:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 255, 68);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_handler_literal_avosuha(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_handler_literal_avosuha F;
    void * gm_l0; /* cx */
    void * gm_l1; /* lit */
    gm_tkn42u7na3t81 gm_l2; /* with_expect */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* hid */
    GM_STACK_CHECK(&F, 350u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_t9805qtonbsag t4 = gm_f_new_handler_literal_bv98g00((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t5 = t4;
        uint32_t t6;
        if (((((uint8_t *)&t5))[4] != 0)) {
            uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            void * t8 = gm_l0;
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
            gm_tl874t2atnlh3 t10 = gm_l3;
            void * t11 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        gm_l4 = t6;
    }
    {
        void * t13 = gm_l0;
        uint32_t t14 = gm_l4;
        void * t15 = gm_l1;
        gm_tkn42u7na3t81 t16 = gm_l2;
        gm_tl874t2atnlh3 t17 = gm_l3;
        gm_b0_1 t18 = gm_f_handler_clauses_mvttmfg((void *)NULL, cx, NULL, t13, t14, t15, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = (void *)(((uint8_t *)t20) + 8);
        uint32_t t22 = gm_l4;
        gm_t53at80pfdus3 t23;
        memset(&t23, 0, sizeof t23);
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)25ULL));
        GM_ST(uint32_t, ((uint8_t *)&t23 + 8), t22);
        gm_t88rcucg1b7uk t24 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t21, &t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t25 = gm_l4;
        gm_teti411eujvf4 t26;
        memset(&t26, 0, sizeof t26);
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)28ULL));
        GM_ST(uint32_t, ((uint8_t *)&t26 + 8), t25);
        gm_tl874t2atnlh3 t27 = gm_l3;
        void * t28 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t24, &t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t29 = 0;
        F.ret = t28;
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
extern __attribute__((aligned(4))) uint8_t gm_f_in_ctl_clause_f2fv82c(void * a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_new_handler_literal_bv98g00(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_new_handler_literal_bv98g00 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* lit */
    gm_tiilmfd16v3fc gm_l2; /* eref */
    uint32_t gm_l4; /* hid */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint8_t gm_l8; /* known */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    int32_t gm_l12; /* n */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    GM_STACK_CHECK(&F, 1637u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 8));
        void * t6 = gm_l1;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        gm_tqu3m2l3n8mnc t8;
        gm_f_resolve_effect_ref_bggirs2((void *)&t8, (void *)NULL, cx, NULL, t3, t5, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqu3m2l3n8mnc t9 = t8;
        gm_tiilmfd16v3fc t10;
        if (((((uint8_t *)&t9))[24] != 0)) {
            gm_tiilmfd16v3fc t11 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_t9805qtonbsag t12;
            memset(&t12, 0, sizeof t12);
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l2 = t10;
    }
    {
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        uint64_t t15 = (uint64_t)t14;
        uint64_t t16 = (uintptr_t)t15;
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 1048) + 0) + 8);
        if (t16 >= (uintptr_t)(t19)) gm_trap_index(t16, t19, gm_srcn0mgmph5u0oo, 273, 13);
        gm_tammvh1iespi3 t20 = GM_LD(gm_tammvh1iespi3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 1048) + 0)) + t16 * 56));
        F.l3 = t20;
    }
    {
        uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)&gm_l2) + 8) + 8);
        uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 24) + 8);
        uint8_t t24 = (!(t22 == t23));
        if (!t24) goto ie21;
        {
            void * t26 = gm_l0;
            void * t27 = gm_l1;
            void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
            gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t28) + 72));
            gm_t3877pk7lv748 t30;
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t30 + 0, t31);
            uint64_t t32 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 24) + 8);
            GM_ST(uint64_t, (uint8_t *)&t30 + 16, t32);
            gm_tabb710d9mq2e t33;
            gm_f_format_ql3u2no((void *)&t33, (void *)NULL, cx, NULL, &t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t34 = t33;
            if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srcn0mgmph5u0oo, 275, 36);
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
            gm_b16_8 t36 = t35;
            gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
            gm_b0_1 t38 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t26, t29, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t9805qtonbsag t39;
            memset(&t39, 0, sizeof t39);
            uint8_t t40 = 0;
            F.ret = t39;
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t41 = gm_l0;
        void * t42 = GM_LD(void *, (((uint8_t *)t41) + 0));
        uint64_t t43 = GM_LD(uintptr_t, ((((uint8_t *)t42) + 504) + 0) + 8);
        uint32_t t44 = (uint32_t)t43;
        gm_l4 = t44;
    }
    {
        void * t45 = gm_l0;
        void * t46 = GM_LD(void *, (((uint8_t *)t45) + 0));
        void * t47 = (void *)(((uint8_t *)t46) + 504);
        gm_t1cs2fjnrqap1 t48;
        memcpy((uint8_t *)&t48 + 0, ((uint8_t *)&gm_l2), 24);
        gm_b16_8 t49 = gm_f_empty_slice_d914i5u();
        GM_ST(gm_b16_8, (uint8_t *)&t48 + 24, t49);
        gm_tftb1odc739t5 t50 = gm_f_push_bu0mlkm(t47, &t48);
        gm_tftb1odc739t5 t51 = t50;
        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srcn0mgmph5u0oo, 279, 5);
        gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
    }
    {
        {
            void * t54 = gm_l1;
            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)t54) + 8));
            gm_l5 = t55;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t57 = gm_l6;
                uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t59 = (t57 < t58);
                if (!t59) goto lb56;
                {
                    uint64_t t62 = gm_l6;
                    uint64_t t63 = (uintptr_t)t62;
                    uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t99vmhfllumbh t67 = GM_LD(gm_t99vmhfllumbh, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t63 * 72));
                    F.l7 = t67;
                    {
                        gm_l8 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
                            gm_l9 = t70;
                        }
                        {
                            gm_l10 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t72 = gm_l10;
                                uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                uint8_t t74 = (t72 < t73);
                                if (!t74) goto lb71;
                                {
                                    uint64_t t77 = gm_l10;
                                    uint64_t t78 = (uintptr_t)t77;
                                    uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                    gm_tv0k2vg3054a6 t82 = GM_LD(gm_tv0k2vg3054a6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t78 * 48));
                                    F.l11 = t82;
                                    {
                                        gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                                        gm_b16_8 t86 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l7) + 0) + 0));
                                        gm_b16_8 t87 = t85;
                                        gm_b16_8 t88 = t86;
                                        uint8_t t89 = (gm_eq0iqiubfm7e(((uint8_t *)&t87), ((uint8_t *)&t88)));
                                        if (!t89) goto ie84;
                                        {
                                            gm_l8 = ((uint8_t)1);
                                        }
                                        goto ix84;
ie84:;
ix84:;
                                    }
                                    goto me80;
mn81:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 282, 9);
me80:;
                                }
lc71:;
                                {
                                    uint64_t t92 = gm_l10;
                                    uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                                    uint64_t t95 = r93;
                                    gm_l10 = t95;
                                }
                            }
lb71:;
                        }
                    }
                    {
                        uint8_t t97 = gm_l8;
                        uint8_t t98 = (uint8_t)!t97;
                        if (!t98) goto ie96;
                        {
                            void * t100 = gm_l0;
                            gm_tl874t2atnlh3 t101 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l7) + 0) + 16));
                            gm_t6j6smj7qoctu t102;
                            gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t102 + 0, t103);
                            gm_b16_8 t104 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l7) + 0) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t102 + 16, t104);
                            gm_tabb710d9mq2e t105;
                            gm_f_format_74vh99o((void *)&t105, (void *)NULL, cx, NULL, &t102);
                            if (gm_unw.kind) {
                                goto gm_ll60;
                            }
                            gm_tabb710d9mq2e t106 = t105;
                            if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 8, gm_srcn0mgmph5u0oo, 285, 46);
                            gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&t106) + 8));
                            gm_b16_8 t108 = t107;
                            gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
                            gm_b0_1 t110 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t100, t101, t109);
                            if (gm_unw.kind) {
                                goto gm_ll60;
                            }
                        }
                        goto ix96;
ie96:;
ix96:;
                    }
                    {
                        gm_l12 = ((int32_t)0LL);
                    }
                    {
                        {
                            void * t112 = gm_l1;
                            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)t112) + 8));
                            gm_l13 = t113;
                        }
                        {
                            gm_l14 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t115 = gm_l14;
                                uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                uint8_t t117 = (t115 < t116);
                                if (!t117) goto lb114;
                                {
                                    uint64_t t120 = gm_l14;
                                    uint64_t t121 = (uintptr_t)t120;
                                    uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                    gm_t99vmhfllumbh t125 = GM_LD(gm_t99vmhfllumbh, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t121 * 72));
                                    F.l15 = t125;
                                    {
                                        gm_b16_8 t128 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l15) + 0) + 0));
                                        gm_b16_8 t129 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l7) + 0) + 0));
                                        gm_b16_8 t130 = t128;
                                        gm_b16_8 t131 = t129;
                                        uint8_t t132 = (gm_eq0iqiubfm7e(((uint8_t *)&t130), ((uint8_t *)&t131)));
                                        if (!t132) goto ie127;
                                        {
                                            int32_t t134 = gm_l12;
                                            int32_t r135; bool o136 = __builtin_add_overflow(t134, ((int32_t)1LL), &r135);
                                            if (o136) gm_trap("integer overflow in `+`", gm_srcn0mgmph5u0oo, 288, 45);
                                            int32_t t137 = r135;
                                            gm_l12 = t137;
                                        }
                                        goto ix127;
ie127:;
ix127:;
                                    }
                                    goto me123;
mn124:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 287, 9);
me123:;
                                }
lc114:;
                                {
                                    uint64_t t139 = gm_l14;
                                    uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                                    uint64_t t142 = r140;
                                    gm_l14 = t142;
                                }
                            }
lb114:;
                        }
                    }
                    {
                        int32_t t144 = gm_l12;
                        uint8_t t145 = (t144 > ((int32_t)1LL));
                        if (!t145) goto ie143;
                        {
                            void * t147 = gm_l0;
                            gm_tl874t2atnlh3 t148 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l7) + 0) + 16));
                            gm_b16_8 t149 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l7) + 0) + 0));
                            gm_tnpifv20irnph t150;
                            GM_ST(gm_b16_8, ((uint8_t *)&t150 + 0), t149);
                            gm_tabb710d9mq2e t151;
                            gm_f_format_kvhj8q4((void *)&t151, (void *)NULL, cx, NULL, t150);
                            if (gm_unw.kind) {
                                goto gm_ll60;
                            }
                            gm_tabb710d9mq2e t152 = t151;
                            if (((((uint8_t *)&t152))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t152) + 8, gm_srcn0mgmph5u0oo, 290, 42);
                            gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&t152) + 8));
                            gm_b16_8 t154 = t153;
                            gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&t154));
                            gm_b0_1 t156 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t147, t148, t155);
                            if (gm_unw.kind) {
                                goto gm_ll60;
                            }
                        }
                        goto ix143;
ie143:;
ix143:;
                    }
                    goto me65;
mn66:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 280, 5);
me65:;
                }
lc56:;
                {
                    uint64_t t158 = gm_l6;
                    uint64_t r159; bool o160 = __builtin_add_overflow(t158, ((uint64_t)1ULL), &r159);
                    uint64_t t161 = r159;
                    gm_l6 = t161;
                }
            }
gm_ll60:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb56; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc56; }
            }
            goto gm_lad;
lb56:;
        }
    }
    {
        uint32_t t162 = gm_l4;
        gm_t9805qtonbsag t163;
        GM_ST(uint8_t, (uint8_t *)&t163 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t163 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t163), t162);
        ((uint8_t *)&t163)[4] = 1;
        uint8_t t164 = 0;
        F.ret = t163;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_note_near_misses_hr4e2cd(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_note_near_misses_hr4e2cd F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* start */
    gm_b16_8 gm_l2; /* handled */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l6; /* named */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tiilmfd16v3fc gm_l9; /* x */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_tiilmfd16v3fc gm_l12; /* x */
    GM_STACK_CHECK(&F, 603u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 192) + 0) + 8);
            void * t5 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t3) + 192) + 0)));
            uint64_t t6 = gm_l1;
            uint64_t t7 = (uintptr_t)t6;
            if (t7 > (uintptr_t)t4 || (uintptr_t)t4 > (uintptr_t)t4) gm_trap_slice(t7, (uintptr_t)t4, t4, gm_srcn0mgmph5u0oo, 172, 14);
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t7 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)((uintptr_t)t4 - t7));
            gm_l3 = t8;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l4;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l4;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tj44oivvogvtl t20 = GM_LD(gm_tj44oivvogvtl, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t16 * 40));
                    F.l5 = t20;
                    {
                        gm_l6 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t23 = gm_l2;
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
                                    gm_tiilmfd16v3fc t35 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t31 * 24));
                                    gm_l9 = t35;
                                    {
                                        void * t38 = (void *)((uint8_t *)&gm_l9);
                                        void * t39 = (void *)(((uint8_t *)&F.l5) + 0);
                                        int32_t t40 = gm_f_cmp_ref_8ntrqll(t38, t39);
                                        uint8_t t41 = ((t40 == ((int32_t)0LL)));
                                        if (!t41) goto ie37;
                                        {
                                            gm_l6 = ((uint8_t)1);
                                        }
                                        goto ix37;
ie37:;
ix37:;
                                    }
                                    goto me33;
mn34:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 174, 9);
me33:;
                                }
lc24:;
                                {
                                    uint64_t t44 = gm_l8;
                                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                                    uint64_t t47 = r45;
                                    gm_l8 = t47;
                                }
                            }
lb24:;
                        }
                    }
                    {
                        uint8_t t49 = gm_l6;
                        if (!t49) goto ie48;
                        {
                            goto lc9;
                        }
                        goto ix48;
ie48:;
ix48:;
                    }
                    {
                        {
                            gm_b16_8 t52 = gm_l2;
                            gm_l10 = t52;
                        }
                        {
                            gm_l11 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t54 = gm_l11;
                                uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                uint8_t t56 = (t54 < t55);
                                if (!t56) goto lb53;
                                {
                                    uint64_t t59 = gm_l11;
                                    uint64_t t60 = (uintptr_t)t59;
                                    uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                    gm_tiilmfd16v3fc t64 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t60 * 24));
                                    gm_l12 = t64;
                                    {
                                        uint32_t t67 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 0));
                                        uint32_t t68 = GM_LD(uint32_t, ((((uint8_t *)&F.l5) + 0) + 0));
                                        uint8_t t69 = ((t67 == t68));
                                        if (!t69) goto ie66;
                                        {
                                            void * t71 = gm_l0;
                                            void * t72 = (void *)(((uint8_t *)t71) + 1120);
                                            gm_tj44oivvogvtl t73;
                                            memset((uint8_t *)&t73 + 36, 0, 4);
                                            memcpy((uint8_t *)&t73 + 0, ((uint8_t *)&gm_l12), 24);
                                            gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 24));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t73 + 24, t74);
                                            gm_tftb1odc739t5 t75 = gm_f_push_3onuaru(t72, &t73);
                                            gm_tftb1odc739t5 t76 = t75;
                                            if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcn0mgmph5u0oo, 180, 17);
                                            gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
                                        }
                                        {
                                            goto lb53;
                                        }
                                        goto ix66;
ie66:;
ix66:;
                                    }
                                    goto me62;
mn63:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 178, 9);
me62:;
                                }
lc53:;
                                {
                                    uint64_t t79 = gm_l11;
                                    uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                                    uint64_t t82 = r80;
                                    gm_l11 = t82;
                                }
                            }
lb53:;
                        }
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 172, 5);
me18:;
                }
lc9:;
                {
                    uint64_t t84 = gm_l4;
                    uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                    uint64_t t87 = r85;
                    gm_l4 = t87;
                }
            }
lb9:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_pending_handler_literal_3hqdgg4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_pending_handler_literal_3hqdgg4 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* lit */
    gm_tl874t2atnlh3 gm_l2; /* span */
    uint32_t gm_l3; /* hid */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 679u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_t9805qtonbsag t4 = gm_f_new_handler_literal_bv98g00((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t5 = t4;
        uint32_t t6;
        if (((((uint8_t *)&t5))[4] != 0)) {
            uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            void * t8 = gm_l0;
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
            gm_tl874t2atnlh3 t10 = gm_l2;
            void * t11 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        gm_l3 = t6;
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t14 = gm_l0;
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((((uint8_t *)t14) + 56) + 0));
            gm_l5 = t15;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t17 = gm_l6;
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t19 = (t17 < t18);
                if (!t19) goto lb16;
                {
                    uint64_t t22 = gm_l6;
                    uint64_t t23 = (uintptr_t)t22;
                    uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_trqfvmjtt4sij t27 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t23 * 40));
                    F.l7 = t27;
                    {
                        void * t29 = (void *)((uint8_t *)&F.l4);
                        uint64_t t30 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                        gm_tftb1odc739t5 t31 = gm_f_push_ucgmlo2(t29, t30);
                        gm_tftb1odc739t5 t32 = t31;
                        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srcn0mgmph5u0oo, 317, 33);
                        gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
                    }
                    goto me25;
mn26:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 317, 5);
me25:;
                }
lc16:;
                {
                    uint64_t t35 = gm_l6;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l6 = t38;
                }
            }
lb16:;
        }
    }
    {
        void * t39 = gm_l0;
        void * t40 = (void *)(((uint8_t *)t39) + 1040);
        gm_tkhl5gcodu1a6 t41;
        memset((uint8_t *)&t41 + 4, 0, 4);
        memset((uint8_t *)&t41 + 28, 0, 4);
        uint32_t t42 = gm_l3;
        GM_ST(uint32_t, (uint8_t *)&t41 + 0, t42);
        void * t43 = gm_l1;
        GM_ST(void *, (uint8_t *)&t41 + 8, t43);
        gm_tl874t2atnlh3 t44 = gm_l2;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t41 + 16, t44);
        void * t45 = gm_l0;
        uint64_t t46 = GM_LD(uintptr_t, ((((uint8_t *)t45) + 576) + 0) + 8);
        GM_ST(uint64_t, (uint8_t *)&t41 + 32, t46);
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t41 + 40, t47);
        void * t48 = gm_l0;
        void * t49 = GM_LD(void *, (((uint8_t *)t48) + 0));
        uint64_t t50 = GM_LD(uintptr_t, ((((uint8_t *)t49) + 1568) + 0) + 8);
        GM_ST(uint64_t, (uint8_t *)&t41 + 56, t50);
        void * t51 = gm_l0;
        uint64_t t52 = GM_LD(uintptr_t, ((((uint8_t *)t51) + 232) + 0) + 8);
        GM_ST(uint64_t, (uint8_t *)&t41 + 64, t52);
        void * t53 = gm_l0;
        uint64_t t54 = GM_LD(uintptr_t, ((((uint8_t *)t53) + 1000) + 0) + 8);
        GM_ST(uint64_t, (uint8_t *)&t41 + 72, t54);
        gm_tftb1odc739t5 t55 = gm_f_push_gi2g734(t40, &t41);
        gm_tftb1odc739t5 t56 = t55;
        if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_srcn0mgmph5u0oo, 318, 5);
        gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
    }
    {
        void * t58 = gm_l0;
        void * t59 = GM_LD(void *, (((uint8_t *)t58) + 0));
        void * t60 = (void *)(((uint8_t *)t59) + 8);
        uint32_t t61 = gm_l3;
        gm_t53at80pfdus3 t62;
        memset(&t62, 0, sizeof t62);
        GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)25ULL));
        GM_ST(uint32_t, ((uint8_t *)&t62 + 8), t61);
        gm_t88rcucg1b7uk t63 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t60, &t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t64 = gm_l3;
        gm_teti411eujvf4 t65;
        memset(&t65, 0, sizeof t65);
        GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)28ULL));
        GM_ST(uint32_t, ((uint8_t *)&t65 + 8), t64);
        gm_tl874t2atnlh3 t66 = gm_l2;
        void * t67 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t63, &t65, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t68 = 0;
        F.ret = t67;
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
__attribute__((aligned(4))) void gm_f_remove_handled_performs_uvibbqm(void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3, uint8_t a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_remove_handled_performs_uvibbqm F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* handled */
    uint8_t gm_l4; /* bad_handler */
    uint64_t gm_l5; /* i */
    uint8_t gm_l7; /* handles */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_tiilmfd16v3fc gm_l10; /* x */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_trevnmecc5pjt gm_l13; /* sh */
    GM_STACK_CHECK(&F, 622u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint64_t t2 = gm_l1;
        gm_l5 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = gm_l5;
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 192) + 0) + 8);
            uint8_t t7 = (t4 < t6);
            if (!t7) goto lb3;
            {
                uint64_t t10 = gm_l5;
                uint64_t t11 = (uintptr_t)t10;
                void * t12 = gm_l0;
                uint64_t t13 = GM_LD(uintptr_t, ((((uint8_t *)t12) + 192) + 0) + 8);
                if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srcn0mgmph5u0oo, 212, 14);
                gm_tj44oivvogvtl t14 = GM_LD(gm_tj44oivvogvtl, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t12) + 192) + 0)) + t11 * 40));
                F.l6 = t14;
            }
            {
                uint8_t t15 = gm_l4;
                gm_l7 = t15;
            }
            {
                {
                    gm_b16_8 t17 = gm_l3;
                    gm_l8 = t17;
                }
                {
                    gm_l9 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t19 = gm_l9;
                        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        uint8_t t21 = (t19 < t20);
                        if (!t21) goto lb18;
                        {
                            uint64_t t24 = gm_l9;
                            uint64_t t25 = (uintptr_t)t24;
                            uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                            gm_tiilmfd16v3fc t29 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t25 * 24));
                            gm_l10 = t29;
                            {
                                void * t32 = (void *)((uint8_t *)&gm_l10);
                                void * t33 = (void *)(((uint8_t *)&F.l6) + 0);
                                int32_t t34 = gm_f_cmp_ref_8ntrqll(t32, t33);
                                uint8_t t35 = ((t34 == ((int32_t)0LL)));
                                if (!t35) goto ie31;
                                {
                                    gm_l7 = ((uint8_t)1);
                                }
                                goto ix31;
ie31:;
ix31:;
                            }
                            goto me27;
mn28:;
                            gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 214, 9);
me27:;
                        }
lc18:;
                        {
                            uint64_t t38 = gm_l9;
                            uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                            uint64_t t41 = r39;
                            gm_l9 = t41;
                        }
                    }
lb18:;
                }
            }
            {
                {
                    void * t43 = gm_l0;
                    gm_b16_8 t44 = GM_LD(gm_b16_8, ((((uint8_t *)t43) + 1080) + 0));
                    gm_l11 = t44;
                }
                {
                    gm_l12 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t46 = gm_l12;
                        uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        uint8_t t48 = (t46 < t47);
                        if (!t48) goto lb45;
                        {
                            uint64_t t51 = gm_l12;
                            uint64_t t52 = (uintptr_t)t51;
                            uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                            gm_trevnmecc5pjt t56 = GM_LD(gm_trevnmecc5pjt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t52 * 24));
                            gm_l13 = t56;
                            {
                                gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 0));
                                gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 24));
                                gm_tl874t2atnlh3 t61 = t59;
                                gm_tl874t2atnlh3 t62 = t60;
                                uint8_t t63 = (gm_eqff85ac10o3(((uint8_t *)&t61), ((uint8_t *)&t62)));
                                if (!t63) goto ie58;
                                uint64_t t64 = gm_l2;
                                uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 16));
                                uint8_t t66 = (t64 >= t65);
                                if (!t66) goto ie58;
                                {
                                    gm_l7 = ((uint8_t)0);
                                }
                                goto ix58;
ie58:;
ix58:;
                            }
                            goto me54;
mn55:;
                            gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 218, 9);
me54:;
                        }
lc45:;
                        {
                            uint64_t t69 = gm_l12;
                            uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                            uint64_t t72 = r70;
                            gm_l12 = t72;
                        }
                    }
lb45:;
                }
            }
            {
                uint8_t t74 = gm_l7;
                if (!t74) goto ie73;
                {
                    {
                        void * t77 = gm_l0;
                        void * t78 = (void *)(((uint8_t *)t77) + 192);
                        uint64_t t79 = gm_l5;
                        gm_tj44oivvogvtl t80;
                        gm_f_remove_vrfdutr((void *)&t80, t78, t79);
                    }
                }
                goto ix73;
ie73:;
                {
                    uint64_t t82 = gm_l5;
                    uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                    if (o84) gm_trap("integer overflow in `+`", gm_srcn0mgmph5u0oo, 224, 13);
                    uint64_t t85 = r83;
                    gm_l5 = t85;
                }
ix73:;
            }
lc3:;
        }
lb3:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void * gm_f_resume_expr_l33thg3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_resume_expr_l33thg3 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* k */
    void * gm_l2; /* value */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* v */
    gm_tcpfb8f2ne9p9 gm_l5; /* c */
    void * gm_l6; /* tys */
    void * gm_l7; /* v */
    void * gm_l8; /* x */
    void * gm_l9; /* e */
    GM_STACK_CHECK(&F, 778u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn6sqlran7h3r));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        void * t4 = gm_l1;
        gm_tuqc9k0g5qf5e t5;
        gm_f_cont_operand_i35ab8d((void *)&t5, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tuqc9k0g5qf5e t6 = t5;
        gm_tcpfb8f2ne9p9 t7;
        if (((((uint8_t *)&t6))[16] != 0)) {
            gm_tcpfb8f2ne9p9 t8 = GM_LD(gm_tcpfb8f2ne9p9, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            {
                void * t11 = gm_l2;
                void * t12 = t11;
                if (!(GM_LD(void *, ((uint8_t *)&t12)) != 0)) goto ie10;
                void * t13 = GM_LD(void *, ((uint8_t *)&t12));
                gm_l4 = t13;
                {
                    {
                        void * t16 = gm_l0;
                        void * t17 = gm_l4;
                        gm_tkn42u7na3t81 t18;
                        memset(&t18, 0, sizeof t18);
                        void * t19 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t16, t17, t18);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                }
                goto ix10;
ie10:;
ix10:;
            }
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
            gm_tl874t2atnlh3 t22 = gm_l3;
            void * t23 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        gm_l5 = t7;
    }
    {
        void * t25 = gm_l0;
        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
        void * t27 = (void *)(((uint8_t *)t26) + 8);
        gm_l6 = t27;
    }
    {
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
        gm_tl874t2atnlh3 t30 = gm_l3;
        void * t31 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t31;
    }
    {
        void * t33 = gm_l2;
        void * t34 = t33;
        if (!(GM_LD(void *, ((uint8_t *)&t34)) != 0)) goto ie32;
        void * t35 = GM_LD(void *, ((uint8_t *)&t34));
        gm_l8 = t35;
        {
            void * t37 = gm_l0;
            void * t38 = gm_l8;
            gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
            gm_tkn42u7na3t81 t40;
            GM_ST(uint8_t, (uint8_t *)&t40 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t40 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t40), t39);
            ((uint8_t *)&t40)[4] = 1;
            void * t41 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t37, t38, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t41;
        }
        {
            void * t42 = gm_l0;
            void * t43 = gm_l9;
            gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
            void * t45 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t42, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t45;
        }
        goto ix32;
ie32:;
        void * t47 = gm_l6;
        gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
        uint8_t t49 = gm_f_is_unit_n83ttnb(t47, GM_CONV(gm_csguankko4lmhd, t48));
        uint8_t t50 = (uint8_t)!t49;
        if (!t50) goto ie46;
        void * t51 = gm_l6;
        gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
        uint8_t t53 = gm_f_is_error_8k27gd1(t51, GM_CONV(gm_csguankko4lmhd, t52));
        uint8_t t54 = (uint8_t)!t53;
        if (!t54) goto ie46;
        {
            void * t56 = gm_l0;
            gm_tl874t2atnlh3 t57 = gm_l3;
            void * t58 = gm_l0;
            gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
            gm_b16_8 t60 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t61;
            GM_ST(gm_b16_8, ((uint8_t *)&t61 + 0), t60);
            gm_tabb710d9mq2e t62;
            gm_f_format_mg3bghj((void *)&t62, (void *)NULL, cx, NULL, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t63 = t62;
            if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srcn0mgmph5u0oo, 526, 24);
            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
            gm_b16_8 t65 = t64;
            gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
            gm_b0_1 t67 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t56, t57, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix46;
ie46:;
ix46:;
ix32:;
    }
    {
        void * t68 = gm_l6;
        gm_t88rcucg1b7uk t69 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t70 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
        void * t71 = gm_l7;
        gm_t3mf58gv9kf3n t72;
        GM_ST(void *, ((uint8_t *)&t72 + 0), t70);
        GM_ST(void *, ((uint8_t *)&t72 + 8), t71);
        gm_teti411eujvf4 t73;
        memset(&t73, 0, sizeof t73);
        GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)53ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t73 + 8), t72);
        gm_tl874t2atnlh3 t74 = gm_l3;
        void * t75 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t69, &t73, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t76 = 0;
        F.ret = t75;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_shield_2k6eclc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2) {
    struct gm_fr_shield_2k6eclc F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* at */
    uint64_t gm_l2; /* handled */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 311u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 1080) + 0) + 8);
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
                        uint64_t t18 = gm_l5;
                        uint64_t t19 = (uintptr_t)t18;
                        void * t20 = gm_l0;
                        uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 1080) + 0) + 8);
                        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srcn0mgmph5u0oo, 387, 12);
                        gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 1080) + 0)) + t19 * 24) + 0));
                        gm_tl874t2atnlh3 t23 = gm_l1;
                        gm_tl874t2atnlh3 t24 = t22;
                        gm_tl874t2atnlh3 t25 = t23;
                        uint8_t t26 = (gm_eqff85ac10o3(((uint8_t *)&t24), ((uint8_t *)&t25)));
                        if (!t26) goto ie17;
                        {
                            uint64_t t28 = gm_l5;
                            uint64_t t29 = (uintptr_t)t28;
                            void * t30 = gm_l0;
                            uint64_t t31 = GM_LD(uintptr_t, ((((uint8_t *)t30) + 1080) + 0) + 8);
                            if (t29 >= (uintptr_t)(t31)) gm_trap_index(t29, t31, gm_srcn0mgmph5u0oo, 388, 13);
                            uint64_t t32 = gm_l2;
                            GM_ST(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t30) + 1080) + 0)) + t29 * 24) + 16), t32);
                        }
                        {
                            gm_b0_1 t33;
                            uint8_t t34 = 0;
                            F.ret = t33;
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 386, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t36 = gm_l4;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l4 = t39;
                }
            }
lb5:;
        }
    }
    {
        void * t40 = gm_l0;
        void * t41 = (void *)(((uint8_t *)t40) + 1080);
        gm_trevnmecc5pjt t42;
        memset((uint8_t *)&t42 + 12, 0, 4);
        gm_tl874t2atnlh3 t43 = gm_l1;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t42 + 0, t43);
        uint64_t t44 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t42 + 16, t44);
        gm_tftb1odc739t5 t45 = gm_f_push_8quniiv(t41, &t42);
        gm_tftb1odc739t5 t46 = t45;
        if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_srcn0mgmph5u0oo, 392, 5);
        gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_with_breaks_19nlbuk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint32_t a2) {
    struct gm_fr_with_breaks_19nlbuk F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* literal_ids */
    uint32_t gm_l2; /* label */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint32_t gm_l6; /* id */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_taqu3436kubri gm_l9; /* b */
    uint32_t gm_l10; /* depth */
    uint64_t gm_l11; /* k */
    gm_tgk5j0pob2pqa gm_l12; /* b */
    GM_STACK_CHECK(&F, 566u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_fu87ckm((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
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
                    uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 4));
                    uint32_t t14 = t13;
                    uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)&t14));
                    gm_l6 = t17;
                    {
                        {
                            void * t20 = gm_l0;
                            gm_b16_8 t21 = GM_LD(gm_b16_8, ((((uint8_t *)t20) + 920) + 0));
                            gm_l7 = t21;
                        }
                        {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t23 = gm_l8;
                                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                uint8_t t25 = (t23 < t24);
                                if (!t25) goto lb22;
                                {
                                    uint64_t t28 = gm_l8;
                                    uint64_t t29 = (uintptr_t)t28;
                                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    gm_taqu3436kubri t33 = GM_LD(gm_taqu3436kubri, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t29 * 20));
                                    gm_l9 = t33;
                                    {
                                        uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 0));
                                        uint32_t t37 = gm_l6;
                                        uint8_t t38 = ((t36 == t37));
                                        if (!t38) goto ie35;
                                        {
                                            void * t40 = (void *)((uint8_t *)&F.l3);
                                            gm_tftb1odc739t5 t41 = gm_f_push_26bmlpd(t40, &(*(gm_taqu3436kubri *)((uint8_t *)&gm_l9)));
                                            gm_tftb1odc739t5 t42 = t41;
                                            if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srcn0mgmph5u0oo, 237, 35);
                                            gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
                                        }
                                        goto ix35;
ie35:;
ix35:;
                                    }
                                    goto me31;
mn32:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 236, 9);
me31:;
                                }
lc22:;
                                {
                                    uint64_t t45 = gm_l8;
                                    uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                                    uint64_t t48 = r46;
                                    gm_l8 = t48;
                                }
                            }
lb22:;
                        }
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 234, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t50 = gm_l5;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    uint64_t t53 = r51;
                    gm_l5 = t53;
                }
            }
lb4:;
        }
    }
    {
        void * t54 = gm_l0;
        uint32_t t55 = gm_f_depth_fr7pn6n(t54);
        gm_l10 = t55;
    }
    {
        gm_l11 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t57 = gm_l11;
            void * t58 = gm_l0;
            uint64_t t59 = GM_LD(uintptr_t, ((((uint8_t *)t58) + 432) + 0) + 8);
            uint8_t t60 = (t57 < t59);
            if (!t60) goto lb56;
            {
                uint64_t t63 = gm_l11;
                uint64_t t64 = (uintptr_t)t63;
                void * t65 = gm_l0;
                uint64_t t66 = GM_LD(uintptr_t, ((((uint8_t *)t65) + 432) + 0) + 8);
                if (t64 >= (uintptr_t)(t66)) gm_trap_index(t64, t66, gm_srcn0mgmph5u0oo, 243, 14);
                gm_tgk5j0pob2pqa t67 = GM_LD(gm_tgk5j0pob2pqa, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t65) + 432) + 0)) + t64 * 24));
                gm_l12 = t67;
            }
            {
                uint32_t t69 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 4));
                uint32_t t70 = gm_l2;
                uint8_t t71 = ((t69 == t70));
                if (!t71) goto ie68;
                uint32_t t72 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 0));
                uint32_t t73 = gm_l10;
                uint8_t t74 = ((t72 == t73));
                if (!t74) goto ie68;
                {
                    void * t76 = (void *)((uint8_t *)&F.l3);
                    gm_taqu3436kubri t77;
                    GM_ST(uint32_t, (uint8_t *)&t77 + 0, ((uint32_t)0ULL));
                    gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l12) + 8));
                    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t77 + 4, t78);
                    gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l12) + 12));
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t77 + 8, t79);
                    gm_tftb1odc739t5 t80 = gm_f_push_26bmlpd(t76, &t77);
                    gm_tftb1odc739t5 t81 = t80;
                    if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_srcn0mgmph5u0oo, 245, 13);
                    gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
                }
                {
                    {
                        void * t84 = gm_l0;
                        void * t85 = (void *)(((uint8_t *)t84) + 432);
                        uint64_t t86 = gm_l11;
                        gm_tgk5j0pob2pqa t87;
                        gm_f_remove_v59hrnl((void *)&t87, t85, t86);
                    }
                }
                goto ix68;
ie68:;
                {
                    uint64_t t89 = gm_l11;
                    uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                    if (o91) gm_trap("integer overflow in `+`", gm_srcn0mgmph5u0oo, 248, 13);
                    uint64_t t92 = r90;
                    gm_l11 = t92;
                }
ix68:;
            }
lc56:;
        }
lb56:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l3), 40);
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
__attribute__((aligned(4))) void * gm_f_with_expr_vi78dtj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3) {
    struct gm_fr_with_expr_vi78dtj F;
    void * gm_l0; /* cx */
    void * gm_l1; /* w */
    uint8_t gm_l2; /* value */
    gm_tkn42u7na3t81 gm_l3; /* expect */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_tkn42u7na3t81 gm_l6; /* with_expect */
    gm_t08olq97rscn3 gm_l7; /* l */
    gm_b16_8 gm_l9; /* handled */
    void * gm_l10; /* on */
    gm_th2lqbo77pvsc gm_l11; /* label_name */
    gm_t08olq97rscn3 gm_l12; /* l */
    uint32_t gm_l13; /* label */
    uint64_t gm_l14; /* start */
    uint64_t gm_l15; /* n */
    void * gm_l16; /* body */
    gm_t88rcucg1b7uk gm_l18; /* ty */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_taqu3436kubri gm_l21; /* b */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_taqu3436kubri gm_l24; /* b */
    gm_t88rcucg1b7uk gm_l25; /* bt */
    uint8_t gm_l26; /* agrees */
    uint8_t gm_l27; /* decl_exits */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    uint32_t gm_l30; /* id */
    gm_tiilmfd16v3fc gm_l31; /* e */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_t88rcucg1b7uk gm_l35; /* with_ty */
    GM_STACK_CHECK(&F, 3472u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 104));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_l5 = t6;
    }
    {
        gm_tkn42u7na3t81 t7;
        uint8_t t9 = gm_l2;
        if (!t9) goto ie8;
        gm_tkn42u7na3t81 t11 = gm_l3;
        gm_tkn42u7na3t81 t12 = t11;
        t7 = t12;
        goto ix8;
ie8:;
        gm_tkn42u7na3t81 t14;
        memset(&t14, 0, sizeof t14);
        gm_tkn42u7na3t81 t15 = t14;
        t7 = t15;
ix8:;
        gm_l6 = t7;
    }
    {
        void * t16 = gm_l0;
        gm_b0_1 t17 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t19 = gm_l1;
        if (!(((((uint8_t *)t19) + 0))[32] != 0)) goto ie18;
        gm_t08olq97rscn3 t20 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)t19) + 0));
        gm_l7 = t20;
        {
            void * t22 = gm_l0;
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_t930q5l38hdap t24;
            memset(&t24, 0, sizeof t24);
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)2ULL));
            gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 16));
            gm_b0_1 t26 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t22, t23, &t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t27 = gm_l0;
        void * t28 = gm_l1;
        gm_tkn42u7na3t81 t29 = gm_l6;
        gm_f_with_handlers_2dg7aho((void *)&F.l8, (void *)NULL, cx, NULL, t27, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l8) + 40) + 0));
        gm_l9 = t30;
    }
    {
        void * t31 = gm_l0;
        void * t32 = gm_l1;
        gm_b16_8 t33 = gm_l9;
        gm_b16_8 t34 = t33;
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
        void * t36 = gm_f_with_stack_9rur454((void *)NULL, cx, NULL, t31, t32, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t36;
    }
    {
        gm_th2lqbo77pvsc t37;
        memset(&t37, 0, sizeof t37);
        gm_l11 = t37;
    }
    {
        void * t39 = gm_l1;
        if (!(((((uint8_t *)t39) + 0))[32] != 0)) goto ie38;
        gm_t08olq97rscn3 t40 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)t39) + 0));
        gm_l12 = t40;
        {
            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
            gm_th2lqbo77pvsc t43;
            GM_ST(uint8_t, (uint8_t *)&t43 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t43 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t43 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t43), t42);
            ((uint8_t *)&t43)[16] = 1;
            gm_l11 = t43;
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        void * t44 = gm_l0;
        gm_tkn42u7na3t81 t45 = gm_l6;
        gm_t1qr0p6kln6tf t46;
        GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)2ULL));
        GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t46 + 4), t45);
        uint32_t t47 = gm_f_new_label_3207tcb((void *)NULL, cx, NULL, t44, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l11)), ((uint8_t)0), t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t47;
    }
    {
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, ((((uint8_t *)t48) + 192) + 0) + 8);
        gm_l14 = t49;
    }
    {
        void * t50 = gm_l0;
        uint64_t t51 = GM_LD(uintptr_t, ((((uint8_t *)t50) + 1000) + 0) + 8);
        gm_l15 = t51;
    }
    {
        void * t52 = gm_l0;
        void * t53 = (void *)(((uint8_t *)t52) + 1000);
        gm_b16_8 t54 = gm_l9;
        gm_b16_8 t55 = t54;
        gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
        gm_tftb1odc739t5 t57 = gm_f_extend_74ac4hb(t53, t56);
        gm_tftb1odc739t5 t58 = t57;
        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_srcn0mgmph5u0oo, 21, 5);
        gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
    }
    {
        void * t60 = gm_l0;
        void * t61 = gm_l1;
        void * t62 = (void *)(((uint8_t *)t61) + 64);
        uint8_t t63 = gm_l2;
        gm_tkn42u7na3t81 t64 = gm_l3;
        void * t65 = gm_f_block_expected_bv4ukm2((void *)NULL, cx, NULL, t60, t62, t63, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t65;
    }
    {
        void * t66 = gm_l0;
        void * t67 = (void *)(((uint8_t *)t66) + 1000);
        uint64_t t68 = gm_l15;
        gm_f_truncate_k0voaim(t67, t68);
    }
    {
        {
            void * t70 = gm_l0;
            void * t71 = (void *)(((uint8_t *)t70) + 232);
            gm_te5gbhu3us3k4 t72;
            gm_f_pop_ruvva9t((void *)&t72, t71);
        }
    }
    {
        void * t73 = gm_l0;
        gm_f_pop_scope_g5j7o4e(t73);
    }
    {
        void * t74 = gm_l0;
        uint64_t t75 = gm_l14;
        gm_b16_8 t76 = gm_l9;
        gm_b16_8 t77 = t76;
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
        gm_b0_1 t79 = gm_f_note_near_misses_hr4e2cd((void *)NULL, cx, NULL, t74, t75, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t80 = gm_l0;
        uint64_t t81 = gm_l14;
        gm_b16_8 t82 = gm_l9;
        gm_b16_8 t83 = t82;
        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
        gm_b0_1 t85 = gm_f_check_handled_coincidenc_10aiigb((void *)NULL, cx, NULL, t80, t81, t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t86 = gm_l0;
        uint64_t t87 = gm_l14;
        uint64_t t88 = gm_l15;
        gm_b16_8 t89 = gm_l9;
        gm_b16_8 t90 = t89;
        gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
        uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 160));
        gm_f_remove_handled_performs_uvibbqm(t86, t87, t88, t91, t92);
    }
    {
        void * t93 = gm_l0;
        gm_b16_8 t94 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l8) + 80) + 0));
        gm_b16_8 t95 = t94;
        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
        uint32_t t97 = gm_l13;
        gm_f_with_breaks_19nlbuk((void *)&F.l17, (void *)NULL, cx, NULL, t93, t96, t97);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_t88rcucg1b7uk t98;
        uint8_t t100 = gm_l2;
        if (!t100) goto ie99;
        gm_tkn42u7na3t81 t102 = gm_l3;
        gm_tkn42u7na3t81 t103 = t102;
        gm_t88rcucg1b7uk t104;
        if (((((uint8_t *)&t103))[4] != 0)) {
            gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t103) + 0));
            t104 = t105;
        } else {
            void * t106 = gm_l16;
            gm_t88rcucg1b7uk t107 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t106) + 0));
            t104 = t107;
        }
        gm_t88rcucg1b7uk t108 = t104;
        t98 = t108;
        goto ix99;
ie99:;
        void * t110 = gm_l5;
        gm_t88rcucg1b7uk t111 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t110);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t112 = t111;
        t98 = t112;
ix99:;
        gm_l18 = t98;
    }
    {
        void * t114 = gm_l5;
        gm_t88rcucg1b7uk t115 = gm_l18;
        gm_t53at80pfdus3 t116;
        gm_f_kind_jcnt4hm((void *)&t116, t114, GM_CONV(gm_csguankko4lmhd, t115));
        gm_t53at80pfdus3 t117 = t116;
        uint8_t t118 = ((GM_LD(uint8_t, ((uint8_t *)&t117) + 0) == ((uint8_t)5ULL)));
        if (!t118) goto ie113;
        {
            {
                gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                gm_l19 = t121;
            }
            {
                gm_l20 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t123 = gm_l20;
                    uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    uint8_t t125 = (t123 < t124);
                    if (!t125) goto lb122;
                    {
                        uint64_t t128 = gm_l20;
                        uint64_t t129 = (uintptr_t)t128;
                        uint64_t t130 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                        gm_taqu3436kubri t133 = GM_LD(gm_taqu3436kubri, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t129 * 20));
                        gm_l21 = t133;
                        {
                            void * t136 = gm_l5;
                            gm_t88rcucg1b7uk t137 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l21) + 4));
                            gm_t53at80pfdus3 t138;
                            gm_f_kind_jcnt4hm((void *)&t138, t136, GM_CONV(gm_csguankko4lmhd, t137));
                            gm_t53at80pfdus3 t139 = t138;
                            uint8_t t140 = (!(GM_LD(uint8_t, ((uint8_t *)&t139) + 0) == ((uint8_t)5ULL)));
                            if (!t140) goto ie135;
                            {
                                gm_t88rcucg1b7uk t142 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l21) + 4));
                                gm_l18 = t142;
                            }
                            {
                                goto lb122;
                            }
                            goto ix135;
ie135:;
ix135:;
                        }
                        goto me131;
mn132:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 33, 9);
me131:;
                    }
lc122:;
                    {
                        uint64_t t144 = gm_l20;
                        uint64_t r145; bool o146 = __builtin_add_overflow(t144, ((uint64_t)1ULL), &r145);
                        uint64_t t147 = r145;
                        gm_l20 = t147;
                    }
                }
lb122:;
            }
        }
        goto ix113;
ie113:;
ix113:;
    }
    {
        uint8_t t149 = gm_l2;
        if (!t149) goto ie148;
        void * t150 = gm_l5;
        void * t151 = gm_l16;
        gm_t88rcucg1b7uk t152 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t151) + 0));
        gm_t53at80pfdus3 t153;
        gm_f_kind_jcnt4hm((void *)&t153, t150, GM_CONV(gm_csguankko4lmhd, t152));
        gm_t53at80pfdus3 t154 = t153;
        uint8_t t155 = (!(GM_LD(uint8_t, ((uint8_t *)&t154) + 0) == ((uint8_t)5ULL)));
        if (!t155) goto ie148;
        {
            void * t157 = gm_l0;
            void * t158 = gm_l16;
            gm_t88rcucg1b7uk t159 = gm_l18;
            void * t160 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t157, t158, t159);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t160;
        }
        goto ix148;
ie148:;
ix148:;
    }
    {
        {
            gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
            gm_l22 = t162;
        }
        {
            gm_l23 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t164 = gm_l23;
                uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                uint8_t t166 = (t164 < t165);
                if (!t166) goto lb163;
                {
                    uint64_t t169 = gm_l23;
                    uint64_t t170 = (uintptr_t)t169;
                    uint64_t t171 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                    gm_taqu3436kubri t174 = GM_LD(gm_taqu3436kubri, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t170 * 20));
                    gm_l24 = t174;
                    {
                        void * t176 = gm_l0;
                        void * t177 = (void *)(((uint8_t *)t176) + 96);
                        void * t178 = gm_l0;
                        void * t179 = GM_LD(void *, (((uint8_t *)t178) + 0));
                        gm_t88rcucg1b7uk t180 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l24) + 4));
                        gm_t88rcucg1b7uk t181 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t177, t179, t180);
                        if (gm_unw.kind) {
                            goto gm_ll167;
                        }
                        gm_l25 = t181;
                    }
                    {
                        gm_t88rcucg1b7uk t182 = gm_l25;
                        gm_t88rcucg1b7uk t183 = gm_l18;
                        gm_t88rcucg1b7uk t184 = t182;
                        gm_t88rcucg1b7uk t185 = t183;
                        uint8_t t186 = (gm_eq9m6mvtmgde(((uint8_t *)&t184), ((uint8_t *)&t185)));
                        gm_l26 = t186;
                    }
                    {
                        uint8_t t188 = gm_l26;
                        uint8_t t189 = (uint8_t)!t188;
                        if (!t189) goto ie187;
                        uint8_t t190 = 0;
                        void * t191 = gm_l5;
                        gm_t88rcucg1b7uk t192 = gm_l25;
                        uint8_t t193 = gm_f_has_infer_3o82a0b(t191, GM_CONV(gm_csguankko4lmhd, t192));
                        if (!t193) {
                            void * t194 = gm_l5;
                            gm_t88rcucg1b7uk t195 = gm_l18;
                            uint8_t t196 = gm_f_has_infer_3o82a0b(t194, GM_CONV(gm_csguankko4lmhd, t195));
                            t190 = t196;
                        } else t190 = 1;
                        if (!t190) goto ie187;
                        {
                            void * t198 = gm_l0;
                            void * t199 = (void *)(((uint8_t *)t198) + 96);
                            void * t200 = gm_l5;
                            gm_t88rcucg1b7uk t201 = gm_l25;
                            gm_t88rcucg1b7uk t202 = gm_l18;
                            gm_t9qkh0rsooc8d t203 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t199, t200, t201, t202);
                            if (gm_unw.kind) {
                                goto gm_ll167;
                            }
                            gm_t9qkh0rsooc8d t204 = t203;
                            uint8_t t205;
                            if (!((((uint8_t *)&t204))[0] != 0)) goto mn207;
                            t205 = ((uint8_t)0);
                            goto me206;
mn207:;
                            t205 = ((uint8_t)1);
                            goto me206;
mn208:;
                            gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 44, 79);
me206:;
                            gm_l26 = t205;
                        }
                        goto ix187;
ie187:;
ix187:;
                    }
                    {
                        void * t210 = gm_l5;
                        gm_t88rcucg1b7uk t211 = gm_l25;
                        gm_t53at80pfdus3 t212;
                        gm_f_kind_jcnt4hm((void *)&t212, t210, GM_CONV(gm_csguankko4lmhd, t211));
                        gm_t53at80pfdus3 t213 = t212;
                        uint8_t t214 = (!(GM_LD(uint8_t, ((uint8_t *)&t213) + 0) == ((uint8_t)5ULL)));
                        if (!t214) goto ie209;
                        void * t215 = gm_l5;
                        gm_t88rcucg1b7uk t216 = gm_l25;
                        uint8_t t217 = gm_f_is_error_8k27gd1(t215, GM_CONV(gm_csguankko4lmhd, t216));
                        uint8_t t218 = (uint8_t)!t217;
                        if (!t218) goto ie209;
                        void * t219 = gm_l5;
                        gm_t88rcucg1b7uk t220 = gm_l18;
                        uint8_t t221 = gm_f_is_error_8k27gd1(t219, GM_CONV(gm_csguankko4lmhd, t220));
                        uint8_t t222 = (uint8_t)!t221;
                        if (!t222) goto ie209;
                        uint8_t t223 = gm_l26;
                        uint8_t t224 = (uint8_t)!t223;
                        if (!t224) goto ie209;
                        {
                            void * t226 = gm_l0;
                            gm_tl874t2atnlh3 t227 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l24) + 8));
                            gm_t6j6smj7qoctu t228;
                            void * t229 = gm_l0;
                            gm_t88rcucg1b7uk t230 = gm_l25;
                            gm_b16_8 t231 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t229, t230);
                            if (gm_unw.kind) {
                                goto gm_ll167;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t228 + 0, t231);
                            void * t232 = gm_l0;
                            gm_t88rcucg1b7uk t233 = gm_l18;
                            gm_b16_8 t234 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t232, t233);
                            if (gm_unw.kind) {
                                goto gm_ll167;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t228 + 16, t234);
                            gm_tabb710d9mq2e t235;
                            gm_f_format_fskpepf((void *)&t235, (void *)NULL, cx, NULL, &t228);
                            if (gm_unw.kind) {
                                goto gm_ll167;
                            }
                            gm_tabb710d9mq2e t236 = t235;
                            if (((((uint8_t *)&t236))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t236) + 8, gm_srcn0mgmph5u0oo, 46, 30);
                            gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)&t236) + 8));
                            gm_b16_8 t238 = t237;
                            gm_b16_8 t239 = GM_LD(gm_b16_8, ((uint8_t *)&t238));
                            gm_b0_1 t240 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t226, t227, t239);
                            if (gm_unw.kind) {
                                goto gm_ll167;
                            }
                        }
                        goto ix209;
ie209:;
ix209:;
                    }
                    goto me172;
mn173:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 41, 5);
me172:;
                }
lc163:;
                {
                    uint64_t t242 = gm_l23;
                    uint64_t r243; bool o244 = __builtin_add_overflow(t242, ((uint64_t)1ULL), &r243);
                    uint64_t t245 = r243;
                    gm_l23 = t245;
                }
            }
gm_ll167:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb163; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc163; }
            }
            goto gm_lad;
lb163:;
        }
    }
    {
        gm_l27 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t247 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l8) + 120) + 0));
            gm_l28 = t247;
        }
        {
            gm_l29 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t249 = gm_l29;
                uint64_t t250 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                uint8_t t251 = (t249 < t250);
                if (!t251) goto lb248;
                {
                    uint64_t t254 = gm_l29;
                    uint64_t t255 = (uintptr_t)t254;
                    uint64_t t256 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                    uint32_t t257 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t255 * 4));
                    uint32_t t258 = t257;
                    uint32_t t261 = GM_LD(uint32_t, ((uint8_t *)&t258));
                    gm_l30 = t261;
                    {
                        uint32_t t263 = gm_l30;
                        uint64_t t264 = (uint64_t)t263;
                        uint64_t t265 = (uintptr_t)t264;
                        void * t266 = gm_l0;
                        void * t267 = GM_LD(void *, (((uint8_t *)t266) + 0));
                        uint64_t t268 = GM_LD(uintptr_t, ((((uint8_t *)t267) + 544) + 0) + 8);
                        if (t265 >= (uintptr_t)(t268)) gm_trap_index(t265, t268, gm_srcn0mgmph5u0oo, 54, 14);
                        gm_tiilmfd16v3fc t269 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t267) + 544) + 0)) + t265 * 64) + 0));
                        gm_l31 = t269;
                    }
                    {
                        {
                            uint32_t t271 = GM_LD(uint32_t, (((uint8_t *)&gm_l31) + 0));
                            uint64_t t272 = (uint64_t)t271;
                            uint64_t t273 = (uintptr_t)t272;
                            void * t274 = gm_l0;
                            void * t275 = GM_LD(void *, (((uint8_t *)t274) + 0));
                            uint64_t t276 = GM_LD(uintptr_t, ((((uint8_t *)t275) + 1048) + 0) + 8);
                            if (t273 >= (uintptr_t)(t276)) gm_trap_index(t273, t276, gm_srcn0mgmph5u0oo, 55, 18);
                            gm_b16_8 t277 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t275) + 1048) + 0)) + t273 * 56) + 40));
                            gm_l32 = t277;
                        }
                        {
                            gm_l33 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t279 = gm_l33;
                                uint64_t t280 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                                uint8_t t281 = (t279 < t280);
                                if (!t281) goto lb278;
                                {
                                    uint64_t t284 = gm_l33;
                                    uint64_t t285 = (uintptr_t)t284;
                                    uint64_t t286 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                                    gm_tv0k2vg3054a6 t289 = GM_LD(gm_tv0k2vg3054a6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t285 * 48));
                                    F.l34 = t289;
                                    {
                                        uint8_t t292 = (!(GM_LD(uint8_t, (((uint8_t *)&F.l34) + 16) + 0) == ((uint8_t)0ULL)));
                                        if (!t292) goto ie291;
                                        {
                                            gm_l27 = ((uint8_t)1);
                                        }
                                        goto ix291;
ie291:;
ix291:;
                                    }
                                    goto me287;
mn288:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 55, 9);
me287:;
                                }
lc278:;
                                {
                                    uint64_t t295 = gm_l33;
                                    uint64_t r296; bool o297 = __builtin_add_overflow(t295, ((uint64_t)1ULL), &r296);
                                    uint64_t t298 = r296;
                                    gm_l33 = t298;
                                }
                            }
lb278:;
                        }
                    }
                    goto me259;
mn260:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 53, 5);
me259:;
                }
lc248:;
                {
                    uint64_t t300 = gm_l29;
                    uint64_t r301; bool o302 = __builtin_add_overflow(t300, ((uint64_t)1ULL), &r301);
                    uint64_t t303 = r301;
                    gm_l29 = t303;
                }
            }
lb248:;
        }
    }
    {
        void * t304 = gm_l0;
        void * t305 = (void *)(((uint8_t *)t304) + 96);
        void * t306 = gm_l0;
        void * t307 = GM_LD(void *, (((uint8_t *)t306) + 0));
        gm_t88rcucg1b7uk t308 = gm_l18;
        gm_t88rcucg1b7uk t309 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t305, t307, t308);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l35 = t309;
    }
    {
        uint64_t t311 = GM_LD(uintptr_t, ((((uint8_t *)&F.l8) + 120) + 0) + 8);
        uint8_t t312 = (t311 > ((uint64_t)0ULL));
        if (!t312) goto ie310;
        void * t313 = gm_l5;
        gm_t88rcucg1b7uk t314 = gm_l35;
        uint8_t t315 = gm_f_has_infer_3o82a0b(t313, GM_CONV(gm_csguankko4lmhd, t314));
        uint8_t t316 = (uint8_t)!t315;
        if (!t316) goto ie310;
        void * t317 = gm_l5;
        gm_t88rcucg1b7uk t318 = gm_l35;
        uint8_t t319 = gm_f_is_error_8k27gd1(t317, GM_CONV(gm_csguankko4lmhd, t318));
        uint8_t t320 = (uint8_t)!t319;
        if (!t320) goto ie310;
        {
            void * t322 = gm_l0;
            void * t323 = GM_LD(void *, (((uint8_t *)t322) + 0));
            void * t324 = (void *)(((uint8_t *)t323) + 760);
            gm_tekl8c9g34a9k t325;
            gm_b16_8 t326 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l8) + 120) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t325 + 0, t326);
            gm_t88rcucg1b7uk t327 = gm_l35;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t325 + 16, t327);
            gm_tl874t2atnlh3 t328 = gm_l4;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t325 + 20, t328);
            gm_tftb1odc739t5 t329 = gm_f_push_v83fn9o(t324, &t325);
            gm_tftb1odc739t5 t330 = t329;
            if (((((uint8_t *)&t330))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t330) + 1, gm_srcn0mgmph5u0oo, 63, 9);
            gm_b0_1 t331 = GM_LD(gm_b0_1, (((uint8_t *)&t330) + 1));
        }
        goto ix310;
ie310:;
ix310:;
    }
    {
        uint8_t t333 = gm_l2;
        uint8_t t334 = (uint8_t)!t333;
        if (!t334) goto ie332;
        void * t335 = gm_l5;
        void * t336 = gm_l16;
        gm_t88rcucg1b7uk t337 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t336) + 0));
        gm_t53at80pfdus3 t338;
        gm_f_kind_jcnt4hm((void *)&t338, t335, GM_CONV(gm_csguankko4lmhd, t337));
        gm_t53at80pfdus3 t339 = t338;
        uint8_t t340 = ((GM_LD(uint8_t, ((uint8_t *)&t339) + 0) == ((uint8_t)5ULL)));
        if (!t340) goto ie332;
        uint64_t t341 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 0) + 8);
        uint8_t t342 = ((t341 == ((uint64_t)0ULL)));
        if (!t342) goto ie332;
        uint8_t t343 = gm_l27;
        uint8_t t344 = (uint8_t)!t343;
        if (!t344) goto ie332;
        {
            void * t346 = gm_l5;
            gm_t88rcucg1b7uk t347 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t346);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t347;
        }
        goto ix332;
ie332:;
ix332:;
    }
    {
        gm_t88rcucg1b7uk t348 = gm_l18;
        gm_t710lsrq1m8gk t349;
        memset((uint8_t *)&t349 + 4, 0, 4);
        uint32_t t350 = gm_l13;
        GM_ST(uint32_t, (uint8_t *)&t349 + 0, t350);
        gm_b16_8 t351 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l8) + 0) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t349 + 8, t351);
        void * t352 = gm_l10;
        GM_ST(void *, (uint8_t *)&t349 + 24, t352);
        void * t353 = gm_l16;
        GM_ST(void *, (uint8_t *)&t349 + 32, t353);
        gm_teti411eujvf4 t354;
        GM_ST(uint8_t, (uint8_t *)&t354 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t354 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t354 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t354 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t354 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t354 + 0), ((uint8_t)58ULL));
        GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t354 + 8), t349);
        gm_tl874t2atnlh3 t355 = gm_l4;
        void * t356 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t348, &t354, t355);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t357 = 0;
        F.ret = t356;
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
__attribute__((aligned(4))) void gm_f_with_handlers_2dg7aho(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2) {
    struct gm_fr_with_handlers_2dg7aho F;
    void * gm_l0; /* cx */
    void * gm_l1; /* w */
    gm_tkn42u7na3t81 gm_l2; /* with_expect */
    void * gm_l3; /* tys */
    uint8_t gm_l8; /* bad */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l12; /* earlier */
    void * gm_l13; /* lit */
    void * gm_l14; /* v */
    gm_t88rcucg1b7uk gm_l15; /* t */
    uint32_t gm_l16; /* id */
    gm_tiilmfd16v3fc gm_l17; /* eref */
    gm_b16_8 gm_l18; /* decls */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint32_t gm_l22; /* id */
    gm_tiilmfd16v3fc gm_l24; /* eref */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    gm_tiilmfd16v3fc gm_l28; /* e */
    uint8_t gm_l29; /* seen */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    gm_tiilmfd16v3fc gm_l32; /* x */
    gm_tnpifv20irnph gm_l33; /* set */
    GM_STACK_CHECK(&F, 2366u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        gm_f_list_p0pjtrh((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_084fcd6((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_go2qg0b((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_go2qg0b((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l8 = ((uint8_t)0);
    }
    {
        {
            void * t6 = gm_l1;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t6) + 40));
            gm_l9 = t7;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l10;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l10;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    gm_tc3oht4ksth9d t19 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t15 * 88));
                    F.l11 = t19;
                    {
                        uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                        gm_l12 = t21;
                    }
                    {
                        void * t22;
                        void * t24 = (void *)((uint8_t *)&F.l11);
                        void * t25 = gm_f_handler_lit_u1lbff2(t24);
                        void * t26 = t25;
                        if (!(GM_LD(void *, ((uint8_t *)&t26)) != 0)) goto ie23;
                        void * t27 = GM_LD(void *, ((uint8_t *)&t26));
                        gm_l13 = t27;
                        void * t29 = gm_l0;
                        void * t30 = gm_l13;
                        gm_tkn42u7na3t81 t31 = gm_l2;
                        gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l11) + 72));
                        void * t33 = gm_f_handler_literal_avosuha((void *)NULL, cx, NULL, t29, t30, t31, t32);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                        void * t34 = t33;
                        t22 = t34;
                        goto ix23;
ie23:;
                        void * t36 = gm_l0;
                        void * t37 = (void *)((uint8_t *)&F.l11);
                        gm_tkn42u7na3t81 t38;
                        memset(&t38, 0, sizeof t38);
                        void * t39 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t36, t37, t38);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                        void * t40 = t39;
                        t22 = t40;
ix23:;
                        gm_l14 = t22;
                    }
                    {
                        void * t41 = gm_l0;
                        void * t42 = gm_l14;
                        gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t42) + 0));
                        gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l11) + 72));
                        gm_t88rcucg1b7uk t45 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t41, t43, t44);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                        gm_l15 = t45;
                    }
                    {
                        void * t46 = gm_l3;
                        gm_t88rcucg1b7uk t47 = gm_l15;
                        gm_t53at80pfdus3 t48;
                        gm_f_kind_jcnt4hm((void *)&t48, t46, GM_CONV(gm_csguankko4lmhd, t47));
                        gm_t53at80pfdus3 t49 = t48;
                        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
                        switch (t55) { case ((uint8_t)0ULL): goto ma53; case ((uint8_t)1ULL): goto ma53; case ((uint8_t)2ULL): goto ma53; case ((uint8_t)3ULL): goto ma53; case ((uint8_t)4ULL): goto ma53; case ((uint8_t)5ULL): goto ma53; case ((uint8_t)6ULL): goto ma53; case ((uint8_t)7ULL): goto ma53; case ((uint8_t)8ULL): goto ma53; case ((uint8_t)9ULL): goto ma53; case ((uint8_t)10ULL): goto ma53; case ((uint8_t)11ULL): goto ma53; case ((uint8_t)12ULL): goto ma53; case ((uint8_t)13ULL): goto ma53; case ((uint8_t)14ULL): goto ma53; case ((uint8_t)15ULL): goto ma53; case ((uint8_t)16ULL): goto ma53; case ((uint8_t)17ULL): goto ma53; case ((uint8_t)18ULL): goto ma53; case ((uint8_t)19ULL): goto ma53; case ((uint8_t)20ULL): goto ma53; case ((uint8_t)21ULL): goto ma53; case ((uint8_t)22ULL): goto ma53; case ((uint8_t)23ULL): goto ma53; case ((uint8_t)24ULL): goto ma53; case ((uint8_t)25ULL): goto ma51; case ((uint8_t)26ULL): goto ma52; default: goto ma53; }
ma51:;
                        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
                        if (t57 != ((uint8_t)25ULL)) goto mn56;
                        uint32_t t58 = GM_LD(uint32_t, (((uint8_t *)&t49) + 8));
                        gm_l16 = t58;
                        {
                            void * t61 = gm_l0;
                            uint64_t t62 = GM_LD(uintptr_t, ((((uint8_t *)t61) + 1040) + 0) + 8);
                            uint8_t t63 = (t62 > ((uint64_t)0ULL));
                            if (!t63) goto ie60;
                            {
                                void * t65 = gm_l0;
                                uint32_t t66 = gm_l16;
                                gm_tkn42u7na3t81 t67 = gm_l2;
                                gm_b0_1 t68 = gm_f_check_pending_handler_l4msk6j((void *)NULL, cx, NULL, t65, t66, t67);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                            }
                            goto ix60;
ie60:;
ix60:;
                        }
                        {
                            uint32_t t69 = gm_l16;
                            uint64_t t70 = (uint64_t)t69;
                            uint64_t t71 = (uintptr_t)t70;
                            void * t72 = gm_l0;
                            void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
                            uint64_t t74 = GM_LD(uintptr_t, ((((uint8_t *)t73) + 504) + 0) + 8);
                            if (t71 >= (uintptr_t)(t74)) gm_trap_index(t71, t74, gm_srcn0mgmph5u0oo, 99, 25);
                            gm_tiilmfd16v3fc t75 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t73) + 504) + 0)) + t71 * 40) + 0));
                            gm_l17 = t75;
                        }
                        {
                            void * t76 = gm_l0;
                            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                            gm_b16_8 t78 = t77;
                            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                            gm_tl874t2atnlh3 t80 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l11) + 72));
                            gm_b0_1 t81 = gm_f_check_one_handler_euhh29f((void *)NULL, cx, NULL, t76, t79, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l17)), t80);
                            if (gm_unw.kind) {
                                goto gm_ll12;
                            }
                        }
                        {
                            void * t82 = (void *)((uint8_t *)&F.l5);
                            gm_tftb1odc739t5 t83 = gm_f_push_d1r4h19(t82, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l17)));
                            gm_tftb1odc739t5 t84 = t83;
                            if (((((uint8_t *)&t84))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t84) + 1, gm_srcn0mgmph5u0oo, 101, 17);
                            gm_b0_1 t85 = GM_LD(gm_b0_1, (((uint8_t *)&t84) + 1));
                        }
                        {
                            void * t86 = (void *)((uint8_t *)&F.l6);
                            uint32_t t87 = gm_l16;
                            gm_tftb1odc739t5 t88 = gm_f_push_im9d4ro(t86, t87);
                            gm_tftb1odc739t5 t89 = t88;
                            if (((((uint8_t *)&t89))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t89) + 1, gm_srcn0mgmph5u0oo, 102, 17);
                            gm_b0_1 t90 = GM_LD(gm_b0_1, (((uint8_t *)&t89) + 1));
                        }
                        {
                            void * t91 = (void *)((uint8_t *)&F.l4);
                            void * t92 = gm_l14;
                            gm_t4lnf71mruqpt t93;
                            GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t93 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t93 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)0ULL));
                            GM_ST(void *, ((uint8_t *)&t93 + 8), t92);
                            gm_tftb1odc739t5 t94 = gm_f_push_f5fsh94(t91, &t93);
                            gm_tftb1odc739t5 t95 = t94;
                            if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_srcn0mgmph5u0oo, 103, 17);
                            gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
                        }
                        goto me50;
mn56:;
ma52:;
                        uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
                        if (t98 != ((uint8_t)26ULL)) goto mn97;
                        {
                            gm_l8 = ((uint8_t)1);
                        }
                        goto me50;
mn97:;
ma53:;
                        {
                            void * t102 = gm_l0;
                            void * t103 = GM_LD(void *, (((uint8_t *)t102) + 0));
                            gm_t88rcucg1b7uk t104 = gm_l15;
                            gm_b16_8 t105 = gm_f_handlers_of_type_5gareb4((void *)NULL, cx, NULL, t103, t104);
                            if (gm_unw.kind) {
                                goto gm_ll12;
                            }
                            gm_l18 = t105;
                        }
                        {
                            uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                            uint8_t t108 = ((t107 == ((uint64_t)0ULL)));
                            if (!t108) goto ie106;
                            {
                                gm_l8 = ((uint8_t)1);
                            }
                            {
                                gm_tl874t2atnlh3 t110 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l11) + 72));
                                void * t111 = gm_l0;
                                gm_t88rcucg1b7uk t112 = gm_l15;
                                gm_b16_8 t113 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t111, t112);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                                gm_tnpifv20irnph t114;
                                GM_ST(gm_b16_8, ((uint8_t *)&t114 + 0), t113);
                                gm_tabb710d9mq2e t115;
                                gm_f_format_beqekat((void *)&t115, (void *)NULL, cx, NULL, t114);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                                gm_tabb710d9mq2e t116 = t115;
                                if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 8, gm_srcn0mgmph5u0oo, 110, 45);
                                gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&t116) + 8));
                                gm_b16_8 t118 = t117;
                                gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
                                gm_f_error_s6hn871((void *)&F.l19, (void *)NULL, cx, NULL, t110, t119);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                            }
                            {
                                void * t121 = gm_l3;
                                gm_t88rcucg1b7uk t122 = gm_l15;
                                gm_t53at80pfdus3 t123;
                                gm_f_kind_jcnt4hm((void *)&t123, t121, GM_CONV(gm_csguankko4lmhd, t122));
                                gm_t53at80pfdus3 t124 = t123;
                                uint8_t t125;
                                uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&t124) + 0));
                                if (t128 != ((uint8_t)8ULL)) goto mn127;
                                t125 = ((uint8_t)0);
                                goto me126;
mn127:;
                                t125 = ((uint8_t)1);
                                goto me126;
mn129:;
                                gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 111, 24);
me126:;
                                if (!t125) goto ie120;
                                void * t130 = gm_l0;
                                void * t131 = GM_LD(void *, (((uint8_t *)t130) + 0));
                                void * t132 = gm_l3;
                                gm_t88rcucg1b7uk t133 = gm_l15;
                                gm_t88rcucg1b7uk t134 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t132, ((uint8_t)0), t133);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                                gm_b16_8 t135 = gm_f_handlers_of_type_5gareb4((void *)NULL, cx, NULL, t131, t134);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                                gm_b16_8 t136 = t135;
                                uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&t136) + 8);
                                uint8_t t138 = (t137 > ((uint64_t)0ULL));
                                if (!t138) goto ie120;
                                {
                                    void * t140 = (void *)((uint8_t *)&F.l19);
                                    void * t141 = gm_l0;
                                    gm_t88rcucg1b7uk t142 = gm_l15;
                                    gm_b16_8 t143 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t141, t142);
                                    if (gm_unw.kind) {
                                        goto gm_ll12;
                                    }
                                    gm_tnpifv20irnph t144;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t144 + 0), t143);
                                    gm_tabb710d9mq2e t145;
                                    gm_f_format_vc10pth((void *)&t145, (void *)NULL, cx, NULL, t144);
                                    if (gm_unw.kind) {
                                        goto gm_ll12;
                                    }
                                    gm_tabb710d9mq2e t146 = t145;
                                    if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 8, gm_srcn0mgmph5u0oo, 112, 36);
                                    gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&t146) + 8));
                                    gm_b16_8 t148 = t147;
                                    gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
                                    gm_f_add_note_46v502s(t140, t149);
                                }
                                goto ix120;
ie120:;
ix120:;
                            }
                            {
                                void * t150 = gm_l0;
                                void * t151 = GM_LD(void *, (((uint8_t *)t150) + 0));
                                gm_b0_1 t152 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t151, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l19)));
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                            }
                            {
                                goto lc8;
                            }
                            goto ix106;
ie106:;
ix106:;
                        }
                        {
                            {
                                gm_b16_8 t154 = gm_l18;
                                gm_l20 = t154;
                            }
                            {
                                gm_l21 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t156 = gm_l21;
                                    uint64_t t157 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                    uint8_t t158 = (t156 < t157);
                                    if (!t158) goto lb155;
                                    {
                                        uint64_t t161 = gm_l21;
                                        uint64_t t162 = (uintptr_t)t161;
                                        uint64_t t163 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                        uint32_t t164 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t162 * 4));
                                        uint32_t t165 = t164;
                                        uint32_t t168 = GM_LD(uint32_t, ((uint8_t *)&t165));
                                        gm_l22 = t168;
                                        {
                                            uint32_t t170 = gm_l22;
                                            uint64_t t171 = (uint64_t)t170;
                                            uint64_t t172 = (uintptr_t)t171;
                                            void * t173 = gm_l0;
                                            void * t174 = GM_LD(void *, (((uint8_t *)t173) + 0));
                                            uint64_t t175 = GM_LD(uintptr_t, ((((uint8_t *)t174) + 544) + 0) + 8);
                                            if (t172 >= (uintptr_t)(t175)) gm_trap_index(t172, t175, gm_srcn0mgmph5u0oo, 118, 29);
                                            gm_tvc83pqnepc2e t176 = GM_LD(gm_tvc83pqnepc2e, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t174) + 544) + 0)) + t172 * 64));
                                            F.l23 = t176;
                                        }
                                        {
                                            gm_tiilmfd16v3fc t177 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&F.l23) + 0));
                                            gm_l24 = t177;
                                        }
                                        {
                                            void * t178 = gm_l0;
                                            gm_b16_8 t179 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                                            gm_b16_8 t180 = t179;
                                            gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&t180));
                                            gm_tl874t2atnlh3 t182 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l11) + 72));
                                            gm_b0_1 t183 = gm_f_check_one_handler_euhh29f((void *)NULL, cx, NULL, t178, t181, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l24)), t182);
                                            if (gm_unw.kind) {
                                                goto gm_ll159;
                                            }
                                        }
                                        {
                                            void * t184 = (void *)((uint8_t *)&F.l5);
                                            gm_tftb1odc739t5 t185 = gm_f_push_d1r4h19(t184, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l24)));
                                            gm_tftb1odc739t5 t186 = t185;
                                            if (((((uint8_t *)&t186))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t186) + 1, gm_srcn0mgmph5u0oo, 121, 21);
                                            gm_b0_1 t187 = GM_LD(gm_b0_1, (((uint8_t *)&t186) + 1));
                                        }
                                        {
                                            gm_f_list_084fcd6((void *)&F.l25, (void *)NULL, cx, NULL);
                                            if (gm_unw.kind) {
                                                goto gm_ll159;
                                            }
                                        }
                                        {
                                            {
                                                gm_b16_8 t189 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l23) + 32) + 0));
                                                gm_l26 = t189;
                                            }
                                            {
                                                gm_l27 = ((uint64_t)0ULL);
                                            }
                                            {
                                                for (;;) {
                                                    uint64_t t191 = gm_l27;
                                                    uint64_t t192 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                                                    uint8_t t193 = (t191 < t192);
                                                    if (!t193) goto lb190;
                                                    {
                                                        uint64_t t196 = gm_l27;
                                                        uint64_t t197 = (uintptr_t)t196;
                                                        uint64_t t198 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                                                        gm_tiilmfd16v3fc t201 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t197 * 24));
                                                        gm_l28 = t201;
                                                        {
                                                            gm_l29 = ((uint8_t)0);
                                                        }
                                                        {
                                                            {
                                                                uint64_t t204 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                                                void * t205 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)));
                                                                uint64_t t206 = gm_l12;
                                                                uint64_t t207 = (uintptr_t)t206;
                                                                if ((uintptr_t)0 > t207 || t207 > (uintptr_t)t204) gm_trap_slice((uintptr_t)0, t207, t204, gm_srcn0mgmph5u0oo, 126, 34);
                                                                gm_b16_8 t208;
                                                                GM_ST(void *, (uint8_t *)&t208, (void *)(((uint8_t *)t205 + (uintptr_t)0 * 24)));
                                                                GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(t207 - (uintptr_t)0));
                                                                gm_l30 = t208;
                                                            }
                                                            {
                                                                gm_l31 = ((uint64_t)0ULL);
                                                            }
                                                            {
                                                                for (;;) {
                                                                    uint64_t t210 = gm_l31;
                                                                    uint64_t t211 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                                                                    uint8_t t212 = (t210 < t211);
                                                                    if (!t212) goto lb209;
                                                                    {
                                                                        uint64_t t215 = gm_l31;
                                                                        uint64_t t216 = (uintptr_t)t215;
                                                                        uint64_t t217 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                                                                        gm_tiilmfd16v3fc t220 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t216 * 24));
                                                                        gm_l32 = t220;
                                                                        {
                                                                            void * t223 = (void *)((uint8_t *)&gm_l32);
                                                                            void * t224 = (void *)((uint8_t *)&gm_l28);
                                                                            int32_t t225 = gm_f_cmp_ref_8ntrqll(t223, t224);
                                                                            uint8_t t226 = ((t225 == ((int32_t)0LL)));
                                                                            if (!t226) goto ie222;
                                                                            {
                                                                                gm_l29 = ((uint8_t)1);
                                                                            }
                                                                            goto ix222;
ie222:;
ix222:;
                                                                        }
                                                                        goto me218;
mn219:;
                                                                        gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 126, 25);
me218:;
                                                                    }
lc209:;
                                                                    {
                                                                        uint64_t t229 = gm_l31;
                                                                        uint64_t r230; bool o231 = __builtin_add_overflow(t229, ((uint64_t)1ULL), &r230);
                                                                        uint64_t t232 = r230;
                                                                        gm_l31 = t232;
                                                                    }
                                                                }
lb209:;
                                                            }
                                                        }
                                                        {
                                                            uint8_t t234 = gm_l29;
                                                            uint8_t t235 = (uint8_t)!t234;
                                                            if (!t235) goto ie233;
                                                            {
                                                                void * t237 = (void *)((uint8_t *)&F.l25);
                                                                gm_tftb1odc739t5 t238 = gm_f_push_d1r4h19(t237, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l28)));
                                                                gm_tftb1odc739t5 t239 = t238;
                                                                if (((((uint8_t *)&t239))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t239) + 1, gm_srcn0mgmph5u0oo, 129, 39);
                                                                gm_b0_1 t240 = GM_LD(gm_b0_1, (((uint8_t *)&t239) + 1));
                                                            }
                                                            goto ix233;
ie233:;
ix233:;
                                                        }
                                                        goto me199;
mn200:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 124, 21);
me199:;
                                                    }
lc190:;
                                                    {
                                                        uint64_t t242 = gm_l27;
                                                        uint64_t r243; bool o244 = __builtin_add_overflow(t242, ((uint64_t)1ULL), &r243);
                                                        uint64_t t245 = r243;
                                                        gm_l27 = t245;
                                                    }
                                                }
lb190:;
                                            }
                                        }
                                        {
                                            gm_b16_8 t246 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                                            gm_b16_8 t247 = t246;
                                            gm_b16_8 t248 = GM_LD(gm_b16_8, ((uint8_t *)&t247));
                                            gm_tnpifv20irnph t249 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t248);
                                            if (gm_unw.kind) {
                                                goto gm_ll159;
                                            }
                                            gm_l33 = t249;
                                        }
                                        {
                                            void * t250 = gm_l0;
                                            void * t251 = (void *)((uint8_t *)&gm_l33);
                                            gm_tl874t2atnlh3 t252 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l11) + 72));
                                            gm_b0_1 t253 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t250, t251, t252);
                                            if (gm_unw.kind) {
                                                goto gm_ll159;
                                            }
                                        }
                                        {
                                            void * t254 = (void *)((uint8_t *)&F.l7);
                                            uint32_t t255 = gm_l22;
                                            gm_tftb1odc739t5 t256 = gm_f_push_im9d4ro(t254, t255);
                                            gm_tftb1odc739t5 t257 = t256;
                                            if (((((uint8_t *)&t257))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t257) + 1, gm_srcn0mgmph5u0oo, 133, 21);
                                            gm_b0_1 t258 = GM_LD(gm_b0_1, (((uint8_t *)&t257) + 1));
                                        }
                                        goto me166;
mn167:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 117, 17);
me166:;
                                    }
lc155:;
                                    {
                                        uint64_t t260 = gm_l21;
                                        uint64_t r261; bool o262 = __builtin_add_overflow(t260, ((uint64_t)1ULL), &r261);
                                        uint64_t t263 = r261;
                                        gm_l21 = t263;
                                    }
                                }
gm_ll159:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb155; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc155; }
                                }
                                goto gm_ll12;
lb155:;
                            }
                        }
                        {
                            void * t264 = (void *)((uint8_t *)&F.l4);
                            gm_tpe1hv3fekn05 t265;
                            gm_b16_8 t266 = gm_l18;
                            GM_ST(gm_b16_8, (uint8_t *)&t265 + 0, t266);
                            void * t267 = gm_l14;
                            GM_ST(void *, (uint8_t *)&t265 + 16, t267);
                            gm_t4lnf71mruqpt t268;
                            GM_ST(uint8_t, (uint8_t *)&t268 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t268 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t268 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t268 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t268 + 8), t265);
                            gm_tftb1odc739t5 t269 = gm_f_push_f5fsh94(t264, &t268);
                            gm_tftb1odc739t5 t270 = t269;
                            if (((((uint8_t *)&t270))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t270) + 1, gm_srcn0mgmph5u0oo, 135, 17);
                            gm_b0_1 t271 = GM_LD(gm_b0_1, (((uint8_t *)&t270) + 1));
                        }
                        goto me50;
mn100:;
ma54:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 96, 9);
me50:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 88, 5);
me17:;
                }
lc8:;
                {
                    uint64_t t273 = gm_l10;
                    uint64_t r274; bool o275 = __builtin_add_overflow(t273, ((uint64_t)1ULL), &r274);
                    uint64_t t276 = r274;
                    gm_l10 = t276;
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
        memset((uint8_t *)&(*(gm_tieqmg4s05m8e *)gm_out) + 161, 0, 7);
        memcpy((uint8_t *)&(*(gm_tieqmg4s05m8e *)gm_out) + 0, ((uint8_t *)&F.l4), 40);
        memcpy((uint8_t *)&(*(gm_tieqmg4s05m8e *)gm_out) + 40, ((uint8_t *)&F.l5), 40);
        memcpy((uint8_t *)&(*(gm_tieqmg4s05m8e *)gm_out) + 80, ((uint8_t *)&F.l6), 40);
        memcpy((uint8_t *)&(*(gm_tieqmg4s05m8e *)gm_out) + 120, ((uint8_t *)&F.l7), 40);
        uint8_t t277 = gm_l8;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tieqmg4s05m8e *)gm_out) + 160, t277);
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
__attribute__((aligned(4))) void * gm_f_with_stack_9rur454(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_with_stack_9rur454 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* w */
    gm_b16_8 gm_l2; /* handled */
    void * gm_l3; /* tys */
    uint8_t gm_l4; /* has_ctl */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tiilmfd16v3fc gm_l7; /* e */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    void * gm_l11; /* st */
    gm_t88rcucg1b7uk gm_l12; /* stack_ty */
    void * gm_l13; /* v */
    GM_STACK_CHECK(&F, 649u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t6 = gm_l2;
            gm_l5 = t6;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l6;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint64_t t13 = gm_l6;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tiilmfd16v3fc t18 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t14 * 24));
                    gm_l7 = t18;
                    {
                        {
                            uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 0));
                            uint64_t t22 = (uint64_t)t21;
                            uint64_t t23 = (uintptr_t)t22;
                            void * t24 = gm_l0;
                            void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
                            uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)t25) + 1048) + 0) + 8);
                            if (t23 >= (uintptr_t)(t26)) gm_trap_index(t23, t26, gm_srcn0mgmph5u0oo, 155, 18);
                            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t25) + 1048) + 0)) + t23 * 56) + 40));
                            gm_l8 = t27;
                        }
                        {
                            gm_l9 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t29 = gm_l9;
                                uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                uint8_t t31 = (t29 < t30);
                                if (!t31) goto lb28;
                                {
                                    uint64_t t34 = gm_l9;
                                    uint64_t t35 = (uintptr_t)t34;
                                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                    gm_tv0k2vg3054a6 t39 = GM_LD(gm_tv0k2vg3054a6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t35 * 48));
                                    F.l10 = t39;
                                    {
                                        uint8_t t42 = ((GM_LD(uint8_t, (((uint8_t *)&F.l10) + 16) + 0) == ((uint8_t)2ULL)));
                                        if (!t42) goto ie41;
                                        {
                                            gm_l4 = ((uint8_t)1);
                                        }
                                        goto ix41;
ie41:;
ix41:;
                                    }
                                    goto me37;
mn38:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 155, 9);
me37:;
                                }
lc28:;
                                {
                                    uint64_t t45 = gm_l9;
                                    uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                                    uint64_t t48 = r46;
                                    gm_l9 = t48;
                                }
                            }
lb28:;
                        }
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn0mgmph5u0oo, 154, 5);
me16:;
                }
lc7:;
                {
                    uint64_t t50 = gm_l6;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    uint64_t t53 = r51;
                    gm_l6 = t53;
                }
            }
lb7:;
        }
    }
    {
        void * t55 = gm_l1;
        void * t56 = GM_LD(void *, (((uint8_t *)t55) + 56));
        void * t57 = t56;
        if (!(GM_LD(void *, ((uint8_t *)&t57)) != 0)) goto ie54;
        void * t58 = GM_LD(void *, ((uint8_t *)&t57));
        gm_l11 = t58;
        {
            uint8_t t61 = gm_l4;
            uint8_t t62 = (uint8_t)!t61;
            if (!t62) goto ie60;
            {
                void * t64 = gm_l0;
                void * t65 = gm_l11;
                gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 72));
                gm_b16_8 t67;
                GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sbn0999b7p1ph));
                GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(82));
                gm_b0_1 t68 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t64, t66, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix60;
ie60:;
ix60:;
        }
        {
            void * t69 = gm_l3;
            void * t70 = gm_l3;
            gm_t88rcucg1b7uk t71 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t70, ((uint8_t)0), ((uint16_t)8ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t72 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t69, ((uint8_t)0), t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t72;
        }
        {
            void * t73 = gm_l0;
            void * t74 = gm_l11;
            gm_t88rcucg1b7uk t75 = gm_l12;
            gm_tkn42u7na3t81 t76;
            GM_ST(uint8_t, (uint8_t *)&t76 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t76 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t76), t75);
            ((uint8_t *)&t76)[4] = 1;
            void * t77 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t73, t74, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t77;
        }
        {
            void * t78 = gm_l0;
            void * t79 = gm_l13;
            gm_t88rcucg1b7uk t80 = gm_l12;
            void * t81 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t78, t79, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t82 = 0;
            F.ret = t81;
            goto gm_fret;
        }
        goto ix54;
ie54:;
ix54:;
    }
    {
        uint8_t t84 = gm_l4;
        if (!t84) goto ie83;
        {
            void * t86 = gm_l0;
            void * t87 = gm_l1;
            gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t87) + 104));
            gm_b16_8 t89;
            GM_ST(void *, (uint8_t *)&t89, (void *)(gm_s82i64ms8kog2));
            GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(93));
            gm_b0_1 t90 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t86, t88, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix83;
ie83:;
ix83:;
    }
    {
        uint8_t t91 = 0;
        F.ret = ((void *)0);
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
